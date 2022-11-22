// Copyright 2022 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <lib/cbuf.h>
#include <lib/debuglog.h>
#include <lib/zircon-internal/macros.h>
#include <lib/zx/result.h>
#include <reg.h>
#include <stdio.h>
#include <string.h>
#include <trace.h>
#include <zircon/boot/driver-config.h>

#include <arch/arm64/periphmap.h>
#include <dev/interrupt.h>
#include <dev/uart.h>
#include <dev/uart/imx/init.h>
#include <kernel/lockdep.h>
#include <kernel/thread.h>
#include <pdev/uart.h>

// clang-format off

// UART register offsets
#define URXD    (0x0)
#define UTXD    (0x40)
#define UCR1    (0x80)
#define UCR2    (0x84)
#define UCR3    (0x88)
#define UCR4    (0x8c)
#define UFCR    (0x90)
#define USR1    (0x94)
#define USR2    (0x98)
#define UESC    (0x9c)
#define UTIM    (0xa0)
#define UBIR    (0xa4)
#define UBMR    (0xa8)
#define UBRC    (0xac)
#define ONEMS   (0xb0)
#define UTS     (0xb4)
#define UMCR    (0xb8)

// UART register bits
#define URXD_RX_DATA_MASK       (0xff)
#define URXD_RX_DATA_SHIFT      (0)

#define URXD_PRERR_MASK         (0x400)
#define URXD_PRERR_SHIFT        (10)

#define URXD_BRK_MASK           (0x800)
#define URXD_BRK_SHIFT          (11)

#define URXD_FRMERR_MASK        (0x1000)
#define URXD_FRMERR_SHIFT       (12)

#define URXD_OVRRUN_MASK        (0x2000)
#define URXD_OVRRUN_SHIFT       (13)

#define URXD_ERR_MASK           (0x4000)
#define URXD_ERR_SHIFT          (14)

#define URXD_CHARRDY_MASK       (0x8000)
#define URXD_CHARRDY_SHIFT      (15)

#define UTXD_TX_DATA_MASK       (0xff)
#define UTXD_TX_DATA_SHIFT      (0)

#define UCR1_UARTEN_MASK        (0x1)
#define UCR1_UARTEN_SHIFT       (0)

#define UCR1_DOZE_MASK          (0x2)
#define UCR1_DOZE_SHIFT         (1)

#define UCR1_ATDMAEN_MASK       (0x4)
#define UCR1_ATDMAEN_SHIFT      (2)

#define UCR1_TXDMAEN_MASK       (0x8)
#define UCR1_TXDMAEN_SHIFT      (3)

#define UCR1_SNDBRK_MASK        (0x10)
#define UCR1_SNDBRK_SHIFT       (4)

#define UCR1_RTSDEN_MASK        (0x20)
#define UCR1_RTSDEN_SHIFT       (5)

#define UCR1_TXMPTYEN_MASK      (0x40)
#define UCR1_TXMPTYEN_SHIFT     (6)

#define UCR1_IREN_MASK          (0x80)
#define UCR1_IREN_SHIFT         (7)

#define UCR1_RXDMAEN_MASK       (0x100)
#define UCR1_RXDMAEN_SHIFT      (8)

#define UCR1_RRDYEN_MASK        (0x200)
#define UCR1_RRDYEN_SHIFT       (9)

#define UCR1_ICD_MASK           (0xc00)
#define UCR1_ICD_SHIFT          (10)

#define UCR1_IDEN_MASK          (0x1000)
#define UCR1_IDEN_SHIFT         (12)

#define UCR1_TRDYEN_MASK        (0x2000)
#define UCR1_TRDYEN_SHIFT       (13)

#define UCR1_ADBR_MASK          (0x4000)
#define UCR1_ADBR_SHIFT         (14)

#define UCR1_ADEN_MASK          (0x8000)
#define UCR1_ADEN_SHIFT         (15)

#define UCR2_SRST_MASK          (0x1)
#define UCR2_SRST_SHIFT         (0)

#define UCR2_RXEN_MASK          (0x2)
#define UCR2_RXEN_SHIFT         (1)

#define UCR2_TXEN_MASK          (0x4)
#define UCR2_TXEN_SHIFT         (2)

#define UCR2_ATEN_MASK          (0x8)
#define UCR2_ATEN_SHIFT         (3)

