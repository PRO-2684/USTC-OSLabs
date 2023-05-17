#include <stdio.h>

struct test_struct {
    int pid;
    char name[128];
};

int main() {
    struct test_struct test_list[5];
    for (int i = 0; i < 5; i++) {
        printf("%d; %s", test_list[i].pid, test_list[i].name);
    }
    return 0;
}