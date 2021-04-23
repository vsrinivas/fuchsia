// Copyright 2020 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <lib/arch/intrin.h>
#include <lib/cbuf.h>
#include <lib/debuglog.h>
#include <lib/zx/status.h>
#include <reg.h>
#include <stdio.h>
#include <trace.h>
#include <zircon/boot/driver-config.h>
#include <vm/physmap.h>

#include <dev/interrupt.h>
#include <dev/uart.h>
#include <kernel/thread.h>
#include <pdev/driver.h>
#include <pdev/uart.h>
#include <platform/debug.h>
#include <platform.h>
#include <sys/types.h>

// simple 16550 driver for the emulated serial port on qemu riscv virt machine

// clang-format off
#define UART_RBR   (0x0)
#define UART_THR   (0x0)
#define UART_IER   (0x1)
#define UART_IIR   (0x2)
#define UART_FCR   (0x2)
#define UART_LCR   (0x3)
#define UART_MCR   (0x4)
#define UART_LSR   (0x5)
#define UART_MSR   (0x6)
#define UART_SCR   (0x7)
#define UART_DLL   (0x0)
#define UART_DLM   (0x1)
// clang-format on

#define UARTREG(base, reg) (*REG8((base) + (reg)))

#define RXBUF_SIZE 128

// values read from zbi
static vaddr_t uart_base = 0;
static uint32_t uart_irq = 0;

static Cbuf uart_rx_buf;

static interrupt_eoi ns16550a_uart_irq(void* arg) {
  /* while fifo is not empty, read chars out of it */
  while (UARTREG(uart_base, UART_LSR) & (1<<0)) {
    char c = (char)UARTREG(uart_base, UART_RBR);
    uart_rx_buf.WriteChar(c);
  }

  return IRQ_EOI_DEACTIVATE;
}

static void ns16550a_uart_init(const void* driver_data, uint32_t length) {
  // Initialize circular buffer to hold received data.
  uart_rx_buf.Initialize(RXBUF_SIZE, malloc(RXBUF_SIZE));

  // assumes interrupts are contiguous
  zx_status_t status = register_int_handler(uart_irq, &ns16550a_uart_irq, NULL);
  DEBUG_ASSERT(status == ZX_OK);

  // enable receive data available interrupt
  UARTREG(uart_base, UART_IER) = 0x1;

  // enable interrupt
  unmask_interrupt(uart_irq);
}

static int ns16550a_uart_getc(bool wait) {
  zx::status<char> result = uart_rx_buf.ReadChar(wait);
  if (result.is_ok()) {
    return result.value();
  }

  return result.error_value();
}

/* panic-time getc/putc */
static void ns16550a_uart_pputc(char c) {
  /* spin while fifo is full */
  while ((UARTREG(uart_base, UART_LSR) & (1 << 6)) == 0)
    ;
  UARTREG(uart_base, UART_THR) = c;
}

static int ns16550a_uart_pgetc() {
  if ((UARTREG(uart_base, UART_LSR) & (1 << 0))) {
    return (int)UARTREG(uart_base, UART_RBR);
  } else {
    return -1;
  }
}

static void ns16550a_dputs(const char* str, size_t len, bool block, bool map_NL) {
  bool copied_CR = false;

  while (len > 0) {
    // Is FIFO Full ?
    while ((UARTREG(uart_base, UART_LSR) & (1 << 6)) == 0)
      ;

    if (*str == '\n' && map_NL && !copied_CR) {
      copied_CR = true;
      UARTREG(uart_base, UART_THR) = '\r';
    } else {
      copied_CR = false;
      UARTREG(uart_base, UART_THR) = *str++;
      len--;
    }
  }
}

static void ns16550a_start_panic() { }

static const struct pdev_uart_ops uart_ops = {
    .getc = ns16550a_uart_getc,
    .pputc = ns16550a_uart_pputc,
    .pgetc = ns16550a_uart_pgetc,
    .start_panic = ns16550a_start_panic,
    .dputs = ns16550a_dputs,
};

static void ns16550a_uart_init_early(const void* driver_data, uint32_t length) {
  ASSERT(length >= sizeof(dcfg_simple_t));
  auto driver = static_cast<const dcfg_simple_t*>(driver_data);
  ASSERT(driver->mmio_phys && driver->irq);

  uart_base = (vaddr_t)paddr_to_physmap(driver->mmio_phys);
  ASSERT(uart_base);
  uart_irq = driver->irq;

  pdev_register_uart(&uart_ops);
}

LK_PDEV_INIT(ns16550a_uart_init_early, KDRV_NS16550A_UART, ns16550a_uart_init_early,
             LK_INIT_LEVEL_PLATFORM_EARLY)
LK_PDEV_INIT(ns16550a_uart_init, KDRV_NS16550A_UART, ns16550a_uart_init, LK_INIT_LEVEL_PLATFORM)

