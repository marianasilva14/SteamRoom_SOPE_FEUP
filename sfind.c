#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <stdio.h>

int main(int argc, char **argv){
struct stat s;
const char *pathname = "/";
stat(pathname,&s);
if (S_ISDIR(s.st_mode)){
printf("Directory\n");
}

if (argc == 2){
	execlp("ls","ls",argv[1],NULL);
}


//readdir
//opendir
//rewinddir??
//sisdir


//implementar exec no fim
