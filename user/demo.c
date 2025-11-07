#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

int main(int argc, char *argv[]) {
    if(argc != 2) {
        fprintf(2, "Usage: demo <number>\n");
        exit(1);
    }
    
    int n = atoi(argv[1]);
    // Calls kernel function via syscall
    if(n==1)demo1();
    else if(n==2)demo2();
    else if(n==3)demo3();
    else if(n==4)demo4();
    else{
        printf("Invalid demo number: %d\n",n);
    }    
    
    exit(0);
}