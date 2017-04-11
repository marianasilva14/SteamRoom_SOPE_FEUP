#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <stdio.h>
#include <dirent.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>

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
	chdir(argv[1]);
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
	while((file = readdir(directory)) != NULL){
		char *fileName = (*file).d_name;
		printf("Current file name: %s\n",fileName);
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
			break;
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
			execlp("lsfind","lsfind",nextDirPath,NULL);
			perror("execlp");
			printf("EXEC FAILED! ABORT!\n");
			}
		}
	}
	return 0;
}
