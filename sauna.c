#include <stdio.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <semaphore.h>
#include <sys/mman.h>



#define fifo_entrada "/tmp/entrada"

typedef enum stateofrequest {PEDIDO, ACEITE, REJEITADO, DESCARTADO} StateOfRequest;


typedef struct{
    char fifo_name[100]; // fifo_to_answer
    int requestID;
    char gender;
    int requestTime;
    int tries;
    StateOfRequest state;
} Request;

//Global Variables
int totalSeats;
char SEM_NAME[] = "/sem1";
sem_t *sem;
char actualGender = 'N'; //N means None


void timeval_subtract(struct timeval *result, struct timeval *t2, struct timeval *t1)
{
    long int diff = (t2->tv_usec + 1000000 * t2->tv_sec) - (t1->tv_usec + 1000000 * t1->tv_sec);
    result->tv_sec = diff / 1000000;
    result->tv_usec = diff % 1000000;

}

void* handleRequest(void * args){
  int fifo_ans;
  struct timeval tvBegin, tvEnd, tvDiff;
  int elapsedMiliseconds;
  Request requestToRead = *(Request*) args[0];
  while ((fifo_ans = open(requestToRead.fifo_name,O_WRONLY))==-1){
    printf("Sauna: Error opening fifo awnser\n");
    sleep(1);
  }
  if (actualGender == 'N' || requestToRead.gender == actualGender){
    //Accept Request
	  requestToRead.state = ACEITE;
    if (actualGender == 'N'){
      actualGender = requestToRead.gender;
    }
    sem_wait(sem); //decrements Semaphore

    gettimeofday(&tvBegin, NULL);
    do{
      gettimeofday(&tvEnd, NULL);
      timeval_subtract(&tvDiff, &tvEnd, &tvBegin);
      elapsedMiliseconds = tvDiff.tv_sec * 1000 + tvDiff.tv_usec/1000.0;
    } while(elapsedMiliseconds < requestToRead.requestTime);
    sem_post(sem);
    int semValue;
    if (sem_getvalue(sem, &semValue) == -1){
      perror("Error Reading Semaphore Value\n");
    }else{
      if (semValue == totalSeats){
        actualGender = 'N';
      }
    }

  }
  else{
    //Reject the Request
    requestToRead.state = REJEITADO;
  }
  write(fifo_ans, &requestToRead, sizeof(requestToRead));
}


int main(int argc, char const *argv[]) {
    /* code */
    if (argc != 2){
      printf("Usage: sauna <n_lugares>\n");
      return 1;
    }
    totalSeats = argv[1];
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

    pthread_t requestThread;


    //create & initialize semaphore
    sem = sem_open(SEM_NAME,O_CREAT,0600,totalSeats);

    Request requestToRead;
    Request answer;

    int n;
    do {
        n = read(fifo_req,&requestToRead, sizeof(requestToRead));
        pthread_create(&requestThread, NULL, handleRequest, &requestToRead);
        pthread_join(requestThread, NULL);

    } while (n > 0);
    sem_destroy(&sem);

    return 0;
}
