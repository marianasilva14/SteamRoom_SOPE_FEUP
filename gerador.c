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
char *fifo_rejeitados = "/tmp/rejeitados_";

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
char fifo_dir[100];
Request *rejectedQueue;
int queueIndex = 0;
int generatedRequests[2] = {0,0};// M-0, F-1
int rejectionsReceived[2] = {0,0};
int discardedRejections[2] = {0,0};
int missResponse;
int nPedidos;


//------------------------------------------------------------------------------------------------------//


/**
 * This function is responsible for issuing log messages to a /tmp/ger.pid file that documents the
 * rollout of the asset. The messages have the format: inst – pid – p: g – dur – tip
 * @param request
 */
void printRegistrationMessages(Request r1){
  pid_t pid = getpid();
  char location[100];
  sprintf(location,"/tmp/ger.%d",pid);
  FILE *f = fopen(location, "a");
  if (f == NULL)
  {
    printf("Error opening file!\n");
    exit(1);
  }
  time_t raw_time;
  time(&raw_time);

  char tip[10];
  switch (r1.state){
    case PEDIDO:
    strcpy(tip,"PEDIDO");
    break;
    case REJEITADO:
    strcpy(tip,"REJEITADO");
    break;
    case DESCARTADO:
    strcpy(tip,"DESCARTADO");
    default:
    break;
  }
  if (fprintf(f,"%lu -%d -%d : %c -%d %s\n", raw_time,pid,r1.requestID,r1.gender,r1.requestTime,tip)<=0){
    perror("Error writing to file\n");
  }
}

/**
 * Function that checks if is still processing
 * @return 0 or 1
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


//------------------------------------------------------------------------------------------------------//


/**
 * This function is responsible for generating requests that will be sent to the sauna program
 * args[0] = number of request
 * args[1] = maximum usage time
 * args[2] = path to answer FIFO
 * @param args
 */
void * generateRequests(void * args){
  int randomNumber;
  int originalGeneratedPedidos = 0;
  int triesToOpenFifo = 0;
  int fifo_req;
  while((fifo_req=open(fifo_entrada,O_WRONLY))==-1){
    sleep(1);
    triesToOpenFifo++;
    if (triesToOpenFifo > 4){
      printf("Failed to Open Fifo Req\n");
      return NULL;
    }
  }

  int maxUtilizationTime = *(int*) args;
  int i = 0;
  Request request;

  while(isStillProcessing()){
    usleep(10000);
    if(queueIndex > 0){
      nPedidos++;
      pthread_mutex_lock(&queueMtx);
      request = rejectedQueue[--queueIndex];
      pthread_mutex_unlock(&queueMtx);
      printf("Sending back Rejected Requests, Sex: %c\n",request.gender);
      if (request.gender == 'M'){
        rejectionsReceived[0]++;
      }else{
        rejectionsReceived[1]++;
      }
    }
    else{
      if (nPedidos > 0){
        originalGeneratedPedidos++;
        strcpy(request.fifo_name, fifo_dir);
        request.requestID = i++;
        randomNumber = rand() % 2;
        request.gender = randomNumber == 0 ? 'M' : 'F';
        request.requestTime = (rand() % maxUtilizationTime) + 1;
        request.tries = 1;
        if (request.gender == 'M'){
          generatedRequests[0]++;
        }else{
          generatedRequests[1]++;
        }
      }
    }
    if (nPedidos > 0){
      printf("Generating Request\n");
      nPedidos--;
      request.state = PEDIDO;
      write(fifo_req, &request, sizeof(request));
      printRegistrationMessages(request);

    }
  }
  printf("Debug 3\n");

  return NULL;
}


//------------------------------------------------------------------------------------------------------//


/**
 * Function that is responsible for handling requests that are rejected by the sauna program and
 * if the number of rejections of a given request exceeds 3, it is discarded
 * @param args
 */
void * handleRejected(void * args){
  int triesToOpenFifo = 0;
  int fifo_ans;
  printf("Fifo Dir %s\n", fifo_dir);
  while((fifo_ans=open(fifo_dir,O_RDONLY))==-1){
    //sleep(1);
    triesToOpenFifo++;
    if (triesToOpenFifo > 5){
      printf("Failed to Open Fifo Rejected\n");
    }
  }
  printf("Debug1\n");
  Request rejected;
  while (isStillProcessing()){
    read(fifo_ans, &rejected, sizeof(rejected));
    pthread_mutex_lock(&mrMtx);
    printf("missResponse value=%d\n",missResponse);
    pthread_mutex_unlock(&mrMtx);
    if(rejected.state == REJEITADO)
    {
      printf("Sauna Rejeitou SEX: %c\n", rejected.gender);
      rejected.tries++;
      if (rejected.tries > 3){
        rejected.state = DESCARTADO;
        pthread_mutex_lock(&mrMtx);
        missResponse--;
        pthread_mutex_unlock(&mrMtx);
        if (rejected.gender == 'M'){
          discardedRejections[0]++;
        }else{
          discardedRejections[1]++;
        }
      }
      else{
        pthread_mutex_lock(&queueMtx);
        rejectedQueue[queueIndex++] = rejected;
        pthread_mutex_unlock(&queueMtx);
      }
      printRegistrationMessages(rejected);
    }
    else{
      pthread_mutex_lock(&mrMtx);
      missResponse--;
      pthread_mutex_unlock(&mrMtx);
      printf("Sauna Aceitou SEX: %c\n", rejected.gender);
    }
  }
  printf("Closing fifo ans\n");
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


//------------------------------------------------------------------------------------------------------//


int main(int argc, char const *argv[]) {
  /* code */
  srand(time(NULL));
  sscanf(argv[1], "%d", &missResponse);
  nPedidos = missResponse;
  printf("Main: nPedidos=%d\n",nPedidos);
  rejectedQueue = malloc(missResponse * sizeof(Request));
  int maxUtilizationTime; //in miliseconds
  sscanf(argv[2], "%d", &maxUtilizationTime);

  strcpy(fifo_dir, fifo_rejeitados);
  int pid = getpid();
  char pidString[100];
  sprintf(pidString, "%d", pid);
  strcat(fifo_dir, pidString);
  mkfifo(fifo_dir, 0660);
  pthread_t requestThread, rejectedThread;
  pthread_create(&requestThread, NULL, generateRequests, &maxUtilizationTime);
  printf("Debug 4\n");
  pthread_create(&rejectedThread, NULL, handleRejected, NULL);
  printf("Debug 5\n");
  pthread_join(requestThread, NULL);
  printf("Debug 6\n");
  pthread_join(rejectedThread, NULL);
  printf("Debug 7\n");
  printStatus();
  printf("Debug 8\n");
  unlink(fifo_dir);

  return 0;
}
