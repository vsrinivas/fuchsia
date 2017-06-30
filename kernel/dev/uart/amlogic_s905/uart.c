// Copyright 2017 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <reg.h>
#include <stdio.h>
#include <trace.h>
#include <string.h>
#include <lib/cbuf.h>
#include <kernel/thread.h>
#include <dev/interrupt.h>
#include <dev/uart.h>

#include <mdi/mdi.h>
#include <mdi/mdi-defs.h>
#include <pdev/driver.h>
#include <pdev/uart.h>

#define S905_UART_WFIFO         (0x0)
#define S905_UART_RFIFO         (0x4)
#define S905_UART_CONTROL       (0x8)
#define S905_UART_STATUS        (0xc)
#define S905_UART_IRQ_CONTROL   (0x10)
#define S905_UART_REG5          (0x14)


#define S905_UART_CONTROL_INVRTS    (1 << 31)
#define S905_UART_CONTROL_MASKERR   (1 << 30)
#define S905_UART_CONTROL_INVCTS    (1 << 29)
#define S905_UART_CONTROL_TXINTEN   (1 << 28)
#define S905_UART_CONTROL_RXINTEN   (1 << 27)
#define S905_UART_CONTROL_INVTX     (1 << 26)
#define S905_UART_CONTROL_INVRX     (1 << 25)
#define S905_UART_CONTROL_CLRERR    (1 << 24)
#define S905_UART_CONTROL_RSTRX     (1 << 23)
#define S905_UART_CONTROL_RSTTX     (1 << 22)
#define S905_UART_CONTROL_XMITLEN   (1 << 20)
#define S905_UART_CONTROL_XMITLEN_MASK   (0x3 << 20)
#define S905_UART_CONTROL_PAREN     (1 << 19)
#define S905_UART_CONTROL_PARTYPE   (1 << 18)
#define S905_UART_CONTROL_STOPLEN   (1 << 16)
#define S905_UART_CONTROL_STOPLEN_MASK   (0x3 << 16)
#define S905_UART_CONTROL_TWOWIRE   (1 << 15)
#define S905_UART_CONTROL_RXEN      (1 << 13)
#define S905_UART_CONTROL_TXEN      (1 << 12)
#define S905_UART_CONTROL_BAUD0     (1 << 0)
#define S905_UART_CONTROL_BAUD0_MASK     (0xfff << 0)

#define S905_UART_STATUS_RXBUSY         (1 << 26)
#define S905_UART_STATUS_TXBUSY         (1 << 25)
#define S905_UART_STATUS_RXOVRFLW       (1 << 24)
#define S905_UART_STATUS_CTSLEVEL       (1 << 23)
#define S905_UART_STATUS_TXEMPTY        (1 << 22)
#define S905_UART_STATUS_TXFULL         (1 << 21)
#define S905_UART_STATUS_RXEMPTY        (1 << 20)
#define S905_UART_STATUS_RXFULL         (1 << 19)
#define S905_UART_STATUS_TXOVRFLW       (1 << 18)
#define S905_UART_STATUS_FRAMEERR       (1 << 17)
#define S905_UART_STATUS_PARERR         (1 << 16)
#define S905_UART_STATUS_TXCOUNT_POS    (8)
#define S905_UART_STATUS_TXCOUNT_MASK   (0x7f << S905_UART_STATUS_TXCOUNT_POS)
#define S905_UART_STATUS_RXCOUNT_POS    (0)
#define S905_UART_STATUS_RXCOUNT_MASK   (0x7f << S905_UART_STATUS_RXCOUNT_POS)

#define UARTREG(base, reg)  (*(volatile uint32_t*)((base)  + (reg)))

#define RXBUF_SIZE 128
#define NUM_UART 5

#define S905_UART0_OFFSET          (0x011084c0)
#define S905_UART1_OFFSET          (0x011084dc)
#define S905_UART2_OFFSET          (0x01108700)
#define S905_UART0_AO_OFFSET       (0x081004c0)
#define S905_UART1_AO_OFFSET       (0x081004e0)



static cbuf_t uart_rx_buf;
static bool initialized = false;
static uintptr_t s905_uart_base = 0;
static uint32_t s905_uart_irq = 0;



static enum handler_return uart_irq(void *arg)
{
    uintptr_t base = (uintptr_t)arg;

    /* read interrupt status and mask */

    while ( (UARTREG(base, S905_UART_STATUS) & S905_UART_STATUS_RXCOUNT_MASK) > 0 ) {
        if (cbuf_space_avail(&uart_rx_buf) == 0) {
                break;
        }
        char c = UARTREG(base, S905_UART_RFIFO);
        cbuf_write_char(&uart_rx_buf, c, false);
    }

