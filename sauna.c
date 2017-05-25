#include <stdio.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
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
 * Global variables
 */
pthread_mutex_t freeSeatsMutex = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t  freeSeatsCond  = PTHREAD_COND_INITIALIZER;
int freeSeats;
int totalSeats;
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
	else if (gender == 'F'){
		arrayOfGender[1]++;
	}
	else{
		printf("WARNING,GENDER IS INCORRECT\n");
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
		pthread_mutex_lock(&freeSeatsMutex);
		if (freeSeats == totalSeats){
			pthread_mutex_lock(&genderMtx);
			actualGender = 'N';
			pthread_mutex_unlock(&genderMtx);
		}
		pthread_mutex_unlock(&freeSeatsMutex);
}


//------------------------------------------------------------------------------------------------------//


/**
 * Function to try open fifo, but if can't open FIFO the function set the thread as available for
 * do other stuff and close.
 *
 * @param fifo_name name of FIFO to try open
 * @param fifo_ans is the fifo if it was opened correctly
 */
int open_FIFO(char* fifo_name, int *fifo_ans){

	int numTries = 0;
	printf("Opening Fifo\n");
	while ((*fifo_ans = open(fifo_name,O_WRONLY))==-1){
		numTries++;
		sleep(1);
		printf("Sauna: Error opening fifo awnser, try number %d\n",numTries);
		if (numTries > 4){
			return -1;
		}
	}
	printf("FIFO opened with no error\n");
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
 */
void sendResponse(Request requestToRead){

	printf("Fifo Rejeitados from current request = %s\n",requestToRead.fifo_name);
	int current_fifo = getIndexOfFifo(requestToRead.fifo_name);
	pthread_mutex_lock(&fifoMtx);
	if (current_fifo == -1){
		printf("New Fifo Ans Detected, Opening it\n");
		if(open_FIFO(requestToRead.fifo_name, &current_fifo)==-1){
			perror("Opening FIFO");
			exit(2);
		}
		printf("Opened FIFO with sucess\n");
		strcpy(namesOfFifos[FIFOS_ITER],requestToRead.fifo_name);
		fifo_array[FIFOS_ITER++] = current_fifo;
	}
	else{
		current_fifo = fifo_array[current_fifo];
	}
	pthread_mutex_unlock(&fifoMtx);
	printf("writing into fifo_ans\n");
	if (write(current_fifo, &requestToRead, sizeof(requestToRead)) == -1){
		perror("Writing Awnser Error");
		exit(2);
	}
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
void restInSauna(Request *requestToRead){
		struct timeval tvEnd, tvDiff;
		struct timeval tvBegin;
		gettimeofday(&tvBegin,NULL);
		int elapsedMiliseconds;
		printf("Going to sleep in Sauna %d ms\n", requestToRead->requestTime);
		//usleep(requestToRead->requestTime*1000);
		do{
			gettimeofday(&tvEnd, NULL);
			timeval_subtract(&tvDiff, &tvEnd, &tvBegin);
			elapsedMiliseconds = tvDiff.tv_sec * 1000 + tvDiff.tv_usec/1000.0;
		} while(elapsedMiliseconds < requestToRead->requestTime);
		printf("Rested in Sauna, SEX:%c\n",requestToRead->gender);
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
	Request requestToRead = *(Request*) args;

	pthread_mutex_lock(&arraysReqRecMtx);
	incrementGender(requestToRead.gender, requestsReceived);
	pthread_mutex_unlock(&arraysReqRecMtx);

	printf("Sauna: Handle Request, SEX:%c\n",requestToRead.gender);
	requestToRead.state = ACEITE;
	pthread_mutex_lock(&arraysReqSerMtx);
	incrementGender(requestToRead.gender, requestsServed);
	pthread_mutex_unlock(&arraysReqSerMtx);
	restInSauna(&requestToRead);
	pthread_mutex_lock(&freeSeatsMutex);
	freeSeats++;
	pthread_cond_signal (&freeSeatsCond);
	pthread_mutex_unlock(&freeSeatsMutex);
	printRegistrationMessages(requestToRead);
	sendResponse(requestToRead);


	actualGenderToDefault();
	return NULL;
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
void freeResources(){
	unlink(fifo_entrada);
	printf("Destryoed FIFO\n");
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
void processRequest(Request* requestToRead)
{
	pthread_t tid;
	freeSeats--;
	pthread_create(&tid, NULL, handleRequest, requestToRead);
}

//------------------------------------------------------------------------------------------------------//


/**
 * This program is a sauna manager. He receives requests to enter the sauna and then accepts
 * or rejects requests sent to the generator. In case of refusing requests these may be
 * because there is no space in the sauna or if the person inside the sauna is of the opposite sex.
 *
 * Main function:
 *  1)
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
	freeSeats = totalSeats;
	int fifo_req;
	createAndOpenFIFO_REQ(&fifo_req);
	openLogFile();

	Request requestToRead;
	int n = 1;
	int readI = 1;
	while(n>=0){
		n=read(fifo_req,&requestToRead, sizeof(requestToRead));
		if(n>0){
			readI = 1;
			printf("SAUNA MAIN: Received Request With Gender %c\n",requestToRead.gender);
			if (requestToRead.gender == actualGender || actualGender == 'N'){
				pthread_mutex_lock(&freeSeatsMutex);
				while(freeSeats < 1){
					pthread_cond_wait(&freeSeatsCond,&freeSeatsMutex);
					}
					processRequest(&requestToRead);
					pthread_mutex_unlock(&freeSeatsMutex);
				}
				else{
					printf("SAUNA MAIN, REJECTING REQUEST\n");
					rejectRequest(&requestToRead);
					printRegistrationMessages(requestToRead);
					sendResponse(requestToRead);
			}

		}
		else if(n==0 && readI){
			printf("Do you want to exit? (Y/N)\n");
			char response;
			scanf("%c", &response);
			if(response == 'y' || response == 'Y')
				break;
			else if(response == 'n' || response == 'N'){
				readI = 0;
			}
		}
	}

	freeResources();
	printStatus();
	return 0;
}
