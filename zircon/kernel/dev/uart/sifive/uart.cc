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

// simple driver for the UARTs in SiFive boards

// clang-format off
#define UART_TXDATA   (0x0)
#define UART_RXDATA   (0x4)
#define UART_TXCTRL   (0x8)
#define UART_RXCTRL   (0xC)
#define UART_IE       (0x10)
#define UART_IE       (0x10)
#define UART_IP       (0x14)
#define UART_DIV      (0x18)
// clang-format on

#define UARTREG(base, reg) (*REG32((base) + (reg)))

#define RXBUF_SIZE 128

// values read from zbi
static vaddr_t uart_base = 0;
static uint32_t uart_irq = 0;

static Cbuf uart_rx_buf;

static interrupt_eoi sifive_uart_irq(void* arg) {
  /* while fifo is not empty, read chars out of it */
  uint32_t rxdata = UARTREG(uart_base, UART_RXDATA);

  while (!(rxdata & (1 << 31))) {
    uart_rx_buf.WriteChar(rxdata & 0xff);
    rxdata = UARTREG(uart_base, UART_RXDATA);
  }

  return IRQ_EOI_DEACTIVATE;
}

static void sifive_uart_init(const void* driver_data, uint32_t length) {
  // Initialize circular buffer to hold received data.
  uart_rx_buf.Initialize(RXBUF_SIZE, malloc(RXBUF_SIZE));

  // assumes interrupts are contiguous
  zx_status_t status = register_int_handler(uart_irq, &sifive_uart_irq, NULL);
  DEBUG_ASSERT(status == ZX_OK);

  // enable TX and RX
  UARTREG(uart_base, UART_TXCTRL) = 1; // txen
  UARTREG(uart_base, UART_RXCTRL) = 1; // rxen, rxcnt = 0

  // enable raising interrupt on received data
  UARTREG(uart_base, UART_IE) = 1 << 1; // rxwvm

  // enable interrupt
  unmask_interrupt(uart_irq);
}

static int sifive_uart_getc(bool wait) {
  zx::status<char> result = uart_rx_buf.ReadChar(wait);
  if (result.is_ok()) {
    return result.value();
  }

  return result.error_value();
}

/* panic-time getc/putc */
static void sifive_uart_pputc(char c) {
  // TODO: Use an amoswap
  /* spin while fifo is full */
  if (c == '\n')
    sifive_uart_pputc('\r');

  while (UARTREG(uart_base, UART_TXDATA) & (1 << 31))
    ;
  UARTREG(uart_base, UART_TXDATA) = c;
}

static int sifive_uart_pgetc() {
  uint32_t rxdata = UARTREG(uart_base, UART_RXDATA);

  if (rxdata & (1 << 31)) {
    return -1;
  } else {
    return rxdata & 0xff;
  }
}

static void sifive_dputs(const char* str, size_t len, bool block, bool map_NL) {
  while (len > 0) {
    sifive_uart_pputc(*str++);
    len--;
  }
}

static void sifive_start_panic() { }

static const struct pdev_uart_ops uart_ops = {
    .getc = sifive_uart_getc,
    .pputc = sifive_uart_pputc,
    .pgetc = sifive_uart_pgetc,
    .start_panic = sifive_start_panic,
    .dputs = sifive_dputs,
};

static void sifive_uart_init_early(const void* driver_data, uint32_t length) {
  ASSERT(length >= sizeof(dcfg_simple_t));
  auto driver = static_cast<const dcfg_simple_t*>(driver_data);
  ASSERT(driver->mmio_phys && driver->irq);

  uart_base = (vaddr_t)paddr_to_physmap(driver->mmio_phys);
  ASSERT(uart_base);
  uart_irq = driver->irq;

  pdev_register_uart(&uart_ops);
}

LK_PDEV_INIT(sifive_uart_init_early, KDRV_SIFIVE_UART, sifive_uart_init_early,
             LK_INIT_LEVEL_PLATFORM_EARLY)
LK_PDEV_INIT(sifive_uart_init, KDRV_SIFIVE_UART, sifive_uart_init,
	     LK_INIT_LEVEL_PLATFORM)

