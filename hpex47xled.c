/////////////////////////////////////////////////////////////////////////////
/////// @file hpex47xled.c
///////
/////// Daemon for controlling the LEDs on the HP MediaSmart Server EX47X
/////// Linux Support - written for Ubuntu 20.04 or greater. 
///////
/////// -------------------------------------------------------------------------
///////
/////// Copyright (c) 2022 Robert Schmaling
/////// 
/////// This software is provided 'as-is', without any express or implied
/////// warranty. In no event will the authors be held liable for any damages
/////// arising from the use of this software.
/////// 
/////// Permission is granted to anyone to use this software for any purpose,
/////// including commercial applications, and to alter it and redistribute it
/////// freely, subject to the following restrictions:
/////// 
/////// 1. The origin of this software must not be misrepresented; you must not
/////// claim that you wrote the original software. If you use this software
/////// in a product, an acknowledgment in the product documentation would be
/////// appreciated but is not required.
/////// 
/////// 2. Altered source versions must be plainly marked as such, and must not
/////// be misrepresented as being the original software.
/////// 
/////// 3. This notice may not be removed or altered from any source
/////// distribution.
///////
/////////////////////////////////////////////////////////////////////////////////
/////// 
/////// Changelog
/////// February 17, 2022
/////// - initial release
/////// - IO work is taken from freebsd_led.c - taken from the http://www.mediasmartserver.net/forums/viewtopic.php?f=2&t=2335 author ndenev
/////// - some udev code taken from https://github.com/merelin/mediasmartserverd - written by merelin
/////// - other udev code taken from examples around the net - sorry I don't recall all the sites I visited looking for answers - you do deserve the credit.
/////// - each drive will flash based on IO activity derrived from /sys/ * /stat files. If you use a drive connected by eSATA - YMMV. Please provide
///////   the /sys/* information path so I can add it to the ide structures, ignore, or exclude them from detection. 
///////
/////// February 18, 2022  
/////// - Change retbyte to return int64_t and the hpled struct r_io and w_io to int64_t as well. reasoning: if this runs long enough, that number will become large.
/////// - added a break to the token return of retbyte - didn't realize I was letting it parse the whole file before returning.
/////// 
/////// February 22, 20222
/////// - Threaded the initialization.
/////// - Threaded the reading of the stat file parse, tokenize, compare, fire led process into one thread per disks.
///////
/////// March 11, 2022
/////// - fixed race condition in hpex47x_thread_run - added pthread_spin_lock and pthread_spin_unlock to resolve issue
/////// - as a 'just in case' added pthread_spin_lock/unlock to the LED control function to give exclusive access during on/off writes.
/////// - ensure that the function that taking the return from retpath() calls free() when finished.
/////// - working on an update_monitor function - will monitor apt-get for system updates on Ubuntu. 
///////   thinking I will set one light or flash all lights once every 10 minutes if updates are available for processing
///////   this idea is blatently plagiarized from the mediasmartserverd - update monitor written by Kai Hendrik Behrends

#include "hpex47xled.h"

struct hpled ide0, ide1, ide2, ide3 ;
struct hpled hpex47x[4];
int debug = 0;
u_int16_t encreg;
int* hpdisks = NULL;
int thread_run = 1; 
pthread_t hpexled_led[4]; /* there can be only 4! */
// Create Thread Attributes - put here for clean-up purposes
pthread_attr_t attr;

// 0 is read I/Os, 4 is write I/Os in the /sys/devices/* /stat file 

