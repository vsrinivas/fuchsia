// Copyright 2019 The Fuchsia Authors
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <lib/cbuf.h>
#include <lib/debuglog.h>
#include <lib/zx/status.h>
#include <reg.h>
#include <stdio.h>
#include <trace.h>
#include <zircon/boot/driver-config.h>

#include <arch/arm64/periphmap.h>
#include <dev/interrupt.h>
#include <dev/uart.h>
#include <kernel/thread.h>
#include <pdev/driver.h>
#include <pdev/uart.h>
#include <platform/debug.h>

// DW8250 implementation

// clang-format off
// UART Registers

#define UART_RBR                    (0x0)   // RX Buffer Register (read-only)
#define UART_THR                    (0x0)   // TX Buffer Register (write-only)
#define UART_DLL                    (0x0)   // Divisor Latch Low (Only when LCR[7] = 1)
#define UART_DLH                    (0x4)   // Divisor Latch High (Only when LCR[7] = 1)
#define UART_IER                    (0x4)   // Interrupt Enable Register
#define UART_IIR                    (0x8)   // Interrupt Identification Register (read-only)
#define UART_FCR                    (0x8)   // FIFO Control Register (write-only)
#define UART_LCR                    (0xc)   // Line Control Register
#define UART_MCR                    (0x10)  // Modem Control Register
#define UART_LSR                    (0x14)  // Line Status Register (read-only)
#define UART_MSR                    (0x18)  // Modem Status Register (read-only)
#define UART_SCR                    (0x1c)  // Scratch Register
#define UART_LPDLL                  (0x20)  // Low Power Divisor Latch (Low) Register
#define UART_LPDLH                  (0x24)  // Low Power Divisor Latch (High) Register
#define UART_SRBR                   (0x30)  // Shadow Receive Buffer Register (read-only)
#define UART_STHR                   (0x34)  // Shadow Transmit Holding Register
#define UART_FAR                    (0x70)  // FIFO Access Register
#define UART_TFR                    (0x74)  // Transmit FIFO Read Register (read-only)
#define UART_RFW                    (0x78)  // Receive FIFO Write Register (write-only)
#define UART_USR                    (0x7C)  // UART Status Register (read-only)
#define UART_TFL                    (0x80)  // Transmit FIFO Level Register (read-only)
#define UART_RFL                    (0x84)  // Receive FIFO Level Register (read-only)
#define UART_SRR                    (0x88)  // Software Reser Register
#define UART_SRTS                   (0x8C)  // Shadow Request to Send Register
#define UART_SBCR                   (0x90)  // Shadow Break Control Register
#define UART_SDMAM                  (0x94)  // Shadow DMA Mode Register
#define UART_SFE                    (0x98)  // Shadow FIFO Enable Register
#define UART_SRT                    (0x9C)  // Shadow RCVR Trigger Register
#define UART_STET                   (0xA0)  // Shadow TX Empty Trigger Register
#define UART_HTX                    (0xA4)  // Halt TX Register
#define UART_DMASA                  (0xA8)  // DMA Software Acknowledge Register (write-only)
#define UART_CPR                    (0xF4)  // Component Parameter Register (read-only)
#define UART_UCV                    (0xF8)  // UART Component Version Register (read-only)
#define UART_CTR                    (0xFC)  // Component Type Register

// IER
#define UART_IER_ERBFI              (1 << 0)
#define UART_IER_ETBEI              (1 << 1)
#define UART_IER_ELSI               (1 << 2)
#define UART_IER_EDSSI              (1 << 3)
#define UART_IER_PTIME              (1 << 5)

// IIR
#define UART_IIR_RLS                (0x06)  // Receiver Line Status
#define UART_IIR_RDA                (0x04)  // Receive Data Available
#define UART_IIR_BUSY               (0x07)  // Busy Detect Indication
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

static Cbuf uart_rx_buf;
static bool uart_tx_irq_enabled = false;
static AutounsignalEvent uart_dputc_event{true};
static SpinLock uart_spinlock;
#define UARTREG(reg) (*(volatile uint32_t*)((uart_base) + (reg)))

