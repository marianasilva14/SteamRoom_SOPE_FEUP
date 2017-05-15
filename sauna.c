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

/**
* Structure that wraps a request and the thread responsible for it.
*/
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
pthread_mutex_t arraysRejMtx = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t arraysReqRecMtx = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t arraysReqSerMtx = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t fifoMtx = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t writeFileMtx = PTHREAD_MUTEX_INITIALIZER;
int *threadsAvailable;
FILE *logFile;
pid_t pid;
int numberOfFIFOs = 10;
int fifo_array[10];
char namesOfFifos[10][100];
int FIFOS_ITER = 0;

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
	printf("Pedidos Recebidos: Total- %d, M- %d, F- %d\n",
		totalReceived,
		requestsReceived[0],
		requestsReceived[1]);
	printf("Rejeicoes: Total- %d, M- %d, F- %d\n",
		totalRejections,
		rejectionsReceived[0],
		rejectionsReceived[1]);
	printf("Pedidos servidos: Total- %d, M- %d, F- %d\n",
		totalServed,
		requestsServed[0],
		requestsServed[1]);
	pthread_mutex_unlock(&arraysMtx);
}


//------------------------------------------------------------------------------------------------------//

/**
 * Closes Log File. Should be called at the end of the program.
 */
void closeLogFile(){
		fclose(logFile);
}

/**
 * Open log file. Should be called at the beggining of the program execution.
 */
void openLogFile(){
	pid = getpid();
	char location[100];
	sprintf(location,"/tmp/bal.%d",pid);
	int triesToOpen = 0;
	while ( (logFile = fopen(location, "a")) == NULL)
	{
		sleep(1);
		triesToOpen++;
		if (triesToOpen>4){
			perror("Error opening log file!\n");
			exit(1);
		}
	}
}

/**
 * This function is responsible for issuing log messages to a /tmp/bar.pid file that documents the
 * rollout of the asset. The messages have the format: inst – pid – tid – p: g – dur – tip
 * @param request
 */
