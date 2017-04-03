// Copyright 2017 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#define FUNCTION(x) .global x; .type x,STT_FUNC; x:
#define UART_IO_PORT 0x3f8

.text

FUNCTION(guest_start)
    xor %rax, %rax
increment:
    inc %rax
    mov $UART_IO_PORT, %dx
    out %al, %dx
    jmp increment
FUNCTION(guest_end)
