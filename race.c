/*
Quellcode zum Praktikum "Echtzeitsysteme" im Fachbereich Elektrotechnik
und Informatik der Hochschule Niederrhein.

Fuer die Generierung wird die Realzeitbibliothek "rt" benoetigt.
Wenn das folgende Makefile verwendet wird, muss zur Generierung nur
noch "make" auf der Kommandozeile eingegeben werden:
==================================
CFLAGS=-g -Wall -m32
LDLIBS=-lrt

all: race

clean:
	rm -f race *.o
==================================

Sa 27. Sep 17:57:59 CEST 2014
*/
#include <stdio.h>
#include <signal.h>
#include <stdlib.h>
#include <fcntl.h>
#include <string.h>
#include <unistd.h>
#include <malloc.h>
#include <time.h>
#include <sys/time.h>
#include <asm/types.h>
#include <pthread.h>

#define make_seconds( a ) (a/1000000)
#define make_mseconds( a ) ((a-((a/1000000)*1000000))/1000)

static void set_speed( int fd, int Speed );

static unsigned char basis_speed_in=0x24, basis_speed_out=0x24;
static unsigned long last_time;
static int fdc=0;
unsigned long time_act_gegner;
static int auslenkung_in=-1, auslenkung_out=-1;

typedef struct _liste {
	int type;
	int length;
	struct _liste *next;
} streckenliste;

static streckenliste *root = NULL; 
//static _liste *gegnerposition = NULL; //Neu
static streckenliste *gegnerposition = NULL;
static struct _liste *add_to_liste( int type, int length )
{
	struct _liste *ptr, *lptr;

	lptr = malloc( sizeof(struct _liste) );
	if( lptr==NULL ) {
		printf("out of memory\n");
		return NULL;
	}
	lptr->type = type;
	lptr->length = length;
	lptr->next = root;
	if( root == NULL ) {
		root = lptr;
		lptr->next = lptr;
		return root;
	}
	for( ptr=root; ptr->next!=root; ptr=ptr->next )
		;
	ptr->next = lptr;
	return lptr->next;
}

static void print_liste(void)
{
	struct _liste *lptr = root;
	int length_sum=0;

	do {
		printf("0x%04x - %d [mm]\n", lptr->type, lptr->length );
		length_sum += lptr->length;
		lptr = lptr->next;
	} while( lptr!=root );
	printf("sum: %d\n", length_sum );
}

static void exithandler( int signr )
{
	if( fdc )
		set_speed( fdc, 0x0 );
	exit( 0 );
}

static void set_speed( int fd, int speed )
{
	printf("new speed: 0x%x\n", speed );
	if( fd>0 )
		write( fd, &speed, sizeof(speed) );
}

static __u16 read_with_time( int fd, unsigned long *time1 )
{
	struct timespec timestamp;
	__u16 state;
	ssize_t ret;

	ret=read( fd, &state, sizeof(state) );
	if (ret<0) {
		perror( "read" );
		return -1;
	}
	clock_gettime(CLOCK_MONOTONIC,&timestamp);
	*time1 = (timestamp.tv_sec * 1000000)+(timestamp.tv_nsec/1000);
	return state;
}

static inline int is_sling( __u16 state )
{
	//return (state&0xf000)==0x0000;
	//Praktikum3
	if ((state&0xf000)!=0x0000)//Keine Auslenkung
		return 0;
	if(state&0x0800){
		int auslenkung = state&0x000f;
		auslenkung_out = auslenkung;
	}else{
		int auslenkung = state&0x000f;
		auslenkung_in = auslenkung;
	}
	//printf ("auslenkung in:%d\n",auslenkung_in);
	//printf("auslenkung_out:%d\n",auslenkung_out);
	return 1;
}

