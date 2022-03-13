#define _GNU_SOURCE
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <assert.h>
#include <sys/stat.h>
#include <pthread.h>
#include <err.h>

int LED_RED, LED_BLUE, LED_ON, LED_OFF;
int led_set(int hphdd, int color, int offstate);
int thread_instance = 0;

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
        thread_instance = 0;
        for(int i = 0; i < 4; i++){
           led_set( i, LED_RED, LED_OFF);     
        }
}

void *update_monitor_thread(void *arg)
{
        thread_instance = 1;
        pthread_cleanup_push(thread_cleanup_handler, NULL);
        while(thread_instance){
                if(pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, NULL) != 0)
                        err(1, "Unable to set pthread_setcancelstate to disable in %s line %d", __FUNCTION__, __LINE__);
                int update_count = -1, security_update_count = -1;
                if( status_update(&update_count, &security_update_count) != 1 ) {

                        break;
                }
                if(reboot_required() == 0){ 
                        led_set(1, LED_RED, LED_ON);
                }
                else if( security_update_count > 0){

                        led_set(1, LED_RED, LED_ON);
                }
                else if( update_count > 0){
                       led_set(1, LED_RED, LED_ON);
                }
                else {
                       led_set(1, LED_RED, LED_OFF);

                }
                if (pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL) != 0)
                        err(1, "Unable to set pthread_setcancelstate to enable in %s line %d", __FUNCTION__, __LINE__);
                sleep(900);
        }        
        pthread_cleanup_pop(1); //Remove handler and execute it.
	return NULL;
}
int led_set(int hphdd, int color, int offstate)
{
       return 1; 
}
int main(int argc, char** argv)
{

	int update_count = -1, security_update_count = -1;
        if( ! status_update(&update_count, &security_update_count) ) {
                printf("Unknown error return from status_update() \n");
                return 0;

        }
        printf("Update Count is : %d \n", update_count);
        printf("Security Update Count is : %d \n", security_update_count);

        if( reboot_required() == 0)
                printf("Reboot is required\n");
        else
                printf("Reboot not required\n");

	return 0;
}
