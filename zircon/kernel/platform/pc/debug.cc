// Copyright 2016 The Fuchsia Authors
// Copyright (c) 2009 Corey Tabaka
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include "debug.h"

#include <bits.h>
#include <lib/acpi_tables.h>
#include <lib/cbuf.h>
#include <lib/cmdline.h>
#include <lib/debuglog.h>
#include <platform.h>
#include <reg.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <trace.h>
#include <zircon/time.h>
#include <zircon/types.h>

#include <arch/x86.h>
#include <arch/x86/apic.h>
#include <dev/interrupt.h>
#include <kernel/spinlock.h>
#include <kernel/thread.h>
#include <kernel/timer.h>
#include <lk/init.h>
#include <platform/console.h>
#include <platform/debug.h>
#include <platform/pc.h>
#include <platform/pc/bootloader.h>
#include <vm/physmap.h>
#include <vm/vm_aspace.h>

#include "memory.h"
#include "platform_p.h"

// Low level debug serial.
//
// This code provides basic serial support for a 16550-compatible UART, used
// for kernel debugging. We support configuring serial from several sources of information:
//
//   1. The kernel command line ("kernel.serial=...")
//   2. Information passed in via the ZBI (ZBI_TYPE_DEBUG_UART)
//   3. From ACPI (the "DBG2" ACPI table)
//
// On system boot, we try each of these sources in decreasing order of priority.
//
// The init code is called several times during the boot sequence:
//
//   pc_init_debug_early():
//       Before the MMU is set up.
//
//   pc_init_debug_post_acpi():
//       After the MMU is set up and ACPI tables are available, but before other
//       CPU cores are enabled.
//
//   pc_init_debug():
//       After virtual memory, kernel, threading and arch-specific code has been enabled.

// Debug port baud rate.
constexpr int kBaudRate = 115200;

// Hardware details of the system debug port.
static DebugPort debug_port = {DebugPort::Type::Unknown, 0, 0, 0, 0};

// UART state.
static bool output_enabled = false;
cbuf_t console_input_buf;
static uint32_t uart_fifo_depth;
static bool uart_tx_irq_enabled = false;  // tx driven irq
static event_t uart_dputc_event =
    EVENT_INITIAL_VALUE(uart_dputc_event, true, EVENT_FLAG_AUTOUNSIGNAL);
static spin_lock_t uart_spinlock = SPIN_LOCK_INITIAL_VALUE;

// Read a single byte from the given UART register.
static uint8_t uart_read(uint8_t reg) {
  ZX_DEBUG_ASSERT(debug_port.type == DebugPort::Type::IoPort
      || debug_port.type == DebugPort::Type::Mmio);

  switch (debug_port.type) {
    case DebugPort::Type::IoPort:
      return (uint8_t)inp((uint16_t)(debug_port.io_port + reg));
    case DebugPort::Type::Mmio: {
      uintptr_t addr = reinterpret_cast<uintptr_t>(debug_port.mem_addr);
      return (uint8_t)readl(addr + 4 * reg);
    }
    default:
      return 0;
  }
}

// Write a single byte to the given UART register.
static void uart_write(uint8_t reg, uint8_t val) {
  ZX_DEBUG_ASSERT(debug_port.type == DebugPort::Type::IoPort
      || debug_port.type == DebugPort::Type::Mmio);

  switch (debug_port.type) {
    case DebugPort::Type::IoPort:
      outp((uint16_t)(debug_port.io_port + reg), val);
      break;
    case DebugPort::Type::Mmio: {
      uintptr_t addr = reinterpret_cast<uintptr_t>(debug_port.mem_addr);
      writel(val, addr + 4 * reg);
      break;
    }
    default:
      break;
  }
}

// Handle an interrupt from the UART.
static interrupt_eoi uart_irq_handler(void* arg) {
  spin_lock(&uart_spinlock);

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
        cbuf_write_char(&console_input_buf, c);
        break;
      }
      case 0b0010:
        // transmitter is empty, signal any waiting senders
        event_signal(&uart_dputc_event, true);
        // disable the tx irq
        uart_write(1, (1 << 0));  // just rx interrupt enable
        break;
      case 0b0110:     // receiver line status
        uart_read(5);  // read the LSR
        break;
      default:
        spin_unlock(&uart_spinlock);
        panic("UART: unhandled ident %#x\n", ident);
    }
  }

  spin_unlock(&uart_spinlock);
  return IRQ_EOI_DEACTIVATE;
}