char* retpath( char* parent, char *delim, int field )
{

    char *last_token = NULL;
    char *copy_parent = NULL;
    int token = 0;
    char* found = NULL;

	if( (copy_parent = (char *)calloc((strlen(parent) + 2), sizeof(char))) == NULL)
		err(1, "Unable to allocate copy_parnet for copy from char* parent in %s line %d ", __FUNCTION__, __LINE__);

	if( !(strcpy(copy_parent, parent)))
		err(1, "Unable to strcpy() parent into copy_parent in %s line %d ", __FUNCTION__, __LINE__);

	if(delim == NULL)
		err(1, "Unknown or illegal delimiter in repath()");

	last_token = strtok( copy_parent, delim);

	while( (last_token != NULL) && (token <= field) ){
		if( token == field) {
			// found = last_token;
			if( (found = (char *)calloc((strlen(last_token) + 2), sizeof(char))) == NULL)
				err(1, "Unable to allocate found for copy from char * token in %s line %d ", __FUNCTION__, __LINE__);
			if( !(strcpy(found, last_token)))
				err(1, "Unable to strcpy() token into found in %s line %d ", __FUNCTION__, __LINE__);
			if(debug)
				printf("The value of retpath field %i is %s in %s line %d \n", token, found, __FUNCTION__, __LINE__);
			break;
                }
                last_token = strtok( NULL, delim );
                token++;
	}
    free(copy_parent);
    copy_parent = NULL;    
    last_token = NULL;
    return found;
}

int64_t retbytes(char* statfile, int field)
{

    const char *delimiter_characters = " ";
    char buffer[ BUFFER_SIZE ];
    char *last_token;
    char *end;
    int token = 0;
    int64_t found = 0;

    FILE *input_file = fopen( statfile, "r" );

    if( input_file == NULL ) {
		err(1, "Unable to open statfile in %s line %d ", __FUNCTION__, __LINE__);
    }
    else {

        while( (fgets(buffer, BUFFER_SIZE, input_file) != NULL)) {

		if( debug ) {
            		fputs( buffer, stdout );
		}

            last_token = strtok( buffer, delimiter_characters );

            while( (last_token != NULL) && (token <= field) ){
                if( token == field) {
                        found = strtoll( last_token, &end, 10 );
					if(*end)
						err(1, "Unable to convert string to int64_t in %s line %d ", __FUNCTION__, __LINE__);

					if(debug)
                        	printf("The value of field %i is %li \n", token, found);
					break;
                }
                last_token = strtok( NULL, delimiter_characters );
                token++;
            }

        }

        if( ferror(input_file) ){
            perror( "The following error occurred: " );
	   		exit(1);
        }

        fclose( input_file );
    }

    return found;

}

void* hpex47x_thread_run (void *arg)
{
	int64_t n_rio, n_wio = 0;
	struct hpled hpex47x = *(struct hpled *)arg;
	int led_state = 0;
	int led_light = 0; /* 1 = blue    2 = red    3 = purple */
	struct timespec tv = { .tv_sec = 0, .tv_nsec = BLINK_DELAY };
	pthread_t thId = pthread_self();

	while(thread_run) {

		if( (pthread_spin_lock(&hpex47x_gpio_lock)) != 0)
			err(1,"Invalid return from pthread_spin_lock for thread %ld in %s line %d", thId, __FUNCTION__, __LINE__);

		n_rio = retbytes(hpex47x.statfile, 0);
		n_wio = retbytes(hpex47x.statfile, 4);
			
		if( (pthread_spin_unlock(&hpex47x_gpio_lock)) != 0)
			err(1, "Invalid return from pthread_spin_unlock for thread %ld in %s line %d", thId, __FUNCTION__, __LINE__);	

		if(debug)
			printf("the disk is: %li \n", hpex47x.hphdd);

		if( ( hpex47x.rio != n_rio ) && ( hpex47x.wio != n_wio) ) {

			hpex47x.rio = n_rio;
			hpex47x.wio = n_wio;

			if(debug) {
				printf("Read I/O = %li Write I/O = %li \n", n_rio, n_wio);
				printf("HP HDD is: %li \n", hpex47x.hphdd);
				printf("Thread ID: %ld \n", thId);
			}
			led_light = PURPLE_CASE;
			led_state = led_set(hpex47x.hphdd, PURPLE_CASE, led_light);

		}
		else if( hpex47x.rio != n_rio ) {

			hpex47x.rio = n_rio;

			if(debug) {
				printf("Read I/O only and is: %li \n", n_rio);
				printf("HP HDD is: %li \n", hpex47x.hphdd);
				printf("Thread ID: %ld \n", thId);
			}
			led_light = PURPLE_CASE;
			led_state = led_set(hpex47x.hphdd, PURPLE_CASE, led_light);
		}
		else if( hpex47x.wio != n_wio ) {

			hpex47x.wio = n_wio;

			if(debug) {
				printf("Write I/O only and is: %li \n", n_wio);
				printf("HP HDD is: %li \n", hpex47x.hphdd);
				printf("Thread ID: %ld \n", thId);
			}
			led_light = BLUE_CASE;
			led_state = led_set(hpex47x.hphdd, BLUE_CASE, led_light);
		}
		else {
			/* turn off the active light */
			assert(nanosleep(&tv, NULL) >= 0);
			nanosleep(&tv, NULL);

			if ( (led_state != 0) || ( inw(ADDR) != OFFSTATE) ) {
				led_state = led_set(hpex47x.hphdd, LED_CASE_OFF, led_light);
			}
			continue;

			}
	}
	pthread_exit(NULL);   

}

