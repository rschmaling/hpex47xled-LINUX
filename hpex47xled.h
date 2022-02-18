#include <libudev.h>
#include <stdio.h>
#include <stdlib.h>
#include <locale.h>
#include <unistd.h>
#include <inttypes.h>
#include <string.h>
#include <signal.h>
#include <err.h>
#include <syslog.h>
#include <getopt.h>
#include <sys/io.h>
#include <pwd.h>
#include <pthread.h>

/* defines */
/*
#define BL1      0x0001     // first blue led                           1
#define BL2      0x0002     // second blue led                          2
#define UNKNOWN1 0x0004     // unknown                                  3
#define BL3      0x0008     // third blue led                           4
#define UNKNOWN2 0x0010     // unknown                                  5
#define BL4      0x0020     // fourth blue led                          6
#define UNKNOWN3 0x0040     // unknown                                  7
#define FLASH    0x0080     // hides (0)/shows(1) onboard flash disk    8
#define RL2      0x0100     // second red led                           9
#define RL3      0x0200     // third red led                            10
#define RL4      0x0400     // fourth red led                           11
#define UNKNOWN4 0x0800     // unknown                                  12
#define RL1      0x1000     // first red led                            13
#define PL1      (BL1 | RL1)// first purple led
#define PL2      (BL2 | RL2)// second purple led
#define PL3      (BL3 | RL3)// third purple led
#define PL4      (BL4 | RL4)// forth purple led
*/

#define ADDR   0x1064 // io address
#define CTL   0xffff // defaults
#define   BL1   0x0001 // first blue led
#define   BL2   0x0002 // second blue led
#define LEDOFF   0x0004 // turns off all leds
#define   BL3   0x0008 // third blue led
#define LEDOFF2   0x0010 // turns off all leds
#define   BL4   0x0020 // fourth blue led
#define LEDOFF3   0x0040 // turns off all leds
#define FLASH   0x0080 // hides/shows onboard flash disk
#define RL2   0x0100 // second red led
#define RL3   0x0200 // third red led
#define RL4   0x0400 // fourth red led
#define W4   0x0800 // led off ?
#define RL1   0x1000 // first red led
#define W6   0x2000 // led off ?
#define W7   0x4000 // led off ?
#define W8   0x8000 // led off ?
#define PL1      (BL1 | RL1)// first purple led
#define PL2      (BL2 | RL2)// second purple led
#define PL3      (BL3 | RL3)// third purple led
#define PL4      (BL4 | RL4)// forth purple led

#define HDD1   1
#define HDD2   2
#define HDD3   3
#define HDD4   4

struct hpled {
        char* statfile;
        int hphdd;
        int64_t rio;
        int64_t wio;
};

void sigterm_handler(int s);
int64_t retbytes( char* statfile, int field );
void* hpex47x_init(void *);
char* curdir(char *str);
int show_help(char * progname );
int show_version(char * progname );
char* retpath( char* parent, char *delim, int field );
void drop_priviledges( void );
int blt(int led);
int rlt(int led);
int plt(int led);
int offled(int led);

