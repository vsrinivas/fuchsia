// Copyright 2016 The Fuchsia Authors
// Copyright (c) 2016 Gurjant Kalsi
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

// TODO(gkalsi): Unify the two UART codepaths and use the port parameter to
// select between the real uart and the miniuart.

#include <inttypes.h>
#include <dev/interrupt.h>
#include <dev/uart.h>
#include <lib/cbuf.h>
#include <mdi/mdi.h>
#include <mdi/mdi-defs.h>
#include <pdev/driver.h>
#include <pdev/uart.h>

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

// values read from MDI
static uint64_t msm_uart_base = 0;
static uint32_t msm_uart_irq = 0;

static cbuf_t uart_rx_buf;
static bool initialized = false;

static inline uint32_t uart_read(int offset) {
    return readl(msm_uart_base + offset);
}

static inline void uart_write(uint32_t val, int offset) {
    writel(val, msm_uart_base + offset);
}

static inline void yield(void)
{
    __asm__ volatile("yield" ::: "memory");
}

static void msm_printstr(const char* str, size_t count) {
    if (!initialized) return;

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

static int msm_putc(char c) {
    msm_printstr(&c, 1);
    return 1;
}

/* panic-time getc/putc */
static int msm_pputc(char c)
{
    msm_printstr(&c, 1);
    return 1;
}

static int msm_pgetc(void)
{
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
    for (i = 1; i < count; i++) {
        cbuf_write_char(rxbuf, bytes[i], false);
    }

    return c;
}

static enum handler_return uart_irq(void *arg)
{
    uint32_t misr = uart_read(UART_DM_MISR);
    bool reschedule = false;

    while (uart_read(UART_DM_SR) & UART_DM_SR_RXRDY) {
        uint32_t rxfs = uart_read(UART_DM_RXFS);
        // count is number of words in RX fifo that have data
        int count = UART_DM_RXFS_FIFO_STATE(rxfs);

        for (int i = 0; i < count; i++) {
            uint32_t val = uart_read(UART_DM_RF(0));
            char* bytes = (char *)&val;

            for (int j = 0; j < 4; j++) {
                // Unfortunately there is no documented way to get number of bytes in each word
                // so we just need to ignore zero bytes here.
                // Apparently this problem doesn't exist in DMA mode.
                char ch = bytes[j];
                if (ch) {
                    cbuf_write_char(&uart_rx_buf, ch, false);
                } else {
                    break;
                }
            }
        }
        reschedule = true;
    }

    if (misr & UART_IRQ_RXSTALE) {
        uart_write(UART_DM_CR_CMD_RESET_STALE_INT, UART_DM_CR);
    }

    // ask to receive more
    uart_write(0xFFFFFF, UART_DM_DMRX);
    uart_write(UART_DM_CR_CMD_ENABLE_STALE_EVENT, UART_DM_CR);

    return (reschedule ? INT_RESCHEDULE : INT_NO_RESCHEDULE);
}

static void msm_uart_init(mdi_node_ref_t* node, uint level) {
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

    cbuf_initialize(&uart_rx_buf, RXBUF_SIZE);

    // enable RX interrupt
    uart_write(UART_IRQ_RXSTALE, UART_DM_IMR);

    register_int_handler(msm_uart_irq, &uart_irq, NULL);
    unmask_interrupt(msm_uart_irq);
}

static int msm_getc(bool wait) {
    char ch;
    size_t count = cbuf_read_char(&uart_rx_buf, &ch, wait);
    return (count == 1 ? ch : -1);
}

static const struct pdev_uart_ops uart_ops = {
    .putc = msm_putc,
    .getc = msm_getc,
    .pputc = msm_pputc,
    .pgetc = msm_pgetc,
};

static void msm_uart_init_early(mdi_node_ref_t* node, uint level) {
    uint64_t msm_uart_base_phys = 0;
    bool got_msm_uart_base_phys = false;
    bool got_msm_uart_irq = false;

    mdi_node_ref_t child;
    mdi_each_child(node, &child) {
        switch (mdi_id(&child)) {
        case MDI_KERNEL_DRIVERS_MSM_UART_BASE_PHYS:
            got_msm_uart_base_phys = !mdi_node_uint64(&child, &msm_uart_base_phys);
            break;
        case MDI_KERNEL_DRIVERS_MSM_UART_IRQ:
            got_msm_uart_irq = !mdi_node_uint32(&child, &msm_uart_irq);
            break;
        }
    }

    if (!got_msm_uart_base_phys) {
        panic("msm uart: msm_uart_base_phys not defined\n");
        return;
    }
    if (!got_msm_uart_irq) {
        panic("msm uart: msm_uart_irq not defined\n");
        return;
    }

    msm_uart_base = (uint64_t)paddr_to_kvaddr(msm_uart_base_phys);
    initialized = true;
    pdev_register_uart(&uart_ops);
}

LK_PDEV_INIT(msm_uart_init_early, MDI_KERNEL_DRIVERS_MSM_UART, msm_uart_init_early, LK_INIT_LEVEL_PLATFORM_EARLY);
LK_PDEV_INIT(msm_uart_init, MDI_KERNEL_DRIVERS_MSM_UART, msm_uart_init, LK_INIT_LEVEL_PLATFORM);
