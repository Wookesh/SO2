/**
 * Łukasz Piesiewicz 334978
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
	int amount;
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

struct Resources res;
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
	res.K = K + 1;
	res.N = N;
	res.resources = (struct Resource *)malloc(sizeof(struct Resource) * res.K);
	int i, errId;
	for (i = 0; i < res.K; ++i) {
		res.resources[i].actualRequest = 0;
		res.resources[i].amount = res.N;
		res.resources[i].inPair = 0;
		res.resources[i].firstPairMember = -1;
		res.resources[i].firstClientRequest = 0;
		if ((errId = pthread_mutex_init(&(res.resources[i].mutex), 0) != 0))
			systemError("%d. Mutex init %d failed", errId, i);
		if ((errId = pthread_cond_init(&(res.resources[i].waitForResources), 0)) != 0)
			systemError("&d Cond init %d failed", errId, i);
		if ((errId = pthread_cond_init(&(res.resources[i].waitForPair), 0)) != 0)
			systemError("&d Cond init %d failed", errId, i);
		if ((errId = pthread_cond_init(&(res.resources[i].waitInQueue), 0)) != 0)
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
	for (i = 0; i < res.K; ++i) {
		if ((errId = pthread_cond_destroy(&(res.resources[i].waitForResources))) != 0)
			systemError("%d. Cond destroy %d failed", errId, i);
		if ((errId = pthread_cond_destroy(&(res.resources[i].waitForPair))) != 0)
			systemError("%d. Cond destroy %d failed", errId, i);
		if ((errId = pthread_cond_destroy(&(res.resources[i].waitInQueue))) != 0)
			systemError("%d. Cond destroy %d failed", errId, i);
		if ((errId = pthread_mutex_destroy(&(res.resources[i].mutex))) != 0)
			systemError("&d. Mutex destroy %d failed", errId, i);
	}
	if ((errId = pthread_cond_destroy(&(server.waitForThreads))) != 0)
		systemError("%d. Cond destroy %d failed", errId, i);
	if ((errId = pthread_mutex_destroy(&(server.mutex))) != 0)
		systemError("&d. Mutex destroy %d failed", errId, i);
	free(res.resources);
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
	fprintf(stderr,"Waiting for working threads\n");
	server.error = 1;
	for (i = 0; i < res.K; ++i) {
		pthread_cond_broadcast(&res.resources[i].waitForResources);
		pthread_cond_broadcast(&res.resources[i].waitForPair);
		pthread_cond_broadcast(&res.resources[i].waitInQueue);
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
	
	if ((errId = pthread_mutex_lock(&(res.resources[k].mutex))) != 0)
		systemError("%d. Lock failed", errId);
	
	res.resources[k].amount += nm;
	if ((res.resources[k].amount >= res.resources[k].actualRequest && res.resources[k].actualRequest != 0) || server.error == 1) {
		if ((errId = pthread_cond_signal(&(res.resources[k].waitForResources))) != 0)
			systemError("%d. Cond signal failed", errId);
	}
	
	if ((errId = pthread_mutex_unlock(&(res.resources[k].mutex))) != 0)
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
}

int getPartner(pid_t myClientPid, pid_t *otherClientPid, int k, int n, int * m)
{
	int errId;
	if ((errId = pthread_mutex_lock(&(res.resources[k].mutex))) != 0)
		systemError("%d. Lock failed", errId);
	
	if (res.resources[k].inPair == 0) {
		++res.resources[k].inPair;
		res.resources[k].firstPairMember = myClientPid;
		res.resources[k].firstClientRequest = n;
		*otherClientPid = myClientPid;
		if ((errId = pthread_cond_wait(&(res.resources[k].waitForPair), &(res.resources[k].mutex))) != 0)
			systemError("%d. Cond wait faild", errId);
		
		if (server.error == 1) {
			if ((errId = pthread_mutex_unlock(&(res.resources[k].mutex))) != 0)
				systemError("%d. Unlock failed", errId);
			return 1;
		}
		
	} else {
		res.resources[k].inPair = 0;
		*m = res.resources[k].firstClientRequest;
		*otherClientPid = res.resources[k].firstPairMember;
		
		if ((errId = pthread_cond_signal(&(res.resources[k].waitForPair))) != 0)
			systemError("%d. Cond signal failed", errId);
	}
	
	if ((errId = pthread_mutex_unlock(&(res.resources[k].mutex))) != 0)
		systemError("%d. Unlock failed", errId);
	
	return 0;
}

int getResources(pid_t firstClient, pid_t secondClient, int k, int n, int m)
{
	int errId;
	if ((errId = pthread_mutex_lock(&(res.resources[k].mutex))) != 0)
		systemError("%d. Lock failed", errId);
	
	while (res.resources[k].actualRequest != 0 && server.error != 1) {
		if ((errId = pthread_cond_wait(&(res.resources[k].waitInQueue), &(res.resources[k].mutex))) != 0)
			systemError("%d. Cond wait faild", errId);
	}
	
	res.resources[k].actualRequest = n + m;
	
	while (res.resources[k].amount < res.resources[k].actualRequest && server.error != 1) {
		if ((errId = pthread_cond_wait(&(res.resources[k].waitForResources), &(res.resources[k].mutex))) != 0)
			systemError("%d. Cond wait faild", errId);
	}
	
	if (server.error == 1) {
		if ((errId = pthread_mutex_unlock(&(res.resources[k].mutex))) != 0)
			systemError("%d. Unlock failed", errId);
		return 1;
	}
	
	res.resources[k].amount -= res.resources[k].actualRequest;
	report(pthread_self(), m, n, k, firstClient, secondClient, res.resources[k].amount);
	res.resources[k].actualRequest = 0;
	
	if ((errId = pthread_cond_signal(&(res.resources[k].waitInQueue))) != 0)
		systemError("%d. Cond signal failed", errId);
	
	if ((errId = pthread_mutex_unlock(&(res.resources[k].mutex))) != 0)
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
	if (getPartner(clientPid, &secondClientPid, k, n, &m) == 1)
		sendErrorInfo(msgType);
	if (secondClientPid != clientPid) {
		secondMsgType = secondClientPid;
		if (getResources(clientPid, secondClientPid, k, n, m) == 0) {
			sendResources(msgType, secondClientPid);
			sendResources(secondMsgType, clientPid);
			getResourcesBack(msgType, secondMsgType, k, n + m);
		} else {
			sendErrorInfo(msgType);
			sendErrorInfo(secondMsgType);
		}
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
	
	endSafe();
	return 0;
}
