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
int requestRejected = 0;
int requestsReceived[2] = {0};// M-0, F-1
int rejectionsReceived[2] = {0};
int requestsServed[2] = {0};


void printStatus(){
	int totalReceived = requestsReceived[0] + requestsReceived[1];
	int totalRejections = rejectionsReceived[0] + rejectionsReceived[1];
	int totalServed = requestsServed[0] + requestsServed[1];
	printf("Pedidos Recebidos: Total- %d, M- %d, F- %d\n",totalReceived,requestsReceived[0],requestsReceived[1]);
	printf("Rejeicoes: Total- %d, M- %d, F- %d\n",totalRejections,rejectionsReceived[0],rejectionsReceived[1]);
	printf("Pedidos servidos: Total- %d, M- %d, F- %d\n",totalServed,requestsServed[0],requestsServed[1]);

}

void printRegistrationMessages(Request r1){
	pid_t pid = getpid();
	char location[100];
	sprintf(location,"/tmp/bal.%d",pid);
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
		strcpy(tip,"RECEBIDO");
		break;
	case REJEITADO:
		strcpy(tip,"REJEITADO");
		break;
	case ACEITE:
		strcpy(tip,"SERVIDO");
		break;
	}
	fprintf(f,"%lu -%d -%d : %c -%d %s\n", raw_time,pid,r1.requestID,r1.gender,r1.requestTime,tip);
	fclose(f);
}


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
	Request requestToRead = *(Request*) args;
	while ((fifo_ans = open(requestToRead.fifo_name,O_WRONLY))==-1){
		printf("Sauna: Error opening fifo awnser\n");
		sleep(1);
		return NULL;
	}

	if (actualGender == 'N' || requestToRead.gender == actualGender){

		sem_wait(sem); //decrements Semaphore
		gettimeofday(&tvBegin, NULL);
		requestToRead.state = ACEITE;
		if (actualGender == 'N'){
			actualGender = requestToRead.gender;
		}
		if (requestToRead.gender == 'M'){
			requestsServed[0]++;
		}else{
			requestsServed[1]++;
		}
    do{
      gettimeofday(&tvEnd, NULL);
      timeval_subtract(&tvDiff, &tvEnd, &tvBegin);
      elapsedMiliseconds = tvDiff.tv_sec * 1000 + tvDiff.tv_usec/1000.0;
			sleep(0.001);
    } while(elapsedMiliseconds < requestToRead.requestTime);
    sem_post(sem);
    int semValue;
    if (sem_getvalue(sem, &semValue) == -1){
      perror("Error Reading Semaphore Value\n");
    }else{
      if (semValue == totalSeats){
				printf("Romm Clear\n");
        actualGender = 'N';
      }
    }
	}
	else{
		//Reject the Request
		requestToRead.state = REJEITADO;
		printf("Sauna: Rejecting Request\n");

		if (requestToRead.gender == 'M'){
			rejectionsReceived[0]++;
		}else{
			rejectionsReceived[1]++;
		}
	}

	if (requestToRead.gender == 'M'){
		requestsReceived[0]++;
	}else{
		requestsReceived[1]++;
	}

	printRegistrationMessages(requestToRead);

	if (write(fifo_ans, &requestToRead, sizeof(requestToRead)) <= 0){
		printf("Nothing to write\n");
	}
  return NULL;
}




int main(int argc, char const *argv[]) {
	/* code */
	if (argc != 2){
		printf("Usage: sauna <n_lugares>\n");
		return 1;
	}
  sscanf(argv[1], "%d", &totalSeats);
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
	int n = 1;
	while(n>0){
		n=read(fifo_req,&requestToRead, sizeof(requestToRead));
		pthread_create(&requestThread, NULL, handleRequest, &requestToRead);
		pthread_join(requestThread, NULL);
	}

  sem_destroy(sem);
	printStatus();
	unlink(fifo_entrada);

	return 0;
}