void* hpex47x_init(void *arg) 
{

	struct udev *udev = NULL;
	struct udev_enumerate *enumerate = NULL;
	struct udev_list_entry *devices = NULL;
	struct udev_device *dev = NULL;
	char *statpath = NULL;
	char *ppath = NULL;
	char *check_ATA = NULL;
	char *check_HOST_BUS = NULL;
	size_t numdisks = 0;

	udev = udev_new();

	if (!udev)
		err(1, "Unable to create struct udev in %s", __FUNCTION__);
	
	enumerate = udev_enumerate_new(udev); 

	udev_enumerate_add_match_subsystem(enumerate, "block");
	udev_enumerate_add_match_property(enumerate, "ID_BUS", "ata");
	udev_enumerate_scan_devices(enumerate);

	devices = udev_enumerate_get_list_entry(enumerate);

	for( ; devices; devices = udev_list_entry_get_next(devices) ) {
		const char *path;
		char *host_bus = NULL;

		path = udev_list_entry_get_name(devices);

		dev = udev_device_new_from_syspath(udev,path);
		
		/* Get the filename of the /sys entry for the device
		   and create a udev_device object (dev) representing it */
		// dev = udev_device_new_from_syspath(udev, path);
		/* udev_device_get_devpath(dev);  the path under sys - so strips off /sys from the entry */

		if( !dev )
			err(1, "Unable to retrieve dev from udev_device_new_from_syspath in %s", __FUNCTION__);

		if( (!(strcmp("ata", udev_device_get_property_value(dev, "ID_BUS")) == 0) && (strcmp("disk", udev_device_get_devtype(dev)) == 0)) || (strcmp("partition", udev_device_get_devtype(dev)) == 0) ) {
			dev = udev_device_unref(dev);
			continue;
		}
		if( (statpath = (char *)calloc(128, sizeof(char))) == NULL)
			err(1, "Unable to allocate statfile for copy from udev_list_entry_get_name() in %s", __FUNCTION__);

		if( !(strcpy(statpath, path))) 
			err(1, "Unable to strcpy() path into statpath in %s", __FUNCTION__);

		if(! (check_ATA = retpath(statpath,"/",4)))
			err(1, "NULL return from retpath in %s line %d", __FUNCTION__, __LINE__);

		/* We only want these host busses as they are associated with the first (1, 2) and second (3, 4) set of drive bays */
		if( ! (strcmp("ata1",check_ATA)) && ! (strcmp("ata2",check_ATA)))
			continue;
		
		if(debug)
			printf("Device host bus is: %s \n", check_ATA);
		
		if( (strcat(statpath, "/stat")) == NULL) 
			err(1, "Unable to concatinate /stat to path in %s", __FUNCTION__);

		if ( debug ) {
			printf("Device Node (/dev) path: %s\n", udev_device_get_devnode(dev));
			printf("Device sys path is: %s \n", path);
			printf("Device stat file is at: %s \n", statpath);
			printf("Device type is: %s \n", udev_device_get_devtype(dev));
		}
		/* reset to get parent device so we can scrape the last section and determine which bay is in use. */
		dev = udev_device_get_parent(dev);
		if (!dev)
			err(1, "Unable to find parent path of scsi device in %s ", __FUNCTION__);

		if( (ppath = (char *)calloc( 128 , sizeof(char)) ) == NULL )
			err(1, "Unable to allocate statfile for copy from udev_list_entry_get_name() in %s", __FUNCTION__);

		if( !(strcpy(ppath, udev_device_get_devpath(dev)))) 
			err(1, "Unable to memcpy() path into statpath in %s ", __FUNCTION__);

		if (debug)
			printf("Device parent path is: %s \n", ppath);
		
		if( !(host_bus = retpath(ppath,"/",6)))
			err(1, "NULL return from retpath in %s", __FUNCTION__);
		if (debug)
			printf("ppath is: %s - if shorter than expected, retpath is still destroying the original string value \n",ppath);
		if (debug)
			printf("The host_bus is : %s \n", host_bus);	
		if(!(check_HOST_BUS = retpath(host_bus, ":", 2)))
			err(1, "NULL return from retpath in %s line %d",__FUNCTION__, __LINE__);
		if (debug)
			printf("Field 2 of host_bus is: %s \n", check_HOST_BUS);
		/* since the HPEX47x only has 4 bays and they are located on either ata1 or ata2 with fixed a host bus - allocate them like this. */
		// if( (strcmp(host_bus,"0:0:0:0")) == 0 ) {
		if( (strcmp("ata1",check_ATA)) == 0 && (strcmp("0",check_HOST_BUS)) == 0) {
			if( (ide0.statfile = (char *)calloc(128, sizeof(char))) == NULL)
				err(1, "Unable to allocate statfile for copy from udev_list_entry_get_name() in %s line %d", __FUNCTION__, __LINE__);
			if( !(strcpy(ide0.statfile, statpath))) 
				err(1, "Unable to strcpy() path into statpath in %s line %d ", __FUNCTION__, __LINE__);
			if(debug)
				printf("ide0.statfile is: %s \n", ide0.statfile);
			ide0.hphdd = 1;
			if ( (ide0.rio = retbytes(ide0.statfile, 0)) < 0)
			       err(1, "Error on return from retbytes in %s line %d ", __FUNCTION__, __LINE__);
			if( (ide0.wio = retbytes(ide0.statfile, 4)) < 0)
				err(1, "Error on return from retbytes in %s line %d ", __FUNCTION__, __LINE__);	
			hpex47x[numdisks] = ide0;
			syslog(LOG_NOTICE,"Adding HP Disk 1 to monitor pool.");
			syslog(LOG_NOTICE,"Statfile path for HP Disk 1 is %s",hpex47x[numdisks].statfile);

			if(debug)
				printf("Found HDD1 \n");
		}
		// else if( (strcmp(host_bus,"0:0:1:0")) == 0 ) {
		else if( (strcmp("ata1",check_ATA)) == 0 && (strcmp("1",check_HOST_BUS)) == 0) {
			if( (ide1.statfile = (char *)calloc(128, sizeof(char))) == NULL)
				err(1, "Unable to allocate statfile for copy from udev_list_entry_get_name() in %s line %d", __FUNCTION__, __LINE__);
			if( !(strcpy(ide1.statfile, statpath))) 
				err(1, "Unable to strcpy() path into statpath in %s line %d", __FUNCTION__, __LINE__);
			if(debug)
				printf("ide1.statfile is: %s \n", ide1.statfile);
			ide1.hphdd = 2;
			if( (ide1.rio = retbytes(ide1.statfile, 0)) < 0)
			       err(1, "Error on return from retbytes in %s line %d", __FUNCTION__, __LINE__);
			if( (ide1.wio = retbytes(ide1.statfile, 4)) < 0)
				err(1, "Error on return from retbytes in %s line %d", __FUNCTION__, __LINE__);	
			hpex47x[numdisks] = ide1;
			syslog(LOG_NOTICE,"Adding HP Disk 2 to monitor pool.");
			syslog(LOG_NOTICE,"Statfile path for HP Disk 2 is %s",hpex47x[numdisks].statfile);

			if(debug)
				printf("Found HDD2 \n");
		}
		// else if( (strcmp(host_bus,"1:0:0:0")) == 0 ) {
		else if( (strcmp("ata2",check_ATA)) == 0 && (strcmp("0",check_HOST_BUS)) == 0) {
			if( (ide2.statfile = (char *)calloc(128, sizeof(char))) == NULL)
				err(1, "Unable to allocate statfile for copy from udev_list_entry_get_name() in %s line %d", __FUNCTION__, __LINE__);
			if( !(strcpy(ide2.statfile, statpath))) 
				err(1, "Unable to strcpy() path into statpath in %s line %d", __FUNCTION__, __LINE__);
			if(debug)
				printf("ide2.statfile is: %s \n", ide2.statfile);
			ide2.hphdd = 3;
			if( (ide2.rio = retbytes(ide2.statfile, 0)) < 0)
			       err(1, "Error on return from retbytes in %s line %d", __FUNCTION__, __LINE__);
			if( (ide2.wio = retbytes(ide2.statfile, 4)) < 0)
				err(1, "Error on return from retbytes in %s line %d", __FUNCTION__, __LINE__);	
			hpex47x[numdisks] = ide2;
			syslog(LOG_NOTICE,"Adding HP Disk 3 to monitor pool.");
			syslog(LOG_NOTICE,"Statfile path for HP Disk 3 is %s",hpex47x[numdisks].statfile);

			if(debug)
				printf("Found HDD3 \n");
		}
		// else if( (strcmp(host_bus,"1:0:1:0")) == 0 ) {
		else if( (strcmp("ata2",check_ATA)) == 0 && (strcmp("1",check_HOST_BUS)) == 0) {
			if( (ide3.statfile = (char *)calloc(128, sizeof(char))) == NULL)
				err(1, "Unable to allocate statfile for copy from udev_list_entry_get_name() in %s line %d", __FUNCTION__, __LINE__);
			if( !(strcpy(ide3.statfile, statpath))) 
				err(1, "Unable to strcpy() path into statpath in %s line %d", __FUNCTION__, __LINE__);
			if(debug)
				printf("ide3.statfile is: %s \n", ide3.statfile);
			ide3.hphdd = 4;
			if( (ide3.rio = retbytes(ide3.statfile, 0)) < 0)
			       err(1, "Error on return from retbytes in %s line %d", __FUNCTION__, __LINE__);
			if( (ide3.wio = retbytes(ide3.statfile, 4)) < 0)
				err(1, "Error on return from retbytes in %s line %d", __FUNCTION__, __LINE__);	
			hpex47x[numdisks] = ide3;
			syslog(LOG_NOTICE,"Adding HP Disk 4 to monitor pool.");
			syslog(LOG_NOTICE,"Statfile path for HP Disk 4 is %s",hpex47x[numdisks].statfile);

			if(debug)
				printf("Found HDD4 \n");
		}
		else 
			err(1,"Unknown host bus found during udev_device_get_devpath(dev) in %s line %d", __FUNCTION__, __LINE__);

		free(statpath);
		statpath = NULL;
		free(ppath);
		ppath = NULL;
		dev = udev_device_unref(dev);
		dev = NULL;
		free(host_bus);
		host_bus = NULL;
		free(check_ATA);
		check_ATA = NULL;
		free(check_HOST_BUS);
		check_HOST_BUS = NULL;
		numdisks++;
	}
	/* Free the enumerator object */
	enumerate = udev_enumerate_unref(enumerate);

	udev = udev_unref(udev);

	if( statpath != NULL ) {
		if (debug) 
			printf("statpath is not equal to NULL prior to return free in %s line %d \n", __FUNCTION__, __LINE__);
		free(statpath);
		statpath = NULL;
	}
	if( ppath != NULL ) {
		if(debug)
			printf("ppath is not equal to NULL prior to return free in %s line %d \n", __FUNCTION__, __LINE__);
		free(ppath);
		ppath = NULL;
	}
	int *answer = malloc(sizeof(*answer));
	*answer = numdisks;
	pthread_exit(answer);       
}

