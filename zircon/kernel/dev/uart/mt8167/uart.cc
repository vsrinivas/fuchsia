// Copyright 2018 The Fuchsia Authors
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


// SOC Registers

#define SOC_INT_POL                 (0x620) // SOC Interrupt polarity registers start

// clang-format on

#define RXBUF_SIZE 32

// values read from zbi
static bool initialized = false;
static vaddr_t uart_base = 0;
static vaddr_t soc_base = 0;
static uint32_t uart_irq = 0;
static Cbuf uart_rx_buf;

static bool uart_tx_irq_enabled = false;
static AutounsignalEvent uart_dputc_event{true};

static SpinLock uart_spinlock;

#define UARTREG(reg) (*(volatile uint32_t*)((uart_base) + (reg)))
#define SOCREG(reg) (*(volatile uint32_t*)((soc_base) + (reg)))

static interrupt_eoi uart_irq_handler(void* arg) {
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
    // 2. AutounsignalEvent only wakes up one thread per Signal() call.
    uart_dputc_event.Signal();
    uart_spinlock.Release();
  }

  return IRQ_EOI_DEACTIVATE;
}

// panic-time getc/putc
static void mt8167_uart_pputc(char c) {
  // spin while fifo is full
  while (!(UARTREG(UART_LSR) & UART_LSR_THRE))
    ;
  UARTREG(UART_THR) = c;
}

static int mt8167_uart_pgetc() {
  // spin while fifo is empty
  while (!(UARTREG(UART_LSR) & UART_LSR_DR))
    ;
  return UARTREG(UART_RBR);
}

static int mt8167_uart_getc(bool wait) {
  if (initialized) {
    zx::status<char> result = uart_rx_buf.ReadChar(wait);
    if (result.is_ok()) {
      return result.value();
    }
    return result.error_value();
  } else {
    // Interrupts are not enabled yet. Use panic calls for now
    return mt8167_uart_pgetc();
  }
}

static void mt8167_dputs(const char* str, size_t len, bool block, bool map_NL) {
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
      mt8167_uart_pputc('\r');
    } else {
      copied_CR = false;
      mt8167_uart_pputc(*str++);
      len--;
    }
  }
  uart_spinlock.ReleaseIrqRestore(state);
}

static void mt8167_start_panic() { uart_tx_irq_enabled = false; }

static const struct pdev_uart_ops uart_ops = {
    .getc = mt8167_uart_getc,
    .pputc = mt8167_uart_pputc,
    .pgetc = mt8167_uart_pgetc,
    .start_panic = mt8167_start_panic,
    .dputs = mt8167_dputs,
};

static void mt8167_uart_init(const void* driver_data, uint32_t length) {
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

  status = register_permanent_int_handler(uart_irq, &uart_irq_handler, NULL);
  if (status != ZX_OK) {
    printf("UART: register_permanent_int_handler failed %d\n", status);
    return;
  }

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

static void mt8167_uart_init_early(const void* driver_data, uint32_t length) {
  ASSERT(length >= sizeof(dcfg_soc_uart_t));
  auto driver = static_cast<const dcfg_soc_uart_t*>(driver_data);
  ASSERT(driver->soc_mmio_phys && driver->uart_mmio_phys && driver->irq);

  soc_base = periph_paddr_to_vaddr(driver->soc_mmio_phys);
  ASSERT(soc_base);

  // Convert Level interrupt polarity in SOC from Low to High as needed by gicv2.
  auto index = (driver->irq - 32);  // index IRQ as SPI (-32 PPIs).
  // 32 interrupts per register, one register every 4 bytes.
  SOCREG(SOC_INT_POL + index / 32 * 4) = static_cast<uint32_t>(1) << (index % 32);

  uart_base = periph_paddr_to_vaddr(driver->uart_mmio_phys);
  ASSERT(uart_base);
  uart_irq = driver->irq;

  pdev_register_uart(&uart_ops);
}

LK_PDEV_INIT(mt8167_uart_init_early, KDRV_MT8167_UART, mt8167_uart_init_early,
             LK_INIT_LEVEL_PLATFORM_EARLY)
LK_PDEV_INIT(mt8167_uart_init, KDRV_MT8167_UART, mt8167_uart_init, LK_INIT_LEVEL_PLATFORM)
