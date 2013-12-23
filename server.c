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
	pthread_cond_t waitForResources;
	pthread_cond_t waitForPair;
	pthread_cond_t waitInQueue;
	pthread_mutex_t mutex;
   int firstClientRequest;
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

void criticalCloseIPC()
{
	msgctl(IPCs[in], IPC_RMID, 0);
	msgctl(IPCs[out], IPC_RMID, 0);
}

void systemError(const char *fmt, ...)
{
	va_list fmt_args;
	criticalCloseIPC();
	syserr(fmt, fmt_args);
}

void createResources(int K, int N) 
{
	drugs.K = K + 1;
	drugs.N = N;
	drugs.resources = (struct Resource *)malloc(sizeof(struct Resource) * drugs.K);
	int i, errId;
	for (i = 0; i < drugs.K; ++i) {
		drugs.resources[i].actualRequest = 0;
		drugs.resources[i].resource = drugs.N;
		drugs.resources[i].inPair = 0;
		drugs.resources[i].firstPairMember = -1;
		drugs.resources[i].firstClientRequest = 0;
		if ((errId = pthread_mutex_init(&(drugs.resources[i].mutex), 0) != 0))
			systemError("%d. Mutex init %d failed", errId, i);
		if ((errId = pthread_cond_init(&(drugs.resources[i].waitForResources), 0)) != 0)
			systemError("&d Cond init %d failed", errId, i);
		if ((errId = pthread_cond_init(&(drugs.resources[i].waitForPair), 0)) != 0)
			systemError("&d Cond init %d failed", errId, i);
		if ((errId = pthread_cond_init(&(drugs.resources[i].waitInQueue), 0)) != 0)
			systemError("&d Cond init %d failed", errId, i);
	}
	
	server.error = 0;
	server.threads = 0;
	if ((errId = pthread_mutex_init(&(server.mutex), 0) != 0))
			systemError("%d. Server mutex init failed", errId);
	if ((errId = pthread_cond_init(&(server.waitForThreads), 0)) != 0)
			systemError("&d. Server cond init failed", errId);
	
	fprintf(stderr,"Resources Created\n");
}

void initiateThreads(pthread_attr_t * attr)
{
	int errId;
	
	if ((errId = pthread_attr_init(attr)) != 0 )
		systemError("%d. Attr init failed", errId);
	
	if ((errId = pthread_attr_setdetachstate(attr, PTHREAD_CREATE_DETACHED)) != 0)
		systemError("%d. Set Detach faild", errId);
}

void deleteResources()
{
	int i, errId;
	for (i = 0; i < drugs.K; ++i) {
		if ((errId = pthread_cond_destroy(&(drugs.resources[i].waitForResources))) != 0)
			systemError("%d. Cond destroy %d failed", errId, i);
		if ((errId = pthread_cond_destroy(&(drugs.resources[i].waitForPair))) != 0)
			systemError("%d. Cond destroy %d failed", errId, i);
		if ((errId = pthread_cond_destroy(&(drugs.resources[i].waitInQueue))) != 0)
			systemError("%d. Cond destroy %d failed", errId, i);
		if ((errId = pthread_mutex_destroy(&(drugs.resources[i].mutex))) != 0)
			systemError("&d. Mutex destroy %d failed", errId, i);
	}
	if ((errId = pthread_cond_destroy(&(server.waitForThreads))) != 0)
		systemError("%d. Cond destroy %d failed", errId, i);
	if ((errId = pthread_mutex_destroy(&(server.mutex))) != 0)
		systemError("&d. Mutex destroy %d failed", errId, i);
	free(drugs.resources);
	fprintf(stderr,"Resources Freed\n");
}

void createIPC()
{
	if ((IPCs[in] = msgget(S_KEY, 0666 | IPC_CREAT | IPC_EXCL)) == -1)
		systemError("msgget IPCs IN", S_KEY);

	if ((IPCs[out] = msgget(K_KEY, 0666 | IPC_CREAT | IPC_EXCL)) == -1)
		systemError("msgget IPCs OUT", K_KEY);
	fprintf(stderr,"IPCs Created\n");
}

void closeIPC()
{
	if (msgctl(IPCs[in], IPC_RMID, 0) == -1)
		systemError("msgctl RMID IPCs IN");

	if (msgctl(IPCs[out], IPC_RMID, 0) == -1)
		systemError("msgctl RMID IPCs OUT");
	fprintf(stderr,"IPCs Closed\n");
}