/* blue led toggle */
int blt(int led)
{
   encreg = inw(ADDR);
   switch (led) {
      case HDD1:
         encreg &= ~BL1;
         break;
      case HDD2:
         encreg &= ~BL2;
	 break;
      case HDD3:
         encreg &= ~BL3;
	 break;
      case HDD4:
         encreg &= ~BL4;
	 break;
   }

	outw(encreg, ADDR);
	int led_state = 1;
	return(led_state);
}

/* red led toggle */
int rlt(int led)
{
   encreg = inw(ADDR);
   switch (led) {
      case HDD1:
	 encreg &= ~RL1;
         break;
      case HDD2:
	 encreg &= ~RL2;
         break;
      case HDD3:
	 encreg &= ~RL3;
         break;
      case HDD4:
	 encreg &= ~RL4;
         break;
   }
   outw(encreg, ADDR);
   int led_state = 1;
   return(led_state);
}

/* purple led toggle */
int plt(int led)
{
	encreg = inw(ADDR);
	switch (led) {
  	   case HDD1:
 	      encreg &= ~PL1;
	      break;
	   case HDD2:
	      encreg &= ~PL2;
	      break;
	   case HDD3:
  	      encreg &= ~PL3;
	      break;
	   case HDD4:
	      encreg &= ~PL4;
	      break;
	}
	outw(encreg, ADDR);
	int led_state = 1;
	return(led_state);
}
/* turn off all led */
int offled(int led, int off_state)
{
	/* 1 = blue    2 = red    3 = purple */
	encreg = inw(ADDR);
	switch( off_state ) {
		case 1:
			switch (led) {
				case HDD1:
					encreg |= BL1;
					break;
				case HDD2:
					encreg |= BL2;
					break;
				case HDD3:
					encreg |= BL3;
					break;
				case HDD4:
					encreg |= BL4;
					break;
				}
			break;
		case 2:
			switch (led) {
				case HDD1:
					encreg |= RL1;
					break;
				case HDD2:
					encreg |= RL2;
					break;
				case HDD3:
					encreg |= RL3;
					break;
				case HDD4:
					encreg |= RL4;
					break;
				}
			break;
		case 3:
			switch (led) {
				case HDD1:
					encreg |= PL1;
					break;
				case HDD2:
					encreg |= PL2;
					break;
				case HDD3:
					encreg |= PL3;
					break;
				case HDD4:
					encreg |= PL4;
					break;
			}
			break;

	}
	outw(encreg, ADDR);
	int led_state = 0;
	return(led_state);
}

