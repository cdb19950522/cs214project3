#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <dirent.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/sendfile.h>
#include <fcntl.h>
#include <pthread.h>
#include <sys/time.h>
#include <signal.h>
#include <errno.h>


void jointhreads();
void *receive(void *args);
char ***readdata(char * file, int filecounter);
void *twowaymerge(void *context);
void kwaymerge();
void senddata();

#define min(a, b) ((a) < (b) ? (a) : (b))
#define max(a, b) ((a) > (b) ? (a) : (b))

#include "mergesort.c"
void mergesort(char ***s, int low, int high, char type, int tosort);


