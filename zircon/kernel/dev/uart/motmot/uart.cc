// Copyright 2021 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <bits.h>
#include <lib/cbuf.h>
#include <lib/debuglog.h>
#include <lib/zircon-internal/macros.h>
#include <platform.h>
#include <reg.h>
#include <stdio.h>
#include <trace.h>
#include <zircon/boot/driver-config.h>

#include <arch/arm64/periphmap.h>
#include <dev/interrupt.h>
#include <dev/uart.h>
#include <dev/uart/motmot/init.h>
#include <kernel/auto_lock.h>
#include <kernel/lockdep.h>
#include <kernel/thread.h>
#include <kernel/timer.h>
#include <ktl/algorithm.h>
#include <pdev/uart.h>
#include <platform/debug.h>

#include <ktl/enforce.h>

#define LOCAL_TRACE 0

// Motmot implementation

// clang-format off
#define UART_ULCON      (0x00)
#define UART_UCON       (0x04)
#define UART_UFCON      (0x08)
#define UART_UMCON      (0x0c)
#define UART_UTRSTAT    (0x10)
#define UART_UERSTAT    (0x14)
#define UART_UFSTAT     (0x18)
#define UART_UMSTAT     (0x1c)
#define UART_UTXH       (0x20)
#define UART_URXH       (0x24)
#define UART_UBRDIV     (0x28)
#define UART_UFRACVAL   (0x2c)
#define UART_UINTP      (0x30)
#define UART_UINTS      (0x34)
#define UART_UINTM      (0x38) // interrupt mask register, protect with uart_spinlock
#define UART_UFLT_CONF  (0x40)
#define UART_FIFO_DEPTH (0xdc)
// clang-format on

#define UARTREG(base, reg) (*REG32((base) + (reg)))
#define UARTREG_RMW(base, reg, mask, val)                        \
  do {                                                           \
    UARTREG(base, reg) = (UARTREG(base, reg) & ~(mask)) | (val); \
  } while (0)

const size_t RXBUF_SIZE = 256;

