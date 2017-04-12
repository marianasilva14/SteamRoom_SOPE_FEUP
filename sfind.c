#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <stdio.h>
#include <dirent.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <signal.h>

//handler CRTL-C
static void sigint_handler(int signo)
{
	printf("In SIGINT handler ...\n");
	printf("Are you sure you want to terminate (Y/N)?");
	char input;
	scanf("%s", &input);
	input = toupper(input);

	switch(input)
	{
		case 'Y':
		printf("Process terminated!\n");
		exit(0);
		break;
		case 'N':
		printf("Process continue!\n");
		break;
	default:
		exit(3);
		break;
	}
}

int main(int argc, char **argv){

	struct sigaction actionINT;
	actionINT.sa_handler = sigint_handler;
	sigemptyset(&actionINT.sa_mask);
	actionINT.sa_flags = 0;

	if (sigaction(SIGINT, &actionINT, NULL) < 0)
	{
		perror("Unable to install SIGINT handler\n");
		exit(1);
	}
	DIR *directory;
	if (argc > 1){
		if(argv[1][0] != '-'){
			chdir(argv[1]);
		}
	}

	if ((directory = opendir(".")) == NULL){
		perror("Error Reading Dir\n");
	}
	struct dirent *file;
	struct stat file_info;
	char dirsFound[100][1024]; //Space for 99dirs 1023 bytes long each
	int dirsIterator = 0;
	int i;
	pid_t parentpid = getpid();
	char cwd[1024];
	if( getcwd(cwd,sizeof(cwd)) == NULL){
		perror("Error reading cwd\n");
	}
	else{
		printf("Current Working Dir:%s\n",cwd);
	}

	while((file = readdir(directory)) != NULL){
		char *fileName = (*file).d_name;
		//printf("Current file name: %s\n",fileName);
		if (stat(fileName,&file_info)==-1){
			printf("Failed to open directory %s\n", argv[1]);
			perror("stat");
			return 1;
		}
		if (S_ISDIR(file_info.st_mode)){
			if (fileName[0] == '.' && (fileName[1] == '\0' || fileName[1] == '.')){
				continue;
			}
			printf("Directory:%s\n",fileName);
			strcpy(dirsFound[dirsIterator++],fileName);
		}
		else if (S_ISREG(file_info.st_mode)){
			printf("Regular file:%s\n",fileName);
		}
	}//close while
	closedir(directory);

	for (i = 0; i < dirsIterator;i++){
		char *dirName = dirsFound[i];
		if (getpid() == parentpid){
			if (fork() == 0) //filho
			{
			printf("\n\nI am process %d, my parent is %d, Opening %s\n ",getpid(),getppid(),dirName);
			char *nextDirPath = cwd;
			strcat(nextDirPath,"/");
			strcat(nextDirPath,dirName);
			printf("Next dir path:%s",nextDirPath);
			sleep(1);
			execlp("sfind","sfind",nextDirPath,NULL);
			perror("EXEC FAILED! ABORT!\n");
			}
			else{
				int status;
				pid_t pid;
				pid = wait(&status);
				printf("Child %d terminated\n",pid);
			}
		}
	}
	return 0;
}
