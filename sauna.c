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
pthread_mutex_t genderMtx = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t arraysMtx = PTHREAD_MUTEX_INITIALIZER;


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
    default:
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

int checkGender(char requestGender){
	int returnVal = 0;
	pthread_mutex_lock(&genderMtx);
	if (actualGender == 'N' || requestGender == actualGender){
		returnVal = 1;
	}
	pthread_mutex_unlock(&genderMtx);
	return returnVal;
}

void* handleRequest(void * args){
	int fifo_ans;
  struct timeval tvBegin, tvEnd, tvDiff;
  int elapsedMiliseconds = 0;
	int semValue;
	Request requestToRead = *(Request*) args;
	while ((fifo_ans = open(requestToRead.fifo_name,O_WRONLY))==-1){
		printf("Sauna: Error opening fifo awnser\n");
		sleep(1);
		return NULL;
	}
	if (checkGender(requestToRead.gender)){
		printf("Sauna: Accepting Request, SEX:%c\n",requestToRead.gender);
		sem_getvalue(sem, &semValue);
		if (sem_wait(sem)==-1){
			perror("sem_wait(sem)\n");
		}
		else{
			gettimeofday(&tvBegin, NULL);
			requestToRead.state = ACEITE;
			pthread_mutex_lock(&genderMtx);
			if (actualGender == 'N'){
				actualGender = requestToRead.gender;
			}
			pthread_mutex_unlock(&genderMtx);

			pthread_mutex_lock(&arraysMtx);
			if (requestToRead.gender == 'M'){
				requestsServed[0]++;
			}else{
				requestsServed[1]++;
			}
			pthread_mutex_unlock(&arraysMtx);
			printf("Resting in Sauna, SEX:%c\n",requestToRead.gender);
	    do{
	      gettimeofday(&tvEnd, NULL);
	      timeval_subtract(&tvDiff, &tvEnd, &tvBegin);
	      elapsedMiliseconds = tvDiff.tv_sec * 1000 + tvDiff.tv_usec/1000.0;
	    } while(elapsedMiliseconds < requestToRead.requestTime);
			printf("Rested in Sauna, SEX:%c\n",requestToRead.gender);
	    sem_post(sem);
		}

    if (sem_getvalue(sem, &semValue) == -1){
      perror("Error Reading Semaphore Value\n");
    }else{
      if (semValue == totalSeats){
				pthread_mutex_lock(&genderMtx);
        actualGender = 'N';
				pthread_mutex_unlock(&genderMtx);
      }
    }
	}
	else{
		//Reject the Request
		requestToRead.state = REJEITADO;
		printf("Sauna: Rejecting Request, SEX:%c\n",requestToRead.gender);
		pthread_mutex_lock(&arraysMtx);
		if (requestToRead.gender == 'M'){
			rejectionsReceived[0]++;
		}else{
			rejectionsReceived[1]++;
		}
		pthread_mutex_unlock(&arraysMtx);
	}
	pthread_mutex_lock(&arraysMtx);
	if (requestToRead.gender == 'M'){
		requestsReceived[0]++;
	}else{
		requestsReceived[1]++;
	}
	pthread_mutex_unlock(&arraysMtx);

	printRegistrationMessages(requestToRead);

	if (write(fifo_ans, &requestToRead, sizeof(requestToRead)) == -1){
		perror("Writing Awnser Error\n");
	}else{
		printf("Sent info back to generator\n");
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
	int threadIterator = 0;
	pthread_t *requestThreads = malloc(sizeof(pthread_t)*numThreads);
	printf("Allocated initial threads\n");

	Request requestToRead;
	int n = 1;
	while(n>0){
		n=read(fifo_req,&requestToRead, sizeof(requestToRead));
		if (threadIterator >= numThreads && n>0){
			printf("Going to Reallocate Memory\n");
			numThreads = numThreads * 2;
			if ( (requestThreads = realloc(requestThreads,numThreads * sizeof(pthread_t))) == NULL){
				perror("Error Reallocating Memory for threads\n");
			}
			else{
				printf("Memory Reallocated\n");
			}
		}
		printf("Read new request\n");
		pthread_create(&requestThreads[threadIterator++], NULL, handleRequest, &requestToRead);
		printf("Created thread %d\n", threadIterator-1);
	}

	int i = 0;
	for (; i < threadIterator;i++){
		printf("Joining thread %d\n",i);
		pthread_join(requestThreads[i], NULL);
		printf("Joined Thread %d\n",i);
	}
	printf("Freeing array of threads\n");
	free(requestThreads);
	printf("Destroying Semaphore\n");
	sem_destroy(sem);
	printf("Semaphore Destroyed\n");
	unlink(fifo_entrada);
	printf("Destryoed FIFO\n");
	printStatus();

	return 0;
}
