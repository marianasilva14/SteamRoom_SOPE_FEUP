#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <stdio.h>
#include <dirent.h>
#include <signal.h>
#include <stdlib.h>

//handler CRTL-C
void sigint_handler(int signo)
{
	printf("In SIGINT handler ...\n");
	printf("Are you sure you want to terminate (Y/N)?");
	char input;

	scanf("%s", &input);

	switch(input)
	{
	case 'y':
		printf("Process terminated!\n");
		exit(0);
	case 'Y':
		printf("Process terminated!\n");
		exit(0);
	case 'n':
		printf("Process continue!\n");
		break;
	case 'N':
		printf("Process continue!\n");
		break;
	}
}

int main(int argc, char **argv){

	if (signal(SIGINT,sigint_handler) < 0)
	{
		fprintf(stderr,"Unable to install SIGINT handler\n");
		exit(1);
	}

	if (argc < 2){
		printf("This program requires at least 1 argument (directory), example ./sfind / \n");
	}
	DIR *directory;
	directory = opendir(argv[1]);
	struct dirent *dire;
	struct stat s;
	while((dire = readdir(directory)) != NULL){
		printf("%s->",(*dire).d_name);
		stat((*dire).d_name,&s);
		switch(s.st_mode & S_IFMT){
		case(S_IFREG):
				printf("Regular file;\n ");
		break;
		case (S_IFDIR):
				printf("Directory;\n ");
		char *dirName = (*dire).d_name;
		if (dirName[0] == '.'){
			if (dirName[1] == '\0' || dirName[1] == '.'){
				break;
			}
		}
		pid_t ppid = getpid();
		if (fork() == 0) //filho
		{
			printf("\n\nI am process %d, my parent is %d, Opening %s\n ",getpid(),ppid,dirName);
			execlp("./sfind","./sfind",dirName,NULL);
			printf("EXEC FAILED! ABORT!\n");
		}
		break;
		}
	}
	printf("\n");
	return 0;
}
//rewinddir??
//sisdir


//implementar exec no fim
