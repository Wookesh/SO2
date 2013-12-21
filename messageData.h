/* Łukasz Piesiewicz 334978
 */

#ifndef MESSAGEDATA_H
#define MESSAGEDATA_H

#include <sys/ipc.h>
#include <sys/msg.h>

#define MAXMESGDATA 4000
#define S_KEY 108L
#define K_KEY 1337L

long int server_t = 69L;
char ERROR[] = "ERROR";
char requestFormat[] = "%d %d";
enum mIPC {
	in = 0,
	out = 1
};

typedef struct {
	long mesg_type;
	char mesg_data[MAXMESGDATA];
} Mesg;

#endif