    return true;
}

static void s905_uart_init(mdi_node_ref_t* node, uint level)
{
        assert(s905_uart_base);
        assert(s905_uart_irq);

        // create circular buffer to hold received data
        cbuf_initialize(&uart_rx_buf, RXBUF_SIZE);

        //reset the port
        UARTREG(s905_uart_base,S905_UART_CONTROL) |=  S905_UART_CONTROL_RSTRX |
                                                      S905_UART_CONTROL_RSTTX |
                                                      S905_UART_CONTROL_CLRERR;
        UARTREG(s905_uart_base,S905_UART_CONTROL) &= ~(S905_UART_CONTROL_RSTRX |
                                                       S905_UART_CONTROL_RSTTX |
                                                       S905_UART_CONTROL_CLRERR);
        // enable rx and tx
        UARTREG(s905_uart_base,S905_UART_CONTROL) |= S905_UART_CONTROL_TXEN |
                                                     S905_UART_CONTROL_RXEN;

        UARTREG(s905_uart_base,S905_UART_CONTROL) |= S905_UART_CONTROL_INVRTS |
                                                     S905_UART_CONTROL_RXINTEN |
                                                     S905_UART_CONTROL_TWOWIRE;

        // Set to interrupt every 1 rx byte
        uint32_t temp2 = UARTREG(s905_uart_base,S905_UART_IRQ_CONTROL);
        temp2 &= 0xffff0000;
        temp2 |= (1 << 8) | ( 1 );
        UARTREG(s905_uart_base,S905_UART_IRQ_CONTROL) = temp2;

        register_int_handler(s905_uart_irq, &uart_irq, (void *)s905_uart_base);

        initialized = true;

        // enable interrupt
        unmask_interrupt(s905_uart_irq);
}

/* panic-time getc/putc */
static int s905_uart_pputc(char c)
{
    if (!s905_uart_base)
        return 0;

    /* spin while fifo is full */
    while (UARTREG(s905_uart_base, S905_UART_STATUS) & S905_UART_STATUS_TXFULL)
        ;
    UARTREG(s905_uart_base, S905_UART_WFIFO) = c;

    return 1;
}

static int s905_uart_pgetc(void)
{
    if (!s905_uart_base)
        return 0;

    if ((UARTREG(s905_uart_base, S905_UART_STATUS) & S905_UART_STATUS_RXEMPTY) == 0) {
        return UARTREG(s905_uart_base, S905_UART_RFIFO);
    } else {
        return -1;
    }
}


static int s905_uart_putc(char c)
{
    if (!s905_uart_base)
        return 0;

    /* spin while fifo is full */
    while (UARTREG(s905_uart_base, S905_UART_STATUS) & S905_UART_STATUS_TXFULL)
        ;
    UARTREG(s905_uart_base, S905_UART_WFIFO) = c;

    return 1;
}

static int s905_uart_getc(bool wait)
{
    if (!s905_uart_base)
        return -1;

    if (initialized) {
        // do cbuf stuff here
        char c;
        if (cbuf_read_char(&uart_rx_buf, &c, false) == 1)
            return c;
        return -1;

    } else {
        //Interupts not online yet, use the panic calls for now.
        return s905_uart_pgetc();
    }
}

static const struct pdev_uart_ops s905_uart_ops = {
    .putc = s905_uart_putc,
    .getc = s905_uart_getc,
    .pputc = s905_uart_pputc,
    .pgetc = s905_uart_pgetc,
};

static void s905_uart_init_early(mdi_node_ref_t* node, uint level)
{
    s905_uart_base = 0;
    s905_uart_irq  = 0;

    mdi_node_ref_t child;
    mdi_each_child(node, &child) {
        switch (mdi_id(&child)) {
            case MDI_BASE_VIRT:
                if(mdi_node_uint64(&child, &s905_uart_base) != MX_OK)
                    return;
                break;
            case MDI_IRQ:
                if(mdi_node_uint32(&child, &s905_uart_irq) != MX_OK)
                    return;
                break;

        }
    }
    if ((s905_uart_base == 0) || (s905_uart_irq == 0))
        return;

    pdev_register_uart(&s905_uart_ops);
}

LK_PDEV_INIT(s905_uart_init_early, MDI_S905_UART, s905_uart_init_early, LK_INIT_LEVEL_PLATFORM_EARLY);
LK_PDEV_INIT(s905_uart_init, MDI_S905_UART, s905_uart_init, LK_INIT_LEVEL_PLATFORM);