int change_speed(__u16 state)
{

	printf ("Anfang change speed:%u,%u\n",basis_speed_in, basis_speed_out);
	switch(auslenkung_in){
		case -1:basis_speed_in +=6; break;
		case 0:basis_speed_in +=3;break;
		case 1:basis_speed_in +=3;break;
		case 2:basis_speed_in +=5;break;
		case 3:basis_speed_in +=5;break;
		case 4:basis_speed_in +=1;break;
		case 5:basis_speed_in +=-5;break;
		case 6:basis_speed_in +=-2;break;
		case 7:basis_speed_in +=-1;break;
		default:printf("Default\n");break;
	}

	switch(auslenkung_out){
		case -1: basis_speed_out +=5;break;
		case 0: basis_speed_out +=5;break;
		case 1: basis_speed_out +=5;break;
		case 2:basis_speed_out +=4;break;
		case 3: basis_speed_out +=3;break;
		case 4: basis_speed_out +=-5;break;
		case 5: basis_speed_out +=-4;break;
		case 6: basis_speed_out +=-5;break;
		case 7: basis_speed_out +=-3;break;
		default:printf ("Default\n");break;
	}
	
	printf ("Basisspeed angepasst!\n");
	auslenkung_in = -1;
	auslenkung_out =-1;
	return 0;
}

static char *decode( __u16 status )
{
	if( (status&0xf000)==0x0000 )
		return "Auslenkungssensor";
	if( (status&0xf000)==0x1000 )
		return "Start/Ziel";
	if( (status&0xf000)==0x2000 )
		return "Gefahrenstelle";
	if( (status&0xf000)==0x3000 )
		return "NICHT KODIERT";
	if( (status&0xf000)==0x4000 )
		return "NICHT KODIERT";
	if( (status&0xf000)==0x5000 )
		return "NICHT KODIERT";
	if( (status&0xf000)==0x6000 ) {
		if( (status&0x0400)==0x0400 )
			return "Brueckenende";
		else
			return "Brueckenanfang";
	}
	if( (status&0xf000)==0x7000 )
		return "NICHT KODIERT";
	if( (status&0xf000)==0x8000 )
		return "Kurve";
	if( (status&0xf000)==0x9000 )
		return "NICHT KODIERT";
	if( (status&0xf000)==0xa000 )
		return "NICHT KODIERT";
	if( (status&0xf000)==0xb000 )
		return "NICHT KODIERT";
	if( (status&0xf000)==0xc000 )
		return "NICHT KODIERT";
	if( (status&0xf000)==0xd000 )
		return "NICHT KODIERT";
	if( (status&0xf000)==0xe000 )
		return "NICHT KODIERT";
	if( (status&0xf000)==0xf000 )
		return "NICHT KODIERT";

	return "TYP UNBEKANNT";
}

static void exploration( int fdc )
{
	__u16 state_old, state_act;
	unsigned long time_act, time_old;
	int rounds=0, length, length_sum=0, v;

	do {
		state_old = read_with_time( fdc, &time_old );
	} while( (state_old&0xf000)!=0x1000 ); // fahre bis Start
	while( rounds<2 ) {
		do {
			state_act=read_with_time( fdc, &time_act );
		} while( is_sling(state_act) ); //ignoriere infos zue auslenkung
		printf("old/act: 0x%x/0x%x\t%6.6ld [ms] - ",
			state_old, state_act,
			(time_act-time_old)/1000 );
		if( state_act == state_old ) {
			v = 22000000/(time_act-time_old);
			if( (state_act&0xf000)==0x1000 ) {
				rounds++;
				last_time = time_act;
				printf("Round: %d - %d [mm]\n",
					rounds, length_sum );
				length_sum = 0;
			}
			printf("v=0,%d\n", v );
		} else {
			length = v * (time_act - time_old)/1000000;

			length_sum += length;
			printf("%s: length=%d\n", decode(state_act),length);
			add_to_liste( state_act, length );
		}
		state_old = state_act;
		time_old = time_act;
	}
}

unsigned long timespec_to_ulong_microseconds(struct timespec *ptr){
	return ((ptr->tv_sec*1000000)+(ptr->tv_nsec/1000));// microsekunde
}

