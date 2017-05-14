#include <stdio.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <semaphore.h>
#include <sys/mman.h>
#include <sys/time.h>
#include <stdlib.h>
#include <time.h>
#include <string.h>
#include <pthread.h>

/**
 * Named pipe (FIFO) to contact the nenerator program
 */
#define fifo_entrada "/tmp/entrada"

/**
 * Enum which has all the types of state that a request can have
 */
typedef enum stateofrequest {PEDIDO, ACEITE, REJEITADO, DESCARTADO} StateOfRequest;

/**
 * Struct which has the main properties of a request
 */
typedef struct{
	char fifo_name[100]; // fifo_to_answer
	int requestID;
	char gender;
	int requestTime;
	int tries;
	StateOfRequest state;
} Request;


typedef struct{
	Request request;
	int threadID;
} RequestWrapper;


/**
 * Global variables
 */
int totalSeats;
char SEM_NAME[] = "/sem1";
sem_t *sem;
char actualGender = 'N'; //N means None
int requestRejected = 0;
int requestsReceived[2] = {0};// M-0, F-1
int rejectionsReceived[2] = {0};
int requestsServed[2] = {0};
pthread_mutex_t genderMtx = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t arraysMtx = PTHREAD_MUTEX_INITIALIZER;
int *threadsAvailable;


//------------------------------------------------------------------------------------------------------//


/**
 * Function that prints the statistical information on the number of requests received, the number
 * of rejections and the number of requests served (total and by gender).
 */
void printStatus(){
	pthread_mutex_lock(&arraysMtx);
	int totalReceived = requestsReceived[0] + requestsReceived[1];
	int totalRejections = rejectionsReceived[0] + rejectionsReceived[1];
	int totalServed = requestsServed[0] + requestsServed[1];
	printf("Pedidos Recebidos: Total- %d, M- %d, F- %d\n",totalReceived,requestsReceived[0],requestsReceived[1]);
	printf("Rejeicoes: Total- %d, M- %d, F- %d\n",totalRejections,rejectionsReceived[0],rejectionsReceived[1]);
	printf("Pedidos servidos: Total- %d, M- %d, F- %d\n",totalServed,requestsServed[0],requestsServed[1]);
	pthread_mutex_unlock(&arraysMtx);
}


//------------------------------------------------------------------------------------------------------//


/**
 * This function is responsible for issuing log messages to a /tmp/bar.pid file that documents the
 * rollout of the asset. The messages have the format: inst – pid – tid – p: g – dur – tip
 * @param request
 */
void printRegistrationMessages(Request r1){
	pid_t pid = getpid();
	char location[100];
	sprintf(location,"/tmp/bal.%d",pid);
	FILE *logFile = fopen(location, "a");
	int triesToOpen = 0;
	while (logFile == NULL)
	{
		sleep(1);
		triesToOpen++;
		logFile = fopen(location, "a");
		if (triesToOpen>4){
			printf("Error opening file!\n");
			exit(1);
		}
	}
	time_t raw_time;
	time(&raw_time);

	char tip[10];
	switch (r1.state){
	case PEDIDO:
		strcpy(tip,"RECEBIDO");
		break;
	case REJEITADO:
		strcpy(tip,"REJEITADO");
		break;
	case ACEITE:
		strcpy(tip,"SERVIDO");
		break;
    default:
    	break;
	}
	fprintf(logFile,"%lu -%d -%d : %c -%d %s\n", raw_time,pid,r1.requestID,r1.gender,r1.requestTime,tip);
	fclose(logFile);
}


//------------------------------------------------------------------------------------------------------//


/**
 * Function which calculates the past time
 * @param result
 * @param timeval2
 * @param timeval1
 */
void timeval_subtract(struct timeval *result, struct timeval *t2, struct timeval *t1)
{
	long int diff = (t2->tv_usec + 1000000 * t2->tv_sec) - (t1->tv_usec + 1000000 * t1->tv_sec);
	result->tv_sec = diff / 1000000;
	result->tv_usec = diff % 1000000;
}


//------------------------------------------------------------------------------------------------------//


/**
 * Function that verifies the gender of the person making the request
 * And if actual gender is define as N will be change to the requestGender
 *
 * @param gender of the person making the request
 */
int checkGender(char requestGender){
	int returnVal = 0;
	pthread_mutex_lock(&genderMtx);
	if (actualGender == 'N'){
		actualGender = requestGender;
		returnVal = 1;
	}
	else if(requestGender == actualGender){
		returnVal = 1;
	}
	pthread_mutex_unlock(&genderMtx);
	return returnVal;
}

