// Copyright 2016 The Fuchsia Authors
// Copyright (c) 2009 Corey Tabaka
// Copyright (c) 2015 Intel Corporation
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <sys/types.h>

#include <arch/ops.h>
#include <platform/pc.h>
#include <platform/pic.h>

#define PIC1 0x20
#define PIC2 0xA0

#define ICW1 0x11
#define ICW4 0x01

/*
 * init the PICs and remap them
 */
void pic_map(uint8_t pic1, uint8_t pic2) {
    /* send ICW1 */
    outp(PIC1, ICW1);
    outp(PIC2, ICW1);

    /* send ICW2 */
    outp(PIC1 + 1, pic1); /* remap */
    outp(PIC2 + 1, pic2); /*  pics */

    /* send ICW3 */
    outp(PIC1 + 1, 4); /* IRQ2 -> connection to slave */
    outp(PIC2 + 1, 2);

    /* send ICW4 */
    outp(PIC1 + 1, 5);
    outp(PIC2 + 1, 1);

    /* disable all IRQs */
    outp(PIC1 + 1, 0xff);
    outp(PIC2 + 1, 0xff);
}

void pic_disable(void) {
    outp(PIC2 + 1, 0xff);
    outp(PIC1 + 1, 0xff);
}
