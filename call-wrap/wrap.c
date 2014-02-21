#include <stdio.h>


int __wrap_main(int argc, char* argv[])
{
    printf("Wrapped main\n");
    return __real_main(argc, argv);
}
 
int main(int argc, char* argv[])
{
    printf("Main\n");
    return 0;
}
