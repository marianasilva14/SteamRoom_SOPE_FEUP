#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <time.h>
#include <pthread.h>

/**
 * Named pipes (FIFOS) to contact the program that manages the sauna
 */
char *fifo_entrada = "/tmp/entrada";
char *fifo_rejeitados;

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
 * GLOBAL VARIABLES
 */
pthread_mutex_t mut = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t mrMtx = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t queueMtx = PTHREAD_MUTEX_INITIALIZER;
Request *rejectedQueue;
int queueIndex = 0;
int generatedRequests[2] = {0,0};// M-0, F-1
int rejectionsReceived[2] = {0,0};
int discardedRejections[2] = {0,0};
int missResponse;
int remainingRequests;
FILE *logFile;
pid_t pid;
int sleepTimeBetweenRequests = 10000;
int maxUtilizationTime;


//------------------------------------------------------------------------------------------------------//


/**
 * This function is responsible for issuing log messages to a /tmp/ger.pid file that documents the
 * rollout of the asset. The messages have the format: inst – pid – p: g – dur – tip
 * @param request
 */
void printRegistrationMessages(Request r1){

  time_t raw_time;
  time(&raw_time);

  char tip[11];
  switch (r1.state){
    case PEDIDO:
    strncpy(tip,"PEDIDO",7);
    break;
    case REJEITADO:
    strncpy(tip,"REJEITADO",9);
    break;
    case DESCARTADO:
    strncpy(tip,"DESCARTADO",11);
    default:
    break;
  }
  if (fprintf(logFile,"%lu -%d -%d : %c -%d %s\n", raw_time,getpid(),r1.requestID,r1.gender,r1.requestTime,tip)<=0){
    perror("Error writing to file\n");
  }
}

/**
 * Function that checks if is still processing
 * @return 0 or 1 (0 if isn't working, 1 if still working)
 */
int isStillProcessing(){
  int returnVal = 0;
  pthread_mutex_lock(&mrMtx);
  if (missResponse > 0){
    returnVal = 1;
  }
  pthread_mutex_unlock(&mrMtx);
  return returnVal;
}

/**
 * This function opens the FIFO which will be used to send the requests.
 * @param fifo_req A null-pointer to an int that will be assigned to the FIFO that will be opened.
 * @param fifo_name The name of the FIFO to open.
 * @param flags The flags to use to open the FIFO.
 * @return -1 in case of error, 0 in case of success.
 */
int openFifo(int *fifo_req, char *fifo_name, int flags){
  int triesToOpenFifo = 0;
  while((*fifo_req=open(fifo_name,flags))==-1){
    sleep(1);
    triesToOpenFifo++;
    if (triesToOpenFifo > 4){
      perror("Failed to Open Fifo\n");
      return -1;
    }
  }
  return 0;
}


/**
 * This function gets a request from the queue, and updates the
 * rejectionsReceived arrays.
 * @param request A null-pointer to a Request that will be assigned to a Request in the queue.
 */
void handleRequestInQueue(Request *request){
  remainingRequests++;
  pthread_mutex_lock(&queueMtx);
  *request = rejectedQueue[--queueIndex];
  pthread_mutex_unlock(&queueMtx);
  printf("Sending back Rejected Requests, Sex: %c\n",request->gender);
  if (request->gender == 'M'){
    rejectionsReceived[0]++;
  }else{
    rejectionsReceived[1]++;
  }
}

/*
 * This function chekcs if there is any request rejected waiting to be sent again.
 * @return An int > 0 if true, 0 otherwise.
 */
int areThereRequestsWaitingInQueue(){
  pthread_mutex_lock(&queueMtx);
  int cpy = queueIndex;
  pthread_mutex_unlock(&queueMtx);
  return cpy;
}

/**
 * This function fills a new request with the necessary information.
 * @param request The Request to be filled.
 * @param id The ID that the request should take.
 */
void fillNewRequest(Request *request, int *id){
  strcpy(request->fifo_name, fifo_rejeitados);
  request->requestID = (*id)++;
  request->gender = (rand() % 2) == 0 ? 'M' : 'F';
  request->requestTime = (rand() % maxUtilizationTime) + 1;
  request->tries = 1;
  if (request->gender == 'M'){
    generatedRequests[0]++;
  }else{
    generatedRequests[1]++;
  }
}

