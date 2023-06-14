#include <stdio.h>
#include <string.h>

int main() {
    printf("char: %d\n", sizeof(char));
    printf("unsigned short: %d\n", sizeof(unsigned short));
    char test[] = " abcd";
    unsigned res = 0;
    memcpy(&res, test + 1, sizeof(res) / sizeof(char));
    // printf("%x\n", test[1]);
    // printf("%x\n", test[2]);
    printf("%x\n", res);
}