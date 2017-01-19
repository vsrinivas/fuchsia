// Copyright 2016 The Fuchsia Authors
// Copyright (c) 2016 Gurjant Kalsi
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

// TODO(gkalsi): Unify the two UART codepaths and use the port parameter to
// select between the real uart and the miniuart.

#include <inttypes.h>
#include <dev/uart.h>
#include <lib/cbuf.h>
#include <platform/msm8998.h>

#define UART_BASE           (MSM8998_PERIPH_BASE_VIRT + 0xc1b0000)
#define UART_SR             0x0008
#define UART_SR_TX_READY    (1 << 2)
#define UART_SR_TX_EMPTY    (1 << 3)

#define UART_CR             0x0010
#define UART_CR_CMD_RESET_TX_READY  (3 << 8)

#define UARTDM_NCF_TX       0x40
#define UARTDM_RF           0x0070

#define RXBUF_SIZE 16

static cbuf_t uart_rx_buf;

static inline void yield(void)
{
    __asm__ volatile("yield" ::: "memory");
}

static void msm_printstr(const char* str, size_t count) {
    char buffer[4];
    uint32_t* bufptr = (uint32_t *)buffer;
    size_t i, j;

    while (!(readl(UART_BASE + UART_SR) & UART_SR_TX_EMPTY)) {
        yield();
    }
    writel(UART_CR_CMD_RESET_TX_READY, UART_BASE + UARTDM_NCF_TX);
    writel(count, UART_BASE + UARTDM_NCF_TX);
    readl(UART_BASE + UARTDM_NCF_TX);

    for (i = 0; i < count; ) {
        size_t num_chars = (count > sizeof(buffer) ? sizeof(buffer) : count);

        for (j = 0; j < num_chars; j++) {
            buffer[j] = *str++;
        }

        // wait for TX ready
        while (!(readl(UART_BASE + UART_SR) & UART_SR_TX_READY))
            yield();

        writel(*bufptr, UART_BASE + UARTDM_RF);

        i += num_chars;
    }
}

int uart_putc(int port, char c) {
    msm_printstr(&c, 1);
    return 1;
}

void uart_init(void) {
    cbuf_initialize(&uart_rx_buf, RXBUF_SIZE);
}

void uart_init_early(void) {
}

int uart_getc(int port, bool wait) {
    cbuf_t* rxbuf = &uart_rx_buf;

    char c;
    if (cbuf_read_char(rxbuf, &c, wait) == 1)
        return c;

    return -1;
}

void uart_flush_tx(int port) {

}

void uart_flush_rx(int port) {

}

void uart_init_port(int port, uint baud) {
}
