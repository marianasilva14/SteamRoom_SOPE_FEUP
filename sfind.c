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

int createChilds(char **dirsFound,int numberOfDirectories);


//handler CRTL-C
static void sigint_child_handler(int signo)
{
	sigset_t newmask, oldmask;
	sigemptyset(&newmask);
	sigaddset(&newmask, SIGCONT);
	sigprocmask(SIG_BLOCK, &newmask, &oldmask);
	sigsuspend(&oldmask);
	sigprocmask(SIG_SETMASK, &oldmask, NULL);
}

//handler CRTL-C
static void sigint_handler(int signo)
{
	printf("In SIGINT handler ...\n");
	printf("Are you sure you want to terminate (Y/N)?\n");
	char input;
	scanf("%s", &input);
	input = toupper(input);
	pid_t precessGroup_pid = getpgid(getpid());
	switch(input)
	{
		case 'Y':
		printf("Process terminated!\n");
		killpg(precessGroup_pid, SIGKILL);
		break;
		case 'N':
		printf("Process continue!\n");
		killpg(precessGroup_pid, SIGCONT);
		break;
		default:
		exit(3);
		break;
	}
}

int setHandlerSIGINT(){
	/*Add SIGINT Handler*/
	struct sigaction actionINT;
	actionINT.sa_handler = sigint_handler;
	sigemptyset(&actionINT.sa_mask);
	actionINT.sa_flags = 0;
	if (sigaction(SIGINT, &actionINT, NULL) < 0)
	{
		perror("Unable to install SIGINT handler\n");
		return 1;
	}
	return 0;
}



int readDirInfo(DIR* directory, char** dirsFound, int* dirsIterator){
	struct dirent *file;
	struct stat file_info;

	while((file = readdir(directory)) != NULL){
		char *fileName = (*file).d_name;
		//printf("Current file name: %s\n",fileName);
		if (stat(fileName,&file_info)==-1){
			printf("Failed to open directory %s\n", fileName);
			perror("stat");
			return 1;
		}
		if (S_ISDIR(file_info.st_mode)){
			if (fileName[0] == '.' && (fileName[1] == '\0' || fileName[1] == '.')){
				continue;
			}
			printf("Directory:%s\n",fileName);

			dirsFound[(*dirsIterator)] = malloc(sizeof(fileName));
			if (*dirsIterator >= 100){
				dirsFound = realloc(dirsFound,2*(sizeof(dirsFound)));
			}
			strcpy(dirsFound[(*dirsIterator)++],fileName);
		}
		else if (S_ISREG(file_info.st_mode)){
			printf("Regular file:%s\n",fileName);
		}
	}
	return 0;
}

int findFiles(char* actualDir){
	chdir(actualDir);
	DIR *directory;
	if ((directory = opendir(".")) == NULL){
		perror("Error Reading Dir\n");
	}

	char* dirsFound[100]; //Space for 99dirs 1023 bytes long each
	int dirsIterator = 0;

	if(readDirInfo(directory, dirsFound, &dirsIterator))
		return 1;
	closedir(directory);
	return createChilds(dirsFound,dirsIterator);

}


int createChilds(char **dirsFound,int numberOfDirectories){
	char cwd[1024];
	if( getcwd(cwd,sizeof(cwd)) == NULL){
		perror("Error reading cwd\n");
	}
	else{
		printf("Current Working Dir:%s\n",cwd);
	}
	pid_t parentpid = getpid();
	int i;
	for (i = 0; i < numberOfDirectories;i++){
		char *dirName = dirsFound[i];
		if (getpid() == parentpid){
			if (fork() == 0) //filho
			{
				signal(getpid(), &sigint_child_handler);
				printf("I am process %d, my parent is %d, Opening %s\n",getpid(),getppid(),dirName);
				char *nextDirPath = cwd;
				strcat(nextDirPath,"/");
				strcat(nextDirPath,dirName);
				printf("Next dir path:%s\n",nextDirPath);
				if(findFiles(nextDirPath)){
					return 1;
				}
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


int main(int argc, char **argv){
	/*Add SIGINT Handler*/
	if(setHandlerSIGINT())
	exit(1);

	char *actualDir;
	/*Change dir*/
	if (argc > 1){
		if(argv[1][0] != '-'){
			actualDir = argv[1];
		}
	}

	if(findFiles(actualDir))
		return 1;


	return 0;
}
