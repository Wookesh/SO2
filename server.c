#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <unistd.h>
#include <sys/types.h>
#include <pthread.h>
#include <string.h>
#include "err.h"
#include "messageData.h"

int * zasoby;
int IPCs[2];

void cerr(char * msg)
{
	char i = 0;
	while (*(msg + i) != '\0')
		++i;
	write(2, msg, i);
}

void report(pthread_t thread, int m, int n, int k, pid_t PID1, pid_t PID2)
{
	printf("Wątek %lu przydziela %d+%d zasobów %d klientom %d %d, pozostało %d zasobów", thread, m, n, k, PID1, PID2, zasoby[k]);
}

void createResources(int K, int N)
{
	zasoby = (int *)malloc(sizeof(int) * K);
	int i;
	for (i = 0; i < K; ++i)
		zasoby[i] = N;
	cerr("Reources Created\n");
}

void deleteResources()
{
	free(zasoby);
	cerr("Resources Freed\n");
}

void createIPC()
{
	if ((IPCs[in] = msgget(S_KEY, 0666 | IPC_CREAT | IPC_EXCL)) == -1)
		syserr("msgget IPCs IN", S_KEY);
	
	if ((IPCs[out] = msgget(K_KEY, 0666 | IPC_CREAT | IPC_EXCL)) == -1)
		syserr("msgget IPCs OUT", K_KEY);
	cerr("IPCs Created\n");
}

void closeIPC()
{
	if (msgctl(IPCs[in], IPC_RMID, 0) == -1)
		syserr("msgctl RMID IPCs IN");
	
	if (msgctl(IPCs[out], IPC_RMID, 0) == -1)
		syserr("msgctl RMID IPCs OUT");
	cerr("IPCs Closed\n");
}

void endSafe()
{
	deleteResources();
	closeIPC();
}

void exitServer(int sig)
{
	endSafe();
	exit(0);
}

long getNewClientMsgType()
{
	Mesg msg;
	if (msgrcv(IPCs[in], &msg, MAXMESGDATA, server_t, 0) <= 0)
		syserr("msgrcv");
	
	cerr("Receiving transmission : ");
	cerr(msg.mesg_data);
	return atol(msg.mesg_data);
}

void sendResources(long msgType, pid_t otherClientPid)
{
	Mesg msg;
	msg.mesg_type = msgType;
	sprintf(msg.mesg_data, "%d", otherClientPid);
	if (msgsnd(IPCs[out], (char *) &msg, strlen(msg.mesg_data), 0) != 0)
		syserr("msgsnd");
}

void waitForResources(long msgType)
{
	Mesg msg;
	if (msgrcv(IPCs[in], &msg, MAXMESGDATA, msgType, 0) <= 0)
		syserr("msgrcv");
}

void createClientThread(long msgType)
{
	
}

int main(int argc, char **argv) 
{
	if (argc != 3)
		syserr("Wrong number of parameters");
	
	if (signal(SIGINT,  exitServer) == SIG_ERR)
		syserr("signal");
	
	int K = atoi(argv[1]);
	int N = atoi(argv[2]);
	int newMsgType;
	createResources(K, N);
	createIPC();
	
	cerr("Server prepared to work\n");
	
	for (;;) {
		newMsgType = getNewClientMsgType();
		createClientThread(newMsgType);
	}
	
	
	cerr("Server turning Off\n");
	
	endSafe();
	return 0;
}
