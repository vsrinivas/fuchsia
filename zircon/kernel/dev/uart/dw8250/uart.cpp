// Copyright 2019 The Fuchsia Authors
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <arch/arm64/periphmap.h>
#include <dev/interrupt.h>
#include <dev/uart.h>
#include <kernel/thread.h>
#include <lib/cbuf.h>
#include <lib/debuglog.h>
#include <pdev/driver.h>
#include <pdev/uart.h>
#include <platform/debug.h>
#include <reg.h>
#include <stdio.h>
#include <trace.h>
#include <zircon/boot/driver-config.h>

// DW8250 implementation

// clang-format off
// UART Registers

#define UART_RBR                    (0x0)   // RX Buffer Register (read-only)
#define UART_THR                    (0x0)   // TX Buffer Register (write-only)
#define UART_IER                    (0x4)   // Interrupt Enable Register
#define UART_IIR                    (0x8)   // Interrupt Identification Register (read-only)
#define UART_FCR                    (0x8)   // FIFO Control Register (write-only)
#define UART_LCR                    (0xc)   // Line Control Register
#define UART_MCR                    (0x10)  // Modem Control Register
#define UART_LSR                    (0x14)  // Line Status Register
#define UART_MSR                    (0x18)  // Modem Status Register
#define UART_SCR                    (0x1c)  // Scratch Register
#define UART_DLL                    (0x0)   // Divisor Latch LS (Only when LCR.DLAB = 1)
#define UART_DLM                    (0x4)   // Divisor Latch MS (Only when LCR.DLAB = 1)
#define UART_EFR                    (0x8)   // Enhanced Feature Register (Only when LCR = 0xbf)
#define UART_XON1                   (0x10)  // XON1 Char Register (Only when LCR = 0xbf)
#define UART_XON2                   (0x14)  // XON2 Char Register (Only when LCR = 0xbf)
#define UART_XOFF1                  (0x18)  // XOFF1 Char Register (Only when LCR = 0xbf)
#define UART_XOFF2                  (0x1c)  // XOFF2 Char Register (Only when LCR = 0xbf)
#define UART_AUTOBAUD_EN            (0x20)  // Auto Baud Detect Enable Register
#define UART_HIGHSPEED              (0x24)  // High Speed Mode Register
#define UART_SAMPLE_COUNT           (0x28)  // Sample Counter Register
#define UART_SAMPLE_POINT           (0x2c)  // Sample Point Register
#define UART_AUTOBAUD_REG           (0x30)  // Auto Baud Monitor Register
#define UART_RATE_FIX_AD            (0x34)  // Clock Rate Fix Register
#define UART_AUTOBAUD_SAMPLE        (0x38)  // Auto Baud Sample Register
#define UART_GUARD                  (0x3c)  // Guard Time Added Register
#define UART_ESCAPE_DAT             (0x40)  // Escape Character Register
#define UART_ESCAPE_EN              (0x44)  // Escape Enable Register
#define UART_SLEEP_EN               (0x48)  // Sleep Enable Register
#define UART_VFIFO_EN               (0x4c)  // DMA Enable Register
#define UART_RXTRI_AD               (0x50)  // RX Trigger Address

// IER
#define UART_IER_ERBFI              (1 << 0)
#define UART_IER_ETBEI              (1 << 1)
#define UART_IER_ELSI               (1 << 2)
#define UART_IER_EDSSI              (1 << 3)
#define UART_IER_XOFFI              (1 << 5)
#define UART_IER_RTSI               (1 << 6)
#define UART_IER_CTSI               (1 << 7)
#define UART_IIR_NO_INT_PENDING     (0x01)

// IIR
#define UART_IIR_RLS                (0x06)  // Receiver Line Status
#define UART_IIR_RDA                (0x04)  // Receive Data Available
#define UART_IIR_CTI                (0x0C)  // Character Timeout Indicator
#define UART_IIR_THRE               (0x02)  // Transmit Holding Register Empty
#define UART_IIR_MS                 (0x00)  // Check Modem Status Register
#define UART_IIR_SW_FLOW_CTRL       (0x10)  // Receive XOFF characters
#define UART_IIR_HW_FLOW_CTRL       (0x20)  // CTS or RTS Rising Edge
#define UART_IIR_FIFO_EN            (0xc0)
#define UART_IIR_INT_MASK           (0x1f)

// LSR
#define UART_LSR_DR                 (1 << 0)
#define UART_LSR_OE                 (1 << 1)
#define UART_LSR_PE                 (1 << 2)
#define UART_LSR_FE                 (1 << 3)
#define UART_LSR_BI                 (1 << 4)
#define UART_LSR_THRE               (1 << 5)
#define UART_LSR_TEMT               (1 << 6)
#define UART_LSR_FIFOERR            (1 << 7)
// clang-format on

#define RXBUF_SIZE 32

// values read from zbi
static bool initialized = false;
static vaddr_t uart_base = 0;
static uint32_t uart_irq = 0;

static cbuf_t uart_rx_buf;
static bool uart_tx_irq_enabled = false;
static event_t uart_dputc_event = EVENT_INITIAL_VALUE(uart_dputc_event,
                                                      true,
                                                      EVENT_FLAG_AUTOUNSIGNAL);
