// Copyright 2018 The Fuchsia Authors
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <reg.h>
#include <stdio.h>
#include <trace.h>
#include <arch/arm64/periphmap.h>
#include <lib/cbuf.h>
#include <kernel/thread.h>
#include <dev/interrupt.h>
#include <dev/uart.h>
#include <platform/debug.h>
#include <pdev/driver.h>
#include <pdev/uart.h>
#include <zircon/boot/driver-config.h>

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

/* UCR1 Bit Definition */
#define UCR1_TRDYEN                 (1 << 13)
#define UCR1_RRDYEN                 (1 << 9)
#define UCR1_UARTEN                 (1 << 0)

/* UCR2 Bit Definition */
#define UCR2_TXEN                   (1 << 2)
#define UCR2_RXEN                   (1 << 1)
#define UCR2_SRST                   (1 << 0)

/* UFCR Bit Definition */
#define UFCR_TXTL(x)                (x << 10)
#define UFCR_RXTL(x)                (x << 0)
#define UFCR_MASK                   (0x3f)

/* USR1 Bit Definition */
#define USR1_TRDY                   (1 << 13)
#define USR1_RRDY                   (1 << 9)

/* USR2 Bit Definition */
#define USR2_TXFE                   (1 << 14)

/* UTS Bit Definition */
#define UTS_TXEMPTY                 (1 << 6)
#define UTS_RXEMPTY                 (1 << 5)
#define UTS_TXFULL                  (1 << 4)
#define UTS_RXFULL                  (1 << 3)

#define RXBUF_SIZE 32


// values read from bootdata
static bool initialized = false;
static vaddr_t uart_base = 0;
static uint32_t uart_irq = 0;
static cbuf_t uart_rx_buf;
// static cbuf_t uart_tx_buf;

static bool uart_tx_irq_enabled = false;
static event_t uart_dputc_event = EVENT_INITIAL_VALUE(uart_dputc_event,
                                                      true,
                                                      EVENT_FLAG_AUTOUNSIGNAL);

static spin_lock_t uart_spinlock = SPIN_LOCK_INITIAL_VALUE;

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

    /* Signal if anyone is waiting to TX */
    if (UARTREG(MX8_UCR1) & UCR1_TRDYEN) {
        spin_lock(&uart_spinlock);
        if (!(UARTREG(MX8_USR2) & UTS_TXFULL)) {
            // signal
            event_signal(&uart_dputc_event, true);
        }
        spin_unlock(&uart_spinlock);
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
    spin_lock_saved_state_t state;
    bool copied_CR = false;

    if (!uart_base) {
        return;
    }
    if (!uart_tx_irq_enabled) {
        block = false;
    }
    spin_lock_irqsave(&uart_spinlock, state);

    while (len > 0) {
        // is FIFO full?
        while ((UARTREG(MX8_UTS) & UTS_TXFULL)) {
            spin_unlock_irqrestore(&uart_spinlock, state);
            if (block) {
                event_wait(&uart_dputc_event);
            } else {
                arch_spinloop_pause();
            }
            spin_lock_irqsave(&uart_spinlock, state);
        }
        if (*str == '\n' && map_NL && !copied_CR) {
            copied_CR = true;
            imx_uart_pputc('\r');
        } else {
            copied_CR = false;
            imx_uart_pputc(*str++);
            len--;
        }
    }
    spin_unlock_irqrestore(&uart_spinlock, state);
}

static void imx_start_panic(void)
{
    uart_tx_irq_enabled = false;
}

static const struct pdev_uart_ops uart_ops = {
    .getc = imx_uart_getc,
    .pputc = imx_uart_pputc,
    .pgetc = imx_uart_pgetc,
    .start_panic = imx_start_panic,
    .dputs = imx_dputs,
};

static void imx_uart_init(const void* driver_data, uint32_t length)
{
    uint32_t regVal;

    // create circular buffer to hold received data
    cbuf_initialize(&uart_rx_buf, RXBUF_SIZE);

    // register uart irq
    register_int_handler(uart_irq, &uart_irq_handler, NULL);

    // set rx fifo threshold to 1 character
    regVal = UARTREG(MX8_UFCR);
    regVal &= ~UFCR_RXTL(UFCR_MASK);
    regVal &= ~UFCR_TXTL(UFCR_MASK);
    regVal |= UFCR_RXTL(1);
    regVal |= UFCR_TXTL(0x2);
    UARTREG(MX8_UFCR) = regVal;

    // enable rx interrupt
    regVal = UARTREG(MX8_UCR1);
    regVal |= UCR1_RRDYEN;
#if !ENABLE_KERNEL_LL_DEBUG
    // enable tx interrupt
    regVal |= UCR1_TRDYEN;
#endif
    UARTREG(MX8_UCR1) = regVal;

    // enable rx and tx transmisster
    regVal = UARTREG(MX8_UCR2);
    regVal |= UCR2_RXEN | UCR2_TXEN;
    UARTREG(MX8_UCR2) = regVal;

#if ENABLE_KERNEL_LL_DEBUG
    uart_tx_irq_enabled = false;
#else
    /* start up tx driven output */
    printf("UART: started IRQ driven TX\n");
    uart_tx_irq_enabled = true;
#endif

    initialized = true;

    // enable interrupts
    unmask_interrupt(uart_irq);
}

static void imx_uart_init_early(const void* driver_data, uint32_t length) {
    ASSERT(length >= sizeof(dcfg_simple_t));
    const dcfg_simple_t* driver = driver_data;
    ASSERT(driver->mmio_phys && driver->irq);

    uart_base = periph_paddr_to_vaddr(driver->mmio_phys);
    ASSERT(uart_base);
    uart_irq = driver->irq;

    pdev_register_uart(&uart_ops);
}

LK_PDEV_INIT(imx_uart_init_early, KDRV_NXP_IMX_UART, imx_uart_init_early, LK_INIT_LEVEL_PLATFORM_EARLY);
LK_PDEV_INIT(imx_uart_init, KDRV_NXP_IMX_UART, imx_uart_init, LK_INIT_LEVEL_PLATFORM);