void start_led(void) 
{
	size_t numtimes = 3;
	size_t color = 1;
	size_t i,j = 0;
	struct timespec tv = { .tv_sec = 0, .tv_nsec = 30000000 };
	
	outw(CTL, ADDR);
	assert(nanosleep(&tv, NULL) >= 0);

	while( numtimes >= 1) {
		for( i = 1; i<=HPDISKS; i++){
			led_set(i, color, 1);
			nanosleep(&tv, NULL);
		}
		for( j = i; j >= 1; j--) {
			led_set(j, 4, color);
			nanosleep(&tv, NULL);
		}
		++color;
		--numtimes;

	}
	nanosleep(&tv, NULL);

	numtimes = 1;
	color = 1;
	while( numtimes <= 3) {
		for( i = 4; i >= 1; i--){
			led_set(i, color, 1);
			nanosleep(&tv, NULL);
		}
		for( j = i; j <= 4; j++) {
			led_set(j, 4, color);
			nanosleep(&tv, NULL);
		}
		++color;
		++numtimes;

	}
	outw(CTL, ADDR);	
}

char* curdir(char *str)
{
	char *cp = strrchr(str, '/');
	return cp ? cp+1 : str;
}

int show_help(char * progname ) 
{

	char *this = curdir(progname);
	printf("%s %s %s", "Usage: ", this,"\n");
	printf("-d, --debug 	Print Debug Messages\n");
	printf("-D, --daemon 	Detach and Run as a Daemon - do not use this in service setup \n");
	printf("-h, --help	Print This Message\n");
	printf("-v, --version	Print Version Information\n");

    return 0;
}

