/* Łukasz Piesiewicz 334978
 */

#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <unistd.h>
#include <sys/types.h>
#include <pthread.h>
#include <string.h>
#include "err.h"
#include "messageData.h"

int IPCs[2];

struct Resources {
	int K;
	int N;
	int error;
	int * resources;
	pthread_cond_t * notEnougthResources;
	pthread_mutex_t * mutex;
};

struct Resources drugs;

void cerr(char * msg)
{
	char i = 0;
	while (*(msg + i) != '\0')
		++i;
	write(2, msg, i);
}

void report(pthread_t thread, int m, int n, int k, pid_t PID1, pid_t PID2)
{
	printf("Wątek %lu przydziela %d+%d zasobów %d klientom %d %d, pozostało %d zasobów", thread, m, n, k, PID1, PID2, drugs.resources[k]);
}

void createResources(int K, int N) 
{
	drugs.K = K;
	drugs.N = N;
	drugs.error = 0;
	drugs.resources = (int *)malloc(sizeof(int) * drugs.K);
	drugs.notEnougthResources = (pthread_cond_t *)malloc(sizeof(pthread_cond_t) * drugs.K);
	drugs.mutex = (pthread_mutex_t *)malloc(sizeof(pthread_mutex_t) * drugs.K);
	int i, errId;
	for (i = 0; i < drugs.K; ++i) {
		drugs.resources[i] = drugs.N;
		if ((errId = pthread_mutex_init(&(drugs.mutex[i]), 0) != 0))
			syserr("%d. Mutex init %d failed", errId, i);
		if ((errId = pthread_cond_init(&(drugs.notEnougthResources[i]), 0)) != 0)
			syserr("&d Cond init %d failed", errId, i);
	}
	
	cerr("Reources Created\n");
}

void initiateThreads(pthread_attr_t * attr)
{
	int errId;
	
	if ((errId = pthread_attr_init(attr)) != 0 )
		syserr("%d. Attr init failed", errId);
	
	if ((errId = pthread_attr_setdetachstate(attr, PTHREAD_CREATE_DETACHED)) != 0)
		syserr("%d. Set Detach faild", errId);
}

void deleteResources()
{
	int i, errId;
	free(drugs.resources);
	for (i = 0; i < drugs.K; ++i) {
		if ((errId = pthread_cond_destroy(&(drugs.notEnougthResources[i]))) != 0)
			syserr("%d. Cond destroy %d failed", errId, i);
		if ((errId = pthread_mutex_destroy(&(drugs.mutex[i]))) != 0)
			syserr ("&d. Mutex destroy %d failed", errId, i);
	}
	free(drugs.mutex);
	free(drugs.notEnougthResources);
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
	int i;
	cerr("Waiting for resources");
	drugs.error = 1;
	for (i = 0; i < drugs.K; ++i) {
		pthread_cond_broadcast(&drugs.notEnougthResources[i]);
	}
	//TODO waitForWorking();
	deleteResources();
	closeIPC();
}

void exitServer(int sig)
{
	endSafe();
	exit(0);
}

int getNewClientPid()
{
	Mesg msg;
	if (msgrcv(IPCs[in], &msg, MAXMESGDATA, server_t, 0) <= 0)
		syserr("msgrcv");

	cerr("Receiving transmission : ");
	cerr(msg.mesg_data);
	return atoi(msg.mesg_data);
}

void sendErrorInfo(long msgType)
{
	Mesg msg;
	msg.mesg_type = msgType;
	strcpy(msg.mesg_data, ERROR);
	if (msgsnd(IPCs[out], (char *) &msg, strlen(msg.mesg_data), 0) != 0)
		syserr("msgsnd");
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

void getRequest(long msgType, int * k, int * n)
{
	Mesg msg;
	if (msgrcv(IPCs[in], &msg, MAXMESGDATA, msgType, 0) <= 0)
		syserr("msgrcv");
	
	sscanf(msg.mesg_data, requestFormat, k, n);
}

pid_t getPartner(pid_t myClientPid, int k, int n)
{
	pid_t otherPid = 134;
	//such TODO
	return otherPid;
}

void *clientThread(void *data)
{
	cerr("Thread Start\n");
	pthread_t pthread_self();
	pid_t clientPid = *(pid_t *)data;
	int k, n;
	long msgType = clientPid;
	getRequest(msgType, &k, &n);
	pid_t otherPid = getPartner(clientPid, k, n);
	if (otherPid == clientPid) {
		sendErrorInfo(msgType);
	} else {
		//report(myData.me, );
		sendResources(msgType, otherPid);
		waitForResources(msgType);
	}
	cerr("Thread End\n");
	return 0;
}

void createClientThread(pid_t clientPid, pthread_attr_t *attr)
{
	int errId;
	pthread_t thread;
	if ((errId = pthread_create(&thread, attr, clientThread, (void *)&clientPid)) != 0)
		syserr("%d. Pthread create", errId);
	
	cerr("Created New Thread\n");
}

int main(int argc, char **argv)
{
	if (argc != 3)
		syserr("Wrong number of parameters");
	
	if (signal(SIGINT,  exitServer) == SIG_ERR)
		syserr("signal");
	
	int K = atoi(argv[1]);
	int N = atoi(argv[2]);
	int newClient;
	pthread_attr_t attr;
	createResources(K, N);
	createIPC();
	initiateThreads(&attr);
	
	cerr("Server prepared to work\n");
	
	for (;;) {
		newClient = getNewClientPid();
		createClientThread(newClient, &attr);
	}
	
	cerr("Server turning off normally\n");
	
	endSafe();
	return 0;
}
