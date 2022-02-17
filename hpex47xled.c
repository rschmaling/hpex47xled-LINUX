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

#include "hpex47xled.h"

#define BUFFER_SIZE 1024 

static struct hpled ide0, ide1, ide2, ide3 ;
static struct hpled hpex47x[4];
static int debug = 0;
static int hpdisks = 0;
u_int16_t encreg;


// 0 is read I/Os, 4 is write I/Os in the /sys/devices/* /stat file 

char* retpath( char* parent, char *delim, int field ){

    char *last_token = NULL;
    char *copy_parent = NULL;
    int token = 0;
    char* found = NULL;

	if( (copy_parent = (char *)calloc((strlen(parent) + 2), sizeof(char))) == NULL)
		err(1, "Unable to allocate copy_parnet for copy from char* parent in retpath() ");

	if( !(strcpy(copy_parent, parent)))
		err(1, "Unable to strcpy() parent into copy_parent in retpath() ");

	if(delim == NULL)
		err(1, "Unknown or illegal delimiter in repath()");

	last_token = strtok( copy_parent, delim);

	while( (last_token != NULL) && (token <= field) ){
		if( token == field) {
			// found = last_token;
			if( (found = (char *)calloc((strlen(last_token) + 2), sizeof(char))) == NULL)
				err(1, "Unable to allocate found for copy from char * token in retpath() ");
			if( !(strcpy(found, last_token)))
				err(1, "Unable to strcpy() token into found in retpath() ");
			if(debug)
				printf("The value of retpath field %i is %s \n", token, found);
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

int retbytes( char* statfile, int field ){

    const char *delimiter_characters = " ";
    char buffer[ BUFFER_SIZE ];
    char *last_token;
    int token = 0;
    int found = 0;

    FILE *input_file = fopen( statfile, "r" );

    if( input_file == NULL ) {
		err(1, "Unable to open statfile in retbytes() ");
    }
    else {

        while( (fgets(buffer, BUFFER_SIZE, input_file) != NULL)) {

            // Write the line to stdout
		if( debug ) {
            		fputs( buffer, stdout );
		}

            // Gets each token as a string and prints it
            last_token = strtok( buffer, delimiter_characters );

            while( (last_token != NULL) && (token <= field) ){
                if( token == field) {
                        found = atoi( last_token );

			if(debug)
                        	printf("The value of field %i is %i \n", token, found);
                }
                last_token = strtok( NULL, delimiter_characters );
                token++;
            }

        }

        if( ferror(input_file) ){
            perror( "The following error occurred" );
	    exit(1);
        }

        fclose( input_file );
    }

    return found;

}

int hpex47x_init (void) {

	struct udev *udev = NULL;
	struct udev_enumerate *enumerate = NULL;
	struct udev_monitor *device_monitor = NULL;
	struct udev_list_entry *devices = NULL;
	struct udev_device *dev = NULL;
	char *statpath = NULL;
	char *ppath = NULL;
	int numdisks = 0;
	// int readio, writeio;

	udev = udev_new();

	if (!udev)
		err(1, "Unable to create struct udev in hpex47x_init() ");
	
	/* Create a list of the devices in the 'hidraw' subsystem. */
	enumerate = udev_enumerate_new(udev); 
	device_monitor = udev_monitor_new_from_netlink(udev, "udev");

	if(!device_monitor)
		err(1, "Cannot create device_monitor in hpex47x_init() ");

	if( udev_monitor_filter_add_match_subsystem_devtype(device_monitor, "scsi", "scsi_device") )
		err(1, "Unable to build filter add match in hpex47x_init() ");

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
			err(1, "Unable to retrieve dev from udev_device_new_from_syspath in hpex47x_init() ");

		if( (!(strcmp("ata", udev_device_get_property_value(dev, "ID_BUS")) == 0) && (strcmp("disk", udev_device_get_devtype(dev)) == 0)) || (strcmp("partition", udev_device_get_devtype(dev)) == 0) ) {
			dev = udev_device_unref(dev);
			continue;
		}
		if( (statpath = (char *)calloc(128, sizeof(char))) == NULL)
			err(1, "Unable to allocate statfile for copy from udev_list_entry_get_name() in hpex47x_init() ");

		if( !(strcpy(statpath, path))) 
			err(1, "Unable to strcpy() path into statpath in hpex47x_init() ");
		
		if( (strcat(statpath, "/stat")) == NULL) 
			err(1, "Unable to concatinate /stat to path in hpex47x_init()");

		if ( debug ) {
			printf("Device Node (/dev) path: %s\n", udev_device_get_devnode(dev));
			printf("Device sys path is: %s \n", path);
			printf("Device stat file is at: %s \n", statpath);
			printf("Device type is: %s \n", udev_device_get_devtype(dev));
		}
		dev = udev_device_get_parent(dev);
		if (!dev)
			err(1, "Unable to find parent path of scsi device in hpex47x_init() ");

		if( (ppath = (char *)calloc( 128 , sizeof(char)) ) == NULL )
			err(1, "Unable to allocate statfile for copy from udev_list_entry_get_name() in hpex47x_init() ");

		if( !(strcpy(ppath, udev_device_get_devpath(dev)))) 
			err(1, "Unable to memcpy() path into statpath in hpex47x_init() ");

		if (debug)
			printf("Device parent path is: %s \n", ppath);
		
		if( !(host_bus = retpath(ppath,"/",6)))
			err(1, "NULL return from retpath in hpex47x_init()");
		if (debug)
			printf("ppath is: %s - if shorter than expected, retpath is still destroying the original string value \n",ppath);
		if (debug)
			printf("The host_bus is : %s \n", host_bus);	
		/* since the HPEX47x only has 4 bays and they are located on either ata1 or ata2 with fixed a host bus - allocate them like this. */
		if( (strcmp(host_bus,"0:0:0:0")) == 0 ) {

			if( (ide0.statfile = (char *)calloc(128, sizeof(char))) == NULL)
				err(1, "Unable to allocate statfile for copy from udev_list_entry_get_name() in hpex47x_init() ");
			if( !(strcpy(ide0.statfile, statpath))) 
				err(1, "Unable to strcpy() path into statpath in hpex47x_init() ");
			if(debug)
				printf("ide0.statfile is: %s \n", ide0.statfile);
			ide0.hphdd = 1;
			if ( (ide0.rio = retbytes(statpath, 0)) <= 0)
			       err(1, "Error on return from retbytes in hpex47x_init() ");
			if( (ide0.wio = retbytes(statpath, 4)) <= 0)
				err(1, "Error on return from retbytes in hpex47x_init() ");	
			hpex47x[numdisks] = ide0;
			syslog(LOG_NOTICE,"Adding HP Disk 1 to monitor pool.");
			syslog(LOG_NOTICE,"Statfile path for HP Disk 1 is %s",hpex47x[numdisks].statfile);

			if(debug)
				printf("Found HDD1 \n");
		}
		else if( (strcmp(host_bus,"0:0:1:0")) == 0 ) {
			if( (ide1.statfile = (char *)calloc(128, sizeof(char))) == NULL)
				err(1, "Unable to allocate statfile for copy from udev_list_entry_get_name() in hpex47x_init() ");
			if( !(strcpy(ide1.statfile, statpath))) 
				err(1, "Unable to strcpy() path into statpath in hpex47x_init() ");
			if(debug)
				printf("ide1.statfile is: %s \n", ide1.statfile);
			ide1.hphdd = 2;
			if( (ide1.rio = retbytes(statpath, 0)) <= 0)
			       err(1, "Error on return from retbytes in hpex47x_init() ");
			if( (ide1.wio = retbytes(statpath, 4)) <= 0)
				err(1, "Error on return from retbytes in hpex47x_init() ");	
			hpex47x[numdisks] = ide1;
			syslog(LOG_NOTICE,"Adding HP Disk 2 to monitor pool.");
			syslog(LOG_NOTICE,"Statfile path for HP Disk 2 is %s",hpex47x[numdisks].statfile);

			if(debug)
				printf("Found HDD2 \n");
		}
		else if( (strcmp(host_bus,"1:0:0:0")) == 0 ) {
			if( (ide2.statfile = (char *)calloc(128, sizeof(char))) == NULL)
				err(1, "Unable to allocate statfile for copy from udev_list_entry_get_name() in hpex47x_init() ");
			if( !(strcpy(ide2.statfile, statpath))) 
				err(1, "Unable to strcpy() path into statpath in hpex47x_init() ");
			if(debug)
				printf("ide2.statfile is: %s \n", ide2.statfile);
			ide2.hphdd = 3;
			if( (ide2.rio = retbytes(statpath, 0)) <= 0)
			       err(1, "Error on return from retbytes in hpex47x_init() ");
			if( (ide2.wio = retbytes(statpath, 4)) <= 0)
				err(1, "Error on return from retbytes in hpex47x_init() ");	
			hpex47x[numdisks] = ide2;
			syslog(LOG_NOTICE,"Adding HP Disk 3 to monitor pool.");
			syslog(LOG_NOTICE,"Statfile path for HP Disk 3 is %s",hpex47x[numdisks].statfile);

			if(debug)
				printf("Found HDD3 \n");
		}
		else if( (strcmp(host_bus,"1:0:1:0")) == 0 ) {
			if( (ide3.statfile = (char *)calloc(128, sizeof(char))) == NULL)
				err(1, "Unable to allocate statfile for copy from udev_list_entry_get_name() in hpex47x_init() ");
			if( !(strcpy(ide3.statfile, statpath))) 
				err(1, "Unable to strcpy() path into statpath in hpex47x_init() ");
			if(debug)
				printf("ide3.statfile is: %s \n", ide3.statfile);
			ide3.hphdd = 4;
			if( (ide3.rio = retbytes(statpath, 0)) <= 0)
			       err(1, "Error on return from retbytes in hpex47x_init() ");
			if( (ide3.wio = retbytes(statpath, 4)) <= 0)
				err(1, "Error on return from retbytes in hpex47x_init() ");	
			hpex47x[numdisks] = ide3;
			syslog(LOG_NOTICE,"Adding HP Disk 4 to monitor pool.");
			syslog(LOG_NOTICE,"Statfile path for HP Disk 4 is %s",hpex47x[numdisks].statfile);

			if(debug)
				printf("Found HDD4 \n");
		}
		else 
			err(1,"Unknown host bus found during udev_device_get_devpath(dev)");

		free(statpath);
		statpath = NULL;
		free(ppath);
		ppath = NULL;
		dev = udev_device_unref(dev);
		dev = NULL;
		free(host_bus);
		host_bus = NULL;
		numdisks++;
	}
	/* Free the enumerator object */
	enumerate = udev_enumerate_unref(enumerate);

	udev = udev_unref(udev);

	device_monitor = udev_monitor_unref(device_monitor);

	if( statpath != NULL ) {
		if (debug) 
			printf("statpath is not equal to NULL prior to return free in hpex47x_init() \n");
		free(statpath);
		statpath = NULL;
	}
	if( ppath != NULL ) {
		if(debug)
			printf("ppath is not equal to NULL prior to return free in hpex47x_init() \n");
		free(ppath);
		ppath = NULL;
	}

	return numdisks;       
}

/* blue led toggle */
int blt(int led)
{
   switch (led) {
      case HDD1:
         encreg = encreg ^ BL1;
         break;
      case HDD2:
         encreg = encreg ^ BL2;
         break;
      case HDD3:
         encreg = encreg ^ BL3;
         break;
      case HDD4:
         encreg = encreg ^ BL4;
         break;
   }
   outw(encreg, ADDR);
   return(0);
}

/* red led toggle */
int rlt(int led)
{
   switch (led) {
      case HDD1:
         encreg = encreg ^ RL1;
         break;
      case HDD2:
         encreg = encreg ^ RL2;
         break;
      case HDD3:
         encreg = encreg ^ RL3;
         break;
      case HDD4:
         encreg = encreg ^ RL4;
         break;
   }
   outw(encreg, ADDR);
   return(0);
}

/* purple led toggle */
int plt(int led)
{
	switch (led) {
  	   case HDD1:
	      encreg = encreg ^ PL1;
	      break;
	   case HDD2:
	      encreg = encreg ^ PL2;
	      break;
	   case HDD3:
	      encreg = encreg ^ PL3;
	      break;
	   case HDD4:
	      encreg = encreg ^ PL4;
	      break;
	}
	outw(encreg, ADDR);
	return(0);
}
/* turn off all led */
int offled(int led)
{
	// usleep(100000);
	usleep(85000);
	/* doing this until I can figure out how to turn off the each light individually */
	encreg = CTL;
	outw(encreg, ADDR);
	return(0);
}

char* curdir(char *str)
{
	char *cp = strrchr(str, '/');
	return cp ? cp+1 : str;
}

int show_help(char * progname ) {

	char *this = curdir(progname);
	printf("%s %s %s", "Usage: ", this,"\n");
	printf("-d, --debug 	Print Debug Messages\n");
	printf("-D, --daemon 	Detach and Run as a Daemon - do not use this in service setup \n");
	printf("-h, --help	Print This Message\n");
	printf("-v, --version	Print Version Information\n");

       return 0;
}

int show_version(char * progname ) {
	char *this = curdir(progname);
        printf("%s %s %s %s %s %s",this,"Version 0.0.1 compiled on", __DATE__,"at", __TIME__ ,"\n") ;
        return 0;
}

void drop_priviledges( void ) {
	struct passwd* pw = getpwnam( "nobody" );
	if ( !pw ) return; /* huh? */
	if ( (setgid( pw->pw_gid )) && (setuid( pw->pw_uid )) != 0 )
		err(1, "Unable to set gid or uid to nobody");

	if(debug) {
		printf("Successfully dropped priviledges to %s \n",pw->pw_name);
		printf("We should now be safe to continue \n");
	}
}


int main (int argc, char** argv) {

	static int run_as_daemon = 0;
	static int n_rio, n_wio;

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
	if (ioperm(ADDR,16,1)) {
		perror("ioperm"); 
		exit(1);
	}
	encreg = CTL;
	signal( SIGTERM, sigterm_handler);
	signal( SIGINT, sigterm_handler);
	signal( SIGQUIT, sigterm_handler);
	signal( SIGILL, sigterm_handler);
	syslog(LOG_NOTICE,"Initializing %s %s %s %s %s ", curdir(argv[0]),"Version 0.0.1 compiled on", __DATE__,"at", __TIME__);

	if ( (hpdisks = hpex47x_init()) <= 0) 
		err(1, "Unable to init in main - bad return from hpex47x_init() ");
	if (debug)
		printf("Disks are now: %i \n", hpdisks);

	/* Try and drop root priviledges now that we have initialized */
	drop_priviledges();

	syslog(LOG_NOTICE,"Initialized. Now monitoring for drive activity - Enjoy the light show!");

	if ( run_as_daemon ) {
		if (daemon( 0, 0 ) > 0 )
			err(1, "Unable to daemonize :");
		syslog(LOG_NOTICE,"Forking to background, running in daemon mode");
	}

	while(1) {
		for(int a = 0; a < hpdisks; a++) {
			n_rio = retbytes(hpex47x[a].statfile, 0);
			n_wio = retbytes(hpex47x[a].statfile, 4);

			if(debug)
				printf("the disk is: %i \n", hpex47x[a].hphdd);

			if( ( hpex47x[a].rio != n_rio ) && ( hpex47x[a].wio != n_wio) ) {
				hpex47x[a].rio = n_rio;
				hpex47x[a].wio = n_wio;
				if(debug) {
					printf("Read I/O = %i Write I/O = %i \n", n_rio, n_wio);
					printf("HP HDD is: %i \n", hpex47x[a].hphdd);
				}
				plt(hpex47x[a].hphdd);
			}
			else if( hpex47x[a].rio != n_rio ) {
				hpex47x[a].rio = n_rio;
				if(debug) {
					printf("Read I/O only and is: %i \n", n_rio);
					printf("HP HDD is: %i \n", hpex47x[a].hphdd);
				}
				plt(hpex47x[a].hphdd);
			}
			else if( hpex47x[a].wio != n_wio ) {
				hpex47x[a].wio = n_wio;
				if(debug) {
					printf("Write I/O only and is: %i \n", n_wio);
					printf("HP HDD is: %i \n", hpex47x[a].hphdd);
				}
				blt(hpex47x[a].hphdd);
			}
			else {
				/* turn off all the lights */
				offled(hpex47x[a].hphdd);
			}
			
		}
	}

	syslog(LOG_NOTICE,"Closing Down");	

	for(int a = 0; a < hpdisks; a++) {
		free(hpex47x[a].statfile);
		hpex47x[a].statfile = NULL;		
		offled(hpex47x[a].hphdd);
	}
	ioperm(ADDR, 16, 0);
	closelog();
	return 0;       
}

void sigterm_handler(int s)
{
	syslog(LOG_NOTICE,"Closing Down due to signal");
	closelog();
	for(int a = 0; a < hpdisks; a++) {
		free(hpex47x[a].statfile);
		hpex47x[a].statfile = NULL;		
		offled(hpex47x[a].hphdd);
	}
	ioperm(ADDR, 16, 0);
	err(0, "Exiting from signal");
}