void printRegistrationMessages(Request r1){
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
	time_t raw_time;
	time(&raw_time);
	pthread_mutex_lock(&writeFileMtx);
	fprintf(logFile,"%lu -%d -%d : %c -%d %s\n", raw_time,pid,r1.requestID,r1.gender,r1.requestTime,tip);
	pthread_mutex_unlock(&writeFileMtx);
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
		arrayOfGender[0]++;
	}
	else{
		arrayOfGender[1]++;
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

	pthread_mutex_lock(&arraysRejMtx);
	incrementGender((*requestToRead).gender, rejectionsReceived);
	pthread_mutex_unlock(&arraysRejMtx);
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
int open_FIFO(char* fifo_name, int *fifo_ans, int threadID){
	int numTries = 0;
	while ((*fifo_ans = open(fifo_name,O_WRONLY))==-1){
		numTries++;
		printf("Sauna: Error opening fifo awnser, try number %d\n",numTries);
		sleep(1);
		if (numTries > 4){
			threadsAvailable[threadID] = 1;
			return -1;
		}
	}
	return 0;
}

//------------------------------------------------------------------------------------------------------//



int getIndexOfFifo(char *fifo_name){
	int i;
	for (i=0; i <= FIFOS_ITER;i++){
		if (strcmp(fifo_name,namesOfFifos[i])==0){
			return i;
		}
	}
	return -1;
}

/**
 * Function to send the response of request to gerador, first try to open the fifo to response
 * and after send the response
 *
 * @param requestToRead request already processed and ready to be sent to gerador
 * @param threadID ID of thread needed for open_FIFO
 */
void sendResponse(Request requestToRead, int threadID){

	pthread_mutex_lock(&fifoMtx);
	int current_fifo = getIndexOfFifo(requestToRead.fifo_name);
	if (current_fifo == -1){
		printf("New Fifo Ans Detected, Opening it\n");
		if(open_FIFO(requestToRead.fifo_name, &current_fifo,threadID)==-1){
			perror("Opening FIFO");
			pthread_mutex_unlock(&fifoMtx);
			sem_post(sem);
			exit(2);
		}
		strcpy(namesOfFifos[FIFOS_ITER],requestToRead.fifo_name);
		fifo_array[FIFOS_ITER++] = current_fifo;
	}
	else{
		current_fifo = fifo_array[current_fifo];
	}

	printf("writing into fifo_ans\n");
	if (write(current_fifo, &requestToRead, sizeof(requestToRead)) == -1){
		perror("Writing Awnser Error");
		pthread_mutex_unlock(&fifoMtx);
		sem_post(sem);
		exit(2);
	}

	pthread_mutex_unlock(&fifoMtx);
	printf("Sent info back to generator\n");
}

//------------------------------------------------------------------------------------------------------//


/**
 * This function should be called every time that a sauna's user should rest in the sauna.
 * The function returns after the user has rested the desired time. Warning: Instead
 * of sleeping (which measures processor time), this function measures time elaped,
 * because this way it is more reallistic.
 *
 */
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

	pthread_mutex_unlock(&arraysReqRecMtx);
	incrementGender(requestToRead.gender, requestsReceived);
	pthread_mutex_unlock(&arraysReqRecMtx);

	if (checkGender(requestToRead.gender)){
		printf("Sauna: Accepting Request, SEX:%c\n",requestToRead.gender);
		/*if (sem_wait(sem)==-1){
			perror("sem_wait(sem)\n");
			exit(2);
		}*/
		gettimeofday(&tvBegin, NULL);
		requestToRead.state = ACEITE;
		pthread_mutex_unlock(&arraysReqSerMtx);
		incrementGender(requestToRead.gender, requestsServed);
		pthread_mutex_unlock(&arraysReqSerMtx);
		restInSauna(requestToRead, &tvBegin);
	}
	else{
		rejectRequest(&requestToRead);
	}

	printRegistrationMessages(requestToRead);
	sendResponse(requestToRead, threadID);

	printf("Thread %d is now available\n",threadID);
	threadsAvailable[threadID] = 1;
	sem_post(sem);
	actualGenderToDefault();
	return NULL;
}

//------------------------------------------------------------------------------------------------------//


/**
* Function to initialize the array threadsAvailable, which is used to know if
* one thread is available to use
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


/**
 * Function to create and initialize semaphore to be used to synchronize threads
 */
void createAndInitializeSem(){
	//create & initialize semaphore
	if ( (sem = sem_open(SEM_NAME,O_CREAT,0660,totalSeats)) == SEM_FAILED){
		perror("Error opening Semaphore\n");
		exit(2);
	}
	if (sem_init(sem,0,totalSeats)==-1){
		perror("sem_init()\n");
		exit(2);
	}
	int tempSemValue;
	sem_getvalue(sem, &tempSemValue);
	printf("Sem Value=%d\n",tempSemValue);
}

//------------------------------------------------------------------------------------------------------//


/**
 * Function to create and open FIFO to receive requests
 *
 * @param fifo_req FIFO after opened
 */
void createAndOpenFIFO_REQ(int* fifo_req){
	printf("Creating and open FIFO_entrada\n");
	mkfifo(fifo_entrada, 0660);
	int triesToOpenFifo = 0;
	while((*fifo_req=open(fifo_entrada,O_RDONLY))==-1){
		sleep(1);
		triesToOpenFifo++;
		if (triesToOpenFifo > 5){
			printf("Failed to Open Fifo Request\n");
			//exit(1);
		}
	}
	printf("FIFO_entrada open\n");
}

//------------------------------------------------------------------------------------------------------//


/**
 * Function to release all resources used
 *
 * @param requestThreads array of threads to be released
 * @param maxIdUsed number of threads on array
 */
