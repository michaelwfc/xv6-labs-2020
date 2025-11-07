
#include "types.h"
#include "riscv.h"
#include "defs.h"
#include "printf.h"
// function of sum the intger to n
int sum_to(int n){
    int acc = 0;
    for(int i=0;i<=n;i++){
        acc+=i;
    }
    return acc;
}

int sum_then_double(int n){
    int acc =  sum_to(n);
    return acc*2;
}

void demo1(void){
    printf("Demo 1 Result: %d\n",sum_to(5));
}

void demo2(void){ 
    printf("Demo 2 Result: %d\n",sum_then_double(5));
}

void _demo3(char a, char b, char c ,char d, char e, char f, char g, char h, char i, char j){
    printf("Result: %d, %d, %d,%d ,%d, %d, %d,%d,%d, %d\n",a,b,c,d,e,f,g,h,i,j);
}

void demo3(void){
    _demo3('a', 'b', 'c', 'd', 'e', 'f', 'g', 'h', 'i', 'j');
}

int dummymain(int argc, char *argv[]){
    int i=0;
    for(;i<argc;i++){
        printf("Argument %d: %s\n",i, argv[i]);
    }
    return 0;
}

void demo4(void){
    char *args[] = {"foo", "bar", "baz"};
    int result = dummymain(sizeof(args)/sizeof(args[0]), args);
    if(result<0){
        printf("panic: Demo 4");
    }
}