#define UCR2_RTSEN_MASK         (0x10)
#define UCR2_RTSEN_SHIFT        (4)

#define UCR2_WS_MASK            (0x20)
#define UCR2_WS_SHIFT           (5)

#define UCR2_STPB_MASK          (0x40)
#define UCR2_STPB_SHIFT         (6)

#define UCR2_PROE_MASK          (0x80)
#define UCR2_PROE_SHIFT         (7)

#define UCR2_PREN_MASK          (0x100)
#define UCR2_PREN_SHIFT         (8)

#define UCR2_RTEC_MASK          (0x600)
#define UCR2_RTEC_SHIFT         (9)

#define UCR2_ESCEN_MASK         (0x800)
#define UCR2_ESCEN_SHIFT        (11)

#define UCR2_CTS_MASK           (0x1000)
#define UCR2_CTS_SHIFT          (12)

#define UCR2_CTSC_MASK          (0x2000)
#define UCR2_CTSC_SHIFT         (13)

#define UCR2_IRTS_MASK          (0x4000)
#define UCR2_IRTS_SHIFT         (14)

#define UCR2_ESCI_MASK          (0x8000)
#define UCR2_ESCI_SHIFT         (15)

#define UCR3_ACIEN_MASK         (0x1)
#define UCR3_ACIEN_SHIFT        (0)

#define UCR3_INVT_MASK          (0x2)
#define UCR3_INVT_SHIFT         (1)

#define UCR3_RXDMUXSEL_MASK     (0x4)
#define UCR3_RXDMUXSEL_SHIFT    (2)

#define UCR3_DTRDEN_MASK        (0x8)
#define UCR3_DTRDEN_SHIFT       (3)

#define UCR3_AWAKEN_MASK        (0x10)
#define UCR3_AWAKEN_SHIFT       (4)

#define UCR3_AIRINTEN_MASK      (0x20)
#define UCR3_AIRINTEN_SHIFT     (5)

#define UCR3_RXDSEN_MASK        (0x40)
#define UCR3_RXDSEN_SHIFT       (6)

#define UCR3_ADNIMP_MASK        (0x80)
#define UCR3_ADNIMP_SHIFT       (7)

#define UCR3_RI_MASK            (0x100)
#define UCR3_RI_SHIFT           (8)

#define UCR3_DCD_MASK           (0x200)
#define UCR3_DCD_SHIFT          (9)

#define UCR3_DSR_MASK           (0x400)
#define UCR3_DSR_SHIFT          (10)

#define UCR3_FRAERREN_MASK      (0x800)
#define UCR3_FRAERREN_SHIFT     (11)

#define UCR3_PARERREN_MASK      (0x1000)
#define UCR3_PARERREN_SHIFT     (12)

#define UCR3_DTREN_MASK         (0x2000)
#define UCR3_DTREN_SHIFT        (13)

#define UCR3_DPEC_MASK          (0xc000)
#define UCR3_DPEC_SHIFT         (14)

#define UCR4_DREN_MASK          (0x1)
#define UCR4_DREN_SHIFT         (0)

#define UCR4_OREN_MASK          (0x2)
#define UCR4_OREN_SHIFT         (1)

#define UCR4_BKEN_MASK          (0x4)
#define UCR4_BKEN_SHIFT         (2)

#define UCR4_TCEN_MASK          (0x8)
#define UCR4_TCEN_SHIFT         (3)

#define UCR4_LPBYP_MASK         (0x10)
#define UCR4_LPBYP_SHIFT        (4)

#define UCR4_IRSC_MASK          (0x20)
#define UCR4_IRSC_SHIFT         (5)

#define UCR4_IDDMAEN_MASK       (0x40)
#define UCR4_IDDMAEN_SHIFT      (6)

#define UCR4_WKEN_MASK          (0x80)
#define UCR4_WKEN_SHIFT         (7)

#define UCR4_ENIRI_MASK         (0x100)
#define UCR4_ENIRI_SHIFT        (8)

#define UCR4_INVR_MASK          (0x200)
#define UCR4_INVR_SHIFT         (9)

#define UCR4_CTSTL_MASK         (0xfc00)
#define UCR4_CTSTL_SHIFT        (10)

