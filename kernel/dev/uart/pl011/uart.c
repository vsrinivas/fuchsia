// Copyright 2016 The Fuchsia Authors
// Copyright (c) 2014-2015 Travis Geiselbrecht
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <reg.h>
#include <stdio.h>
#include <trace.h>
#include <lib/cbuf.h>
#include <kernel/thread.h>
#include <dev/interrupt.h>
#include <dev/uart.h>
#include <platform/debug.h>
#include <mdi/mdi.h>
#include <mdi/mdi-defs.h>
#include <pdev/driver.h>
#include <pdev/uart.h>

/* PL011 implementation */
#define UART_DR    (0x00)
#define UART_RSR   (0x04)
#define UART_TFR   (0x18)
#define UART_ILPR  (0x20)
#define UART_IBRD  (0x24)
#define UART_FBRD  (0x28)
#define UART_LCRH  (0x2c)
#define UART_CR    (0x30)
#define UART_IFLS  (0x34)
#define UART_IMSC  (0x38)
#define UART_TRIS  (0x3c)
#define UART_TMIS  (0x40)
#define UART_ICR   (0x44)
#define UART_DMACR (0x48)

#define UARTREG(base, reg)  (*REG32((base)  + (reg)))

#define RXBUF_SIZE 16

// values read from MDI
static uint64_t uart_base = 0;
static uint32_t uart_irq = 0;

static cbuf_t uart_rx_buf;

static enum handler_return pl011_uart_irq(void *arg)
{
    bool resched = false;

    /* read interrupt status and mask */
    uint32_t isr = UARTREG(uart_base, UART_TMIS);

    if (isr & ((1<<4) | (1<<6))) { // rxmis
        /* while fifo is not empty, read chars out of it */
        while ((UARTREG(uart_base, UART_TFR) & (1<<4)) == 0) {
            /* if we're out of rx buffer, mask the irq instead of handling it */
            if (cbuf_space_avail(&uart_rx_buf) == 0) {
                UARTREG(uart_base, UART_IMSC) &= ~((1<<4)|(1<<6)); // !rxim
                break;
            }

            char c = UARTREG(uart_base, UART_DR);
            cbuf_write_char(&uart_rx_buf, c, false);

            resched = true;
        }
    }

    return resched ? INT_RESCHEDULE : INT_NO_RESCHEDULE;
}

static void pl011_uart_init(mdi_node_ref_t* node, uint level)
{
    // create circular buffer to hold received data
    cbuf_initialize(&uart_rx_buf, RXBUF_SIZE);

    // assumes interrupts are contiguous
    register_int_handler(uart_irq, &pl011_uart_irq, NULL);

    // clear all irqs
    UARTREG(uart_base, UART_ICR) = 0x3ff;

    // set fifo trigger level
    UARTREG(uart_base, UART_IFLS) = 0; // 1/8 rxfifo, 1/8 txfifo

    // enable rx interrupt
    UARTREG(uart_base, UART_IMSC) = (1 <<4 ) |  //  rxim
                                    (1 << 6);   //  rtim

    // enable receive
    UARTREG(uart_base, UART_CR) |= (1<<9); // rxen

    // enable interrupt
    unmask_interrupt(uart_irq);
}

static int pl011_uart_putc(char c)
{
    /* spin while fifo is full */
    while (UARTREG(uart_base, UART_TFR) & (1<<5))
        ;
    UARTREG(uart_base, UART_DR) = c;

    return 1;
}

static int pl011_uart_getc(bool wait)
{
    char c;
    if (cbuf_read_char(&uart_rx_buf, &c, wait) == 1) {
        UARTREG(uart_base, UART_IMSC) = ((1<<4)|(1<<6)); // rxim
        return c;
    }

    return -1;
}

/* panic-time getc/putc */
static int pl011_uart_pputc(char c)
{
    /* spin while fifo is full */
    while (UARTREG(uart_base, UART_TFR) & (1<<5))
        ;
    UARTREG(uart_base, UART_DR) = c;

    return 1;
}

static int pl011_uart_pgetc(void)
{
    if ((UARTREG(uart_base, UART_TFR) & (1<<4)) == 0) {
        return UARTREG(uart_base, UART_DR);
    } else {
        return -1;
    }
}

static const struct pdev_uart_ops uart_ops = {
    .putc = pl011_uart_putc,
    .getc = pl011_uart_getc,
    .pputc = pl011_uart_pputc,
    .pgetc = pl011_uart_pgetc,
};

static void pl011_uart_init_early(mdi_node_ref_t* node, uint level) {
    uint64_t uart_base_virt = 0;
    bool got_uart_base_virt = false;
    bool got_uart_irq = false;

    mdi_node_ref_t child;
    mdi_each_child(node, &child) {
        switch (mdi_id(&child)) {
        case MDI_BASE_VIRT:
            got_uart_base_virt = !mdi_node_uint64(&child, &uart_base_virt);
            break;
        case MDI_IRQ:
            got_uart_irq = !mdi_node_uint32(&child, &uart_irq);
            break;
        }
    }

    if (!got_uart_base_virt) {
        panic("pl011 uart: uart_base_virt not defined\n");
    }
    if (!got_uart_irq) {
        panic("pl011 uart: uart_irq not defined\n");
    }

    uart_base = (uint64_t)uart_base_virt;

    UARTREG(uart_base, UART_CR) = (1<<8)|(1<<0); // tx_enable, uarten

    pdev_register_uart(&uart_ops);
}

LK_PDEV_INIT(pl011_uart_init_early, MDI_ARM_PL011_UART, pl011_uart_init_early, LK_INIT_LEVEL_PLATFORM_EARLY);
LK_PDEV_INIT(pl011_uart_init, MDI_ARM_PL011_UART, pl011_uart_init, LK_INIT_LEVEL_PLATFORM);