// Read all pending inputs from the UART.
static void platform_drain_debug_uart_rx() {
  while (uart_read(5) & (1 << 0)) {
    unsigned char c = uart_read(0);
    cbuf_write_char(&console_input_buf, c);
  }
}

static constexpr TimerSlack kSlack{ZX_MSEC(1), TIMER_SLACK_CENTER};

// Poll for inputs on the UART.
//
// Used for devices where the UART rx interrupt isn't available.
static void uart_rx_poll(timer_t* t, zx_time_t now, void* arg) {
  const Deadline deadline(zx_time_add_duration(now, ZX_MSEC(10)), kSlack);
  timer_set(t, deadline, uart_rx_poll, NULL);
  platform_drain_debug_uart_rx();
}

// Create a polling thread for the UART.
static void platform_debug_start_uart_timer() {
  static timer_t uart_rx_poll_timer;
  static bool started = false;

  if (!started) {
    started = true;
    timer_init(&uart_rx_poll_timer);
    const Deadline deadline(zx_time_add_duration(current_time(), ZX_MSEC(10)), kSlack);
    timer_set(&uart_rx_poll_timer, deadline, uart_rx_poll, NULL);
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

// Configure the serial device "port".
static void setup_uart(const DebugPort& port) {
  DEBUG_ASSERT(port.type != DebugPort::Type::Unknown);

  // Update the port information.
  debug_port = port;

  // Enable the UART.
  if (port.type == DebugPort::Type::Disabled) {
    dprintf(INFO, "UART disabled.\n");
    return;
  }
  init_uart();
  output_enabled = true;
  dprintf(INFO, "UART: enabled with FIFO depth %u\n", uart_fifo_depth);
}

bool platform_serial_enabled() {
  switch (debug_port.type) {
    case DebugPort::Type::Unknown:
    case DebugPort::Type::Disabled:
      return false;
    default:
      return true;
  }
}

zx_status_t parse_serial_cmdline(const char* serial_mode, DebugPort* port) {
  // Check if the user has explicitly disabled the UART.
  if (!strcmp(serial_mode, "none")) {
    port->type = DebugPort::Type::Disabled;
    return ZX_OK;
  }

  // Legacy mode port (x86 IO ports).
  if (!strcmp(serial_mode, "legacy")) {
    port->type = DebugPort::Type::IoPort;
    port->io_port = 0x3f8;
    port->irq = ISA_IRQ_SERIAL1;
    return ZX_OK;
  }

  // type can be "ioport" or "mmio"
  constexpr size_t kMaxTypeLen = 6 + 1;
  char type_buf[kMaxTypeLen];
  // Addr can be up to 32 characters (numeric in any base strtoul will take),
  // and + 1 for \0
  constexpr size_t kMaxAddrLen = 32 + 1;
  char addr_buf[kMaxAddrLen];

  char* endptr;
  const char* addr_start;
  const char* irq_start;
  size_t addr_len, type_len;
  unsigned long irq_val;

  addr_start = strchr(serial_mode, ',');
  if (addr_start == nullptr) {
    return ZX_ERR_INVALID_ARGS;
  }
  addr_start++;
  irq_start = strchr(addr_start, ',');
  if (irq_start == nullptr) {
    return ZX_ERR_INVALID_ARGS;
  }
  irq_start++;

  // Parse out the type part
  type_len = addr_start - serial_mode - 1;
  if (type_len + 1 > kMaxTypeLen) {
    return ZX_ERR_INVALID_ARGS;
  }
  memcpy(type_buf, serial_mode, type_len);
  type_buf[type_len] = 0;
  if (!strcmp(type_buf, "ioport")) {
    port->type = DebugPort::Type::IoPort;
  } else if (!strcmp(type_buf, "mmio")) {
    port->type = DebugPort::Type::Mmio;
  } else {
    return ZX_ERR_INVALID_ARGS;
  }

  // Parse out the address part
  addr_len = irq_start - addr_start - 1;
  if (addr_len == 0 || addr_len + 1 > kMaxAddrLen) {
    return ZX_ERR_INVALID_ARGS;
  }
  memcpy(addr_buf, addr_start, addr_len);
  addr_buf[addr_len] = 0;
  uint64_t base = strtoul(addr_buf, &endptr, 0);
  if (endptr != addr_buf + addr_len) {
    return ZX_ERR_INVALID_ARGS;
  }
  if (port->type == DebugPort::Type::IoPort) {
    port->io_port = static_cast<uint32_t>(base);
  } else {
    port->phys_addr = base;
  }

  // Parse out the IRQ part
  irq_val = strtoul(irq_start, &endptr, 0);
  if (endptr == irq_start || *endptr != '\0' || irq_val > UINT32_MAX) {
    return ZX_ERR_INVALID_ARGS;
  }

  // For now, we don't support non-ISA IRQs
  if (irq_val >= NUM_ISA_IRQS) {
    return ZX_ERR_NOT_SUPPORTED;
  }

  port->irq = static_cast<uint32_t>(irq_val);
  return ZX_OK;
}

// Update the ZBI_TYPE_DEBUG_UART entry in the "bootloader" global to contain details in "port".
static void update_zbi_uart(const DebugPort& port) {
  switch (port.type) {
    case DebugPort::Type::IoPort:
      bootloader.uart.type = ZBI_UART_PC_PORT;
      bootloader.uart.base = port.io_port;
      bootloader.uart.irq = port.irq;
      break;

    case DebugPort::Type::Mmio:
      bootloader.uart.type = ZBI_UART_PC_MMIO;
      bootloader.uart.base = port.phys_addr;
      bootloader.uart.irq = port.irq;
      break;

    case DebugPort::Type::Unknown:
    case DebugPort::Type::Disabled:
      bootloader.uart.type = ZBI_UART_NONE;
      break;
  }
}

// Parse the kernel command line.
//
// Return "true" if a debug port was found.
static bool handle_serial_cmdline(DebugPort* port) {
  // Fetch the command line.
  const char* serial_mode = gCmdline.GetString("kernel.serial");
  if (serial_mode == nullptr) {
    // Nothing provided.
    return false;
  }

  // Otherwise, parse command line and update "bootloader.uart".
  zx_status_t result = parse_serial_cmdline(serial_mode, port);
  if (result != ZX_OK) {
    dprintf(INFO, "Failed to parse \"kernel.serial\" parameter. Disabling serial.\n");
    // Explictly disable the serial.
    port->type = DebugPort::Type::Disabled;
    return true;
  }

  // Finish setup.
  if (port->type == DebugPort::Type::Mmio) {
    // Convert the physical address specified in the command line into a virtual
    // address and mark it as reserved.
    port->mem_addr = reinterpret_cast<vaddr_t>(paddr_to_physmap(port->phys_addr));
    mark_mmio_region_to_reserve(port->phys_addr, PAGE_SIZE);
  } else if (port->type == DebugPort::Type::IoPort) {
    // Reserve the IO port range.
    mark_pio_region_to_reserve(port->io_port, 8);
  }

  return true;
}

// Attempt to read information about a debug UART out of the ZBI.
//
// Return "true" if a debug port was found.
static bool handle_serial_zbi(DebugPort* port) {
  switch (bootloader.uart.type) {
    case ZBI_UART_PC_PORT:
      port->type = DebugPort::Type::IoPort;
      port->io_port = static_cast<uint32_t>(bootloader.uart.base);
      mark_pio_region_to_reserve(port->io_port, 8);
      port->irq = bootloader.uart.irq;
      return true;

    case ZBI_UART_PC_MMIO:
      port->type = DebugPort::Type::Mmio;
      port->phys_addr = bootloader.uart.base;
      port->mem_addr = reinterpret_cast<vaddr_t>(paddr_to_physmap(bootloader.uart.base));
      mark_mmio_region_to_reserve(port->phys_addr, PAGE_SIZE);
      port->irq = bootloader.uart.irq;
      return true;

    case ZBI_UART_NONE:
    default:
      return false;
  }
}

void pc_init_debug_early() {
  // Fetch serial information from the command line.
  DebugPort port;
  if (handle_serial_cmdline(&port)) {
    setup_uart(port);
    return;
  }

  // Failing that, fetch serial information from the ZBI.
  if (handle_serial_zbi(&port)) {
    setup_uart(port);
    return;
  }
}

void pc_init_debug() {
  // At this stage, we have threads, interrupts, the heap, and virtual memory
  // available to us, which wasn't available at stages.
  //
  // Finish setting up the UART, including:
  //   - Update the global "bootloader" structure, so that preconfigured serial
  //     works across mexec().
  //   - Setting up interrupts for TX and RX, or polling timers if we can't
  //     use interrupts;
  //   - RX buffers.

  cbuf_initialize(&console_input_buf, 1024);

  // Update the ZBI with current serial port settings.
  //
  // The updated information is used by mexec() to pass onto the next kernel.
  update_zbi_uart(debug_port);

  if (!platform_serial_enabled()) {
    // Need to bail after initializing the input_buf to prevent uninitialized
    // access to it.
    return;
  }

  // If we don't support interrupts, set up a polling timer.
  if ((debug_port.irq == 0) || gCmdline.GetBool("kernel.debug_uart_poll", false)) {
    printf("debug-uart: polling enabled\n");
    platform_debug_start_uart_timer();
    return;
  }

  // Otherwise, set up interrupts.
  uint32_t irq = apic_io_isa_to_global(static_cast<uint8_t>(debug_port.irq));
  zx_status_t status = register_int_handler(irq, uart_irq_handler, NULL);
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
                                          size_t* wrote_bytes, bool map_NL) {
  size_t i, copy_bytes;
  char* s = (char*)str;

  copy_bytes = MIN(uart_fifo_depth, *len);
  for (i = 0; i < copy_bytes; i++) {
    if (*s == '\n' && map_NL && !*copied_CR) {
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
// map_NL : If true, map a '\n' to '\r'+'\n'
static void platform_dputs(const char* str, size_t len, bool block, bool map_NL) {
  spin_lock_saved_state_t state;
  bool copied_CR = false;
  size_t wrote;

  // drop strings if we haven't initialized the uart yet
  if (unlikely(!output_enabled))
    return;
  if (!uart_tx_irq_enabled)
    block = false;
  spin_lock_irqsave(&uart_spinlock, state);
  while (len > 0) {
    // Is FIFO empty ?
    while (!(uart_read(5) & (1 << 5))) {
      if (block) {
        // We want to Tx more and FIFO is empty, re-enable Tx interrupts before blocking.
        uart_write(1, static_cast<uint8_t>((1 << 0) | ((uart_tx_irq_enabled ? 1 : 0)
                                                       << 1)));  // rx and tx interrupt enable
        spin_unlock_irqrestore(&uart_spinlock, state);
        event_wait(&uart_dputc_event);
      } else {
        spin_unlock_irqrestore(&uart_spinlock, state);
        arch_spinloop_pause();
      }
      spin_lock_irqsave(&uart_spinlock, state);
    }
    // Fifo is completely empty now, we can shove an entire
    // fifo's worth of Tx...
    str = debug_platform_tx_FIFO_bytes(str, &len, &copied_CR, &wrote, map_NL);
    if (block && wrote > 0) {
      // If blocking/irq driven wakeps, enable rx/tx intrs
      uart_write(1, static_cast<uint8_t>((1 << 0) | ((uart_tx_irq_enabled ? 1 : 0)
                                                     << 1)));  // rx and tx interrupt enable
    }
  }
  spin_unlock_irqrestore(&uart_spinlock, state);
}

void platform_dputs_thread(const char* str, size_t len) {
  if (platform_serial_enabled()) {
    platform_dputs(str, len, true, true);
  }
}

void platform_dputs_irq(const char* str, size_t len) {
  if (platform_serial_enabled()) {
    platform_dputs(str, len, false, true);
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
    arch_spinloop_pause();
  }
  uart_write(0, c);
}

int platform_dgetc(char* c, bool wait) {
  if (platform_serial_enabled()) {
    return static_cast<int>(cbuf_read_char(&console_input_buf, c, wait));
  }
  return ZX_ERR_NOT_SUPPORTED;
}

// panic time polling IO for the panic shell
void platform_pputc(char c) {
  if (platform_serial_enabled()) {
    if (c == '\n')
      debug_uart_putc_poll('\r');
    debug_uart_putc_poll(c);
  }
}

int platform_pgetc(char* c, bool wait) {
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
