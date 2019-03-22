// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include <zircon/syscalls.h>

// This is a simple app for testing various aspects of the debugger. To build,
// set include_test_app to true in the BUILD.gn file in this directory.

// The binary will end up in /system/test/zxdb_test_app.

void DebugBreak() {
#if defined(__x86_64__)
  __asm__ volatile("int3");
#elif defined(__aarch64__)
  __asm__ volatile("brk 0");
#else
#error
#endif
}

struct Foo {
  int bar;
};

struct NestedInner {
  int data[256] = {0};
  char asdf = 'c';
};

struct NestedOuter {
  int a = 42;
  const char* c = "Some string";
  NestedInner bar;
  char b = 'a';
};

// This function is helpful to test handling of duplicate functions on the
// stack for e.g. "finish".
__attribute__((noinline)) void RecursiveCall(int times) {
  if (times > 0)
    RecursiveCall(times - 1);
  zx_debug_write("hello\n", 6);  // Prevent tail recursion optimizations.
}

void PrintHello() {
  const char msg[] = "Hello from zxdb_test_app!\n";
  zx_debug_write(msg, strlen(msg));

  // This code is here to test disassembly of FP instructions and printing
  // of values.
  volatile float a = 3.14159265358979;
  volatile float b = 2.71828182845904;
  volatile int z = 1;
  volatile float c = a * b + z;
  volatile int* pz = &z;
  *pz = 45;
  (void)c;  // Prevent unused variable warning.

  volatile NestedOuter outer;
  (void)outer;
}

void DoFoo(Foo* f) {
  if (f->bar > 1)
    zx_debug_write(" ", 1);
  volatile int a = 1;
  (void)a;
  PrintHello();
}

void DoRefs(int& a, const Foo& f) {
  int array[5] = {100, 101, 102, 103, 104};

  int* array2 = &array[0];

  a = 56;
  if (f.bar > 1)
    zx_debug_write("         ", array[a] - 100);
  (void)array2;
}

void DoArrays(int x, int y) {
  double array[4][3] = {{0, 1, 2}, {3, 4, 5}, {6, 7, 8}, {9, 10, 11}};
  double foo = array[1][2];
  (void)foo;
  char buf[2];
  buf[0] = '0' + (int)array[x][y];
  buf[1] = 0;
  zx_debug_write(buf, 1);
}

int main(int argc, char* argv[]) {
  // Print out the address of main to the system debug log.
  char buf[128];
  snprintf(buf, sizeof(buf), "zxdb_test_app, &PrintHello = 0x%llx\n",
           static_cast<unsigned long long>(
               reinterpret_cast<uintptr_t>(&PrintHello)));
  zx_debug_write(buf, strlen(buf));

  DebugBreak();
  PrintHello();
  RecursiveCall(3);

  DoArrays(1, 2);

  Foo foo;
  foo.bar = 0;
  DoFoo(&foo);
  foo.bar = 100;
  DoFoo(&foo);
  int i = 2;
  DoRefs(i, foo);
  return 0;
}
