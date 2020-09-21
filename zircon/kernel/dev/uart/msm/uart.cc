// Copyright 2019 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

// TODO(gkalsi): Unify the two UART codepaths and use the port parameter to
// select between the real uart and the miniuart.

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

#define UART_MR1                            0x0000
#define UART_MR1_RX_RDY_CTL                 (1 << 7)

#define UART_MR2                            0x0004
#define UART_DM_IPR                         0x0018
#define UART_DM_DMRX                        0x0034
#define UART_DM_N0_CHARS_FOR_TX             0x0040

#define UART_DM_SR                          0x00A4
#define UART_DM_SR_RXRDY                    (1 << 0)
#define UART_DM_SR_RXFULL                   (1 << 1)
#define UART_DM_SR_TXRDY                    (1 << 2)
#define UART_DM_SR_TXEMT                    (1 << 3)
#define UART_DM_SR_OVERRUN                  (1 << 4)
#define UART_DM_SR_PAR_FRAME_ERR            (1 << 5)
#define UART_DM_SR_RX_BREAK                 (1 << 6)
#define UART_DM_SR_HUNT_CHAR                (1 << 7)

#define UART_DM_CR                          0x00A8
#define UART_DM_CR_RX_EN                    (1 << 0)
#define UART_DM_CR_RX_DISABLE               (1 << 1)
#define UART_DM_CR_TX_EN                    (1 << 2)
#define UART_DM_CR_TX_DISABLE               (1 << 3)

#define UART_DM_CR_CMD_RESET_RX             (1 << 4)
#define UART_DM_CR_CMD_RESET_TX             (2 << 4)
#define UART_DM_CR_CMD_RESET_ERR            (3 << 4)
#define UART_DM_CR_CMD_RESET_BRK_CHG_INT    (4 << 4)
#define UART_DM_CR_CMD_START_BRK            (5 << 4)
#define UART_DM_CR_CMD_STOP_BRK             (6 << 4)
#define UART_DM_CR_CMD_RESET_CTS_N          (7 << 4)
#define UART_DM_CR_CMD_RESET_STALE_INT      (8 << 4)
#define UART_DM_CR_CMD_SET_RFR              (13 << 4)
#define UART_DM_CR_CMD_RESET_RFR            (14 << 4)
#define UART_DM_CR_CMD_CLEAR_TX_ERROR       (16 << 4)
#define UART_DM_CR_CMD_CLEAR_TX_DONE        (17 << 4)
#define UART_DM_CR_CMD_RESET_BRK_START_INT  (18 << 4)
#define UART_DM_CR_CMD_RESET_BRK_END_INT    (19 << 4)
#define UART_DM_CR_CMD_RESET_PAR_FRAME_ERR_INT (20 << 4)
#define UART_DM_CR_CMD_CLEAR_TX_WR_ERROR_IRQ (25 << 4)
#define UART_DM_CR_CMD_CLEAR_RX_RD_ERROR_IRQ (26 << 4)
#define UART_DM_CR_CMD_CLEAR_TX_COMP_IRQ    (27 << 4)
#define UART_DM_CR_CMD_CLEAR_WWT_IRQ        (28 << 4)
#define UART_DM_CR_CMD_CLEAR_NO_FINISH_CMD_VIO_IRQ (30 << 4)

#define UART_DM_CR_CMD_RESET_TX_READY       (3 << 8)
#define UART_DM_CR_CMD_FORCE_STALE          (4 << 8)
#define UART_DM_CR_CMD_ENABLE_STALE_EVENT   (5 << 8)
#define UART_DM_CR_CMD_DISABLE_STALE_EVENT  (6 << 8)

#define UART_DM_RXFS                        0x0050
#define UART_DM_RXFS_RX_BUFFER_STATE(r)     ((r >> 7) & 7)
#define UART_DM_RXFS_FIFO_STATE(r)          ((r >> 14) | (r & 0x3F))

#define UART_DM_MISR                        0x00AC
#define UART_DM_IMR                         0x00B0
#define UART_DM_ISR                         0x00B4

#define UART_IRQ_TXLEV                      (1 << 0)
#define UART_IRQ_RXHUNT                     (1 << 1)
#define UART_IRQ_RXBREAK_CHANGE             (1 << 2)
#define UART_IRQ_RXSTALE                    (1 << 3)
#define UART_IRQ_RXLEV                      (1 << 4)
#define UART_IRQ_DELTA_CTS                  (1 << 5)
#define UART_IRQ_CURRENT_CTS                (1 << 6)
#define UART_IRQ_TX_READY                   (1 << 7)
#define UART_IRQ_TX_ERROR                   (1 << 8)
#define UART_IRQ_TX_DONE                    (1 << 9)
#define UART_IRQ_RXBREAK_START              (1 << 10)
#define UART_IRQ_RXBREAK_END                (1 << 11)
#define UART_IRQ_PAR_FRAME_ERR_IRQ          (1 << 12)
#define UART_IRQ_TX_WR_ERROR_IRQ            (1 << 13)
#define UART_IRQ_RX_RD_ERROR_IRQ            (1 << 14)
#define UART_IRQ_TXCOMP_IRQ                 (1 << 15)
#define UART_IRQ_WWT_IRQ                    (1 << 16)
#define UART_IRQ_NO_FINISH_CMD_VIOL         (1 << 17)

