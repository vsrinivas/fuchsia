// Copyright 2017 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <lib/cbuf.h>
#include <lib/debuglog.h>
#include <lib/zircon-internal/macros.h>
#include <lib/zx/status.h>
#include <reg.h>
#include <stdio.h>
#include <string.h>
#include <trace.h>
#include <zircon/boot/driver-config.h>

#include <arch/arm64/periphmap.h>
#include <dev/interrupt.h>
#include <dev/uart.h>
#include <dev/uart/amlogic_s905/init.h>
#include <kernel/lockdep.h>
#include <kernel/thread.h>
#include <pdev/uart.h>

// clang-format off

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

// clang-format on

static Cbuf uart_rx_buf;
static bool initialized = false;
static vaddr_t s905_uart_base = 0;
static uint32_t s905_uart_irq = 0;

/*
 * Tx driven irq:
 * According to the meson s905 UART spec
 * https://dn.odroid.com/S905/DataSheet/S905_Public_Datasheet_V1.1.4.pdf
 * 1) Tx Fifo depth is 64 bytes
 * 2) The Misc register (aka irq control), by default will
 * interrupt when the # of bytes in the fifo falls below 32
 * but this can be changed if necessary (XMIT_IRQ_CNT).
 * But no need to change this right now.
 * 3) UART status register (TXCOUNT_MASK) holds the # of bytes
 * in the Tx FIFO. More usefully, the TXFULL bit tells us when
 * the Tx FIFO is full. We can use this to continue shoving
 * data into the FIFO.
 * 4) Setting TXINTEN will generate an interrupt each time a byte is
 * read from the Tx FIFO. So we can leave the interrupt unmasked.
 */
static bool uart_tx_irq_enabled = false;
static AutounsignalEvent uart_dputc_event{true};

namespace {
// It's important to ensure that no other locks are acquired while holding this lock.  This lock is
// needed for the printf and panic code paths, and printing and panicking must be safe while holding
// (almost) any lock.
DECLARE_SINGLETON_SPINLOCK_WITH_TYPE(uart_spinlock, MonitoredSpinLock);
}  // namespace

static inline void uartreg_and_eq(uintptr_t base, ptrdiff_t reg, uint32_t flags) {
  volatile uint32_t* ptr = reinterpret_cast<volatile uint32_t*>(base + reg);
  *ptr = *ptr & flags;
}

static inline void uartreg_or_eq(uintptr_t base, ptrdiff_t reg, uint32_t flags) {
  volatile uint32_t* ptr = reinterpret_cast<volatile uint32_t*>(base + reg);
  *ptr = *ptr | flags;
}

static void uart_irq(void* arg) {
  uintptr_t base = (uintptr_t)arg;

  /* read interrupt status and mask */
  while ((UARTREG(base, S905_UART_STATUS) & S905_UART_STATUS_RXCOUNT_MASK) > 0) {
    if (uart_rx_buf.Full()) {
      // Drop the data if our buffer is full
      // NOTE: This breaks flow control, but allows
      // serial to work when disconnecting/reconnecting the cable.
      __UNUSED char c = static_cast<char>(UARTREG(base, S905_UART_RFIFO));
      continue;
    }
    char c = static_cast<char>(UARTREG(base, S905_UART_RFIFO));
    uart_rx_buf.WriteChar(c);
  }

  /* handle any framing/parity errors */
  if (UARTREG(base, S905_UART_STATUS) & (S905_UART_STATUS_FRAMEERR | S905_UART_STATUS_PARERR)) {
    /* clear the status by writing to the control register */
    uartreg_or_eq(base, S905_UART_CONTROL, S905_UART_CONTROL_CLRERR);
  }

  /* handle TX */
  if (UARTREG(s905_uart_base, S905_UART_CONTROL) & S905_UART_CONTROL_TXINTEN) {
    bool signal;
    {
      Guard<MonitoredSpinLock, NoIrqSave> guard{uart_spinlock::Get(), SOURCE_TAG};
      // Drop the uart_spinlock before calling |Event::Signal| to avoid creating an invalid lock
      // dependency between uart_spinlock and any locks Event::Signal may acquire.
      signal = !(UARTREG(s905_uart_base, S905_UART_STATUS) & S905_UART_STATUS_TXFULL);
    }
    if (signal) {
      /* Signal any waiting Tx */
      uart_dputc_event.Signal();
    }
  }
}