static spin_lock_t uart_spinlock = SPIN_LOCK_INITIAL_VALUE;
#define UARTREG(reg) (*(volatile uint32_t*)((uart_base) + (reg)))

static interrupt_eoi dw8250_uart_irq(void* arg) {
    // read interrupt status and mask
    while (UARTREG(UART_LSR) & UART_LSR_DR) {
        if (cbuf_space_avail(&uart_rx_buf) == 0) {
            break;
        }
        char c = UARTREG(UART_RBR) & 0xFF;
        cbuf_write_char(&uart_rx_buf, c);
    }

    // Signal if anyone is waiting to TX
    if (UARTREG(UART_LSR) & UART_LSR_THRE) {
        UARTREG(UART_IER) &= ~UART_IER_ETBEI; // Disable TX interrupt
        spin_lock(&uart_spinlock);
        // TODO(andresoportus): Revisit all UART drivers usage of events, from event.h:
        // 1. The reschedule flag is not supposed to be true in interrupt context.
        // 2. FLAG_AUTOUNSIGNAL only wakes up one thread.
        event_signal(&uart_dputc_event, true);
        spin_unlock(&uart_spinlock);
    }

    return IRQ_EOI_DEACTIVATE;
}

/* panic-time getc/putc */
static int dw8250_uart_pputc(char c) {
    if (!uart_base) {
        return -1;
    }

    // spin while fifo is full
    while (!(UARTREG(UART_LSR) & UART_LSR_THRE))
        ;
    UARTREG(UART_THR) = c;

    return 1;
}

static int dw8250_uart_pgetc() {
    if (!uart_base) {
        return ZX_ERR_NOT_SUPPORTED;
    }

    // spin while fifo is empty
    while (!(UARTREG(UART_LSR) & UART_LSR_DR))
        ;
    return UARTREG(UART_RBR);
}

static int dw8250_uart_getc(bool wait) {
    if (!uart_base) {
        return ZX_ERR_NOT_SUPPORTED;
    }

    if (initialized) {
        char c;
        if (cbuf_read_char(&uart_rx_buf, &c, wait) == 1) {
            return c;
        }
        return ZX_ERR_INTERNAL;
    } else {
        // Interrupts are not enabled yet. Use panic calls for now
        return dw8250_uart_pgetc();
    }
}

static void dw8250_dputs(const char* str, size_t len,
                        bool block, bool map_NL) {
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
        while (!(UARTREG(UART_LSR) & UART_LSR_THRE)) {
            spin_unlock_irqrestore(&uart_spinlock, state);
            if (block) {
                UARTREG(UART_IER) |= UART_IER_ETBEI; // Enable TX interrupt.
                event_wait(&uart_dputc_event);
            } else {
                arch_spinloop_pause();
            }
            spin_lock_irqsave(&uart_spinlock, state);
        }
        if (*str == '\n' && map_NL && !copied_CR) {
            copied_CR = true;
            dw8250_uart_pputc('\r');
        } else {
            copied_CR = false;
            dw8250_uart_pputc(*str++);
            len--;
        }
    }
    spin_unlock_irqrestore(&uart_spinlock, state);
}

static void dw8250_uart_init(const void* driver_data, uint32_t length) {
    return;
    // create circular buffer to hold received data
    cbuf_initialize(&uart_rx_buf, RXBUF_SIZE);

    if (dlog_bypass() == true) {
        uart_tx_irq_enabled = false;
        return;
    }
    zx_status_t status = register_int_handler(uart_irq, &dw8250_uart_irq, NULL);
    DEBUG_ASSERT(status == ZX_OK);

    // enable interrupt
    unmask_interrupt(uart_irq);

    UARTREG(UART_IER) |= UART_IER_ERBFI; // Enable RX interrupt.
    initialized = true;

    // Start up tx driven output.
    uart_tx_irq_enabled = true;
}

static void dw8250_start_panic() {
    uart_tx_irq_enabled = false;
}

static const struct pdev_uart_ops uart_ops = {
    .getc = dw8250_uart_getc,
    .pputc = dw8250_uart_pputc,
    .pgetc = dw8250_uart_pgetc,
    .start_panic = dw8250_start_panic,
    .dputs = dw8250_dputs,
};

extern "C" void uart_mark(unsigned char x);
static void dw8250_uart_init_early(const void* driver_data, uint32_t length) {
    ASSERT(length >= sizeof(dcfg_simple_t));
    auto driver = static_cast<const dcfg_simple_t*>(driver_data);
    ASSERT(driver->mmio_phys && driver->irq);

    uart_base = periph_paddr_to_vaddr(driver->mmio_phys);
    ASSERT(uart_base);
    uart_irq = driver->irq;

    pdev_register_uart(&uart_ops);
}

LK_PDEV_INIT(dw8250_uart_init_early, KDRV_DW8250_UART, dw8250_uart_init_early, LK_INIT_LEVEL_PLATFORM_EARLY)
LK_PDEV_INIT(dw8250_uart_init, KDRV_DW8250_UART, dw8250_uart_init, LK_INIT_LEVEL_PLATFORM)
