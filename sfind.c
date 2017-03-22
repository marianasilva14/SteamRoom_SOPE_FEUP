#include <unistd.h>

int main(int argc, char **argv){
  if (argc == 2){
	   execlp("ls","ls",argv[1],NULL);
  }
  return 0;
}


//readdir
//opendir
//rewinddir??
//sisdir