static interrupt_eoi dw8250_uart_irq(void* arg) {
  if ((UARTREG(UART_IIR) & UART_IIR_BUSY) == UART_IIR_BUSY) {
    // To clear the USR (UART Status Register) we need to read it.
    volatile uint32_t unused = UARTREG(UART_USR);
    static_cast<void>(unused);
  }

  // read interrupt status and mask
  while (UARTREG(UART_LSR) & UART_LSR_DR) {
    if (uart_rx_buf.Full()) {
      break;
    }
    char c = UARTREG(UART_RBR) & 0xFF;
    uart_rx_buf.WriteChar(c);
  }

  // Signal if anyone is waiting to TX
  if (UARTREG(UART_LSR) & UART_LSR_THRE) {
    UARTREG(UART_IER) &= ~UART_IER_ETBEI;  // Disable TX interrupt
    uart_spinlock.Acquire();
    // TODO(andresoportus): Revisit all UART drivers usage of events, from event.h:
    // 1. The reschedule flag is not supposed to be true in interrupt context.
    // 2. AutounsignalEvent only wakes up one thread per Signal().
    uart_dputc_event.Signal();
    uart_spinlock.Release();
  }

  return IRQ_EOI_DEACTIVATE;
}

/* panic-time getc/putc */
static void dw8250_uart_pputc(char c) {
  // spin while fifo is full
  while (!(UARTREG(UART_LSR) & UART_LSR_THRE))
    ;
  UARTREG(UART_THR) = c;
}

static int dw8250_uart_pgetc() {
  // spin while fifo is empty
  while (!(UARTREG(UART_LSR) & UART_LSR_DR))
    ;
  return UARTREG(UART_RBR);
}

static int dw8250_uart_getc(bool wait) {
  if (initialized) {
    zx::status<char> result = uart_rx_buf.ReadChar(wait);
    if (result.is_ok()) {
      return result.value();
    }
    return result.error_value();
  } else {
    // Interrupts are not enabled yet. Use panic calls for now
    return dw8250_uart_pgetc();
  }
}

static void dw8250_dputs(const char* str, size_t len, bool block, bool map_NL) {
  interrupt_saved_state_t state;
  bool copied_CR = false;

  if (!uart_tx_irq_enabled) {
    block = false;
  }
  uart_spinlock.AcquireIrqSave(state);

  while (len > 0) {
    // is FIFO full?
    while (!(UARTREG(UART_LSR) & UART_LSR_THRE)) {
      uart_spinlock.ReleaseIrqRestore(state);
      if (block) {
        UARTREG(UART_IER) |= UART_IER_ETBEI;  // Enable TX interrupt.
        uart_dputc_event.Wait();
      } else {
        arch::Yield();
      }
      uart_spinlock.AcquireIrqSave(state);
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
  uart_spinlock.ReleaseIrqRestore(state);
}

static void dw8250_uart_init(const void* driver_data, uint32_t length) {
  // Initialize circular buffer to hold received data.
  uart_rx_buf.Initialize(RXBUF_SIZE, malloc(RXBUF_SIZE));

  if (dlog_bypass() == true) {
    uart_tx_irq_enabled = false;
    return;
  }

  zx_status_t status =
      configure_interrupt(uart_irq, IRQ_TRIGGER_MODE_LEVEL, IRQ_POLARITY_ACTIVE_HIGH);
  if (status != ZX_OK) {
    printf("UART: configure_interrupt failed %d\n", status);
    return;
  }

  status = register_permanent_int_handler(uart_irq, &dw8250_uart_irq, NULL);
  if (status != ZX_OK) {
    printf("UART: register_permanent_int_handler failed %d\n", status);
    return;
  }
  // enable interrupt
  status = unmask_interrupt(uart_irq);
  if (status != ZX_OK) {
    printf("UART: unmask_interrupt failed %d\n", status);
    return;
  }

  UARTREG(UART_IER) |= UART_IER_ERBFI;  // Enable RX interrupt.
  initialized = true;
  // Start up tx driven output.
  printf("UART: starting IRQ driven TX\n");
  uart_tx_irq_enabled = true;
}

static void dw8250_start_panic() { uart_tx_irq_enabled = false; }

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

LK_PDEV_INIT(dw8250_uart_init_early, KDRV_DW8250_UART, dw8250_uart_init_early,
             LK_INIT_LEVEL_PLATFORM_EARLY)
LK_PDEV_INIT(dw8250_uart_init, KDRV_DW8250_UART, dw8250_uart_init, LK_INIT_LEVEL_PLATFORM)