//------------------------------------------------------------------------------------------------------//


/**
 * Function that increment value on array of int, that correspond to a counter
 *
 * @param gender of the person to increment on array
 * @param arrayOfGender array that is one counter of genders
 */
void incrementGender(char gender, int * arrayOfGender){
	if (gender == 'M'){
		pthread_mutex_lock(&arraysMtx);
		arrayOfGender[0]++;
		pthread_mutex_unlock(&arraysMtx);
	}
	else{
		pthread_mutex_lock(&arraysMtx);
		arrayOfGender[1]++;
		pthread_mutex_unlock(&arraysMtx);
	}
}

//------------------------------------------------------------------------------------------------------//


/**
 * Function to reject one request and increment the counter of request rejecteds
 *
 * @param requestToRead request to reject
 */
void rejectRequest(Request* requestToRead){
	(*requestToRead).state = REJEITADO;
	printf("Sauna: Rejecting Request, SEX:%c\n",(*requestToRead).gender);

	incrementGender((*requestToRead).gender, rejectionsReceived);
}

//------------------------------------------------------------------------------------------------------//


/**
 * Function to set actualGender to default value if sauna is empty
 */
void actualGenderToDefault(){
	int semValue;
	if (sem_getvalue(sem, &semValue) == -1){
		perror("Error Reading Semaphore Value\n");
		exit(2);
	}
	if (semValue == totalSeats){
		pthread_mutex_lock(&genderMtx);
		actualGender = 'N';
		pthread_mutex_unlock(&genderMtx);
	}
}


//------------------------------------------------------------------------------------------------------//


/**
 * Function to try open fifo, but if can't open FIFO the function set the thread as available for
 * do other stuff and close.
 *
 * @param fifo_name name of FIFO to try open
 * @param fifo_ans is the fifo if it was opened correctly
 * @param threadID ID of thread to set as available
 */
int open_FIFO(char* fifo_name, int* fifo_ans, int threadID){
	int numTries = 0;
	while ((*fifo_ans = open(fifo_name,O_WRONLY))==-1){
		numTries++;
		printf("Sauna: Error opening fifo awnser, try number %d\n",numTries);
		sleep(1);
		if (numTries > 4){
			threadsAvailable[threadID] = 1;
			return 1;
		}
	}
	return 0;
}

//------------------------------------------------------------------------------------------------------//


/**
 * Function to send the response of request to gerador, first try to open the fifo to response
 * and after send the response
 *
 * @param requestToRead request already processed and ready to be sent to gerador
 * @param threadID ID of thread needed for open_FIFO
 */
void sendResponse(Request requestToRead, int threadID){
	int fifo_ans;
	if(open_FIFO(requestToRead.fifo_name, &fifo_ans, threadID)){
		perror("Opening FIFO");
		exit(2);
	}

	if (write(fifo_ans, &requestToRead, sizeof(requestToRead)) == -1){
		perror("Writing Awnser Error\n");
		exit(2);
	}
	printf("Sent info back to generator\n");
}

//------------------------------------------------------------------------------------------------------//


//TODO: COMMENT I DONT NO WHAT SAY ABAOUT THIS :/
void restInSauna(Request requestToRead, struct timeval* tvBegin){
	struct timeval tvEnd, tvDiff;
  	int elapsedMiliseconds = 0;
		printf("Resting in Sauna, SEX:%c\n",requestToRead.gender);
		do{
			gettimeofday(&tvEnd, NULL);
			timeval_subtract(&tvDiff, &tvEnd, tvBegin);
			elapsedMiliseconds = tvDiff.tv_sec * 1000 + tvDiff.tv_usec/1000.0;
		} while(elapsedMiliseconds < requestToRead.requestTime);
		printf("Rested in Sauna, SEX:%c\n",requestToRead.gender);
}

//------------------------------------------------------------------------------------------------------//


/**
 * Function that is responsible for handling requests.
 * Reject the request if it is a different user from the one already occupied.
 * If the request is the same as that of the users already in the sauna, it waits for notification
 * that a place has wandered and accepts the request;
 * @param args
 */