#define UART_DM_TF                          0x0100
#define UART_DM_RF(n)                       (0x0140 + 4 * (n))

#define RXBUF_SIZE 128

// clang-format on

// values read from zbi
static uint64_t uart_base = 0;
static uint32_t uart_irq = 0;

static Cbuf uart_rx_buf;

static bool uart_tx_irq_enabled = false;
static AutounsignalEvent uart_dputc_event{true};
static AutounsignalEvent uart_txemt_event{true};

static SpinLock uart_spinlock;

static inline uint32_t uart_read(int offset) { return readl(uart_base + offset); }

static inline void uart_write(uint32_t val, int offset) { writel(val, uart_base + offset); }

static inline void yield(void) { __asm__ volatile("yield" ::: "memory"); }

// panic-time getc/putc
static void msm_pputc(char c) {
  // spin while fifo is full
  while (!(uart_read(UART_DM_SR) & UART_DM_SR_TXEMT)) {
    yield();
  }
  uart_write(UART_DM_CR_CMD_RESET_TX_READY, UART_DM_N0_CHARS_FOR_TX);
  uart_write(1, UART_DM_N0_CHARS_FOR_TX);
  uart_read(UART_DM_N0_CHARS_FOR_TX);

  // wait for TX ready
  while (!(uart_read(UART_DM_SR) & UART_DM_SR_TXRDY)) {
    yield();
  }

  uart_write(c, UART_DM_TF);
}

static int msm_pgetc(void) {
  Cbuf* rxbuf = &uart_rx_buf;

  char c;
  int count = 0;
  uint32_t val, rxfs, sr;
  char* bytes;

  // see if we have chars left from previous read
  zx::status<char> result = rxbuf->ReadChar(false);
  if (result.is_ok()) {
    return result.value();
  }

  if ((uart_read(UART_DM_SR) & UART_DM_SR_OVERRUN)) {
    uart_write(UART_DM_CR_CMD_RESET_ERR, UART_DM_CR);
  }

  do {
    rxfs = uart_read(UART_DM_RXFS);
    sr = uart_read(UART_DM_SR);
    count = UART_DM_RXFS_RX_BUFFER_STATE(rxfs);
    if (!(sr & UART_DM_SR_RXRDY) && !count) {
      return -1;
    }
  } while (count == 0);

  uart_write(UART_DM_CR_CMD_FORCE_STALE, UART_DM_CR);
  val = uart_read(UART_DM_RF(0));
  uart_read(UART_DM_RF(1));

  uart_write(UART_DM_CR_CMD_RESET_STALE_INT, UART_DM_CR);
  uart_write(0xffffff, UART_DM_DMRX);

  bytes = (char*)&val;
  c = bytes[0];

  // save remaining chars for next call
  for (int i = 1; i < count; i++) {
    rxbuf->WriteChar(bytes[i]);
  }

  return c;
}

static interrupt_eoi uart_irq_handler(void* arg) {
  uint32_t misr = uart_read(UART_DM_MISR);
  if (misr & UART_IRQ_TX_READY) {
    uart_txemt_event.SignalNoResched();
    uart_write(UART_DM_CR_CMD_RESET_TX_READY, UART_DM_CR);
  }
  if (misr & UART_IRQ_RXSTALE) {
    while (uart_read(UART_DM_SR) & UART_DM_SR_RXRDY) {
      uint32_t rxfs = uart_read(UART_DM_RXFS);
      // count is number of words in RX fifo that have data
      int count = UART_DM_RXFS_FIFO_STATE(rxfs);

      for (int i = 0; i < count; i++) {
        uint32_t val = uart_read(UART_DM_RF(0));
        char* bytes = (char*)&val;

        for (int j = 0; j < 4; j++) {
          // Unfortunately there is no documented way to get number of bytes in each word
          // so we just need to ignore zero bytes here.
          // Apparently this problem doesn't exist in DMA mode.
          char ch = bytes[j];
          if (ch) {
            uart_rx_buf.WriteChar(ch);
          } else {
            break;
          }
        }
      }
    }

    if (misr & UART_IRQ_RXSTALE) {
      uart_write(UART_DM_CR_CMD_RESET_STALE_INT, UART_DM_CR);
    }

    // ask to receive more
    uart_write(0xFFFFFF, UART_DM_DMRX);
    uart_write(UART_DM_CR_CMD_ENABLE_STALE_EVENT, UART_DM_CR);
  }
  return IRQ_EOI_DEACTIVATE;
}