static void sleep_for_length(unsigned int length,int percent){
	unsigned int time = length * percent *10000;
	struct timespec sleep;
	sleep.tv_sec =0;
	sleep.tv_nsec = time;
	clock_nanosleep(CLOCK_MONOTONIC,0,&sleep,NULL);
	
}

static int fahre_segment(unsigned int line, unsigned int type, unsigned int length){
	printf ("Fahre segment:");
	type = type&0xf000;
	struct timespec sleep;
	sleep.tv_sec =0;
	sleep.tv_nsec=0;
	
	if (line == 0x0100){ //aussen
		switch (type){
			case 0x1000:
				printf ("Start/Ziel\n");
				set_speed(fdc,0xff);
				break;
			case 0x2000:
				printf ("Kreuzung\n");
				set_speed(fdc,0x0b);
				sleep_for_length(length,1);
				set_speed(fdc,0x0c);
				set_speed(fdc,0xff);
				break;
			case 0x6000:
				printf("Bruecken\n");
				set_speed(fdc,0xff);
				sleep_for_length(length,50);
				set_speed(fdc,0x50);
				break;
			case 0x8000:
				printf("Kurve\n");
				set_speed(fdc,0x0b);
				sleep_for_length(length,8);
				set_speed(fdc,0x0c);
				set_speed(fdc,basis_speed_out);
				break;
		}
	}else{//innen
		switch(type){
			 case 0x1000:
                                printf ("Start/Ziel\n");
                                set_speed(fdc,0xff);
                                break;
                        case 0x2000:
                                printf ("Kreuzung\n");
                                set_speed(fdc,0x0b);
                                sleep_for_length(length,1);
                                set_speed(fdc,0x0c);
                                set_speed(fdc,0xff);
                                break;
                        case 0x6000:
                                printf("Bruecken\n");
                                set_speed(fdc,0xff);
                                sleep_for_length(length,50);
                                set_speed(fdc,0x50);
                                break;
                        case 0x8000:
                                printf("Kurve\n");
                                set_speed(fdc,0x70);
                                sleep_for_length(length,8);
                                set_speed(fdc,0x80);
                                set_speed(fdc,basis_speed_in);
                                break;

		}
	}
	return 0;
}




static void tracking( int rounds_to_go )
{
	struct _liste *position = root;
	int rounds=0;
	__u16 state_act;
	unsigned long time_act, besttime=(-1), meantime=0;

	//neu
	struct timespec sleeptime;
	unsigned long diff_zeit_in_us;
	struct timespec remaining;
	struct timespec aktuellezeit, gegnerzeit;
	unsigned long time_in_us;
	unsigned long gegnerzeit_in_us;

	sleeptime.tv_sec = 0;
	sleeptime.tv_nsec = 2500000;


	do{
		state_act = read_with_time(fdc,&time_act);
	}while((state_act&0xf000)!=0x1000);//fahre bis Start
	set_speed(fdc,0x0b);
	set_speed(fdc,0x0c);
	set_speed(fdc,0x00);
	printf ("Zum Starten....");
	getchar();
	set_speed(fdc,0x80);

	do {
		do {
			state_act=read_with_time( fdc, &time_act );
		} while( is_sling(state_act) );
		state_act=read_with_time( fdc, &time_act );

		//Praktikum 3

		fahre_segment(state_act&0x0800,position->type,position->length);

		printf("0x%04x (expected 0x%04x)\n",state_act,position->type);
		if( state_act != position->type){
			printf("wrong position 0x%04x (0x%04x)\n",
				state_act, position->type);
			
			while (position->type != state_act){
				position = position->next ;
			}
		}
		if( (state_act&0xf000)==0x1000 ) { // Start/Ziel
			change_speed(state_act);
			rounds++;
			rounds_to_go--;
			if( last_time ) {
				if( (time_act-last_time)<besttime )
					besttime = time_act-last_time;
				meantime += time_act-last_time;
				printf("\n---> Runde: %d - Zeit: %ld.%03lds "
					"(Beste: %ld.%03lds, "
					"Mean: %ld.%03lds)\n",
					rounds,
				make_seconds((time_act-last_time)),
				make_mseconds((time_act-last_time)),
				make_seconds(besttime),
				make_mseconds(besttime),
				make_seconds(meantime/rounds),
				make_mseconds(meantime/rounds));
			}
			last_time = time_act;
		}

		if ((state_act&0xf000)==0x2000){ //colision
			gegnerzeit_in_us = time_act_gegner;

			diff_zeit_in_us = time_act - gegnerzeit_in_us;
			printf ("Gefahrstelle %ld\n",diff_zeit_in_us);
			while((position==gegnerposition)&&(diff_zeit_in_us<2500000)){
				printf("%4.4x - %4.4x - diff_zeit_in_us: %ld\n",position->type,gegnerposition->type,diff_zeit_in_us);
			
				set_speed(fdc,0x0b);
				printf("Bremsen");
				clock_nanosleep(CLOCK_MONOTONIC,0,&sleeptime,&remaining);
				clock_gettime(CLOCK_MONOTONIC,&aktuellezeit);
				//time_in_us = (aktuellezeit.tv_sec*1000000)+(aktuellezeit.tv_nsec/1000);
				time_in_us = timespec_to_ulong_microseconds(&aktuellezeit);
				diff_zeit_in_us = time_in_us - gegnerzeit_in_us;
			}
			set_speed(fdc,0x0c);
			printf("Fahren");

		}
		position = position->next;
		change_speed(state_act);
	} while( rounds_to_go );
}

