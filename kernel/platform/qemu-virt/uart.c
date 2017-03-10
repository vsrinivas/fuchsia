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
#include <platform/qemu-virt.h>

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

static cbuf_t uart_rx_buf;

static enum handler_return uart_irq(void *arg)
{
    bool resched = false;

    /* read interrupt status and mask */
    uint32_t isr = UARTREG(UART_BASE, UART_TMIS);

    if (isr & (1<<4)) { // rxmis
        /* while fifo is not empty, read chars out of it */
        while ((UARTREG(UART_BASE, UART_TFR) & (1<<4)) == 0) {
            /* if we're out of rx buffer, mask the irq instead of handling it */
            if (cbuf_space_avail(&uart_rx_buf) == 0) {
                UARTREG(UART_BASE, UART_IMSC) &= ~(1<<4); // !rxim
                break;
            }

            char c = UARTREG(UART_BASE, UART_DR);
            cbuf_write_char(&uart_rx_buf, c, false);

            resched = true;
        }
    }

    return resched ? INT_RESCHEDULE : INT_NO_RESCHEDULE;
}

void uart_init(void)
{
    // create circular buffer to hold received data
    cbuf_initialize(&uart_rx_buf, RXBUF_SIZE);

    // assumes interrupts are contiguous
    register_int_handler(UART0_INT, &uart_irq, NULL);

    // clear all irqs
    UARTREG(UART_BASE, UART_ICR) = 0x3ff;

    // set fifo trigger level
    UARTREG(UART_BASE, UART_IFLS) = 0; // 1/8 rxfifo, 1/8 txfifo

    // enable rx interrupt
    UARTREG(UART_BASE, UART_IMSC) = (1<<4); // rxim

    // enable receive
    UARTREG(UART_BASE, UART_CR) |= (1<<9); // rxen

    // enable interrupt
    unmask_interrupt(UART0_INT);
}

void uart_init_early(void)
{
    UARTREG(UART_BASE, UART_CR) = (1<<8)|(1<<0); // tx_enable, uarten
}

int uart_putc(char c)
{
    /* spin while fifo is full */
    while (UARTREG(UART_BASE, UART_TFR) & (1<<5))
        ;
    UARTREG(UART_BASE, UART_DR) = c;

    return 1;
}

int uart_getc(bool wait)
{
    char c;
    if (cbuf_read_char(&uart_rx_buf, &c, wait) == 1) {
        UARTREG(UART_BASE, UART_IMSC) = (1<<4); // rxim
        return c;
    }

    return -1;
}

/* panic-time getc/putc */
int uart_pputc(char c)
{
    /* spin while fifo is full */
    while (UARTREG(UART_BASE, UART_TFR) & (1<<5))
        ;
    UARTREG(UART_BASE, UART_DR) = c;

    return 1;
}

int uart_pgetc()
{
    if ((UARTREG(UART_BASE, UART_TFR) & (1<<4)) == 0) {
        return UARTREG(UART_BASE, UART_DR);
    } else {
        return -1;
    }
}
