#define _GNU_SOURCE
// #include <stdio.h>
// #include <unistd.h>
// #include <string.h>
// #include <stdlib.h>
// #include <assert.h>
// #include <sys/stat.h>
// #include <pthread.h>
// #include <err.h>
#include "hpex47xled.h"

int update_thread_instance = 0;
extern int debug;

int rand_drive(void)
{
        size_t myRandomNumber = -1;
        unsigned long seed = (unsigned)time((time_t *)NULL);

        // FILE *fp = fopen("/dev/urandom", "r");
        // fread(&myRandomValue, sizeof(int64_t), 1, fp);
        // fclose(fp);
        // srandom(myRandomValue);
        srandom(seed);

        myRandomNumber = random() % 4;

        if(debug) printf("In update monitor thread - random drive number is %ld from %s line %d", myRandomNumber, __FUNCTION__, __LINE__);
        return myRandomNumber;
}
char* retfield( char* parent, char *delim, int field )
{

    char *last_token = NULL;
    char *copy_parent = NULL;
    int token = 0;
    char* found = NULL;

	if( (copy_parent = (char *)calloc((strlen(parent) + 2), sizeof(char))) == NULL)
		err(1, "Unable to allocate copy_parnet for copy from char* parent in %s ", __FUNCTION__);

	if( !(strcpy(copy_parent, parent)))
		err(1, "Unable to strcpy() parent into copy_parent in %s", __FUNCTION__);

	if(delim == NULL)
		err(1, "Unknown or illegal delimiter in %s", __FUNCTION__);

	last_token = strtok( copy_parent, delim);

	while( (last_token != NULL) && (token <= field) ){
		if( token == field) {
			// found = last_token;
			if( (found = (char *)calloc((strlen(last_token) + 2), sizeof(char))) == NULL)
				err(1, "Unable to allocate found for copy from char * token in %s ", __FUNCTION__);
			if( !(strcpy(found, last_token)))
				err(1, "Unable to strcpy() token into found in %s ", __FUNCTION__);
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

int status_update(int *update_count, int *security_update_count)
{
	char *str_line = NULL;
        char *update = NULL;
        char *security = NULL;

        FILE* apt_check = popen("/usr/lib/update-notifier/apt-check 2>&1", "r");
        if (apt_check == NULL)
        {
                printf("apt-check does not exist or can't be read.\n");
                return 0;
        }
        char* line = NULL;
        size_t len = 0;
        int res = getline(&line, &len, apt_check);
        pclose(apt_check);
        if (res == -1 || len < 3)
        {
                printf("Couldn't read line. res = %d len = %ld  line = %s \n", res, len, line); 
                return 0;
        }
	if ( asprintf(&str_line, "%s", line) == -1){ 
		printf("unable to copy line into str_line in asprintf() \n");
		return 0;
	}
        printf("str_line is : %s in %s line %d \n", str_line, __FUNCTION__, __LINE__);
        update = retfield(str_line, ";", 0);
        security = retfield(str_line, ";", 1);
	*update_count = atoi(update);
        *security_update_count = atoi(security);
        
        if(update != NULL){
                free(update);
                update = NULL;
        }

        if(security != NULL){
                free(security);    
                security = NULL;  
        }
        if(str_line != NULL){
                free(str_line);
                str_line = NULL;
        }
        if(line != NULL){
                free(line);
                line = NULL;
        }
        return 1;
}

int reboot_required(void)
{
        struct stat buffer;
        const char *reboot = "/var/run/reboot-required";
        
        return ( (stat(reboot, &buffer)) );
}

void thread_cleanup_handler(void *arg)
{
        update_thread_instance = 0;
        for(int i = 0; i < 4; i++){
           led_set( i, LED_CASE_OFF, RED_CASE);     
        }
        syslog(LOG_NOTICE,"Update Monitor Thread Cleaned Up and Ending");
        if(debug) printf("\n\n\nUpdate Monitor Thread Ending in %s line %d\n",__FUNCTION__, __LINE__);
}

void *update_monitor_thread(void *arg)
{
        size_t lastdrive = -1;
        update_thread_instance = 1;
        pthread_cleanup_push(thread_cleanup_handler, NULL);
        
        syslog(LOG_NOTICE,"Initialized. Now Monitoring for Updates with the Update Monitor Thread");
        
        if(debug) printf("\n\nUpdate Monitor Thread Executing\n");
        
        while(update_thread_instance){
                if(pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, NULL) != 0)
                        err(1, "Unable to set pthread_setcancelstate to disable in %s line %d", __FUNCTION__, __LINE__);
                int update_count = -1, security_update_count = -1;
                
                if(lastdrive >= 0)
                        led_set(lastdrive, LED_CASE_OFF, RED_CASE);

                lastdrive = rand_drive();
                assert(lastdrive < 4);
                
                if( status_update(&update_count, &security_update_count) != 1 ) {

                        break;
                }
                if(reboot_required() == 0 || security_update_count > 0 || update_count > 0){ 
                        assert(lastdrive >= 0);
                        led_set(lastdrive, RED_CASE, RED_CASE);
                        syslog(LOG_NOTICE,"UPDATE MONITOR THREAD: Updates: %d Security Updates: %d Reboot Required: %s", update_count, security_update_count, (reboot_required() == 0) ? "YES" : "NO");

                }
                else {
                        led_set(lastdrive, LED_CASE_OFF, RED_CASE);
                }
                if (pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL) != 0)
                        err(1, "Unable to set pthread_setcancelstate to enable in %s line %d", __FUNCTION__, __LINE__);
                sleep(900);
        }        
        pthread_cleanup_pop(1); //Remove handler and execute it.
        if(debug) printf("\n\n\nUpdate Monitor Thread Ending\n in %s line %d\n",__FUNCTION__, __LINE__);
	return NULL;
}