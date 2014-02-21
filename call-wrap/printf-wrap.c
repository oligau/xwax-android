#include <stdarg.h>
#include <stdio.h>

int __real_printf(const char *format, ...);

int __wrap_printf(const char *format, ...)
{
    fprintf(stdout, "wrapped printf: ");
    
    va_list arg;
    int done;
    
    return __real_printf(format, arg);
}
 
int main(int argc, char* argv[])
{
    int a = 23;
    printf("Main \n",23);
    return 0;
}
