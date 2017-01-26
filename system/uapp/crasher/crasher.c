// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <magenta/syscalls.h>

// defined in cpp_specific.cpp.
int cpp_out_of_mem(void);

typedef struct {
    const char* name;
    int (*func)(volatile unsigned int*);
    const char* desc;
} command_t;

int blind_write(volatile unsigned int* addr) {
    *addr = 0xBAD1DEA;
    return 0;
}

int blind_read(volatile unsigned int* addr) {
    return (int)(*addr);
}

int ro_write(volatile unsigned int* addr) {
    // test that we cannot write to RO code memory
    volatile unsigned int* p = (volatile unsigned int*)&ro_write;
    *p = 99;
    return 0;
}

int nx_run(volatile unsigned int* addr) {
    // Test that we cannot execute NX memory.  Use stack memory for this
    // because using a static means the compiler might generate a direct
    // branch to the symbol rather than computing the function pointer
    // address in a register as the code looks like it would do, and
    // declaring a static writable variable that the compiler can see
    // nobody writes leaves the compiler free to morph it into a static
    // const variable, which gets put into a mergeable rodata section, and
    // the Gold linker for aarch64 cannot handle a branch into a mergeable
    // section.
    uint8_t codebuf[16] = {};
    void (*func)(void) = (void*)codebuf;
    func();
    return 0;
}

// Note that as of 5/21/16 the crash reads:
// PageFault:199: UNIMPLEMENTED: faulting with a page already present.
int stack_overflow(volatile unsigned int* i_array) {
    volatile unsigned int array[512];
    if (i_array) {
        array[0] = i_array[0] + 1;
        if (array[0] < 4096)
            return stack_overflow(array);
    } else {
        array[0] = 0;
        return stack_overflow(array);
    }
    return 0;
}

int stack_buf_overrun(volatile unsigned int* arg) {
    volatile unsigned int array[6];
    if (!arg) {
        return stack_buf_overrun(array);
    } else {
        memset((void*)arg, 0, sizeof(array[0]) * 7);
    }
    return 0;
}

int undefined(volatile unsigned int* unused) {
#if defined(__x86_64__)
    __asm__ volatile("ud2");
#elif defined(__aarch64__)
    __asm__ volatile("brk #0"); // not undefined, but close enough
#else
#error "need to define undefined for this architecture"
#endif
    return 0;
}

int oom(volatile unsigned int* unused) {
    return cpp_out_of_mem();
}

#include <unistd.h>

// volatile to ensure compiler doesn't optimize the allocs away
volatile char* mem_alloc;

int mem(volatile unsigned int* arg) {
    int count = 0;
    for (;;) {
        mem_alloc = malloc(1024*1024);
        memset((void*)mem_alloc, 0xa5, 1024*1024);
        count++;
        if ((count % 128) == 0) {
            mx_nanosleep(MX_MSEC(250));
            write(1, ".", 1);
        }
    }

}

command_t commands[] = {
    {"write0", blind_write, "write to address 0x0"},
    {"read0", blind_read, "read address 0x0"},
    {"writero", ro_write, "write to read only code segment"},
    {"stackov", stack_overflow, "overflow the stack (recursive)"},
    {"stackbuf", stack_buf_overrun, "overrun a buffer on the stack"},
    {"und", undefined, "undefined instruction"},
    {"nx_run", nx_run, "run in no-execute memory"},
    {"oom", oom, "out of memory c++ death"},
    {"mem", mem, "out of memory"},
    {NULL, NULL, NULL}};

int main(int argc, char** argv) {
    printf("=@ crasher @=\n");

    if (argc < 2) {
        printf("default to write0  (use 'help' for more options).\n");
        blind_write(NULL);
    } else {
        if (strcmp("help", argv[1])) {
            for (command_t* cmd = commands; cmd->name != NULL; ++cmd) {
                if (strcmp(cmd->name, argv[1]) == 0) {
                    printf("doing : %s\n", cmd->desc);
                    cmd->func(NULL);
                    goto exit; // should not reach here.
                }
            }
        }

        printf("known commands are:\n");
        for (command_t* cmd = commands; cmd->name != NULL; ++cmd) {
            printf("%s : %s\n", cmd->name, cmd->desc);
        }
        return 0;
    }

exit:
    printf("crasher: exiting normally ?!!\n");
    return 0;
}
