// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include "syscalls_priv.h"

mx_status_t sys_syscall_test_0(void) {
    return 0;
}
mx_status_t sys_syscall_test_1(int a) {
    return a;
}
mx_status_t sys_syscall_test_2(int a, int b) {
    return a + b;
}
mx_status_t sys_syscall_test_3(int a, int b, int c) {
    return a + b + c;
}
mx_status_t sys_syscall_test_4(int a, int b, int c, int d) {
    return a + b + c + d;
}
mx_status_t sys_syscall_test_5(int a, int b, int c, int d, int e) {
    return a + b + c + d + e;
}
mx_status_t sys_syscall_test_6(int a, int b, int c, int d, int e, int f) {
    return a + b + c + d + e + f;
}
mx_status_t sys_syscall_test_7(int a, int b, int c, int d, int e, int f, int g) {
    return a + b + c + d + e + f + g;
}
mx_status_t sys_syscall_test_8(int a, int b, int c, int d, int e, int f, int g, int h) {
    return a + b + c + d + e + f + g + h;
}
mx_status_t sys_syscall_test_wrapper(int a, int b, int c) {
    return a + b + c;
}