void AmlogicS905UartInitLate() {
  DEBUG_ASSERT(s905_uart_base != 0);
  DEBUG_ASSERT(s905_uart_irq != 0);

  // create circular buffer to hold received data
  uart_rx_buf.Initialize(RXBUF_SIZE, malloc(RXBUF_SIZE));

  // reset the port
  uartreg_or_eq(s905_uart_base, S905_UART_CONTROL,
                S905_UART_CONTROL_RSTRX | S905_UART_CONTROL_RSTTX | S905_UART_CONTROL_CLRERR);
  uartreg_and_eq(s905_uart_base, S905_UART_CONTROL,
                 ~(S905_UART_CONTROL_RSTRX | S905_UART_CONTROL_RSTTX | S905_UART_CONTROL_CLRERR));
  // enable rx and tx
  uartreg_or_eq(s905_uart_base, S905_UART_CONTROL, S905_UART_CONTROL_TXEN | S905_UART_CONTROL_RXEN);

  uint32_t val;
  val = S905_UART_CONTROL_INVRTS | S905_UART_CONTROL_RXINTEN | S905_UART_CONTROL_TWOWIRE;
  if (dlog_bypass() == false) {
    val |= S905_UART_CONTROL_TXINTEN;
  }
  uartreg_or_eq(s905_uart_base, S905_UART_CONTROL, val);

  // Set to interrupt every 1 rx byte
  uint32_t temp2 = UARTREG(s905_uart_base, S905_UART_IRQ_CONTROL);
  temp2 &= 0xffff0000;
  temp2 |= (1 << 8) | (1);
  UARTREG(s905_uart_base, S905_UART_IRQ_CONTROL) = temp2;

  zx_status_t status =
      register_permanent_int_handler(s905_uart_irq, &uart_irq, (void*)s905_uart_base);
  DEBUG_ASSERT(status == ZX_OK);

  initialized = true;

  if (dlog_bypass() == true) {
    uart_tx_irq_enabled = false;
  } else {
    /* start up tx driven output */
    printf("UART: started IRQ driven TX\n");
    uart_tx_irq_enabled = true;
  }

  // enable interrupt
  unmask_interrupt(s905_uart_irq);
}

/* panic-time getc/putc */
static void s905_uart_pputc(char c) {
  /* spin while fifo is full */
  while (UARTREG(s905_uart_base, S905_UART_STATUS) & S905_UART_STATUS_TXFULL)
    ;
  UARTREG(s905_uart_base, S905_UART_WFIFO) = c;
}

static int s905_uart_pgetc() {
  if ((UARTREG(s905_uart_base, S905_UART_STATUS) & S905_UART_STATUS_RXEMPTY) == 0) {
    return UARTREG(s905_uart_base, S905_UART_RFIFO);
  } else {
    return ZX_ERR_INTERNAL;
  }
}

static int s905_uart_getc(bool wait) {
  if (initialized) {
    // do cbuf stuff here
    zx::result<char> result = uart_rx_buf.ReadChar(wait);
    if (result.is_ok()) {
      return result.value();
    }
    return result.error_value();

  } else {
    // Interrupts not online yet, use the panic calls for now.
    return s905_uart_pgetc();
  }
}

/*
 * Keeping this simple for now, we try to write 1 byte at a time
 * to the Tx FIFO. Blocking or spinning if the Tx FIFO is full.
 * The event is signaled up from the interrupt handler, when a
 * byte is read from the Tx FIFO.
 * (setting TXINTEN results in the generation of an interrupt
 * each time a byte is read from the Tx FIFO).
 */
static void s905_dputs(const char* str, size_t len, bool block) {
  bool copied_CR = false;

  if (!uart_tx_irq_enabled) {
    block = false;
  }

  Guard<MonitoredSpinLock, IrqSave> guard{uart_spinlock::Get(), SOURCE_TAG};
  while (len > 0) {
    /* Is FIFO Full ? */
    while (UARTREG(s905_uart_base, S905_UART_STATUS) & S905_UART_STATUS_TXFULL) {
      guard.CallUnlocked([&block]() {
        if (block) {
          uart_dputc_event.Wait();
        } else {
          arch::Yield();
        }
      });
    }

    if (*str == '\n' && !copied_CR) {
      copied_CR = true;
      UARTREG(s905_uart_base, S905_UART_WFIFO) = '\r';
    } else {
      copied_CR = false;
      UARTREG(s905_uart_base, S905_UART_WFIFO) = *str++;
      len--;
    }
  }
}

static void s905_uart_start_panic() { uart_tx_irq_enabled = false; }

static const struct pdev_uart_ops s905_uart_ops = {
    .getc = s905_uart_getc,
    .pputc = s905_uart_pputc,
    .pgetc = s905_uart_pgetc,
    .start_panic = s905_uart_start_panic,
    .dputs = s905_dputs,
};

void AmlogicS905UartInitEarly(const zbi_dcfg_simple_t& config) {
  ASSERT(config.mmio_phys != 0);
  ASSERT(config.irq != 0);

  s905_uart_base = periph_paddr_to_vaddr(config.mmio_phys);
  ASSERT(s905_uart_base != 0);
  s905_uart_irq = config.irq;

  pdev_register_uart(&s905_uart_ops);
}
