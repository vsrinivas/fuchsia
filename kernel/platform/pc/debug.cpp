// Copyright 2016 The Fuchsia Authors
// Copyright (c) 2009 Corey Tabaka
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <stdarg.h>
#include <reg.h>
#include <stdio.h>
#include <kernel/thread.h>
#include <kernel/timer.h>
#include <kernel/vm/pmm.h>
#include <lk/init.h>
#include <arch/x86.h>
#include <arch/x86/apic.h>
#include <lib/cbuf.h>
#include <dev/interrupt.h>
#include <kernel/cmdline.h>
#include <platform.h>
#include <platform/pc.h>
#include <platform/pc/memmap.h>
#include <platform/console.h>
#include <platform/debug.h>
#include <trace.h>

#include "platform_p.h"

static const int uart_baud_rate = 115200;
static const int uart_io_port = 0x3f8;
static uint64_t uart_mem_addr = 0;

cbuf_t console_input_buf;
static bool output_enabled = false;

static uint8_t uart_read(uint8_t reg)
{
    if (uart_mem_addr) {
        return (uint8_t)readl(uart_mem_addr + 4 * reg);
    } else {
        return (uint8_t)inp((uint16_t)(uart_io_port + reg));
    }
}

static void uart_write(uint8_t reg, uint8_t val)
{
    if (uart_mem_addr) {
        writel(val, uart_mem_addr + 4 * reg);
    } else {
        outp((uint16_t)(uart_io_port + reg), val);
    }
}

static enum handler_return platform_drain_debug_uart_rx(void)
{
    unsigned char c;
    bool resched = false;

    while (uart_read(5) & (1<<0)) {
        c = uart_read(0);
        cbuf_write_char(&console_input_buf, c, false);
        resched = true;
    }

    return resched ? INT_RESCHEDULE : INT_NO_RESCHEDULE;
}

static enum handler_return uart_irq_handler(void *arg)
{
    return platform_drain_debug_uart_rx();
}

// for devices where the uart rx interrupt doesn't seem to work
static enum handler_return uart_rx_poll(struct timer *t, lk_time_t now, void *arg)
{
    timer_set_oneshot(t, now + LK_MSEC(10), uart_rx_poll, NULL);
    return platform_drain_debug_uart_rx();
}

// also called from the pixel2 quirk file
void platform_debug_start_uart_timer(void);

void platform_debug_start_uart_timer(void)
{
    static timer_t uart_rx_poll_timer;
    static bool started = false;

    if (!started) {
        started = true;
        timer_init(&uart_rx_poll_timer);
        timer_set_oneshot(&uart_rx_poll_timer, current_time() + LK_MSEC(10), uart_rx_poll, NULL);
    }
}


void platform_init_debug_early(void)
{
    /* configure the uart */
    int divisor = 115200 / uart_baud_rate;

    /* get basic config done so that tx functions */
    uart_write(1, 0); // mask all irqs
    uart_write(3, 0x80); // set up to load divisor latch
    uart_write(0, static_cast<uint8_t>(divisor)); // lsb
    uart_write(1, static_cast<uint8_t>(divisor >> 8)); // msb
    uart_write(3, 3); // 8N1
    uart_write(2, 0xc7); // enable FIFO, clear, 14-byte threshold

    output_enabled = true;
}

void platform_init_debug(void)
{
    // command line isn't available in platform_init_debug_early()
    // so we read it here instead
    uint64_t uart_paddr = cmdline_get_uint64("pc.uart.paddr", 0);
    if (uart_paddr) {
        uart_mem_addr = (uint64_t)paddr_to_kvaddr(uart_paddr);
        // need to reinitialize in this case
        platform_init_debug_early();
    }

    /* finish uart init to get rx going */
    cbuf_initialize(&console_input_buf, 1024);

    uint32_t irq = apic_io_isa_to_global(ISA_IRQ_SERIAL1);
    register_int_handler(irq, uart_irq_handler, NULL);
    unmask_interrupt(irq);

    uart_write(1, 0x1); // enable receive data available interrupt

    // modem control register: Axiliary Output 2 is another IRQ enable bit
    const uint8_t mcr = uart_read(4);
    uart_write(4, mcr | 0x8);

    if (cmdline_get_bool("kernel.debug_uart_poll", false)) {
        platform_debug_start_uart_timer();
    }
}

static void debug_uart_putc(char c)
{
#if WITH_LEGACY_PC_CONSOLE
    cputc(c);
#endif
    if (unlikely(!output_enabled))
        return;

    while ((uart_read(5) & (1<<6)) == 0) {
        arch_spinloop_pause();
    }
    uart_write(0, c);
}

void platform_dputs(const char* str, size_t len)
{
    while (len-- > 0) {
        char c = *str++;
        if (c == '\n') {
            debug_uart_putc('\r');
        }
        debug_uart_putc(c);
    }
}

int platform_dgetc(char *c, bool wait)
{
    return static_cast<int>(cbuf_read_char(&console_input_buf, c, wait));
}

// panic time polling IO for the panic shell
void platform_pputc(char c)
{
    platform_dputc(c);
}

int platform_pgetc(char *c, bool wait)
{
    if (uart_read(5) & (1<<0)) {
        *c = uart_read(0);
        return 0;
    }

    return -1;
}
