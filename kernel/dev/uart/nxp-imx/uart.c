// Copyright 2018 The Fuchsia Authors
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

/* Registers */
#define MX8_URXD                    (0x00)
#define MX8_UTXD                    (0x40)
#define MX8_UCR1                    (0x80)
#define MX8_UCR2                    (0x84)
#define MX8_UCR3                    (0x88)
#define MX8_UCR4                    (0x8C)
#define MX8_UFCR                    (0x90)
#define MX8_USR1                    (0x94)
#define MX8_USR2                    (0x98)
#define MX8_UTS                     (0xB4)


#define UCR1_RRDYEN             (1 << 9)
#define UCR1_UARTEN             (1 << 0)
#define UCR2_TXEN               (1 << 2)
#define UCR2_RXEN               (1 << 1)
#define UCR2_SRST               (1 << 0)
#define UFCR_RXTL(x)            (x << 0)
#define USR1_RRDY               (1 << 9)
#define UTS_TXEMPTY             (1 << 6)
#define UTS_RXEMPTY             (1 << 5)
#define UTS_TXFULL              (1 << 4)
#define UTS_RXFULL              (1 << 3)

#define RXBUF_SIZE 32


// values read from MDI
static bool initialized = false;
static uint64_t uart_base = 0;
static uint32_t uart_irq = 0;
static cbuf_t uart_rx_buf;

#define UARTREG(reg)          (*(volatile uint32_t*)((uart_base)  + (reg)))

static void uart_irq_handler(void *arg)
{
    /* read interrupt status and mask */
    while ((UARTREG(MX8_USR1) & USR1_RRDY)) {
        if (cbuf_space_avail(&uart_rx_buf) == 0) {
                break;
        }
        char c = UARTREG(MX8_URXD) & 0xFF;
        cbuf_write_char(&uart_rx_buf, c);
    }
}



/* panic-time getc/putc */
static int imx_uart_pputc(char c)
{
    if (!uart_base) {
        return -1;
    }

    /* spin while fifo is full */
    while (UARTREG(MX8_UTS) & UTS_TXFULL)
        ;
    UARTREG(MX8_UTXD) = c;

    return 1;
}

static int imx_uart_pgetc(void)
{
    if (!uart_base) {
        return -1;
    }

    if ((UARTREG(MX8_UTS) & UTS_RXEMPTY)) {
        return -1;
    }

   return UARTREG(MX8_URXD);
}

static int imx_uart_getc(bool wait)
{
    if (!uart_base) {
        return -1;
    }

    if (initialized) {
        char c;
        if (cbuf_read_char(&uart_rx_buf, &c, wait) == 1) {
            return c;
        }
        return -1;
    } else {
        // Interrupts are not enabled yet. Use panic calls for now
        return imx_uart_pgetc();
    }

}

static void imx_dputs(const char* str, size_t len,
                        bool block, bool map_NL)
{
    bool copied_CR = false;
    while (len > 0) {
        if (*str == '\n' && map_NL && !copied_CR) {
            copied_CR = true;
            imx_uart_pputc('\r');
        } else {
            copied_CR = false;
            imx_uart_pputc(*str++);
            len--;
        }
    }
}

static void imx_start_panic(void)
{
}

static const struct pdev_uart_ops uart_ops = {
    .getc = imx_uart_getc,
    .pputc = imx_uart_pputc,
    .pgetc = imx_uart_pgetc,
    .start_panic = imx_start_panic,
    .dputs = imx_dputs,
};

static void imx_uart_init(mdi_node_ref_t* node, uint level)
{
    uint32_t regVal;

    // create circular buffer to hold received data
    cbuf_initialize(&uart_rx_buf, RXBUF_SIZE);

    // register uart irq
    register_int_handler(uart_irq, &uart_irq_handler, NULL);

    // set rx fifo threshold to 1 character
    regVal = UARTREG(MX8_UFCR);
    regVal &= ~UFCR_RXTL(0x1f);
    regVal |= UFCR_RXTL(1);
    UARTREG(MX8_UFCR) = regVal;

    // enable rx interrupt
    regVal = UARTREG(MX8_UCR1);
    regVal |= UCR1_RRDYEN;
    UARTREG(MX8_UCR1) = regVal;

    // enable rx and tx transmisster
    regVal = UARTREG(MX8_UCR2);
    regVal |= UCR2_RXEN | UCR2_TXEN;
    UARTREG(MX8_UCR2) = regVal;

    // enable interrupts
    unmask_interrupt(uart_irq);

    initialized = true;
}

static void imx_uart_init_early(mdi_node_ref_t* node, uint level) {
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
        panic("imx uart: uart_base_virt not defined\n");
    }
    if (!got_uart_irq) {
        panic("imx uart: uart_irq not defined\n");
    }

    uart_base = (uint64_t)uart_base_virt;

    pdev_register_uart(&uart_ops);
}

LK_PDEV_INIT(imx_uart_init_early, MDI_ARM_NXP_IMX_UART, imx_uart_init_early, LK_INIT_LEVEL_PLATFORM_EARLY);
LK_PDEV_INIT(imx_uart_init, MDI_ARM_NXP_IMX_UART, imx_uart_init, LK_INIT_LEVEL_PLATFORM);
