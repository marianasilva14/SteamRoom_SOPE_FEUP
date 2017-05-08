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
int requestRejected = 0;
int requestsReceived[2] = {0};// M-0, F-1
int rejectionsReceived[2] = {0};
int requestsServed[2] = {0};

void* handleRequest(void * args){
	int fifo_ans;
	Request requestToRead = *(Request*) args[0];
	while ((fifo_ans = open(requestToRead.fifo_name,O_WRONLY))==-1){
		printf("Sauna: Error opening fifo awnser\n");
		sleep(1);
	}
	if (actualGender == 'N' || requestToRead.gender == actualGender){
		//Accept Request
		requestToRead.state = ACEITE;

		if (requestToRead.gender == 'M'){
			requestsServed[0]++;
		}else{
			requestsServed[1]++;
		}
		sem_wait(sem); //decrements Semaphore
		//Start counting Time. When time = requesTime, post the semaphore to free 1 seat

		sem_post(sem);
	}
	else{
		//Reject the Request
		requestToRead.state = REJEITADO;

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

	printRegistrationMessages(&requestToRead);

	write(fifo_ans, &requestToRead, sizeof(requestToRead));
}

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
	char *location;
	sprintf(location,"/tmp/bal.%d",pid);
	FILE *f = fopen(location, "w");
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
	}
	fprintf(f,"%lu -%d -%d : %c -%d %s", raw_time,pid,r1.requestID,r1.gender,r1.requestTime,tip);
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

	printStatus();

	return 0;
}