#define UFCR_RXTL_MASK          (0x3f)
#define UFCR_RXTL_SHIFT         (0)
#define UFCR_RXTL(x)            ((x << UFCR_RXTL_SHIFT) & UFCR_RXTL_MASK)

#define UFCR_DCEDTE_MASK        (0x40)
#define UFCR_DCEDTE_SHIFT       (6)

#define UFCR_RFDIV_MASK         (0x380)
#define UFCR_RFDIV_SHIFT        (7)

#define UFCR_TXTL_MASK          (0xfc00)
#define UFCR_TXTL_SHIFT         (10)
#define UFCR_TXTL(x)            ((x << UFCR_TXTL_SHIFT) & UFCR_TXTL_MASK)

#define USR1_SAD_MASK           (0x8)
#define USR1_SAD_SHIFT          (3)

#define USR1_AWAKE_MASK         (0x10)
#define USR1_AWAKE_SHIFT        (4)

#define USR1_AIRINT_MASK        (0x20)
#define USR1_AIRINT_SHIFT       (5)

#define USR1_RXDS_MASK          (0x40)
#define USR1_RXDS_SHIFT         (6)

#define USR1_DTRD_MASK          (0x80)
#define USR1_DTRD_SHIFT         (7)

#define USR1_AGTIM_MASK         (0x100)
#define USR1_AGTIM_SHIFT        (8)

#define USR1_RRDY_MASK          (0x200)
#define USR1_RRDY_SHIFT         (9)

#define USR1_FRAMERR_MASK       (0x400)
#define USR1_FRAMERR_SHIFT      (10)

#define USR1_ESCF_MASK          (0x800)
#define USR1_ESCF_SHIFT         (11)

#define USR1_RTSD_MASK          (0x1000)
#define USR1_RTSD_SHIFT         (12)

#define USR1_TRDY_MASK          (0x2000)
#define USR1_TRDY_SHIFT         (13)

#define USR1_RTSS_MASK          (0x4000)
#define USR1_RTSS_SHIFT         (14)

#define USR1_PARITYERR_MASK     (0x8000)
#define USR1_PARITYERR_SHIFT    (15)

#define USR2_RDR_MASK           (0x1)
#define USR2_RDR_SHIFT          (0)

#define USR2_ORE_MASK           (0x2)
#define USR2_ORE_SHIFT          (1)

#define USR2_BRCD_MASK          (0x4)
#define USR2_BRCD_SHIFT         (2)

#define USR2_TXDC_MASK          (0x8)
#define USR2_TXDC_SHIFT         (3)

#define USR2_RTSF_MASK          (0x10)
#define USR2_RTSF_SHIFT         (4)

#define USR2_DCDIN_MASK         (0x20)
#define USR2_DCDIN_SHIFT        (5)

#define USR2_DCDDELT_MASK       (0x40)
#define USR2_DCDDELT_SHIFT      (6)

#define USR2_WAKE_MASK          (0x80)
#define USR2_WAKE_SHIFT         (7)

#define USR2_IRINT_MASK         (0x100)
#define USR2_IRINT_SHIFT        (8)

#define USR2_RIIN_MASK          (0x200)
#define USR2_RIIN_SHIFT         (9)

#define USR2_RIDELT_MASK        (0x400)
#define USR2_RIDELT_SHIFT       (10)

#define USR2_ACST_MASK          (0x800)
#define USR2_ACST_SHIFT         (11)

#define USR2_IDLE_MASK          (0x1000)
#define USR2_IDLE_SHIFT         (12)

#define USR2_DTRF_MASK          (0x2000)
#define USR2_DTRF_SHIFT         (13)

#define USR2_TXFE_MASK          (0x4000)
#define USR2_TXFE_SHIFT         (14)

#define USR2_ADET_MASK          (0x8000)
#define USR2_ADET_SHIFT         (15)

#define UESC_ESC_CHAR_MASK      (0xff)
#define UESC_ESC_CHAR_SHIFT     (0)

#define UTIM_TIM_MASK           (0xfff)
#define UTIM_TIM_SHIFT          (0)

#define UBIR_INC_MASK           (0xffff)
#define UBIR_INC_SHIFT          (0)

#define UBMR_MOD_MASK           (0xffff)
#define UBMR_MOD_SHIFT          (0)

#define UBRC_BCNT_MASK          (0xffff)
#define UBRC_BCNT_SHIFT         (0)

