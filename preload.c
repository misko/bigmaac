#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

int main(int argc, char *argv[]) {
    if (argc<3) {
        fprintf(stdout, "%s LIB.so exec ...\n",argv[0]);
        return 0;
    }
    setenv("LD_PRELOAD", argv[1], 1);
    execvp( argv[2], argv+2);
    return 0;
}