void freeResources(pthread_t *requestThreads, int* maxIdUsed){
	int i = 0;
	for (; i <= *maxIdUsed;i++){
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
}

//------------------------------------------------------------------------------------------------------//


/**
 * Function to realloc memory of the array of threads to double
 *
 * @param requestThreads array of threads to be realloced
 * @param numThreads size of array before realloc
 */
void reallocMemory(pthread_t* requestThreads, int* numThreads){
	printf("Going to Reallocate Memory\n");
	int j = *numThreads;
	*numThreads = *numThreads * 2;
	if ( (requestThreads = realloc(requestThreads,*numThreads * sizeof(pthread_t))) == NULL){
		perror("Error Reallocating Memory for threads\n");
		exit(2);
	}
	for (; j < *numThreads;j++){
		threadsAvailable[j] = 1;
	}
	printf("Memory Reallocated\n");
}

//------------------------------------------------------------------------------------------------------//


/**
 * Function to process request readed and create a new thread that calls
 * handleRequest
 *
 * @param requestThreads array of all threads
 * @param requestToRead request readed to be processed
 * @param nextTheadAvailable id of one thread available to be used
 * @param maxIdUsed number to know what is the max number of threads used simultaneously
 */
void processRequest(pthread_t *requestThreads,
					Request* requestToRead,
					int* nextThreadAvailable,
					int* maxIdUsed)
{
	printf("Process new request\n");
	RequestWrapper requestWrapper;
	requestWrapper.request = *requestToRead;
	requestWrapper.threadID = *nextThreadAvailable;
	pthread_t threadAvailable = requestThreads[*nextThreadAvailable];
	pthread_create(&threadAvailable, NULL, handleRequest, &requestWrapper);
	threadsAvailable[*nextThreadAvailable] = 0;
	if (*nextThreadAvailable > *maxIdUsed){
		*maxIdUsed = *nextThreadAvailable;
	}
	printf("Created thread %d\n", *nextThreadAvailable);
}

//------------------------------------------------------------------------------------------------------//


/**
 * This program is a sauna manager. He receives requests to enter the sauna and then accepts
 * or rejects requests sent to the generator. In case of refusing requests these may be
 * because there is no space in the sauna or if the person inside the sauna is of the opposite sex.
 *
 * Main function:
 *  1) create and inizialize one semaphore to synchronize the threads of program
 *  2) open one FIFO to receive request
 *  3) create a array of all threads to be used
 *  4) read request
 *  5) process request
 *  6) when all done clean all memory used
 *
 * @param argc number of arguments
 * @param argv arrays of arguments
 */
int main(int argc, char const *argv[]) {
	/* code */
	if (argc != 2){
		printf("Usage: sauna <n_lugares>\n");
		return 1;
	}
	sscanf(argv[1], "%d", &totalSeats);
	printf("Total Seats available = %d\n",totalSeats);
	createAndInitializeSem();
	int fifo_req;
	createAndOpenFIFO_REQ(&fifo_req);
	openLogFile();
	int numThreads = totalSeats;
	pthread_t *requestThreads = malloc(sizeof(pthread_t)*numThreads);
	threadsAvailable = malloc(sizeof(int)*numThreads);
	initAvailableThreads(numThreads);
	printf("Allocated initial threads\n");
	int maxIdUsed = -1;

	Request requestToRead;
	int n = 1;
	while(n>0){
		n=read(fifo_req,&requestToRead, sizeof(requestToRead));
		if (sem_wait(sem)==-1){
			perror("sem_wait(sem)\n");
			exit(2);
		}
		int nextThreadAvailable = findNextAvailableThread(numThreads);
		if (nextThreadAvailable == -1){
			reallocMemory(requestThreads, &numThreads);
			nextThreadAvailable = findNextAvailableThread(numThreads);
		}
		if (n > 0){
			processRequest(requestThreads, &requestToRead, &nextThreadAvailable, &maxIdUsed);
		}
	}

	freeResources(requestThreads, &maxIdUsed);
	printStatus();
	return 0;
}