void* handleRequest(void * args){
	struct timeval tvBegin;
	RequestWrapper requestWrapper = *(RequestWrapper*) args;
	Request requestToRead = requestWrapper.request;
	int threadID = requestWrapper.threadID;

	incrementGender(requestToRead.gender, requestsReceived);

	if (checkGender(requestToRead.gender)){
		printf("Sauna: Accepting Request, SEX:%c\n",requestToRead.gender);
		if (sem_wait(sem)==-1){
			perror("sem_wait(sem)\n");
			exit(2);
		}
		gettimeofday(&tvBegin, NULL);
		requestToRead.state = ACEITE;
		incrementGender(requestToRead.gender, requestsServed);
		restInSauna(requestToRead, &tvBegin);
		sem_post(sem);
		actualGenderToDefault();
	}
	else{
		//Reject the Request
		rejectRequest(&requestToRead);
	}

	printRegistrationMessages(requestToRead);
	sendResponse(requestToRead, threadID);

	printf("Thread %d is now available\n",threadID);
	threadsAvailable[threadID] = 1;
	return NULL;
}

//------------------------------------------------------------------------------------------------------//


/**
*	Function to initialize the array of int to know if one thread is available to use
*/

void initAvailableThreads(int numThreads){
	int i;
	for (i=0; i <numThreads;i++){
		threadsAvailable[i] = 1;
	}
}

//------------------------------------------------------------------------------------------------------//


/**
*
*@return Index of the next available thread, or -1 case there is no one available.
*/
int findNextAvailableThread(int numThreads){
	int i;
	for (i=0; i<numThreads;i++){
		if (threadsAvailable[i] == 1){
			return i;
		}
	}
	return -1;
}



//------------------------------------------------------------------------------------------------------//

int main(int argc, char const *argv[]) {
	/* code */
	if (argc != 2){
		printf("Usage: sauna <n_lugares>\n");
		return 1;
	}
	sscanf(argv[1], "%d", &totalSeats);
	printf("Total Seats available = %d\n",totalSeats);
	//create & initialize semaphore
	if ( (sem = sem_open(SEM_NAME,O_CREAT,0660,totalSeats)) == SEM_FAILED){
		perror("Error opening Semaphore\n");
		return 2;
	}
	if (sem_init(sem,0,totalSeats)==-1){
		perror("sem_init()\n");
	}
	int tempSemValue;
	sem_getvalue(sem, &tempSemValue);
	printf("in Main Sem Value=%d\n",tempSemValue);
	mkfifo(fifo_entrada, 0660);

	int fifo_req;
	int triesToOpenFifo = 0;
	while((fifo_req=open(fifo_entrada,O_RDONLY))==-1){
		sleep(1);
		triesToOpenFifo++;
		if (triesToOpenFifo > 5){
			printf("Failed to Open Fifo Request\n");
			//exit(1);
		}
	}

	int numThreads = totalSeats;
	pthread_t *requestThreads = malloc(sizeof(pthread_t)*numThreads);
	threadsAvailable = malloc(sizeof(int)*numThreads);
	initAvailableThreads(numThreads);
	int maxIdUsed = -1;
	printf("Allocated initial threads\n");

	Request requestToRead;
	RequestWrapper requestWrapper;
	int n = 1;
	while(n>0){
		n=read(fifo_req,&requestToRead, sizeof(requestToRead));
		int nextThreadAvailable = findNextAvailableThread(numThreads);
		if (nextThreadAvailable == -1){
			printf("Going to Reallocate Memory\n");
			int j = numThreads;
			numThreads = numThreads * 2;
			if ( (requestThreads = realloc(requestThreads,numThreads * sizeof(pthread_t))) == NULL){
				perror("Error Reallocating Memory for threads\n");
			}
			else{
				for (; j < numThreads;j++){
					threadsAvailable[j] = 1;
				}
				nextThreadAvailable = findNextAvailableThread(numThreads);
				printf("Memory Reallocated\n");
			}
		}
		if (n > 0){
			printf("Read new request\n");
			requestWrapper.request = requestToRead;
			requestWrapper.threadID = nextThreadAvailable;
			pthread_create(&requestThreads[nextThreadAvailable], NULL, handleRequest, &requestWrapper);
			threadsAvailable[nextThreadAvailable] = 0;
			if (nextThreadAvailable>maxIdUsed){
				maxIdUsed = nextThreadAvailable;
			}
			printf("Created thread %d\n", nextThreadAvailable);
		}

	}

	int i = 0;
	for (; i <= maxIdUsed;i++){
		printf("Joining thread %d\n",i);
		pthread_join(requestThreads[i], NULL);
		printf("Joined Thread %d\n",i);
	}
	printf("Freeing array of threads\n");
	free(requestThreads);
	printf("Freeing array of available threads\n");
	free(threadsAvailable);
	printf("Destroying Semaphore\n");
	sem_destroy(sem);
	printf("Semaphore Destroyed\n");
	unlink(fifo_entrada);
	printf("Destryoed FIFO\n");
	printStatus();

	return 0;
}