void *gegner_thread(){
	int status;
	__u16 state, tempstate;
struct timespec zeit;

unsigned long time_act;
zeit.tv_sec=0;
zeit.tv_nsec=30000000;


gegnerposition = root;
__u16 oldstatus;
//int fdstatus;

if ((status=open("/dev/Carrera.other",O_RDONLY))<0){
	perror("/dev/Carrera.other");
	return -1;
	}

oldstatus = read_with_time(status,&time_act)^0x0800;

while(1){
/*state=read_with_time(status,&time_act);
if (tempstate != state){
	printf ("Gegner:%04x\n",state);
	tempstate = state;
	}
	clock_nanosleep(CLOCK_MONOTONIC,0,&zeit,NULL);
}*/

do{
	clock_nanosleep(CLOCK_MONOTONIC,0,&zeit,NULL);
	state= read_with_time(status,&time_act)^0x800;
}while(oldstatus==state || is_sling(state));


oldstatus=state;
time_act_gegner = time_act;

while(state != gegnerposition->type){
	gegnerposition = gegnerposition->next;
}

printf("\n Gegnerposition:%s",decode(state));

} }





int main( int argc, const char **argv )
{
	int rounds_to_go=10;
	struct sigaction new_action;

	fdc = open( "/dev/Carrera", O_RDWR );
	if( fdc<0 ) {
		perror( "/dev/Carrera" );
		return -1;
	}
	if( argc > 1 ) {
		basis_speed_in=basis_speed_out=
			(unsigned char)strtoul(argv[1],NULL,0);
		if( argc > 2 ) {
			rounds_to_go = (unsigned int)strtoul(argv[2],NULL,0);
		}
	}
	new_action.sa_handler = exithandler;

	sigemptyset( &new_action.sa_mask );
	new_action.sa_flags = 0;
	sigaction( SIGINT, &new_action, NULL );

	set_speed( fdc, basis_speed_in );
	exploration( fdc );
	print_liste();
	pthread_t child_thread;
	//pthread_create(&child_thread,NULL,gegner_thread,NULL);
	
	/*if(pthread_create(&child_thread,NULL,gegner_thread,NULL)!=0){
		fprintf(stderr,"Erzeugung fehlgeschlagen\n");
		return -1;
	}*/
	//pthread_t child_thread;
	pthread_create(&child_thread,NULL,gegner_thread,NULL);

	tracking( rounds_to_go );
	set_speed( fdc, 0x0 );
	return 0;
}


