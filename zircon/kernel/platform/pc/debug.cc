// Copyright 2016 The Fuchsia Authors
// Copyright (c) 2009 Corey Tabaka
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include "platform/pc/debug.h"

#include <bits.h>
#include <lib/arch/intrin.h>
#include <lib/boot-options/boot-options.h>
#include <lib/cbuf.h>
#include <lib/debuglog.h>
#include <lib/zircon-internal/macros.h>
#include <platform.h>
#include <reg.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string-file.h>
#include <string.h>
#include <trace.h>
#include <zircon/time.h>
#include <zircon/types.h>

#include <arch/x86.h>
#include <arch/x86/apic.h>
#include <dev/interrupt.h>
#include <kernel/lockdep.h>
#include <kernel/spinlock.h>
#include <kernel/thread.h>
#include <kernel/timer.h>
#include <ktl/algorithm.h>
#include <ktl/move.h>
#include <ktl/variant.h>
#include <phys/handoff.h>
#include <platform/console.h>
#include <platform/debug.h>
#include <platform/pc.h>
#include <vm/physmap.h>
#include <vm/vm_aspace.h>

#include "memory.h"

#include <ktl/enforce.h>

// Hardware details of the system's debug port.
struct DebugPort {
  enum class Type {
    // Unknown or disabled.
    kNull,
    // Debug port is a 16550-compatible UART using legacy PC ports.
    kIoPort,
    // Debug port is a 16550-compatible UART using MMIO.
    kMmio,
  };

  Type type = Type::kNull;

  // IRQ for UART. 0 indicates interrupts are not supported.
  uint32_t irq = 0;

  // State for IO port.
  uint32_t io_port = 0;

  // State for MMIO.
  vaddr_t mem_addr = 0;
  paddr_t phys_addr = 0;
};

// Debug port baud rate.
constexpr int kBaudRate = 115200;

// Hardware details of the system debug port.
static DebugPort gDebugPort;

// UART state.
static bool output_enabled = false;
Cbuf console_input_buf;
static uint32_t uart_fifo_depth;
static bool uart_tx_irq_enabled = false;  // tx driven irq
static AutounsignalEvent uart_dputc_event{true};

namespace {
DECLARE_SINGLETON_SPINLOCK_WITH_TYPE(uart_tx_spinlock, MonitoredSpinLock);
}  // namespace

// Read a single byte from the given UART register.
static uint8_t uart_read(uint8_t reg) {
  DEBUG_ASSERT(gDebugPort.type == DebugPort::Type::kIoPort ||
               gDebugPort.type == DebugPort::Type::kMmio);

  switch (gDebugPort.type) {
    case DebugPort::Type::kIoPort:
      return (uint8_t)inp((uint16_t)(gDebugPort.io_port + reg));
    case DebugPort::Type::kMmio: {
      uintptr_t addr = reinterpret_cast<uintptr_t>(gDebugPort.mem_addr);
      return (uint8_t)readl(addr + 4 * reg);
    }
    default:
      return 0;
  }
}

// Write a single byte to the given UART register.
static void uart_write(uint8_t reg, uint8_t val) {
  DEBUG_ASSERT(gDebugPort.type == DebugPort::Type::kIoPort ||
               gDebugPort.type == DebugPort::Type::kMmio);

  switch (gDebugPort.type) {
    case DebugPort::Type::kIoPort:
      outp((uint16_t)(gDebugPort.io_port + reg), val);
      break;
    case DebugPort::Type::kMmio: {
      uintptr_t addr = reinterpret_cast<uintptr_t>(gDebugPort.mem_addr);
      writel(val, addr + 4 * reg);
      break;
    }
    default:
      break;
  }
}