/**
 * This function sends a request through the specified fifo
 * @param fifo_req The FIFO which shall be used to send the request
 * @param request The Request to be sent
 */
void writeRequestInFifo(int fifo_req,Request *request){
  printf("Generating Request\n");
  remainingRequests--;
  request->state = PEDIDO;
  write(fifo_req, request, sizeof(*request));
}

/**
 * This function is responsible for generating requests that will be sent to the sauna program
 * @param args NULL
 */
void * generateRequests(void * args){
  int fifo_req;
  if (openFifo(&fifo_req,fifo_entrada,O_WRONLY)==-1){
    return NULL;
  }
  int requestCounter = 0;
  Request request;
  while(isStillProcessing()){
    usleep(sleepTimeBetweenRequests);

    if(areThereRequestsWaitingInQueue()){
      handleRequestInQueue(&request);
    }
    else{
      if (remainingRequests > 0){
        fillNewRequest(&request,&requestCounter);
      }
    }
    if (remainingRequests > 0){
      writeRequestInFifo(fifo_req,&request);
      printRegistrationMessages(request);

    }
  }
  return NULL;
}


//------------------------------------------------------------------------------------------------------//


/**
 * This function updates the state of a discarded request, updates the missResponse value
 * and updates the discardedRejections arrays.
 *
 * @param discarded A pointer to the request that should be discarded.
 */
void handleDiscardedRequest(Request *discarded){
  discarded->state = DESCARTADO;
  pthread_mutex_lock(&mrMtx);
  missResponse--;
  pthread_mutex_unlock(&mrMtx);
  if (discarded->gender == 'M'){
    discardedRejections[0]++;
  }else{
    discardedRejections[1]++;
  }
}

/**
 * Function that is responsible for updating the tries of a Request and the rejectedQueue;
 * It also calls a function to handle discarded requests.
 * @param rejected A pointer to the rejected request,
 */
void handleRejectedRequest(Request *rejected){
  rejected->tries++;
  if (rejected->tries > 3){
    handleDiscardedRequest(rejected);
  }
  else{
    pthread_mutex_lock(&queueMtx);
    rejectedQueue[queueIndex++] = *rejected;
    pthread_mutex_unlock(&queueMtx);
  }
  printRegistrationMessages(*rejected);
}


/**
 * Function that is responsible for handling requests that are rejected by the sauna program and
 * if the number of rejections of a given request exceeds 3, it is discarded
 * @param args NULL-not used
 */
void * handleSaunaReply(void * args){
  int fifo_ans;
  openFifo(&fifo_ans,fifo_rejeitados,O_RDONLY);
  Request rejected;
  while (isStillProcessing()){
    read(fifo_ans, &rejected, sizeof(rejected));
    pthread_mutex_lock(&mrMtx);
    printf("missResponse value=%d\n",missResponse);
    pthread_mutex_unlock(&mrMtx);
    if(rejected.state == REJEITADO)
    {
      handleRejectedRequest(&rejected);
    }
    else{
      pthread_mutex_lock(&mrMtx);
      missResponse--;
      pthread_mutex_unlock(&mrMtx);
    }
  }
  close(fifo_ans);

  return NULL;
}


//------------------------------------------------------------------------------------------------------//


/**
 * This function prints statistical information on the number of requests generated, the number
 * of rejections received and the number of discards discarded (total and by gender).
 */
void printStatus(){
  int totalGenerated = generatedRequests[0] + generatedRequests[1];
  int totalRejections = rejectionsReceived[0] + rejectionsReceived[1];
  int totalDiscarded = discardedRejections[0] + discardedRejections[1];
  printf("Pedidos Gerados: Total- %d, M- %d, F- %d\n",totalGenerated,generatedRequests[0],generatedRequests[1]);
  printf("Rejeicoes Recebidas: Total- %d, M- %d, F- %d\n",totalRejections,rejectionsReceived[0],rejectionsReceived[1]);
  printf("Rejeicoes Descartadas: Total- %d, M- %d, F- %d\n",totalDiscarded,discardedRejections[0],discardedRejections[1]);
}

/**
 * setLogFile() is responsible for creating and open the file to which the current process
 * should output the necessary information.
 */