#define ONEMS_ONEMS_MASK        (0xffffff)
#define ONEMS_ONEMS_SHIFT       (0)

#define UTS_SOFTRST_MASK        (0x1)
#define UTS_SOFTRST_SHIFT       (0)

#define UTS_RXFULL_MASK         (0x8)
#define UTS_RXFULL_SHIFT        (3)

#define UTS_TXFULL_MASK         (0x10)
#define UTS_TXFULL_SHIFT        (4)

#define UTS_RXEMPTY_MASK        (0x20)
#define UTS_RXEMPTY_SHIFT       (5)

#define UTS_TXEMPTY_MASK        (0x40)
#define UTS_TXEMPTY_SHIFT       (6)

#define UTS_RXDBG_MASK          (0x200)
#define UTS_RXDBG_SHIFT         (9)

#define UTS_LOOPIR_MASK         (0x400)
#define UTS_LOOPIR_SHIFT        (10)

#define UTS_DBGEN_MASK          (0x800)
#define UTS_DBGEN_SHIFT         (11)

#define UTS_LOOP_MASK           (0x1000)
#define UTS_LOOP_SHIFT          (12)

#define UTS_FRCPERR_MASK        (0x2000)
#define UTS_FRCPERR_SHIFT       (13)

#define UMCR_MDEN_MASK          (0x1)
#define UMCR_MDEN_SHIFT         (0)

#define UMCR_SLAM_MASK          (0x2)
#define UMCR_SLAM_SHIFT         (1)

#define UMCR_TXB8_MASK          (0x4)
#define UMCR_TXB8_SHIFT         (2)

#define UMCR_SADEN_MASK         (0x8)
#define UMCR_SADEN_SHIFT        (3)

#define UMCR_SLADDR_MASK        (0xff00)
#define UMCR_SLADDR_SHIFT       (8)

#define RXBUF_SIZE  (32)

#define UARTREG(base, reg)  (*(volatile uint32_t*)((base)  + (reg)))

// clang-format on

static vaddr_t imx_uart_base = 0;
static uint32_t imx_uart_irq = 0;
static Cbuf uart_rx_buf;
static bool uart_tx_irq_enabled = false;
static AutounsignalEvent uart_dputc_event{true};

namespace {
// It's important to ensure that no other locks are acquired while holding this lock.  This lock is
// needed for the printf and panic code paths, and printing and panicking must be safe while holding
// (almost) any lock.
DECLARE_SINGLETON_SPINLOCK_WITH_TYPE(uart_spinlock, MonitoredSpinLock);
}  // namespace

static inline void imx_uart_mask_tx() TA_REQ(uart_spinlock::Get()) {
  UARTREG(imx_uart_base, UCR1) &= ~UCR1_TRDYEN_MASK;
}

static inline void imx_uart_unmask_tx() TA_REQ(uart_spinlock::Get()) {
  UARTREG(imx_uart_base, UCR1) |= UCR1_TRDYEN_MASK;
}

static inline void imx_uart_mask_rx() TA_REQ(uart_spinlock::Get()) {
  UARTREG(imx_uart_base, UCR1) &= ~UCR1_RRDYEN_MASK;
}

static inline void imx_uart_unmask_rx() TA_REQ(uart_spinlock::Get()) {
  UARTREG(imx_uart_base, UCR1) |= UCR1_RRDYEN_MASK;
}

static void imx_uart_irq_handler(void* arg) {
  while ((UARTREG(imx_uart_base, USR1) & USR1_RRDY_MASK)) {
    // if we're out of rx buffer, mask the irq instead of handling it
    //
    // This critical section is paired with the one in |imx_uart_getc|
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
      imx_uart_mask_rx();
      break;
    }

    char c = (char)(UARTREG(imx_uart_base, URXD) & URXD_RX_DATA_MASK) >> URXD_RX_DATA_SHIFT;
    uart_rx_buf.WriteChar(c);
  }

  if ((UARTREG(imx_uart_base, UCR1) & UCR1_TRDYEN_MASK) &&
      (UARTREG(imx_uart_base, USR1) & USR1_TRDY_MASK)) {
    // Signal if anyone is waiting to TX
    uart_dputc_event.Signal();
    {
      // It's important we're not holding the |uart_spinlock| while calling
      // |Event::Signal|.  Otherwise we'd create an invalid lock dependency
      // between |uart_spinlock| and any locks |Event::Signal| may acquire.
      Guard<MonitoredSpinLock, NoIrqSave> guard{uart_spinlock::Get(), SOURCE_TAG};
      // Mask the TX irq, imx_uart_dputs will unmask if necessary.
      imx_uart_mask_tx();
    }
  }
}

