#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <time.h>
#include <pthread.h>

#define fifo_entrada "/tmp/entrada"
#define fifo_rejeitados "/tmp/rejeitados_"


typedef enum stateofrequest {PEDIDO, ACEITE, REJEITADO, DESCARTADO} StateOfRequest;

typedef struct{
    char fifo_name[100]; // fifo_to_answer
    //TODO: complete with data for request
    int requestID;
    char gender;
    int requestTime;
    int tries;
    StateOfRequest state;
} Request;


//GLOBAL
int nPedidos;
pthread_mutex_t mut = PTHREAD_MUTEX_INITIALIZER;
char fifo_dir[100];
Request *rejectedQueue;
int queueIndex = 0;
int requestRejected = 0;


//------------------------------------------------------------------------------------------------------//
//args[0] = n Pedidos
//args[1] = maxUtilizationTime
//args[2] = path to awnser FIFO
void * generateRequests(void * args){
  int randomNumber;
  int triesToOpenFifo = 0;
  int fifo_req;
  while((fifo_req=open(fifo_entrada,O_WRONLY))==-1){
    sleep(1);
    triesToOpenFifo++;
    if (triesToOpenFifo > 5){
      printf("Cannot Open Fifo Req\n");
      //exit(1);
    }
  }
  int maxUtilizationTime = *(int*) args;
  int i = 0;
  Request request;
  do {
    if(requestRejected > 0){
      pthread_mutex_lock(&mut);
      request = rejectedQueue[queueIndex - requestRejected--];
      pthread_mutex_unlock(&mut);
    }
    else{
      strcpy(request.fifo_name, fifo_dir);
      request.requestID = i++;
      randomNumber  = rand() % 2;
      request.gender = randomNumber == 0 ? 'M' : 'F';
      request.requestTime = (rand() % maxUtilizationTime) + 1;
      request.tries = 0;
      request.state = PEDIDO;
    }

    pthread_mutex_lock(&mut);
    nPedidos--;
    pthread_mutex_unlock(&mut);
    write(fifo_req, &request, sizeof(request));
    sleep(1);
  } while(nPedidos > 0);
}

//------------------------------------------------------------------------------------------------------//
void * handleRejected(void * missResponse){
  int triesToOpenFifo = 0;
  int fifo_ans;
  while((fifo_ans=open(fifo_dir,O_RDONLY))==-1){
    sleep(1);
    triesToOpenFifo++;
    if (triesToOpenFifo > 5){
      printf("Cannot Open Fifo Req\n");
      //abort();
    }
  }

  Request rejected;
  while (missResponse > 0){
    read(fifo_ans, &rejected, sizeof(rejected));
    if(rejected.state == REJEITADO)
    {
      pthread_mutex_lock(&mut);
      nPedidos++;
      rejected.tries++;
      if (rejected.tries > 3){
        rejected.state = DESCARTADO;
      }
      else{
        rejectedQueue[queueIndex++] = rejected;
        requestRejected++;
      }
      pthread_mutex_unlock(&mut);
    }
  }
}


int main(int argc, char const *argv[]) {
  /* code */
  srand(time(NULL));
  sscanf(argv[1], "%d", &nPedidos);
  int missResponse = nPedidos;
  rejectedQueue = malloc(nPedidos * sizeof(Request));
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
  pthread_create(&rejectedThread, NULL, handleRejected, &missResponse);
  pthread_join(requestThread, NULL);
  pthread_join(rejectedThread, NULL);

  return 0;
}
