// Copyright 2016 The Fuchsia Authors
// Copyright (c) 2016 Gurjant Kalsi
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <assert.h>
#include <dev/interrupt.h>
#include <dev/uart.h>
#include <kernel/thread.h>
#include <lib/cbuf.h>
#include <dev/bcm28xx.h>
#include <platform/debug.h>
#include <reg.h>
#include <stdio.h>
#include <trace.h>
#include <mdi/mdi.h>
#include <mdi/mdi-defs.h>
#include <pdev/driver.h>
#include <pdev/uart.h>

#define RXBUF_SIZE 16

static cbuf_t uart_rx_buf;

struct bcm283x_mu_regs {
    uint32_t io;
    uint32_t ier;
    uint32_t iir;
    uint32_t lcr;
    uint32_t mcr;
    uint32_t lsr;
    uint32_t msr;
    uint32_t scratch;
    uint32_t cntl;
    uint32_t stat;
    uint32_t baud;
};

struct bcm283x_aux_regs {
    uint32_t auxirq;
    uint32_t auxenb;
};

#define AUX_IRQ_MINIUART (1 << 0)
#define AUX_ENB_MINIUART (1 << 0)

#define MU_IIR_BYTE_AVAIL (1 << 2)    // For reading
#define MU_IIR_CLR_XMIT_FIFO (1 << 2) // For writing.
#define MU_IIR_CLR_RECV_FIFO (1 << 1)

#define MU_IIR_EN_RX_IRQ (1 << 0) // Enable the recv interrupt.

#define MU_LSR_TX_EMPTY (1 << 5)

static enum handler_return aux_irq(void* arg) {
    volatile struct bcm283x_mu_regs* mu_regs =
        (struct bcm283x_mu_regs*)MINIUART_BASE;
    volatile struct bcm283x_aux_regs* aux_regs =
        (struct bcm283x_aux_regs*)AUX_BASE;

    // Make sure this interrupt is intended for the miniuart.
    uint32_t auxirq = readl(&aux_regs->auxirq);
    if ((auxirq & AUX_IRQ_MINIUART) == 0) {
        return INT_NO_RESCHEDULE;
    }

    bool resched = false;

    while (true) {
        uint32_t iir = readl(&mu_regs->iir);
        if ((iir & MU_IIR_BYTE_AVAIL) == 0)
            break;

        resched = true;
        char ch = readl(&mu_regs->io);
        cbuf_write_char(&uart_rx_buf, ch, false);
    }

    return resched ? INT_RESCHEDULE : INT_NO_RESCHEDULE;
}

static int bcm28xx_putc(char c) {
    struct bcm283x_mu_regs* regs = (struct bcm283x_mu_regs*)MINIUART_BASE;

    /* Wait until there is space in the FIFO */
    while (!(readl(&regs->lsr) & MU_LSR_TX_EMPTY))
        ;

    /* Send the character */
    writel(c, &regs->io);

    return 1;
}

static void bcm28xx_uart_init(mdi_node_ref_t* node, uint level) {
    volatile struct bcm283x_mu_regs* mu_regs =
        (struct bcm283x_mu_regs*)MINIUART_BASE;
    volatile struct bcm283x_aux_regs* aux_regs =
        (struct bcm283x_aux_regs*)AUX_BASE;

    // Create circular buffer to hold received data.
    cbuf_initialize(&uart_rx_buf, RXBUF_SIZE);

    // AUX Interrupt handler handles interrupts for SPI1, SPI2, and miniuart
    // Interrupt handler must decode IRQ.
    register_int_handler(INTERRUPT_AUX, &aux_irq, NULL);

    // Enable the Interrupt.
    unmask_interrupt(INTERRUPT_AUX);

    writel(MU_IIR_CLR_RECV_FIFO | MU_IIR_CLR_XMIT_FIFO, &mu_regs->iir);

    // Enable the miniuart peripheral. This also enables Miniuart register
    // access. It's likely that the VideoCore chip already enables this
    // peripheral for us, but we hit the enable bit just to be sure.
    writel(AUX_ENB_MINIUART, &aux_regs->auxenb);

    // Enable the receive interrupt on the UART peripheral.
    writel(MU_IIR_EN_RX_IRQ, &mu_regs->ier);
}

static int bcm28xx_getc(bool wait) {
    cbuf_t* rxbuf = &uart_rx_buf;

    char c;
    if (cbuf_read_char(rxbuf, &c, wait) == 1)
        return c;

    return -1;
}

static int bcm28xx_pputc(char c)
{
    struct bcm283x_mu_regs* regs = (struct bcm283x_mu_regs*)MINIUART_BASE;

    /* Wait until there is space in the FIFO */
    while (!(readl(&regs->lsr) & MU_LSR_TX_EMPTY))
        ;

    /* Send the character */
    writel(c, &regs->io);

    return 1;
}

static int bcm28xx_pgetc(void) {
    volatile struct bcm283x_mu_regs* mu_regs =
        (struct bcm283x_mu_regs*)MINIUART_BASE;

    while((readl(&mu_regs->iir) & MU_IIR_BYTE_AVAIL) == 0)
        ;

    return readl(&mu_regs->io);
}

static const struct pdev_uart_ops uart_ops = {
    .putc = bcm28xx_putc,
    .getc = bcm28xx_getc,
    .pputc = bcm28xx_pputc,
    .pgetc = bcm28xx_pgetc,
};

static void bcm28xx_uart_init_early(mdi_node_ref_t* node, uint level) {
    pdev_register_uart(&uart_ops);
}

LK_PDEV_INIT(bcm28xx_uart_init_early, MDI_BCM28XX_UART, bcm28xx_uart_init_early, LK_INIT_LEVEL_PLATFORM_EARLY);
LK_PDEV_INIT(bcm28xx_uart_init, MDI_BCM28XX_UART, bcm28xx_uart_init, LK_INIT_LEVEL_PLATFORM);