static void imx_uart_pputc(char c) {
  // Wait for the space in TxFIFO
  while ((UARTREG(imx_uart_base, USR1) & USR1_TRDY_MASK) == 0)
    ;
  UARTREG(imx_uart_base, UTXD) = (uint32_t)c & UTXD_TX_DATA_MASK;
}

static int imx_uart_pgetc() {
  // Wait for receive data ready, indicates that at least 1
  // character is received and written to the RxFIFO.
  if ((UARTREG(imx_uart_base, USR2) & USR2_RDR_MASK) == 0) {
    return ZX_ERR_INTERNAL;
  }
  return (int)((UARTREG(imx_uart_base, URXD) & URXD_RX_DATA_MASK) >> URXD_RX_DATA_SHIFT);
}

static int imx_uart_getc(bool wait) {
  zx::result<char> result = uart_rx_buf.ReadChar(wait);
  if (result.is_ok()) {
    {
      // See the comment on the critical section in |imx_uart_irq_handler|.
      Guard<MonitoredSpinLock, IrqSave> guard{uart_spinlock::Get(), SOURCE_TAG};
      imx_uart_unmask_rx();
    }
    return result.value();
  }

  return result.error_value();
}

static void imx_uart_dputs(const char* str, size_t len, bool block) {
  bool copied_CR = false;

  if (!uart_tx_irq_enabled) {
    block = false;
  }

  Guard<MonitoredSpinLock, IrqSave> guard{uart_spinlock::Get(), SOURCE_TAG};

  while (len > 0) {
    while ((UARTREG(imx_uart_base, USR1) & USR1_TRDY_MASK) == 0) {
      if (block) {
        imx_uart_unmask_tx();
      }
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
      UARTREG(imx_uart_base, UTXD) = '\r';
    } else {
      copied_CR = false;
      UARTREG(imx_uart_base, UTXD) = *str++;
      len--;
    }
  }
}

static void imx_uart_start_panic() { uart_tx_irq_enabled = false; }

static const struct pdev_uart_ops imx_uart_ops = {
    .getc = imx_uart_getc,
    .pputc = imx_uart_pputc,
    .pgetc = imx_uart_pgetc,
    .start_panic = imx_uart_start_panic,
    .dputs = imx_uart_dputs,
};

void ImxUartInitEarly(const zbi_dcfg_simple_t& config) {
  ASSERT(config.mmio_phys);
  ASSERT(config.irq);

  imx_uart_base = periph_paddr_to_vaddr(config.mmio_phys);
  ASSERT(imx_uart_base);
  imx_uart_irq = config.irq;

  pdev_register_uart(&imx_uart_ops);
}

void ImxUartInitLate() {
  uint32_t reg;

  // Initialize circular buffer to hold received data.
  uart_rx_buf.Initialize(RXBUF_SIZE, malloc(RXBUF_SIZE));

  // register uart irq
  register_int_handler(imx_uart_irq, imx_uart_irq_handler, NULL);

  // Set tx watermark to 2, rx watermark to 1
  reg = UARTREG(imx_uart_base, UFCR);
  reg &= ~(UFCR_TXTL_MASK | UFCR_RXTL_MASK);
  reg |= UFCR_TXTL(2) | UFCR_RXTL(1);
  UARTREG(imx_uart_base, UFCR) = reg;

  // Enable Rx/Tx
  UARTREG(imx_uart_base, UCR2) |= (UCR2_TXEN_MASK | UCR2_RXEN_MASK);

  // Enable Rx ready interrupt
  UARTREG(imx_uart_base, UCR1) |= UCR1_RRDYEN_MASK;

  if (dlog_bypass()) {
    uart_tx_irq_enabled = false;
  } else {
    uart_tx_irq_enabled = true;
    // Enable Tx ready interrupt
    UARTREG(imx_uart_base, UCR1) |= UCR1_TRDYEN_MASK;
  }

  // enable interrupts
  unmask_interrupt(imx_uart_irq);
}
