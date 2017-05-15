// Copyright 2017 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#define FUNCTION(x)     .global x; .type x,STT_FUNC; x:
#define UART_IO_PORT    0x3f8
#define IA32_GS_BASE    0xc0000101

.text

FUNCTION(guest_start)
    mov $UART_IO_PORT, %dx
    mov $'m', %al
    out %al, %dx
    mov $'x', %al
    out %al, %dx

    mov %cr3, %rax
    mov %rax, %cr3

    xor %eax, %eax
    xor %edx, %edx
    mov $IA32_GS_BASE, %ecx
    wrmsr

    xor %eax, %eax
    cpuid

    vmcall
FUNCTION(guest_end)