static void msm_uart_init(const void* driver_data, uint32_t length) {
  uint32_t temp;

  // disable interrupts
  uart_write(0, UART_DM_IMR);

  uart_write(UART_DM_CR_TX_EN | UART_DM_CR_RX_EN, UART_DM_CR);
  uart_write(UART_DM_CR_CMD_RESET_TX, UART_DM_CR);
  uart_write(UART_DM_CR_CMD_RESET_RX, UART_DM_CR);
  uart_write(UART_DM_CR_CMD_RESET_ERR, UART_DM_CR);
  uart_write(UART_DM_CR_CMD_RESET_BRK_CHG_INT, UART_DM_CR);
  uart_write(UART_DM_CR_CMD_RESET_CTS_N, UART_DM_CR);
  uart_write(UART_DM_CR_CMD_SET_RFR, UART_DM_CR);
  uart_write(UART_DM_CR_CMD_CLEAR_TX_DONE, UART_DM_CR);

  uart_write(0xFFFFFF, UART_DM_DMRX);
  uart_write(UART_DM_CR_CMD_ENABLE_STALE_EVENT, UART_DM_CR);

  temp = uart_read(UART_MR1);
  temp |= UART_MR1_RX_RDY_CTL;
  uart_write(temp, UART_MR1);

  uart_rx_buf.Initialize(RXBUF_SIZE, malloc(RXBUF_SIZE));

  // enable RX and TX interrupts
  uart_write(UART_IRQ_RXSTALE | UART_IRQ_TX_READY, UART_DM_IMR);

  register_permanent_int_handler(uart_irq, &uart_irq_handler, nullptr);
  unmask_interrupt(uart_irq);
}

static int msm_getc(bool wait) {
  zx::status<char> result = uart_rx_buf.ReadChar(wait);
  if (result.is_ok()) {
    return result.value();
  } else {
    return result.error_value();
  }
}

static void msm_start_panic(void) { uart_tx_irq_enabled = false; }

static void msm_dputs(const char* str, size_t len, bool block, bool map_NL) {
  interrupt_saved_state_t state;
  bool copied_CR = false;

  if (!uart_tx_irq_enabled) {
    block = false;
  }
  uart_spinlock.AcquireIrqSave(state);

  while (len > 0) {
    // is FIFO full?
    while (!(uart_read(UART_DM_SR) & UART_DM_SR_TXRDY)) {
      uart_spinlock.ReleaseIrqRestore(state);
      if (block) {
        uart_dputc_event.Wait();
      } else {
        uart_txemt_event.Wait();
      }
      uart_spinlock.AcquireIrqSave(state);
    }
    if (*str == '\n' && map_NL && !copied_CR) {
      copied_CR = true;
      msm_pputc('\r');
    } else {
      copied_CR = false;
      msm_pputc(*str++);
      len--;
    }
  }
  uart_spinlock.ReleaseIrqRestore(state);
}

static const struct pdev_uart_ops uart_ops = {
    .getc = msm_getc,
    .pputc = msm_pputc,
    .pgetc = msm_pgetc,
    .start_panic = msm_start_panic,
    .dputs = msm_dputs,
};

static void msm_uart_init_early(const void* driver_data, uint32_t length) {
  ASSERT(length >= sizeof(dcfg_simple_t));
  auto driver = static_cast<const dcfg_simple_t*>(driver_data);
  uart_base = periph_paddr_to_vaddr(driver->mmio_phys);
  uart_irq = driver->irq;
  ASSERT(uart_base);
  ASSERT(uart_irq);

  pdev_register_uart(&uart_ops);
}

LK_PDEV_INIT(msm_uart_init_early, KDRV_MSM_UART, msm_uart_init_early, LK_INIT_LEVEL_PLATFORM_EARLY)
LK_PDEV_INIT(msm_uart_init, KDRV_MSM_UART, msm_uart_init, LK_INIT_LEVEL_PLATFORM)

// boot time hacked version to directly print
#if 0
#define UART_DM_N0_CHARS_FOR_TX 0x0040
#define UART_DM_CR_CMD_RESET_TX_READY (3 << 8)

#define UART_DM_SR 0x00A4
#define UART_DM_SR_TXRDY (1 << 2)
#define UART_DM_SR_TXEMT (1 << 3)

#define UART_DM_TF 0x0100

#define UARTREG(reg) (*(volatile uint32_t*)(0x078af000 + (reg)))

extern "C" void msm_putc(char c) {
    while (!(UARTREG(UART_DM_SR) & UART_DM_SR_TXEMT)) {
        ;
    }
    UARTREG(UART_DM_N0_CHARS_FOR_TX) = UART_DM_CR_CMD_RESET_TX_READY;
    UARTREG(UART_DM_N0_CHARS_FOR_TX) = 1;
    __UNUSED uint32_t foo = UARTREG(UART_DM_N0_CHARS_FOR_TX);

    // wait for TX ready
    while (!(UARTREG(UART_DM_SR) & UART_DM_SR_TXRDY))
        ;

    UARTREG(UART_DM_TF) = c;

    // wait for TX ready
    while (!(UARTREG(UART_DM_SR) & UART_DM_SR_TXRDY))
        ;
}

extern "C" void msm_print_hex(uint64_t value) {
    const char digits[16] = {'0', '1', '2', '3', '4', '5', '6', '7',
                             '8', '9', 'a', 'b', 'c', 'd', 'e', 'f'};
    for (int i = 60; i >= 0; i -= 4) {
        msm_putc(digits[(value >> i) & 0xf]);
    }

    msm_putc(' ');
}
#endif  // boot hack
