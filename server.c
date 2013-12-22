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

pthread_t mainThread;

struct Resource {
	int resource;
	int inPair;
	int actualRequest;
	pid_t firstPairMember;
	pid_t secondPairMember;
	pthread_cond_t waitForResources;
	pthread_cond_t waitForPair;
	pthread_mutex_t mutex;
	pid_t firstAwaken;
};

struct Resources {
	int K;
	int N;
	struct Resource * resources;
};

struct Server {
	int error;
	int threads;
	pthread_cond_t waitForThreads;
	pthread_mutex_t mutex;
};

struct Resources drugs;
struct Server server;

void report(pthread_t thread, int m, int n, int k, pid_t PID1, pid_t PID2, int resourcesLeft)
{
	printf(threadReportFormat, thread, m, n, k, PID1, PID2, resourcesLeft);
}

void createResources(int K, int N) 
{
	drugs.K = K;
	drugs.N = N;
	server.error = 0;
	drugs.resources = (struct Resource *)malloc(sizeof(struct Resource) * drugs.K);
	int i, errId;
	for (i = 0; i < drugs.K; ++i) {
		drugs.resources[i].actualRequest = 0;
		drugs.resources[i].resource = drugs.N;
		drugs.resources[i].inPair = 0;
		drugs.resources[i].firstPairMember = 0;
		drugs.resources[i].secondPairMember = 0;
		drugs.resources[i].firstAwaken = 0;
		if ((errId = pthread_mutex_init(&(drugs.resources[i].mutex), 0) != 0))
			syserr("%d. Mutex init %d failed", errId, i);
		if ((errId = pthread_cond_init(&(drugs.resources[i].waitForResources), 0)) != 0)
			syserr("&d Cond init %d failed", errId, i);
		if ((errId = pthread_cond_init(&(drugs.resources[i].waitForPair), 0)) != 0)
			syserr("&d Cond init %d failed", errId, i);
	}
	
	server.threads = 0;
	if ((errId = pthread_mutex_init(&(server.mutex), 0) != 0))
			syserr("%d. Server mutex init failed", errId);
	if ((errId = pthread_cond_init(&(server.waitForThreads), 0)) != 0)
			syserr("&d. Server cond init failed", errId);
	
	fprintf(stderr,"Resources Created\n");
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
	for (i = 0; i < drugs.K; ++i) {
		if ((errId = pthread_cond_destroy(&(drugs.resources[i].waitForResources))) != 0)
			syserr("%d. Cond destroy %d failed", errId, i);
		if ((errId = pthread_cond_destroy(&(drugs.resources[i].waitForPair))) != 0)
			syserr("%d. Cond destroy %d failed", errId, i);
		if ((errId = pthread_mutex_destroy(&(drugs.resources[i].mutex))) != 0)
			syserr ("&d. Mutex destroy %d failed", errId, i);
	}
	if ((errId = pthread_cond_destroy(&(server.waitForThreads))) != 0)
		syserr("%d. Cond destroy %d failed", errId, i);
	if ((errId = pthread_mutex_destroy(&(server.mutex))) != 0)
		syserr ("&d. Mutex destroy %d failed", errId, i);
	free(drugs.resources);
	fprintf(stderr,"Resources Freed\n");
}

void createIPC()
{
	if ((IPCs[in] = msgget(S_KEY, 0666 | IPC_CREAT | IPC_EXCL)) == -1)
		syserr("msgget IPCs IN", S_KEY);

	if ((IPCs[out] = msgget(K_KEY, 0666 | IPC_CREAT | IPC_EXCL)) == -1)
		syserr("msgget IPCs OUT", K_KEY);
	fprintf(stderr,"IPCs Created\n");
}

void closeIPC()
{
	if (msgctl(IPCs[in], IPC_RMID, 0) == -1)
		syserr("msgctl RMID IPCs IN");

	if (msgctl(IPCs[out], IPC_RMID, 0) == -1)
		syserr("msgctl RMID IPCs OUT");
	fprintf(stderr,"IPCs Closed\n");
}

