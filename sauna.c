#include <stdio.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#define fifo_entrada "/tmp/entrada"
//Global Variables
int freeSeats;



typedef struct{
    char fifo_name[100]; // fifo_to_answer
    //TODO: complete with data for request
} Request;

typedef struct{
    //TODO: complete with data for answer
} Answer;

int main(int argc, char const *argv[]) {
    /* code */
    if (argc != 2){
      printf("Usage: sauna <n_lugares>\n");
      return 1;
    }
    freeSeats = argv[1];
    mkfifo(fifo_entrada, 0660);

    int fifo_req, fifo_ans;
    while((fifo_req=open(fifo_entrada,O_WRONLY))==-1){
      sleep(1);
      triesToOpenFifo++;
      if (triesToOpenFifo > 5){
        printf("Failed to Open Fifo Request\n");
        //exit(1);
      }
    }

    Request requestToRead;
    Answer answer;

    int n;
    do {
        n = read(fifo_req,&requestToRead, sizeof(requestToRead));
        /* code */

        //try open rejeitados (restruct do while())
        do {
            fifo_ans = open(requestToRead.fifo_name,O_WRONLY);
            if (fifo_ans == -1) sleep(1);
        } while(fifo_ans == -1);

        write(fifo_ans, &answer, sizeof(answer));

    } while (n > 0);

    return 0;
}
