#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <time.h>

#define fifo_entrada "/tmp/entrada"
#define fifo_rejeitados "/tmp/rejeitados_"
int nPedidos;
pthread_mutex_t mut = PTHREAD_MUTEX_INITIALIZER;
char fifo_dir[100];

typedef struct{
    char fifo_name[100]; // fifo_to_answer
    //TODO: complete with data for request
    int requestID;
    char gender;
    int requestTime;
    int tries;
} Request;


Request *rejectedQueue;



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
      abort();
    }
  }
  int maxUtilizationTime = *(int*) args[1];
  int i = 0;
  while(nPedidos > 0){
    pthread_mutex_lock(&mut);
    nPedidos--;
    pthread_mutex_unlock(&mut);
    Request request;
    request.fifo_name = (char *) args[2];
    request.requestID = i++;
    randomNumber  = rand() % 2;
    request.gender = randomNumber == 0 ? 'M' : 'F';
    request.requestTime = (rand() % maxUtilizationTime) + 1;
    request.tries = 0;
    sleep(1);
    write(fifo_req, &request, sizeof(request));

  }

}

//------------------------------------------------------------------------------------------------------//
void * handleRejected(void *args){
  int triesToOpenFifo = 0;
  while((fifo_ans=open(fifo_dir,O_RDONLY))==-1){
    sleep(1);
    triesToOpenFifo++;
    if (triesToOpenFifo > 5){
      printf("Cannot Open Fifo Req\n");
      abort();
    }
  }

  Request rejected;
  while (1){
    read(fifo_ans, &rejected, sizeof(rejected));
    nPedidos++;
    //Keep Coding Here
  }


}


int main(int argc, char const *argv[]) {
  /* code */
  srand(time(NULL));
  nPedidos = argv[1];
  int maxUtilizationTime = argv[2]; //in miliseconds

  fifo_dir = fifo_rejeitados;
  int pid = getpid();
  char pidString[100];
  sprintf(pidString, "%d", pid);
  strcat(fifo_dir, pidString);
  mkfifo(fifo_dir, 0660);







  return 0;
}
