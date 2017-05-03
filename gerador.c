#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#define fifo_entrada "/tmp/entrada"
#define fifo_rejeitados "/tmp/rejeitados_"

typedef struct{
    char fifo_name[100]; // fifo_to_answer
    //TODO: complete with data for request
} Request;

typedef struct{
    //TODO: complete with data for answer
} Answer;

int main(int argc, char const *argv[]) {
  /* code */
  char fifo_dir[100] = fifo_rejeitados;
  int pid = getpid();
  char pidString[100];
  sprintf(pidString, "%d", pid);
  strcat(fifo_dir, pidString);
  mkfifo(fifo_dir, 0660);

  int fifo_req, fifo_ans;
  do {
    fifo_req=open(fifo_entrada,O_WRONLY);
    if (fifo_req == -1) sleep(1);
  } while(fifo_req == -1);

  Request requestToWrite;
  Answer answerToRead;

  write(fifo_req, &requestToWrite, sizeof(requestToWrite));

  do {
    fifo_ans = open(fifo_dir,O_RDONLY);
    if (fifo_ans == -1) sleep(1);
  } while(fifo_ans == -1);

  read(fifo_ans, &answerToRead, sizeof(answerToRead));

  return 0;
}