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

void* handleRequest(void * args){
  int fifo_ans;
  Request requestToRead = *(Request*) args[0];
  while ((fifo_ans = open(requestToRead.fifo_name,O_WRONLY))==-1){
    printf("Sauna: Error opening fifo awnser\n");
    sleep(1);
  }
  if (actualGender == 'N' || requestToRead.gender == actualGender){
    //Accept Request
    sem_wait(sem); //decrements Semaphore
    //Start counting Time. When time = requesTime, post the semaphore to free 1 seat

    sem_post(sem);
  }
  else{
    //Reject the Request
    requestToRead.state = REJEITADO;
    write(fifo_ans, &requestToRead, sizeof(requestToRead));
  }

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

    //create & initialize semaphore
    sem = sem_open(SEM_NAME,O_CREAT,0600,totalSeats);

    Request requestToRead;
    Request answer;

    int n;
    do {
        n = read(fifo_req,&requestToRead, sizeof(requestToRead));
        write(fifo_ans, &answer, sizeof(answer));

    } while (n > 0);

    return 0;
}
