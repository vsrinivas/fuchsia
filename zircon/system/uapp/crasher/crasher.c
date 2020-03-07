// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <threads.h>
#include <zircon/syscalls.h>

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

int blind_read(volatile unsigned int* addr) { return (int)(*addr); }

int blind_execute(volatile unsigned int* addr) {
  void (*func)(void) = (void*)addr;
  func();
  return 0;
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
  __asm__ volatile("brk #0");  // not undefined, but close enough
#else
#error "need to define undefined for this architecture"
#endif
  return 0;
}

int oom(volatile unsigned int* unused) { return cpp_out_of_mem(); }

#include <unistd.h>

// volatile to ensure compiler doesn't optimize the allocs away
volatile char* mem_alloc;

int mem(volatile unsigned int* arg) {
  int count = 0;
  for (;;) {
    mem_alloc = malloc(1024 * 1024);
    memset((void*)mem_alloc, 0xa5, 1024 * 1024);
    count++;
    if ((count % 128) == 0) {
      zx_nanosleep(zx_deadline_after(ZX_MSEC(250)));
      write(1, ".", 1);
    }
  }
}

int use_after_free(volatile unsigned int* arg) {
  char* p = strdup("Hello, world!");
  free(p);
  puts(p);
  return 0;
}

typedef struct {
  int depth;
  int max_depth;
  int thread_index;
} deep_sleep_args_t;

int deep_sleep(deep_sleep_args_t* args) {
  if (args->depth < args->max_depth) {
    args->depth++;
    deep_sleep(args);
  }
  // The compiler chokes on -Winfinite-recursion otherwise.
  volatile bool running = true;
  while (running) {
    zx_nanosleep(zx_deadline_after(ZX_MSEC(10)));
  }
  return 0;
}

int thread_func(void* arg) { return deep_sleep((deep_sleep_args_t*)arg); }

int blind_write_multithreaded(volatile unsigned int* addr) {
  // Start 5 separate threads that will recurse a bit then sleep.
  int kThreads = 5;
  thrd_t threads[kThreads];
  deep_sleep_args_t args[kThreads];
  for (int i = 0; i < kThreads; ++i) {
    args[i].thread_index = i;
    args[i].depth = 0;
    args[i].max_depth = i;

    char thread_name[128];
    snprintf(thread_name, sizeof(thread_name), "deep_sleep%d", i);

    int ret = thrd_create_with_name(&threads[i], thread_func, &args[i], thread_name);
    if (ret != thrd_success) {
      printf("Unexpected thread create return: %d\n", ret);
      return 1;
    }
  }

  // Wait for the threads to have finished their recursion then crash the main thread.
  for (int i = 0; i < kThreads; ++i) {
    while (args[i].depth < args[i].max_depth) {
      zx_nanosleep(zx_deadline_after(ZX_MSEC(1)));
    }
  }
  blind_write(addr);

  for (int i = 0; i < kThreads; ++i) {
    int ret = thrd_join(threads[i], NULL);
    if (ret != thrd_success) {
      printf("Unexpected thread join return: %d\n", ret);
      return 1;
    }
  }
  return 0;
}

int channel_overflow(volatile unsigned int* arg) {
  zx_handle_t ch[2];
  zx_status_t status = zx_channel_create(0u, &ch[0], &ch[1]);
  if (status != ZX_OK) {
    printf("channel creation failed. error: %d\n", status);
    return 1;
  }

  char message[256] = {0x55};

  int count = 0;
  for (;;) {
    status = zx_channel_write(ch[1], 0u, message, sizeof(message), NULL, 0);
    if (status != ZX_OK) {
      printf("channel write failed. error: %d after %d writes\n", status, count);
      break;
    }
    ++count;
    if ((count % 100) == 0) {
      write(1, ".", 1);
    }
  }

  zx_handle_close(ch[0]);
  zx_handle_close(ch[1]);
  return 0;
}

command_t commands[] = {
    {"write0", blind_write, "write to address 0x0"},
    {"read0", blind_read, "read address 0x0"},
    {"execute0", blind_execute, "execute address 0x0"},
    {"writero", ro_write, "write to read only code segment"},
    {"stackov", stack_overflow, "overflow the stack (recursive)"},
    {"stackbuf", stack_buf_overrun, "overrun a buffer on the stack"},
    {"und", undefined, "undefined instruction"},
    {"nx_run", nx_run, "run in no-execute memory"},
    {"oom", oom, "out of memory c++ death"},
    {"mem", mem, "out of memory"},
    {"channelw", channel_overflow, "overflow a channel with writes"},
    {"use_after_free", use_after_free, "use memory after freeing it"},
    {"write0_mt", blind_write_multithreaded,
     "write to address 0x0 in one thread, sleeping in 5 others"},
    {NULL, NULL, NULL},
};

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
          goto exit;  // should not reach here.
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
