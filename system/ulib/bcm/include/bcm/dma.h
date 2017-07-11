// Copyright 2017 The Fuchsia Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#pragma once

#include <ddk/io-buffer.h>


// clang-format off
#define BCM_DMA_FLAGS_USE_MEM_INDEX (uint32_t)( 1 << 0)
#define BCM_DMA_FLAGS_CIRCULAR      (uint32_t)( 1 << 1)

#define BCM_DMA_DREQ_ID_NONE        (0)
#define BCM_DMA_DREQ_ID_DSI         (1)
#define BCM_DMA_DREQ_ID_PCM_TX      (2)
#define BCM_DMA_DREQ_ID_PCM_RX      (3)

#define BCM_DMA_CS_ACTIVE           (uint32_t)( 1 << 0 )
#define BCM_DMA_CS_INT              (uint32_t)( 1 << 2 )
#define BCM_DMA_CS_WAIT             (uint32_t)( 1 << 28)
#define BCM_DMA_CS_RESET            (uint32_t)( 1 << 31)

#define BCM_DMA_TI_SRC_INC          (uint32_t)( 1 << 8 )
#define BCM_DMA_TI_DEST_DREQ        (uint32_t)( 1 << 6 )
#define BCM_DMA_TI_WAIT_RESP        (uint32_t)( 1 << 3 )
#define BCM_DMA_TI_INTEN            (uint32_t)( 1 << 0 )

// clang-format on
typedef enum {
    BCM_DMA_STATE_SHUTDOWN = 0,
    BCM_DMA_STATE_INITIALIZED,
    BCM_DMA_STATE_READY,
    BCM_DMA_STATE_RUNNING
} bcm_dma_state_t;

typedef volatile struct {
    uint32_t transfer_info;
    uint32_t source_addr;
    uint32_t dest_addr;
    uint32_t transfer_len;
    uint32_t stride;
    uint32_t next_ctl_blk_addr;
    uint32_t reserved1;
    uint32_t reserved2;
} __PACKED bcm_dma_cb_t;

typedef volatile struct {
    uint32_t cs;
    uint32_t ctl_blk_addr;
    uint32_t transfer_info;
    uint32_t source_addr;
    uint32_t dest_addr;
    uint32_t transfer_len;
    uint32_t stride;
    uint32_t next_ctl_blk_addr;
    uint32_t debug;
    uint32_t reserved[55]; // 256 bytes (64 words) per channel control block.
} __PACKED bcm_dma_chan_t; //  Pad so we can lay them out as array (see below).

typedef volatile struct {
    bcm_dma_chan_t channels[15]; //note: the 16th DMA channel is not in this page
    uint8_t reserved[0xe0];
    uint32_t int_status;
    uint8_t reserved2[12];
    uint32_t enable;
} __PACKED bcm_dma_ctrl_regs_t;

typedef struct {
    mx_paddr_t paddr;
    uint32_t offset;
    uint32_t len;
} bcm_dma_vmo_index_t;

typedef void (*dma_cb_t)(void* arg);

typedef struct bcm_dma {
    uint32_t ch_num;
    io_buffer_t ctl_blks;
    io_buffer_t regs_buffer;
    bcm_dma_state_t state;
    mtx_t dma_lock;
    bcm_dma_vmo_index_t* mem_idx;
    uint32_t mem_idx_len;
    dma_cb_t callback;
    mx_handle_t irq_handle;
    thrd_t irq_thrd;
    volatile bool irq_thrd_stop;
} bcm_dma_t;

mx_status_t bcm_dma_start(bcm_dma_t* dma);
mx_status_t bcm_dma_stop(bcm_dma_t* dma);
mx_status_t bcm_dma_init(bcm_dma_t* dma, uint32_t ch);

/* Initialize a vmo->fifo transaction.  This assumes that the destination address
    is a non-incrementing physical address.

        vmo - the vmo containing the source data.
        t_info - transaction info (see BCM2835Datasheet.pdf)
        dest - physical address of destination.  This is most likely a peripheral fifo
            and if this is the case then t_info should be configured appropriately.
*/
mx_status_t bcm_dma_init_vmo_to_fifo_trans(bcm_dma_t* dma, mx_handle_t vmo, uint32_t t_info,
                                           mx_paddr_t dest, uint32_t flags);
void bcm_dma_deinit(bcm_dma_t* dma);

mx_status_t bcm_dma_paddr_to_offset(bcm_dma_t* dma, mx_paddr_t paddr, uint32_t* offset);
mx_paddr_t bcm_dma_get_position(bcm_dma_t* dma);