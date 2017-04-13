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


typedef struct{
	int name;
	char* filename;
	int type;
	char* typename;
	int perm;
	int permHex;
	int print;
	int delete;
}Args;



//PROTOS
int createChild(const char* fileName, Args* args);

//handler CRTL-C
static void sigint_child_handler(int signo)
{
	sigset_t newmask, oldmask;
	sigemptyset(&newmask);
	sigaddset(&newmask, SIGUSR1);
	sigprocmask(SIG_BLOCK, &newmask, &oldmask);//oldmask <- newmask; newmask<- newmask | currentmask
	sigsuspend(&oldmask); //temporarly replaces currentmask<-oldmask and suspends the process until a signal arrives
	sigprocmask(SIG_SETMASK, &oldmask, NULL);// currentmask<-oldmask(only sigusr1 blocked)
}

static void sigusr_handler(int signo)
{
	char* line = "Process continue!\n";
	write(STDOUT_FILENO,line,strlen(line));
}

//handler CRTL-C
static void sigint_handler(int signo)
{
	char* line = "In SIGINT handler ...\n";
	write(STDOUT_FILENO,line,strlen(line));
	line = "Are you sure you want to terminate (Y/N)?\n";
	write(STDOUT_FILENO,line,strlen(line));
	char input;
	scanf("%s", &input);
	input = toupper(input);
	pid_t precessGroup_pid = getpgid(getpid());
	switch(input)
	{
	case 'Y':
		line = "Process terminated!\n";
		write(STDOUT_FILENO,line,strlen(line));
		killpg(precessGroup_pid, SIGKILL);
		break;
	case 'N':
		killpg(precessGroup_pid, SIGUSR1);
		break;
	}
}

void printOrDelete(Args* args, char* fileName, char *actualDir, char* fileType){
	if(args->print){
		printf("%s%s\n",actualDir,fileName);
	}
	if (args->delete){
		if (strcmp(fileType, "d")==0){
			execlp("rm","rm","-ri",fileName,NULL);
		}
		execlp("rm","rm",fileName,NULL);
	}
}


void processArguments(Args * args, int perms, char* fileType, char* fileName, char* actualDir){
	if (args->name){
		if (args->type){
			if (args->perm){
				if (((perms & args->permHex) == args->permHex) && strcmp(args->typename,fileType) == 0 && strcmp(args->filename,fileName) == 0){
					printOrDelete(args,fileName,actualDir,fileType);
				}
			}
			else if( strcmp(args->typename,fileType) == 0 && strcmp(args->filename,fileName) == 0){
				printOrDelete(args,fileName,actualDir,fileType);
			}
		}else{
			if (args->perm){
				if (((perms & args->permHex) == args->permHex) && strcmp(args->filename,fileName) == 0){
					printOrDelete(args,fileName,actualDir,fileType);
				}
			}
			else if (strcmp(args->filename,fileName) == 0){
				printOrDelete(args,fileName,actualDir,fileType);
			}
		}
	}
	else
	{
		if (args->type){
			if (args->perm){
				if (((perms & args->permHex) == args->permHex) && strcmp(args->typename,fileType) == 0){
					printOrDelete(args,fileName,actualDir,fileType);
				}
			}
			else if( strcmp(args->typename,fileType) == 0){
				printOrDelete(args,fileName,actualDir,fileType);
			}
		}
		else{
			if (args->perm){
				//printf("Perms: %o, PermHex: %o\n",perms,args->permHex);
				if ((perms & args->permHex) == args->permHex){

					printf("Bit-Bit And:%o\n", perms & args->permHex);
					printOrDelete(args,fileName,actualDir,fileType);
				}
			}
		}
	}
}