int show_version(char * progname ) 
{
	char *this = curdir(progname);
    printf("%s %s %s %s %s %s",this,"Version 1.0.2 compiled on", __DATE__,"at", __TIME__ ,"\n") ;
    return 0;
}

void drop_priviledges( void ) 
{
	struct passwd* pw = getpwnam( "nobody" );
	if ( !pw ) return; /* huh? */
	if ( (setgid( pw->pw_gid )) && (setuid( pw->pw_uid )) != 0 )
		err(1, "Unable to set gid or uid to nobody");

	if(debug) {
		printf("Successfully dropped priviledges to %s \n",pw->pw_name);
		printf("We should now be safe to continue \n");
	}
}

int led_set(int hphdd, int color, int offstate) 
{
	/* we don't use offstate except to turn off the LEDs */
	/* we spin here with another spinlock on led_set */
	if( (pthread_spin_lock(&hpex47x_gpio_lock2)) != 0 )
		err(1,"invalid return from pthread_spin_lock in %s line %d", __FUNCTION__, __LINE__);

	int led_return = 0;
	switch( color ) {
		case 1:
				led_return = blt(hphdd);
				break;
		case 2:
				led_return = rlt(hphdd);
				break;
		case 3:
				led_return = plt(hphdd);
				break;
		case 4:
				led_return = offled(hphdd, offstate);
				break;
		default:
				led_return = offled(hphdd, offstate);
	}
	
	if( (pthread_spin_unlock(&hpex47x_gpio_lock2)) != 0)
		err(1, "invalid return from pthread_spin_unlock in %s line %d", __FUNCTION__, __LINE__);

	return led_return;
}

