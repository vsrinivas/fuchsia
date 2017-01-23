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

#define UART_BASE   (MSM8998_PERIPH_BASE_VIRT + 0xc1b0000)

#define UART_MR1                            0x0000
#define UART_MR1_RX_RDY_CTL                 (1 << 7)

#define UART_MR2                            0x0004
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

#define UART_DM_CR_CMD_RESET_TX_READY       (3 << 8)
#define UART_DM_CR_CMD_FORCE_STALE          (4 << 8)
#define UART_DM_CR_CMD_ENABLE_STALE_EVENT_  (5 << 8)
#define UART_DM_CR_CMD_DISABLE_STALE_EVENT  (6 << 8)

#define UART_DM_RXFS                        0x0050
#define UART_DM_RXFS_RX_BUFFER_STATE(r)     ((r >> 7) & 7)
#define UART_DM_RXFS_FIFO_STATE(r)          ((r >> 14) | (r & 0x3F))

#define UART_DM_TF                          0x0100
#define UART_DM_RF(n)                       (0x0140 + 4 * (n))

#define RXBUF_SIZE 16

static cbuf_t uart_rx_buf;

static inline uint32_t uart_read(int offset) {
    return readl(UART_BASE + offset);
}

static inline void uart_write(uint32_t val, int offset) {
    writel(val, UART_BASE + offset);
}

static inline void yield(void)
{
    __asm__ volatile("yield" ::: "memory");
}

static void msm_printstr(const char* str, size_t count) {
    char buffer[4];
    uint32_t* bufptr = (uint32_t *)buffer;
    size_t i, j;

    while (!(uart_read(UART_DM_SR) & UART_DM_SR_TXEMT)) {
        yield();
    }
    uart_write(UART_DM_CR_CMD_RESET_TX_READY, UART_DM_N0_CHARS_FOR_TX);
    uart_write(count, UART_DM_N0_CHARS_FOR_TX);
    uart_read(UART_DM_N0_CHARS_FOR_TX);

    for (i = 0; i < count; ) {
        size_t num_chars = (count > sizeof(buffer) ? sizeof(buffer) : count);

        for (j = 0; j < num_chars; j++) {
            buffer[j] = *str++;
        }

        // wait for TX ready
        while (!(uart_read(UART_DM_SR) & UART_DM_SR_TXRDY))
            yield();

        uart_write(*bufptr, UART_DM_TF);

        i += num_chars;
    }
}

int uart_putc(int port, char c) {
    msm_printstr(&c, 1);
    return 1;
}

void uart_init(void) {
    uint32_t temp;

    uart_write(UART_DM_CR_TX_EN | UART_DM_CR_RX_EN, UART_DM_CR);
    uart_write(UART_DM_CR_CMD_RESET_TX, UART_DM_CR);
    uart_write(UART_DM_CR_CMD_RESET_RX, UART_DM_CR);
    uart_write(UART_DM_CR_CMD_RESET_ERR, UART_DM_CR);
    uart_write(UART_DM_CR_CMD_RESET_BRK_CHG_INT, UART_DM_CR);
    uart_write(UART_DM_CR_CMD_RESET_CTS_N, UART_DM_CR);
    uart_write(UART_DM_CR_CMD_SET_RFR, UART_DM_CR);

    uart_write(0xFFFFFF, UART_DM_DMRX);
    uart_write(UART_DM_CR_CMD_RESET_STALE_INT, UART_DM_CR);
    uart_write(UART_DM_CR_CMD_DISABLE_STALE_EVENT, UART_DM_CR);

    temp = uart_read(UART_MR1);
    temp |= UART_MR1_RX_RDY_CTL;
    uart_write(temp, UART_MR1);

    cbuf_initialize(&uart_rx_buf, RXBUF_SIZE);
}

void uart_init_early(void) {
}

int uart_getc(int port, bool wait) {
    cbuf_t* rxbuf = &uart_rx_buf;
    char c;
    int count = 0;
    uint32_t val, rxfs, sr;
    char* bytes;
    int i;

    // see if we have chars left from previous read
    if (cbuf_read_char(rxbuf, &c, false) == 1) {
        return c;
    }

    if ((uart_read(UART_DM_SR) & UART_DM_SR_OVERRUN)) {
        printf("UART overrun!\n");
        uart_write(UART_DM_CR_CMD_RESET_ERR, UART_DM_CR);
    }

    do {
        rxfs = uart_read(UART_DM_RXFS);
        sr = uart_read(UART_DM_SR);
        count = UART_DM_RXFS_RX_BUFFER_STATE(rxfs);
        if (!(sr & UART_DM_SR_RXRDY) && !count) {
            if (wait) {
                yield();
            } else {
                return -1;
            }
        }
    } while (count == 0);

    uart_write(UART_DM_CR_CMD_FORCE_STALE, UART_DM_CR);
    val = uart_read(UART_DM_RF(0));
    uart_read(UART_DM_RF(1));

    uart_write(UART_DM_CR_CMD_RESET_STALE_INT, UART_DM_CR);
    uart_write(0xFFFFFF, UART_DM_DMRX);

    bytes = (char *)&val;
    c = bytes[0];

    // save remaining chars for next call
    for (i = 1; i < count; i++) {
        cbuf_write_char(rxbuf, bytes[i], false);
    }

    return c;
}

void uart_flush_tx(int port) {

}

void uart_flush_rx(int port) {

}

void uart_init_port(int port, uint baud) {
}