int readDirInfo(char* actualDir, Args* args){
	chdir(actualDir);
	strcat(actualDir,"/");
	DIR *directory;
	if ((directory = opendir(".")) == NULL){
		perror("Error Reading Dir\n");
	}

	struct dirent *file;
	struct stat file_info;
	while((file = readdir(directory)) != NULL){
		char *fileName = (*file).d_name;
		int mask = 0x01ff;
		int perms = mask & file_info.st_mode;

		if (stat(fileName,&file_info)==-1){
			printf("Failed to open directory %s\n", fileName);
			perror("stat");
			return 1;
		}
		if (S_ISDIR(file_info.st_mode)){
			if (fileName[0] == '.' && (fileName[1] == '\0' || fileName[1] == '.')){
				continue;
			}
			processArguments(args,perms,"d",fileName,actualDir);
			createChild(fileName, args);
		}
		else if (S_ISREG(file_info.st_mode)){
			processArguments(args,perms,"f",fileName,actualDir);
		}
		else if (S_ISLNK(file_info.st_mode)){
			processArguments(args,perms,"l",fileName,actualDir);
		}
	}
	closedir(directory);
	return 0;
}

char * getNextDir(const char* fileName){
	char cwd[1024];
	if(getcwd(cwd,sizeof(cwd)) == NULL){
		perror("Error reading cwd\n");
	}
	char *nextDirPath = malloc(sizeof(char)*1024);
	sprintf(nextDirPath,"%s",cwd);
	strcat(nextDirPath,"/");
	strcat(nextDirPath,fileName);
	/*char* line = "Next dir path:";
	write(STDOUT_FILENO,line,strlen(line));
	write(STDOUT_FILENO,nextDirPath,strlen(nextDirPath));
	write(STDOUT_FILENO,"\n",1);*/
	return nextDirPath;
}

int createChild(const char* fileName, Args* args){
	char *nextDirPath = getNextDir(fileName);
	pid_t pid = fork();
	if (pid == 0) //filho
	{
		//printf("%d: my parent is %d, Opening %s\n",getpid(),getppid(),fileName);
		readDirInfo(nextDirPath, args);
		exit(0);
	}
	else if (pid > 0){ //parent
		int status;
		wait(&status);
		//printf("%d: Child %d terminated\n",getpid(),pid);
	}
	else{ //error
		return 1;
	}
	return 0;
}

void prepareArgs(int argc, char **argv, Args* args){
	int i = 2;
	args->perm = 0;
	args->name = 0;
	args->type = 0;
	args->print =0;
	args->delete = 0;

  while(argv[i] != NULL){
		char* a = argv[i];
		if(strcmp(a,"-name") == 0){
			args->name = 1;
			args->filename = argv[++i];
		}
		else if(strcmp(a, "-type") == 0){
			args->type = 1;
			args->typename = argv[++i];
		}
		else if(strcmp(a, "-perm") == 0){
			args->perm = 1;
			int octalNumber;
			sscanf(argv[++i], "%o", &octalNumber);
			args->permHex = octalNumber;
			printf("Octal read : %o\n",octalNumber);
		}
		else if(strcmp(a, "-print") == 0){
			args->print = 1;
		}
		else if(strcmp(a, "-delete") == 0){
			args->delete = 1;
		}
		i++;
	}
}

int main(int argc, char **argv){
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
	signal(SIGUSR1, SIG_IGN);

	if(argc < 5){
		char * line = "Args: [PATH] -[NAME | TYPE] [FILENAME | TYPE | PERM] -[PRINT | DELETE]\n";
		write(STDOUT_FILENO,line,strlen(line));
		return 1;
	}

	char* actualDir = ".";
	/*Change dir*/
	if (argc > 1){
		if(argv[1][0] != '-'){
			actualDir = argv[1];
		}
	}

	Args args;
	prepareArgs(argc, argv, &args);

	pid_t pid = fork();
	if (pid == 0) //filho
	{
		signal(SIGINT, &sigint_child_handler);
		signal(SIGUSR1, &sigusr_handler);
		readDirInfo(actualDir, &args);

		exit(0);
	}
	else if (pid > 0){ //parent
		int status;
		while(wait(&status) != pid);
	}
	return 0;
}