int main (int argc, char** argv) 
{

	size_t run_as_daemon = 0;
	size_t num_threads = 0;
	
	// Thread IDs
	pthread_t tid;

	if (geteuid() !=0 ) {
		printf("Try running as root to avoid Segfault and core dump \n");
		errx(1, "not running as root user");
	}

	// long command line arguments
	//
    const struct option long_opts[] = {
        { "debug",          no_argument,       0, 'd' },
        { "daemon",         no_argument,       0, 'D' },
        { "help",           no_argument,       0, 'h' },
        { "version",        no_argument,       0, 'v' },
        { 0, 0, 0, 0 },
        };

        // pass command line arguments
        while ( 1 ) {
                const int c = getopt_long( argc, argv, "dDhv?", long_opts, 0 );
                if ( -1 == c ) break;

        	switch ( c ) {
				case 'D': // daemon
						run_as_daemon++;
						break;
                case 'd': // debug
                        debug++;
                        break;
                case 'h': // help!
                        return show_help(argv[0]);
                case 'v': // our version
                        return show_version(argv[0] );
                case '?': // no idea
                        return show_help(argv[0] );
                default:
                      printf("++++++....\n"); 
                }
        }
	
	openlog("hpex47xled:", LOG_CONS | LOG_PID, LOG_DAEMON );

	if( (pthread_spin_init(&hpex47x_gpio_lock, PTHREAD_PROCESS_PRIVATE)) !=0 )
		err(1,"Unable to initialize spin_lock in %s at %d", __FUNCTION__, __LINE__);
	if( (pthread_spin_init(&hpex47x_gpio_lock2, PTHREAD_PROCESS_PRIVATE)) !=0 )
		err(1,"Unable to initialize spin_lock in %s at %d", __FUNCTION__, __LINE__);	

	if ((pthread_attr_init(&attr)) < 0 )
		err(1, "Unable to execute pthread_attr_init(&attr) in %s line %d", __FUNCTION__, __LINE__);

	if( (pthread_create(&tid, &attr, hpex47x_init, NULL)) != 0)
		err(1, "Unable to init in main - bad return from hpex47x_init() in %s line %d ", __FUNCTION__, __LINE__);

	if (ioperm(ADDR,8,1)) {
		perror("ioperm"); 
		exit(1);
	}
	
	signal( SIGTERM, sigterm_handler);
	signal( SIGINT, sigterm_handler);
	signal( SIGQUIT, sigterm_handler);
	signal( SIGILL, sigterm_handler);

	syslog(LOG_NOTICE,"Initializing %s %s %s %s %s ", curdir(argv[0]),"compiled on", __DATE__,"at", __TIME__);

	/* Try and drop root priviledges now that we have initialized */
	drop_priviledges();

	if( (pthread_join(tid,(void**)&hpdisks)) != 0)
		err(1, "Unable to rejoin thread prior to execution in %s line %d",__FUNCTION__, __LINE__);

	if (debug)
		printf("Disks are now: %i \n", *hpdisks);

	if ( run_as_daemon ) {
		if (daemon( 0, 0 ) > 0 )
			err(1, "Unable to daemonize :");
		syslog(LOG_NOTICE,"Forking to background, running in daemon mode");
	}
	
	start_led();

	for(size_t i = 0; i < *hpdisks; i++) {
        if ( (pthread_create(&hpexled_led[i], &attr, &hpex47x_thread_run, &hpex47x[i])) != 0)
			err(1, "Unable to create thread for hpex47x_thread_run in %s line %d", __FUNCTION__, __LINE__);
        ++num_threads;
        if(debug)
			printf("HP HDD is %li - created thread %li \n", hpex47x[i].hphdd, num_threads);
    }

	syslog(LOG_NOTICE,"Initialized monitor threads. Monitoring with %li threads", num_threads);
	syslog(LOG_NOTICE,"Initialized. Now monitoring for drive activity - Enjoy the light show!");

	for(size_t i = 0; i < *hpdisks; i++) {
        if ( (pthread_join(hpexled_led[i], NULL)) != 0)
			err(1, "Unable to join threads - pthread_join in main() before close in %s line %d", __FUNCTION__, __LINE__);
    }

	thread_run = 0; /* kludge - tell any threads that may still be running to end */
	
	for(size_t a = 0; a < *hpdisks; a++) {
		free(hpex47x[a].statfile);
		hpex47x[a].statfile = NULL;		
	}
	outw(CTL, ADDR);
	syslog(LOG_NOTICE,"Standard Close of Program");	
	pthread_spin_destroy(&hpex47x_gpio_lock);
	pthread_spin_destroy(&hpex47x_gpio_lock2);
	pthread_attr_destroy(&attr);
	ioperm(ADDR, 8, 0);
	closelog();
	free(hpdisks);
	hpdisks = NULL;
	return 0;       
}

void sigterm_handler(int s)
{
	thread_run = 0; /* kludge - tell any threads that may still be running to end */
	if( hpdisks != NULL) {
		for(size_t i = 0; i < *hpdisks; i++) {
			if ( (pthread_join(hpexled_led[i], NULL)) != 0)
				err(1, "Unable to join threads - pthread_join in main() before close in %s line %d", __FUNCTION__, __LINE__);
		}

	}	
	if( (pthread_spin_destroy(&hpex47x_gpio_lock)) != 0 )
		perror("pthread_spin_destroy lock 1");
	if( (pthread_spin_destroy(&hpex47x_gpio_lock2)) != 0 )
		perror("pthread_spin_destroy lock 2");

	if (hpdisks != NULL) {

		for(int a = 0; a < *hpdisks; a++) {
			free(hpex47x[a].statfile);
			hpex47x[a].statfile = NULL;		
		}
	}
	pthread_attr_destroy(&attr);
	outw(CTL,ADDR);
	ioperm(ADDR, 8, 0);
	free(hpdisks);
	hpdisks = NULL;
	syslog(LOG_NOTICE,"Shutting Down on Signal");
	closelog();
	errx(0, "Exiting From Signal Handler");
}