namespace {

// values read from zbi
vaddr_t uart_base = 0;
uint32_t uart_irq = 0;

Cbuf uart_rx_buf;

// Tx driven irq:
// NOTE: For the motmot, txim is the "ready to transmit" interrupt. So we must
// mask it when we no longer care about it and unmask it when we start txing.
bool uart_tx_irq_enabled = false;
uint32_t uart_tx_fifo_size = 0;
uint32_t uart_rx_fifo_size = 0;
AutounsignalEvent uart_dputc_event{true};

// It's important to ensure that no other locks are acquired while holding this lock.  This lock
// is needed for the printf and panic code paths, and printing and panicking must be safe while
// holding (almost) any lock.
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

// The UINTM register is contended from both IRQ and threaded mode, so protect
// accesses via the uart_spinlock.
static inline void motmot_uart_mask_tx() TA_REQ(uart_spinlock::Get()) {
  uartreg_or_eq(uart_base, UART_UINTM, (1 << 2));  // txd
}
static inline void motmot_uart_unmask_tx() TA_REQ(uart_spinlock::Get()) {
  uartreg_and_eq(uart_base, UART_UINTM, ~(1 << 2));  // txd
}
static inline void motmot_uart_mask_rx() TA_REQ(uart_spinlock::Get()) {
  uartreg_or_eq(uart_base, UART_UINTM, (1 << 0));  // rxd
}
static inline void motmot_uart_unmask_rx() TA_REQ(uart_spinlock::Get()) {
  uartreg_and_eq(uart_base, UART_UINTM, ~(1 << 0));  // rxd
}

static void motmot_uart_irq(void* arg) {
  // read interrupt status
  uint32_t isr = UARTREG(uart_base, UART_UINTP);

  LTRACEF("irq UINTP %#x UINTS %#x ", isr, UARTREG(uart_base, UART_UINTS));
  LTRACEF("UTRSTAT %#x\n", UARTREG(uart_base, UART_UTRSTAT));

  uint32_t pending_ack = 0;  // accumulate pending writes to UINTP at the end

  if (isr & (1 << 0)) {  // rxd
    // while fifo is not empty, read chars out of it
    while ((UARTREG(uart_base, UART_UFSTAT) & (0x1ff)) != 0) {  // uart fifo level
      LTRACEF("fstat %#x\n", UARTREG(uart_base, UART_UFSTAT));
      // if we're out of rx buffer, mask the irq instead of handling it
      {
        // This critical section is paired with the one in |motmot_uart_getc|
        // where RX is unmasked. This is necessary to avoid the following race
        // condition:
        //
        // Assume we have two threads, a reader R and a writer W, and the
        // buffer is full. For simplicity, let us assume the buffer size is 1;
        // the same process applies with a larger buffer and more readers.
        //
        // W: Observes the buffer is full.
        // R: Reads a character. The buffer is now empty.
        // R: Unmasks RX.
        // W: Masks RX.
        //
        // At this point, we have an empty buffer and RX interrupts are masked -
        // we're stuck! Thus, to avoid this, we acquire the spinlock before
        // checking if the buffer is full, and release after (conditionally)
        // masking RX interrupts. By pairing this with the acquisition of the
        // same lock around unmasking RX interrupts, we prevent the writer above
        // from being interrupted by a read-and-unmask.
        Guard<MonitoredSpinLock, NoIrqSave> guard{uart_spinlock::Get(), SOURCE_TAG};
        if (uart_rx_buf.Full()) {
          LTRACEF("out of buf, masking rx\n");
          motmot_uart_mask_rx();
          break;
        }
      }

      // See if there are any pending errors queued up in the error stack.
      // The hardware keeps a parallel stack of pending errors next to the RX fifo
      // with the idea that the error only rises to the surface when the character
      // that it was triggered on is the current top of the rx stack. Thusly we
      // need to read the error status register on every char and make sure we're
      // not actually looking at a fouled read.
      //
      // It's a bit unclear, but it seems that overrun and break detects are somewhat
      // independent of the character in the fifo itself, but are really triggered
      // at the boundary between it and the next character, so only discard fifo reads
      // for framing and parity errors.
      bool discard_char = false;
      uint32_t err = UARTREG(uart_base, UART_UERSTAT);
      if (unlikely(err & 0b1111)) {
        if (err & (1 << 0)) {  // overrun error
          // not much we can do except log and move on
          printf("UART: rx overrun\n");
        }
        if (err & (1 << 1)) {  // parity error
          printf("UART: rx parity\n");
          // discard the next char
          discard_char = true;
        }
        if (err & (1 << 2)) {  // framing error
          printf("UART: rx frame err\n");
          // discard the next char
          discard_char = true;
        }
        if (err & (1 << 3)) {  // break detect
          printf("UART: brk\n");
        }
      }

      char c = static_cast<char>(UARTREG(uart_base, UART_URXH));
      if (!discard_char) {
        LTRACEF("%#hhx in cbuf\n", c);
        uart_rx_buf.WriteChar(c);
      }
    }
    pending_ack |= (1 << 0);  // clear rxd
  }

  if (uart_tx_irq_enabled) {
    if (isr & (1 << 2)) {       // txd
      pending_ack |= (1 << 2);  // clear txd

      // Wake up any waiters in uart_dputs.
      uart_dputc_event.Signal();

      // Mask the TX irq, uart_dputs will unmask if necessary.
      {
        Guard<MonitoredSpinLock, NoIrqSave> guard{uart_spinlock::Get(), SOURCE_TAG};
        motmot_uart_mask_tx();
      }
    }
  }

  // ack any pending irqs
  if (pending_ack) {
    UARTREG(uart_base, UART_UINTP) = pending_ack;
  }
}

static int motmot_uart_getc(bool wait) {
  // RX irq based
  zx::status<char> result = uart_rx_buf.ReadChar(wait);
  if (result.is_ok()) {
    {
      // See the comment on the critical section in |motmot_uart_irq|.
      Guard<MonitoredSpinLock, IrqSave> guard{uart_spinlock::Get(), SOURCE_TAG};
      motmot_uart_unmask_rx();
    }
    return result.value();
  }

  return result.error_value();
}

// panic-time getc/putc
static void motmot_uart_pputc(char c) {
  if (c == '\n') {
    motmot_uart_pputc('\r');
  }

  // spin while fifo is full
  while (UARTREG(uart_base, UART_UFSTAT) & (1 << 24))  // tx fifo full
    ;
  UARTREG(uart_base, UART_UTXH) = c;
}

static int motmot_uart_pgetc() {
  for (;;) {
    if ((UARTREG(uart_base, UART_UFSTAT) & (0x1ff)) != 0) {  // rx fifo count
      // read and discard character if framing or parity error is queued
      uint32_t err = UARTREG(uart_base, UART_UERSTAT);
      if (err & 0b0110) {  // framing and parity error
        volatile uint32_t discard __UNUSED = UARTREG(uart_base, UART_URXH);
        continue;
      }

      return UARTREG(uart_base, UART_URXH);
    } else {
      return -1;
    }
  }
}

static void motmot_uart_dputs(const char* str, size_t len, bool block) {
  bool copied_CR = false;

  if (!uart_tx_irq_enabled) {
    block = false;
  }

  while (len > 0) {
    bool wait = false;
    size_t to_write = 0;

    // Acquire the main uart spinlock once every iteration to try to cap the worst
    // case time holding it. If a large string is passed, for example, this routine
    // will write up to 64 bytes at a time into the fifo per iteration, dropping and
    // reacquiring the spinlock every cycle.
    Guard<MonitoredSpinLock, IrqSave> guard{uart_spinlock::Get(), SOURCE_TAG};

    uint32_t ufstat = UARTREG(uart_base, UART_UFSTAT);
    if (ufstat & (1 << 24)) {  // tx fifo full
      // Is the FIFO completely full? if so, block or spin at the end of the loop
      wait = true;
    } else {
      // Compute how much space we have in the fifo and put up to that many
      // characters in.
      uint32_t used_fifo = BITS_SHIFT(ufstat, 23, 16);  // tx fifo count
      size_t remaining_fifo = uart_tx_fifo_size - used_fifo;

      to_write = ktl::min(len, remaining_fifo);
    }

    // stuff up to to_write number of chars into the fifo
    for (size_t i = 0; i < to_write; i++) {
      if (!copied_CR && *str == '\n') {
        copied_CR = true;
        UARTREG(uart_base, UART_UTXH) = '\r';
      } else {
        copied_CR = false;
        UARTREG(uart_base, UART_UTXH) = *str++;
        len--;
      }
    }

    // If at the end of the loop we've decided to wait, block or spin. Otherwise loop
    // around.
    if (wait) {
      if (block) {
        // Unmask Tx interrupts before we block on the event. The TX irq handler
        // will signal the event when the fifo falls below a threshold.
        motmot_uart_unmask_tx();

        // drop the spinlock before waiting
        guard.Release();
        uart_dputc_event.Wait();
      } else {
        // drop the spinlock before yielding
        guard.Release();
        arch::Yield();
      }
    }

    // Note spinlock will be dropped and reaquired around this loop
  }
}

static void motmot_uart_start_panic() { uart_tx_irq_enabled = false; }

static const struct pdev_uart_ops uart_ops = {
    .getc = motmot_uart_getc,
    .pputc = motmot_uart_pputc,
    .pgetc = motmot_uart_pgetc,
    .start_panic = motmot_uart_start_panic,
    .dputs = motmot_uart_dputs,
};

void MotmotUartInitEarly(const zbi_dcfg_simple_t& config) {
  ASSERT(config.mmio_phys != 0);
  ASSERT(config.irq != 0);

  uart_base = periph_paddr_to_vaddr(config.mmio_phys);
  ASSERT(uart_base != 0);
  uart_irq = config.irq;

  UARTREG(uart_base, UART_ULCON) = (3 << 0);  // no parity, one stop bit, 8 bit
  UARTREG(uart_base, UART_UMCON) = 0;         // no auto flow control

  // read the tx and rx fifo size, useful later
  uint32_t fifo_depth = UARTREG(uart_base, UART_FIFO_DEPTH);
  uart_tx_fifo_size = BITS_SHIFT(fifo_depth, 24, 16);
  uart_rx_fifo_size = BITS(fifo_depth, 8, 0);

  // sanity check the sizes in case hardware returns something bogus
  if (uart_tx_fifo_size == 0 || uart_tx_fifo_size > 256) {
    uart_tx_fifo_size = 1;
  }
  if (uart_rx_fifo_size == 0 || uart_rx_fifo_size > 256) {
    uart_rx_fifo_size = 1;
  }

  pdev_register_uart(&uart_ops);
}

void MotmotUartInitLate() {
  // create circular buffer to hold received data
  uart_rx_buf.Initialize(RXBUF_SIZE, malloc(RXBUF_SIZE));

  // register IRQ handler
  zx_status_t status = register_int_handler(uart_irq, &motmot_uart_irq, nullptr);
  DEBUG_ASSERT(status == ZX_OK);

  // mask all irqs
  UARTREG(uart_base, UART_UINTM) = 0xf;  // mask CTS, TX, error, RX

  // clear all irqs
  UARTREG(uart_base, UART_UINTP) = 0xf;  // clear CTX, TX, error, RX

  // disable fifos, set tx/rx threshold to minimum
  UARTREG(uart_base, UART_UFCON) = 0;

  // reset rx fifo
  uartreg_or_eq(uart_base, UART_UFCON, (1 << 1));

  // wait for it to clear
  while (UARTREG(uart_base, UART_UFCON) & (1 << 1))
    ;

  // enable fifos
  uartreg_or_eq(uart_base, UART_UFCON, (1 << 0));

  // enable receive
  // clang-format off
  UARTREG_RMW(uart_base, UART_UCON,
      (0xf << 12) | (1 << 11) | (3 << 0),
      (3 << 12)    // default rx timeout interval
      | (0 << 11)  // disable rx timeout when rx fifo empty
      | (1 << 7)   // rx timeout enable
      | (1 << 6)   // rx interrupt enable
      | (1 << 0)); // rx enable interrupt mode
  // clang-format on

  LTRACEF("UART: FIFO_DEPTH %#x\n", UARTREG(uart_base, UART_FIFO_DEPTH));
  LTRACEF("UCON %#x\n", UARTREG(uart_base, UART_UCON));
  LTRACEF("UFCON %#x\n", UARTREG(uart_base, UART_UFCON));
  LTRACEF("UMCON %#x\n", UARTREG(uart_base, UART_UMCON));
  LTRACEF("UERSTAT %#x\n", UARTREG(uart_base, UART_UERSTAT));

  // unmask rx interrupt
  {
    Guard<MonitoredSpinLock, IrqSave> guard{uart_spinlock::Get(), SOURCE_TAG};
    motmot_uart_unmask_rx();
  }

  // level triggered irq
  configure_interrupt(uart_irq, IRQ_TRIGGER_MODE_LEVEL, IRQ_POLARITY_ACTIVE_HIGH);

  // enable interrupt
  unmask_interrupt(uart_irq);

  // use PIO driven TX if bypassing debuglog
  if (dlog_bypass() == true) {
    uart_tx_irq_enabled = false;
  } else {
    // start up tx driven output
    printf("UART: started IRQ driven TX\n");
    uart_tx_irq_enabled = true;
  }

  printf("UART: rx fifo len %u tx fifo len %u\n", uart_rx_fifo_size, uart_tx_fifo_size);
}