// Handle an interrupt from the UART.
//
// NOTE: Register access is not explicitly synchronized between the IRQ, TX, and
// RX paths. This is safe because none of the paths perform read-modify-write
// operations on the UART registers. Additionally, the TX and RX functions are
// largely independent. The only synchronization between IRQ and TX/RX is
// internal to the Cbuf and Event objects. It is critical to keep
// synchronization inside the IRQ path to a minimum, otherwise it is possible to
// introduce long spin periods in IRQ context that can seriously degrade system
// performance.
static void uart_irq_handler(void* arg) {
  // see why we have gotten an irq
  for (;;) {
    uint8_t iir = uart_read(2);
    if (BIT(iir, 0))
      break;  // no valid interrupt

    // 3 bit identification field
    uint ident = BITS(iir, 3, 0);
    switch (ident) {
      case 0b0100:
      case 0b1100: {
        // rx fifo is non empty, drain it
        unsigned char c = uart_read(0);
        console_input_buf.WriteChar(c);
        break;
      }
      case 0b0010:
        // disable the tx irq
        uart_write(1, (1 << 0));  // just rx interrupt enable
        // transmitter is empty, signal any waiting senders
        uart_dputc_event.Signal();
        break;
      case 0b0110:     // receiver line status
        uart_read(5);  // read the LSR
        break;
      default:
        panic("UART: unhandled ident %#x\n", ident);
    }
  }
}

// Read all pending inputs from the UART.
static void platform_drain_debug_uart_rx() {
  while (uart_read(5) & (1 << 0)) {
    unsigned char c = uart_read(0);
    console_input_buf.WriteChar(c);
  }
}

static constexpr TimerSlack kSlack{ZX_MSEC(1), TIMER_SLACK_CENTER};

// Poll for inputs on the UART.
//
// Used for devices where the UART rx interrupt isn't available.
static void uart_rx_poll(Timer* t, zx_time_t now, void* arg) {
  const Deadline deadline(zx_time_add_duration(now, ZX_MSEC(10)), kSlack);
  t->Set(deadline, uart_rx_poll, NULL);
  platform_drain_debug_uart_rx();
}

static Timer uart_rx_poll_timer;

// Create a polling thread for the UART.
static void platform_debug_start_uart_timer() {
  static bool started = false;

  if (!started) {
    started = true;
    const Deadline deadline = Deadline::after(ZX_MSEC(10), kSlack);
    uart_rx_poll_timer.Set(deadline, uart_rx_poll, NULL);
  }
}

// Setup the UART hardware.
static void init_uart() {
  // configure the uart
  int divisor = 115200 / kBaudRate;

  // get basic config done so that tx functions
  uart_write(1, 0);                                   // mask all irqs
  uart_write(3, 0x80);                                // set up to load divisor latch
  uart_write(0, static_cast<uint8_t>(divisor));       // lsb
  uart_write(1, static_cast<uint8_t>(divisor >> 8));  // msb
  // enable FIFO, rx FIFO reset, tx FIFO reset, 16750 64 byte fifo enable,
  // Rx FIFO irq trigger level at 14-bytes, must be done while divisor
  // latch is enabled in order to write the 16750 64 byte fifo enable bit
  uart_write(2, 0xe7);
  uart_write(3, 3);  // 8N1

  // Drive flow control bits high since we don't actively manage them
  uart_write(4, 0x3);

  // figure out the fifo depth
  uint8_t fcr = uart_read(2);
  if (BITS_SHIFT(fcr, 7, 6) == 3 && BIT(fcr, 5)) {
    // this is a 16750
    uart_fifo_depth = 64;
  } else if (BITS_SHIFT(fcr, 7, 6) == 3) {
    // this is a 16550A
    uart_fifo_depth = 16;
  } else {
    uart_fifo_depth = 1;
  }
}

bool platform_serial_enabled() { return gDebugPort.type != DebugPort::Type::kNull; }