void waitForWorking()
{
	int errId;
	if ((errId = pthread_mutex_lock(&(server.mutex))) != 0)
		systemError("%d. Lock failed", errId);
	
	while (server.threads != 0) {
		if ((errId = pthread_cond_wait(&(server.waitForThreads), &(server.mutex))) != 0)
			systemError("%d. Cond wait faild", errId);
	}
	
	if ((errId = pthread_mutex_unlock(&(server.mutex))) != 0)
		systemError("%d. Unlock failed", errId);
}

void endSafe()
{
	int i;
	fprintf(stderr,"Waiting for resources\n");
	server.error = 1;
	for (i = 0; i < drugs.K; ++i) {
		pthread_cond_broadcast(&drugs.resources[i].waitForResources);
		pthread_cond_broadcast(&drugs.resources[i].waitForPair);
		pthread_cond_broadcast(&drugs.resources[i].waitInQueue);
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
		systemError("msgrcv");

	fprintf(stderr,"Receiving transmission : %s", msg.mesg_data);
	return atoi(msg.mesg_data);
}

void sendErrorInfo(long msgType)
{
	Mesg msg;
	msg.mesg_type = msgType;
	strcpy(msg.mesg_data, ERROR);
	if (msgsnd(IPCs[out], (char *) &msg, strlen(msg.mesg_data), 0) != 0)
		systemError("msgsnd");
}

void sendResources(long msgType, pid_t otherClientPid)
{
	Mesg msg;
	msg.mesg_type = msgType;
	sprintf(msg.mesg_data, "%d", otherClientPid);
	if (msgsnd(IPCs[out], (char *) &msg, strlen(msg.mesg_data), 0) != 0)
		systemError("msgsnd");
}

void getResourcesBack(long firstMsgType, long secondMsgType, int k, int nm)
{
	int errId;
	Mesg msg;
	if (msgrcv(IPCs[in], &msg, MAXMESGDATA, firstMsgType, 0) <= 0)
		systemError("msgrcv");
	
	if (msgrcv(IPCs[in], &msg, MAXMESGDATA, secondMsgType, 0) <= 0)
		systemError("msgrcv");
	
	if ((errId = pthread_mutex_lock(&(drugs.resources[k].mutex))) != 0)
		systemError("%d. Lock failed", errId);
	
	fprintf(stderr, "Received resources back %d, %d\n", k, nm);
	drugs.resources[k].resource += nm;
	if ((drugs.resources[k].resource >= drugs.resources[k].actualRequest && drugs.resources[k].actualRequest != 0) || server.error == 1) {
		if ((errId = pthread_cond_signal(&(drugs.resources[k].waitForResources))) != 0)
			systemError("%d. Cond signal failed", errId);
	}
	
	if ((errId = pthread_mutex_unlock(&(drugs.resources[k].mutex))) != 0)
		systemError("%d. Unlock failed", errId);
}

void getRequest(long msgType, int * k, int * n)
{
	int size;
	Mesg msg;
	if ((size = msgrcv(IPCs[in], &msg, MAXMESGDATA, msgType, 0)) <= 0)
		systemError("msgrcv");
	msg.mesg_data[size] = '\0';
	sscanf(msg.mesg_data, requestFormat, k, n);
}

void threadStart()
{
	int errId;
	if ((errId = pthread_mutex_lock(&(server.mutex))) != 0)
		systemError("%d. Lock failed", errId);
	
	++server.threads;
	
	if ((errId = pthread_mutex_unlock(&(server.mutex))) != 0)
		systemError("%d. Unlock failed", errId);
	fprintf(stderr,"Thread Start\n");
}

void threadEnd()
{
		int errId;
	if ((errId = pthread_mutex_lock(&(server.mutex))) != 0)
		systemError("%d. Lock failed", errId);
	
	--server.threads;
	if (server.threads == 0 && server.error == 1) {
		if ((errId = pthread_cond_signal(&(server.waitForThreads))) != 0)
			systemError("%d. Cond signal failed", errId);
	}
	
	if ((errId = pthread_mutex_unlock(&(server.mutex))) != 0)
		systemError("%d. Unlock failed", errId);
	fprintf(stderr,"Thread End\n");
}

int getPartner(pid_t myClientPid, pid_t *otherClientPid, int k, int n, int * m)
{
	int errId;
	if ((errId = pthread_mutex_lock(&(drugs.resources[k].mutex))) != 0)
		systemError("%d. Lock failed", errId);
	
	if (drugs.resources[k].inPair == 0) {
		++drugs.resources[k].inPair;
		drugs.resources[k].firstPairMember = myClientPid;
		drugs.resources[k].firstClientRequest = n;
		*otherClientPid = myClientPid;
		if ((errId = pthread_cond_wait(&(drugs.resources[k].waitForPair), &(drugs.resources[k].mutex))) != 0)
			systemError("%d. Cond wait faild", errId);
		
		if (server.error == 1) {
			if ((errId = pthread_mutex_unlock(&(drugs.resources[k].mutex))) != 0)
				systemError("%d. Unlock failed", errId);
			return 1;
		}
		
	} else {
		drugs.resources[k].inPair = 0;
		*m = drugs.resources[k].firstClientRequest;
		*otherClientPid = drugs.resources[k].firstPairMember;
		
		if ((errId = pthread_cond_signal(&(drugs.resources[k].waitForPair))) != 0)
			systemError("%d. Cond signal failed", errId);
	}
	
	if ((errId = pthread_mutex_unlock(&(drugs.resources[k].mutex))) != 0)
		systemError("%d. Unlock failed", errId);
	
	return 0;
}

int getResources(pid_t firstClient, pid_t secondClient, int k, int n, int m)
{
	int errId;
	if ((errId = pthread_mutex_lock(&(drugs.resources[k].mutex))) != 0)
		systemError("%d. Lock failed", errId);
	
	while (drugs.resources[k].actualRequest != 0 && server.error != 1) {
		if ((errId = pthread_cond_wait(&(drugs.resources[k].waitInQueue), &(drugs.resources[k].mutex))) != 0)
			systemError("%d. Cond wait faild", errId);
	}
	
	drugs.resources[k].actualRequest = n + m;
	
	while (drugs.resources[k].resource < drugs.resources[k].actualRequest && server.error != 1) {
		if ((errId = pthread_cond_wait(&(drugs.resources[k].waitForResources), &(drugs.resources[k].mutex))) != 0)
			systemError("%d. Cond wait faild", errId);
	}
	
	if (server.error == 1) {
		if ((errId = pthread_mutex_unlock(&(drugs.resources[k].mutex))) != 0)
			systemError("%d. Unlock failed", errId);
		return 1;
	}
	
	drugs.resources[k].resource -= drugs.resources[k].actualRequest;
	report(pthread_self(), m, n, k, firstClient, secondClient, drugs.resources[k].resource);
	drugs.resources[k].actualRequest = 0;
	
	if ((errId = pthread_cond_signal(&(drugs.resources[k].waitInQueue))) != 0)
		systemError("%d. Cond signal failed", errId);
	
	if ((errId = pthread_mutex_unlock(&(drugs.resources[k].mutex))) != 0)
		systemError("%d. Unlock failed", errId);
	
	return 0;
}

void *clientThread(void *data)
{
	threadStart();
	pthread_t pthread_self();
	pid_t clientPid = *(pid_t *)data, secondClientPid = 0;
	int k = 0, n = 0, m = 0;
	long msgType = clientPid, secondMsgType = 0;
	getRequest(msgType, &k, &n);
	fprintf(stderr,"Received request %d, %d\n", k, n);
	if (getPartner(clientPid, &secondClientPid, k, n, &m) == 1)
		sendErrorInfo(msgType);
	if (secondClientPid != clientPid) {
		fprintf(stderr, "Master thread\n");
		secondMsgType = secondClientPid;
		if (getResources(clientPid, secondClientPid, k, n, m) == 0) {
			sendResources(msgType, secondClientPid);
			sendResources(secondMsgType, clientPid);
			getResourcesBack(msgType, secondMsgType, k, n + m);
			fprintf(stderr,"Received resources\n");
		} else {
			sendErrorInfo(msgType);
			sendErrorInfo(secondMsgType);
		}
	} else {
		fprintf(stderr, "Minor thread\n");
	}
	free(data);
	threadEnd();
	return 0;
}

void createClientThread(pid_t clientPid, pthread_attr_t *attr)
{
	int errId;
	pthread_t thread;
	pid_t * client = (pid_t *)malloc(sizeof(pid_t));
	*client = clientPid;
	if ((errId = pthread_create(&thread, attr, clientThread, (void *)client)) != 0)
		systemError("%d. Pthread create", errId);
	
	fprintf(stderr,"Created New Thread\n");
}

int main(int argc, char **argv)
{
	if (argc != 3)
		systemError("Wrong number of parameters");
	
	if (signal(SIGINT, exitServer) == SIG_ERR)
		systemError("signal");
	
	int K = atoi(argv[1]);
	int N = atoi(argv[2]);
	mainThread = pthread_self();
	pthread_attr_t attr;
	
	createResources(K, N);
	createIPC();
	initiateThreads(&attr);
	
	fprintf(stderr,"Server prepared to work\n");
	
	for (;;) {
		pid_t newClient = getNewClientPid();
		createClientThread(newClient, &attr);
	}
	
	fprintf(stderr,"Server turning off normally\n");
	
	endSafe();
	return 0;
}
