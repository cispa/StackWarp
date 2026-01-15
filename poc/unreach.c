#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>

#define REP8(X) X X X X X X X X

void unreachable_func() {
    printf("Unreachable!!!\n");
    exit(0);
}


__attribute__((naked)) void f1(void *p) {
    // FILL STACK WITH `&unreachable_func`
    for (int i = 0; i < 8; i++) {
        asm volatile("push rdi\n\t");
    }

    // RESUME STACK POINTER
    for (int i = 0; i < 8; i++) {
        // MAKE IT SLOW
        REP8(asm volatile("mfence\n\t");)
        asm volatile("pop rdi\n\t");
    }

    asm volatile("ret\n\t");
}

int main() {
    while (1) {
        f1(&unreachable_func);
        asm volatile ("nop\n");
    }
}