void X86UartInitEarly(const uart::all::Driver& serial) {
  // Updates gDebugPort with the provided UART metadata, given by a variant of
  // libuart driver types, each with methods to indicate the ZBI item type and
  // payload.
  constexpr auto set_debug_port = [](const auto& uart) {
    const auto config = uart.config();
    using config_type = ktl::decay_t<decltype(config)>;

    if constexpr (ktl::is_same_v<config_type, zbi_dcfg_simple_t>) {
      gDebugPort = {
          .type = DebugPort::Type::kMmio,
          .irq = config.irq,
          .mem_addr = reinterpret_cast<vaddr_t>(paddr_to_physmap(config.mmio_phys)),
          .phys_addr = static_cast<paddr_t>(config.mmio_phys),
      };
      mark_mmio_region_to_reserve(gDebugPort.phys_addr, PAGE_SIZE);
      dprintf(INFO, "UART: kernel serial enabled: mmio=%#lx, irq=%#x\n", gDebugPort.phys_addr,
              gDebugPort.irq);
    } else if constexpr (ktl::is_same_v<config_type, zbi_dcfg_simple_pio_t>) {
      gDebugPort = {
          .type = DebugPort::Type::kIoPort,
          .irq = config.irq,
          .io_port = static_cast<uint32_t>(config.base),
      };
      mark_pio_region_to_reserve(gDebugPort.io_port, 8);
      dprintf(INFO, "UART: kernel serial enabled: port=%#x, irq=%#x\n", gDebugPort.io_port,
              gDebugPort.irq);
    }
  };

  ktl::visit(set_debug_port, serial);

  if (!platform_serial_enabled()) {
    dprintf(INFO, "UART: unknown or disabled.\n");
    return;
  }

  init_uart();
  output_enabled = true;
  dprintf(INFO, "UART: enabled with FIFO depth %u\n", uart_fifo_depth);
}

void X86UartInitLate() {
  // At this stage, we have threads, interrupts, the heap, and virtual memory
  // available to us, which wasn't available at earlier stages.
  //
  // Finish setting up the UART, including:
  //   - Setting up interrupts for TX and RX, or polling timers if we can't
  //     use interrupts;
  //   - RX buffers.

  console_input_buf.Initialize(1024, malloc(1024));

  if (!platform_serial_enabled()) {
    // Need to bail after initializing the input_buf to prevent uninitialized
    // access to it.
    return;
  }

  // If we don't support interrupts, set up a polling timer.
  if ((gDebugPort.irq == 0) || gBootOptions->debug_uart_poll) {
    printf("debug-uart: polling enabled\n");
    platform_debug_start_uart_timer();
    return;
  }

  // Otherwise, set up interrupts.
  uint32_t irq = apic_io_isa_to_global(static_cast<uint8_t>(gDebugPort.irq));
  zx_status_t status = register_permanent_int_handler(irq, uart_irq_handler, NULL);
  DEBUG_ASSERT(status == ZX_OK);
  unmask_interrupt(irq);

  uart_write(1, (1 << 0));  // enable receive data available interrupt

  // modem control register: Auxiliary Output 2 is another IRQ enable bit
  const uint8_t mcr = uart_read(4);
  uart_write(4, mcr | 0x8);
  printf("UART: started IRQ driven RX\n");
  bool tx_irq_driven = !dlog_bypass();
  if (tx_irq_driven) {
    // start up tx driven output
    printf("UART: started IRQ driven TX\n");
    uart_tx_irq_enabled = true;
  }
}

void pc_suspend_debug() { output_enabled = false; }

void pc_resume_debug() {
  if (platform_serial_enabled()) {
    init_uart();
    output_enabled = true;
  }
}

// This is called when the FIFO is detected to be empty. So we can write an
// entire FIFO's worth of bytes. Much more efficient than writing 1 byte
// at a time (and then checking for FIFO to drain).
static char* debug_platform_tx_FIFO_bytes(const char* str, size_t* len, bool* copied_CR,
                                          size_t* wrote_bytes) {
  size_t i, copy_bytes;
  char* s = (char*)str;

  copy_bytes = ktl::min(static_cast<size_t>(uart_fifo_depth), *len);
  for (i = 0; i < copy_bytes; i++) {
    if (*s == '\n' && !*copied_CR) {
      uart_write(0, '\r');
      *copied_CR = true;
      if (++i == copy_bytes)
        break;
      uart_write(0, '\n');
    } else {
      uart_write(0, *s);
      *copied_CR = false;
    }
    s++;
    (*len)--;
  }
  if (wrote_bytes != NULL)
    *wrote_bytes = i;
  return s;
}

