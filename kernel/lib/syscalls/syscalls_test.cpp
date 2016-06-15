// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include "syscalls_priv.h"

int sys_syscall_test_0(void) {
    return 0;
}
int sys_syscall_test_1(int a) {
    return a;
}
int sys_syscall_test_2(int a, int b) {
    return a + b;
}
int sys_syscall_test_3(int a, int b, int c) {
    return a + b + c;
}
int sys_syscall_test_4(int a, int b, int c, int d) {
    return a + b + c + d;
}
int sys_syscall_test_5(int a, int b, int c, int d, int e) {
    return a + b + c + d + e;
}
int sys_syscall_test_6(int a, int b, int c, int d, int e, int f) {
    return a + b + c + d + e + f;
}
int sys_syscall_test_7(int a, int b, int c, int d, int e, int f, int g) {
    return a + b + c + d + e + f + g;
}
int sys_syscall_test_8(int a, int b, int c, int d, int e, int f, int g, int h) {
    return a + b + c + d + e + f + g + h;
}