void waitForWorking()
{
	int errId;
	if ((errId = pthread_mutex_lock(&(server.mutex))) != 0)
		syserr("%d. Lock failed", errId);
	
	while (server.threads != 0) {
		if ((errId = pthread_cond_wait(&(server.waitForThreads), &(server.mutex))) != 0)
			syserr("%d. Cond wait faild", errId);
	}
	
	if ((errId = pthread_mutex_unlock(&(server.mutex))) != 0)
		syserr("%d. Unlock failed", errId);
}

void endSafe()
{
	int i;
	fprintf(stderr,"Waiting for resources\n");
	server.error = 1;
	for (i = 0; i < drugs.K; ++i) {
		pthread_cond_broadcast(&drugs.resources[i].waitForResources);
		pthread_cond_broadcast(&drugs.resources[i].waitForPair);
	}
	waitForWorking();
	fprintf(stderr, "All threads finished\n");
	deleteResources();
	closeIPC();
}

void exitServer(int sig)
{
	if (pthread_self() == mainThread) {
		endSafe();
		exit(0);
	} else {
		pthread_kill(mainThread, SIGINT);
	}
}

int getNewClientPid()
{
	Mesg msg;
	if (msgrcv(IPCs[in], &msg, MAXMESGDATA, server_t, 0) <= 0)
		syserr("msgrcv");

	fprintf(stderr,"Receiving transmission : %s", msg.mesg_data);
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

void getResourcesBack(long msgType, int k, int n)
{
	int errId;
	Mesg msg;
	if (msgrcv(IPCs[in], &msg, MAXMESGDATA, msgType, 0) <= 0)
		syserr("msgrcv");
	
	if ((errId = pthread_mutex_lock(&(drugs.resources[k].mutex))) != 0)
		syserr("%d. Lock failed", errId);
	
	drugs.resources[k].resource += n;
	if ((drugs.resources[k].resource >= drugs.resources[k].actualRequest && drugs.resources[k].inPair == 2) || server.error == 1) {
		if ((errId = pthread_cond_signal(&(drugs.resources[k].waitForResources))) != 0)
			syserr("%d. Cond signal failed", errId);
	}
	
	if ((errId = pthread_mutex_unlock(&(drugs.resources[k].mutex))) != 0)
		syserr("%d. Unlock failed", errId);
}

void getRequest(long msgType, int * k, int * n)
{
	Mesg msg;
	if (msgrcv(IPCs[in], &msg, MAXMESGDATA, msgType, 0) <= 0)
		syserr("msgrcv");
	
	sscanf(msg.mesg_data, requestFormat, k, n);
	*k = *k - 1;
}

void printResorurces(int k)
{
	printf("%d Resource : Max = %d --- Actual = %d\n", k, drugs.N, drugs.resources[k].resource);
}

pid_t getPartner(pid_t myClientPid, int k, int n)
{
	printResorurces(k);
	pid_t otherPid = 0;
	int errId;
	
	if ((errId = pthread_mutex_lock(&(drugs.resources[k].mutex))) != 0)
		syserr("%d. Lock failed", errId);
	
	while (drugs.resources[k].inPair == 2 && server.error != 1) {
		if ((errId = pthread_cond_wait(&(drugs.resources[k].waitForPair), &(drugs.resources[k].mutex))) != 0)
			syserr("%d. Cond wait faild", errId);
	}
	
	if (drugs.resources[k].inPair < 2 && server.error != 1) {
		++drugs.resources[k].inPair;
		drugs.resources[k].actualRequest += n;
		
		if (drugs.resources[k].firstPairMember == 0)
			drugs.resources[k].firstPairMember = myClientPid;
		else
			drugs.resources[k].secondPairMember = myClientPid;
		
		if (drugs.resources[k].inPair == 1 || drugs.resources[k].resource < drugs.resources[k].actualRequest) {
			if ((errId = pthread_cond_wait(&(drugs.resources[k].waitForResources), &(drugs.resources[k].mutex))) != 0)
				syserr("%d. Cond wait faild", errId);
		}
		
		if (drugs.resources[k].firstAwaken == 0)
			drugs.resources[k].firstAwaken = myClientPid;
		
		if (drugs.resources[k].firstPairMember == myClientPid)
			otherPid = drugs.resources[k].secondPairMember;
		else
			otherPid = drugs.resources[k].firstPairMember;
		
	}
	
	//NOTE If first client took resources befor error occoured, I let second client to do so.
	if (server.error == 1 && (drugs.resources[k].firstAwaken == myClientPid || drugs.resources[k].firstAwaken == 0)) {
		
		drugs.resources[k].firstAwaken = 0;
		
		if ((errId = pthread_mutex_unlock(&(drugs.resources[k].mutex))) != 0)
			syserr("%d. Unlock failed", errId);
		
		return myClientPid;
	}
	
	if (drugs.resources[k].firstAwaken == myClientPid) {
		drugs.resources[k].resource -= (drugs.resources[k].actualRequest);
		report(pthread_self(), drugs.resources[k].actualRequest - n, n, k + 1, otherPid, myClientPid, drugs.resources[k].resource);
		
		if ((errId = pthread_cond_signal(&(drugs.resources[k].waitForResources))) != 0)
			syserr("%d. Cond signal failed", errId);
	} else {
		drugs.resources[k].actualRequest = 0;
		drugs.resources[k].inPair = 0;
		drugs.resources[k].firstPairMember = 0;
		drugs.resources[k].secondPairMember = 0;
		drugs.resources[k].firstAwaken = 0;
		
		if ((errId = pthread_cond_signal(&(drugs.resources[k].waitForPair))) != 0)
			syserr("%d. Cond signal failed", errId);
		
		if ((errId = pthread_cond_signal(&(drugs.resources[k].waitForPair))) != 0)
			syserr("%d. Cond signal failed", errId);
	}
	
	if ((errId = pthread_mutex_unlock(&(drugs.resources[k].mutex))) != 0)
		syserr("%d. Unlock failed", errId);
	
	return otherPid;
}

void threadStart()
{
	int errId;
	if ((errId = pthread_mutex_lock(&(server.mutex))) != 0)
		syserr("%d. Lock failed", errId);
	
	++server.threads;
	
	if ((errId = pthread_mutex_unlock(&(server.mutex))) != 0)
		syserr("%d. Unlock failed", errId);
	fprintf(stderr,"Thread Start\n");
}

void threadEnd()
{
		int errId;
	if ((errId = pthread_mutex_lock(&(server.mutex))) != 0)
		syserr("%d. Lock failed", errId);
	
	--server.threads;
	if (server.threads == 0 && server.error == 1) {
		if ((errId = pthread_cond_signal(&(server.waitForThreads))) != 0)
			syserr("%d. Cond signal failed", errId);
	}
	
	if ((errId = pthread_mutex_unlock(&(server.mutex))) != 0)
		syserr("%d. Unlock failed", errId);
	fprintf(stderr,"Thread End\n");
}

void *clientThread(void *data)
{
	threadStart();
	pthread_t pthread_self();
	pid_t clientPid = *(pid_t *)data;
	int k, n;
	long msgType = clientPid;
	getRequest(msgType, &k, &n);
	fprintf(stderr,"Received request\n");
	pid_t otherPid = getPartner(clientPid, k, n);
	if (otherPid == clientPid) {
		sendErrorInfo(msgType);
		fprintf(stderr,"Sent Error message\n");
	} else {
		sendResources(msgType, otherPid);
		getResourcesBack(msgType, k, n);
		fprintf(stderr,"Received resources\n");
	}
	threadEnd();
	return 0;
}

void createClientThread(pid_t clientPid, pthread_attr_t *attr)
{
	int errId;
	pthread_t thread;
	if ((errId = pthread_create(&thread, attr, clientThread, (void *)&clientPid)) != 0)
		syserr("%d. Pthread create", errId);
	
	fprintf(stderr,"Created New Thread\n");
}

int main(int argc, char **argv)
{
	if (argc != 3)
		syserr("Wrong number of parameters");
	
	if (signal(SIGINT, exitServer) == SIG_ERR)
		syserr("signal");
	
	int K = atoi(argv[1]);
	int N = atoi(argv[2]);
	int newClient;
	mainThread = pthread_self();
	pthread_attr_t attr;
	createResources(K, N);
	createIPC();
	initiateThreads(&attr);
	
	fprintf(stderr,"Server prepared to work\n");
	
	for (;;) {
		newClient = getNewClientPid();
		createClientThread(newClient, &attr);
	}
	
	fprintf(stderr,"Server turning off normally\n");
	
	endSafe();
	return 0;
}