void setLogFile(){
  pid = getpid();
  char location[100];
  sprintf(location,"/tmp/ger.%d",pid);
  logFile = fopen(location, "a");
  int triesToOpen = 0;
	while (logFile== NULL)
	{
		sleep(1);
		triesToOpen++;
    logFile = fopen(location,"a");
		if (triesToOpen>4){
			printf("Error opening file %s!\n", location);
			exit(1);
		}
	}
}




/**
* createFifoToReceiveAwnsers() is rensponsible to set the current value on string fifo_rejeitados
* and to create the Fifo itself.
*/
void createFifoToReceiveAwnsers(){
  fifo_rejeitados = malloc(sizeof(char) * 26);
  strcpy(fifo_rejeitados,"/tmp/rejeitados_");
  char pidString[10];
  sprintf(pidString, "%d", pid);
  strcat(fifo_rejeitados, pidString);
  mkfifo(fifo_rejeitados, 0660);
}

/**
 * createThreads(pthread_t *requestThread, pthread_t *rejectedThread) is responsible for creating the thread
 * that will generate requests and the thread that will handle rejected requests from sauna.
 * @param requestThread pointer to the thread responsible for generating requests.
 * @param rejectedThread pointer to the thread responsible for handling rejected requests.
 */
void createThreads(pthread_t *requestThread, pthread_t *replyThread){
  pthread_create(requestThread, NULL, generateRequests,NULL);
  pthread_create(replyThread, NULL, handleSaunaReply, NULL);
}

/**
 * waitAndTerminateThreads(pthread_t *requestThread,pthread_t rejectedThread) is responsible for
 * waiting and terminating the requestThread and the rejectedThread.
 *
 * @param requestThread pointer to the thread responsible for generating requests.
 * @param rejectedThread pointer to the thread responsible for handling rejected requests.
 */
void waitAndTerminateThreads(pthread_t *requestThread,pthread_t *replyThread){
  pthread_join(*requestThread, NULL);
  pthread_join(*replyThread, NULL);

}

/**
 * handleThreads() is responsible for creating the necessary threads, wait and terminate them.
 */
void handleThreads( ){
  pthread_t requestThread, replyThread;
  createThreads(&requestThread,&replyThread);
  waitAndTerminateThreads(&requestThread,&replyThread);
}

/**
 * readSleepTimeBetweenRequests(int argc, char const *argv[]) is responsible for setting the time between requests generation.
 * If no time is passed as argument, a default value = 10ms will be used;
 * @param argc The number of arguments read when program was lauched.
 * @param argv The arguments read when program was launched.
 */
void readSleepTimeBetweenRequests(int argc, char const *argv[]){
  if (argc==4){
    int tempSleepTime;
    sscanf(argv[3],"%d",&tempSleepTime);
    sleepTimeBetweenRequests = tempSleepTime * 1000;
  }
}

/**
 * readConsoleArguments(int argc, char const *argv[]) is rensponsible for reading the arguments passed by launch,
 * and saving them on the global variables.
 *
 * @param argc The number of arguments read when program was lauched.
 * @param argv The arguments read when program was launched.
 */
int readConsoleArguments(int argc, char const *argv[]){
  if (argc > 2 && argc < 5){
    sscanf(argv[1], "%d", &missResponse);
    sscanf(argv[2], "%d", &maxUtilizationTime);
    readSleepTimeBetweenRequests(argc,argv);
    return 0;
  }
  else{
    printf("Invalid number of arguments\n");
    printf("Example of how to run: 'gerador <n_pedidos> <tempo_max_utilizacao> <tempo_entre_geracao_pedidos>(optional)'\n");
    return 1;
  }

}

//------------------------------------------------------------------------------------------------------//


/**
 * main(int argc, char const *argv[]) is the first function to run.
 * @param argc The number of arguments read when program was lauched.
 * @param argv The arguments read when program was launched.
 */
int main(int argc, char const *argv[]) {
  srand(time(NULL));
  if(readConsoleArguments(argc,argv))
    return 1;
  remainingRequests = missResponse;
  rejectedQueue = malloc(missResponse * sizeof(Request));
  setLogFile();
  createFifoToReceiveAwnsers();
  handleThreads();
  printStatus();
  unlink(fifo_rejeitados);

  return 0;
}
