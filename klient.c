/* Łukasz Piesiewicz 334978
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <string.h>
#include "err.h"
#include "messageData.h"

int IPCs[2];
long int type;

void getIPCs()
{
	if ((IPCs[out] = msgget(S_KEY, 0)) == -1)
		syserr("msgget");
	
	if ((IPCs[in] = msgget(K_KEY, 0)) == -1)
		syserr("msgget");
}

void sendBeginProtocol(pid_t myPid)
{
	Mesg msg;
	msg.mesg_type = server_t;
	sprintf(msg.mesg_data, "%d\n", myPid);
	if (msgsnd(IPCs[out], (char *) &msg, strlen(msg.mesg_data), 0) != 0)
		syserr("msgsnd");
}

void requestResources(int k, int n)
{
	Mesg msg;
	msg.mesg_type = type;
	sprintf(msg.mesg_data, requestFormat, k, n);
	if (msgsnd(IPCs[out], (char *) &msg, strlen(msg.mesg_data), 0) != 0)
		syserr("msgsnd");
}

void sendEndProtocol()
{
	Mesg msg;
	msg.mesg_type = type;
	sprintf(msg.mesg_data, "THXBRO\n");
	if (msgsnd(IPCs[out], (char *) &msg, strlen(msg.mesg_data), 0) != 0)
		syserr("msgsnd");
}

pid_t getResources()
{
	Mesg msg;
	if (msgrcv(IPCs[in], &msg, MAXMESGDATA, type, 0) <= 0)
		syserr("msgrcv");
	
	if (strcmp(msg.mesg_data, ERROR) == 0)
		syserr("Server Closed\n");
	
	return atoi(msg.mesg_data);
}

void work(int s)
{
	sleep(s);
}

void report(int k, int n, pid_t myPid, pid_t otherPid)
{
	printf("%d %d %d %d\n", k, n, myPid, otherPid);
}

int main(int argc, char **argv)
{
	if (argc != 4)
		syserr("Wrong number of parameters");
	
	pid_t myPid = getpid(), otherPid;
	type = myPid;
	int k = atoi(argv[1]);
	int n = atoi(argv[2]);
	int s = atoi(argv[3]);
	
	getIPCs();
	sendBeginProtocol(myPid);
	printf("Send BP\n");
	requestResources(k, n);
	printf("RequestMade\n");
	otherPid = getResources();
	report(k, n, myPid, otherPid);
	work(s);
	sendEndProtocol();
	return 0;
}