// dputs() Tx is either polling driven (if the caller is non-preemptible
// or earlyboot or panic) or blocking (and irq driven).
//
// TODO - buffered Tx support may be a win, (lopri but worth investigating)
// When we do that dputs() can be completely asynchronous, and return when
// the write has been (atomically) deposited into the buffer, except when
// we run out of room in the Tx buffer (rare) - we'd either need to spin
// (if non-blocking) or block waiting for space in the Tx buffer (adding
// support to optionally block in cbuf_write_char() would be easiest way
// to achieve that).
//
// block : Blocking vs Non-Blocking
static void platform_dputs(const char* str, size_t len, bool block) {
  bool copied_CR = false;
  size_t wrote;

  // drop strings if we haven't initialized the uart yet
  if (unlikely(!output_enabled))
    return;
  if (!uart_tx_irq_enabled)
    block = false;
  Guard<MonitoredSpinLock, IrqSave> guard{uart_tx_spinlock::Get(), SOURCE_TAG};
  while (len > 0) {
    // Is FIFO empty ?
    while (!(uart_read(5) & (1 << 5))) {
      if (block) {
        // We want to Tx more and FIFO is empty, re-enable Tx interrupts before blocking.
        uart_write(1, static_cast<uint8_t>((1 << 0) | ((uart_tx_irq_enabled ? 1 : 0)
                                                       << 1)));  // rx and tx interrupt enable
        guard.CallUnlocked([]() { uart_dputc_event.Wait(); });
      } else {
        guard.CallUnlocked([]() { arch::Yield(); });
      }
    }
    // Fifo is completely empty now, we can shove an entire
    // fifo's worth of Tx...
    str = debug_platform_tx_FIFO_bytes(str, &len, &copied_CR, &wrote);
    if (block && wrote > 0) {
      // If blocking/irq driven wakeps, enable rx/tx intrs
      uart_write(1, static_cast<uint8_t>((1 << 0) | ((uart_tx_irq_enabled ? 1 : 0)
                                                     << 1)));  // rx and tx interrupt enable
    }
  }
}

void platform_dputs_thread(const char* str, size_t len) {
  if (platform_serial_enabled()) {
    platform_dputs(str, len, true);
  }
}

void platform_dputs_irq(const char* str, size_t len) {
  if (platform_serial_enabled()) {
    platform_dputs(str, len, false);
  }
}

// polling versions of debug uart read/write
static int debug_uart_getc_poll(char* c) {
  // if there is a character available, read it
  if (uart_read(5) & (1 << 0)) {
    *c = uart_read(0);
    return 0;
  }

  return -1;
}

static void debug_uart_putc_poll(char c) {
  // while the fifo is non empty, spin
  while (!(uart_read(5) & (1 << 6))) {
    arch::Yield();
  }
  uart_write(0, c);
}

int platform_dgetc(char* c, bool wait) {
  if (!platform_serial_enabled()) {
    return ZX_ERR_NOT_SUPPORTED;
  }

  zx::result<char> result = console_input_buf.ReadChar(wait);
  if (result.is_ok()) {
    *c = result.value();
    return 1;
  }
  if (result.error_value() == ZX_ERR_SHOULD_WAIT) {
    return 0;
  }
  return result.error_value();
}

// panic time polling IO for the panic shell
void platform_pputc(char c) {
  if (platform_serial_enabled()) {
    if (c == '\n')
      debug_uart_putc_poll('\r');
    debug_uart_putc_poll(c);
  }
}

int platform_pgetc(char* c) {
  if (platform_serial_enabled()) {
    return debug_uart_getc_poll(c);
  }
  return ZX_ERR_NOT_SUPPORTED;
}

// Called on start of a panic.
//
// When we do Tx buffering, drain the Tx buffer here in polling mode.
// Turn off Tx interrupts, so force Tx be polling from this point
void platform_debug_panic_start() { uart_tx_irq_enabled = false; }
