// Copyright 2017 The Fuchsia Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <magenta/compiler.h>
#include <magenta/assert.h>
#include <magenta/syscalls.h>
#include <magenta/threads.h>

#include <ddk/binding.h>
#include <ddk/device.h>
#include <ddk/driver.h>

#include <bcm/bcm28xx.h>
#include <bcm/dma.h>

// clang-format off
#if TRACE
#define xprintf(fmt...) printf("BCMDMA: "fmt)
#else
#define xprintf(fmt...) \
    do {                \
    } while (0)
#endif

#define BCM_DMA_PAGE_SIZE               (4096)
#define BCM_DMA_NUM_CONTROL_BLOCKS      (64)
#define BCM_DMA_MAX_CH                  (14)
// clang-format on

static bcm_dma_ctrl_regs_t* dma_regs;

static int dma_irq_thread(void* arg) {
    bcm_dma_t* dma = arg;
    mx_handle_t irq_handle = dma->irq_handle;
    xprintf("dma interrupt thread started\n");

    mx_status_t stat;

    while (!dma->irq_thrd_stop) {

        mx_interrupt_complete(irq_handle);
        stat = mx_interrupt_wait(irq_handle);
        MX_DEBUG_ASSERT(stat == MX_OK);

        dma_regs->channels[dma->ch_num].cs |= BCM_DMA_CS_INT;
        if (stat != MX_OK) {
            xprintf("dma interrupt wait failed = %d\n", stat);
            break;
        }
        if (dma->irq_thrd_stop)
            break;
        if (dma->callback)
            (dma->callback)(dma);
    }

    dma->irq_thrd_stop = false;
    xprintf("dma interrupt thread quitting\n");
    return 0;
}

mx_status_t bcm_dma_init(bcm_dma_t* dma, uint32_t ch) {

    xprintf("Initializing dma channel %u\n", ch);
    mx_status_t status;
    mx_handle_t irq_handle = MX_HANDLE_INVALID;

    mtx_lock(&dma->dma_lock);

    if (dma->state > BCM_DMA_STATE_SHUTDOWN) {
        mtx_unlock(&dma->dma_lock);
        return MX_ERR_BAD_STATE;
    }

    static_assert(BCM_DMA_MAX_CH < countof(dma_regs->channels),
                            "DMA channel out of range");
    if (ch > BCM_DMA_MAX_CH) { // Don't use ch 15 as it has different properties
        mtx_unlock(&dma->dma_lock);
        return MX_ERR_INVALID_ARGS;
    }

    if (dma_regs == NULL) {
        status = io_buffer_init_physical(&dma->regs_buffer, DMA_BASE, BCM_DMA_PAGE_SIZE,
            get_root_resource(), MX_CACHE_POLICY_UNCACHED_DEVICE);
        if (status != MX_OK) {
            goto dma_init_err;
        }
        dma_regs = io_buffer_virt(&dma->regs_buffer);
    }

    xprintf("Initializing control block buffers\n");
    // pre-init the control block buffer
    status = io_buffer_init(&dma->ctl_blks,
                            BCM_DMA_NUM_CONTROL_BLOCKS * sizeof(bcm_dma_cb_t),
                            IO_BUFFER_RW);
    if (status != MX_OK) {
        xprintf("\nBCM_DMA: Error Allocating control blocks: %d\n", status);
        goto dma_init_err;
    }

    dma->mem_idx = NULL;
    dma->ch_num = ch;
    dma->callback = NULL;

    xprintf("Initializing interrupt handler\n");
    status = mx_interrupt_create(get_root_resource(),
                                     INTERRUPT_DMA0 + ch,
                                     MX_FLAG_REMAP_IRQ,
                                     &irq_handle);
    if (status != MX_OK) {
        xprintf("bcm-dma: failed to create interrupt handle, handle = %d\n",
                status);
        goto dma_init_err;
    }

    dma->irq_handle = irq_handle;

    dma_regs->channels[dma->ch_num].cs = BCM_DMA_CS_RESET; //reset the channel

    // Create a thread to handle IRQs.
    xprintf("Creating interrupt thread\n");
    char thrd_name[20];
    snprintf(thrd_name, sizeof(thrd_name), "dma%02u_irq_thrd", ch);
    int thrd_rc = thrd_create_with_name(&dma->irq_thrd, dma_irq_thread, dma,
                                        thrd_name);
    if (thrd_rc != thrd_success) {
        xprintf("failed to create irq thread\n");
        status = thrd_status_to_mx_status(thrd_rc);
        goto dma_init_err;
    }

    dma->state = BCM_DMA_STATE_INITIALIZED;

    mtx_unlock(&dma->dma_lock);
    return MX_OK;

dma_init_err:
    mx_handle_close(dma->irq_handle);
    dma->irq_handle = MX_HANDLE_INVALID;

    if (io_buffer_is_valid(&dma->ctl_blks)) {
        io_buffer_release(&dma->ctl_blks);
    }
    if (irq_handle > 0) {
        mx_handle_close(irq_handle);
    }

    mtx_unlock(&dma->dma_lock);
    return status;
}

mx_paddr_t bcm_dma_get_position(bcm_dma_t* dma) {
    /* .source_address reports the physical bus address of the memory location,
            which doesn't neccesarily equal the physical memory address as observed
            by the ARM cores (depending on L2 configuration).  The base address
            of the bus mapping is BCM_SDRAM_BUS_ADDR_BASE (0xc0000000). We use
            BCM_PHYS_ADDR_MASK to make to the phys address needed by the cpu.
    */
    uint32_t address = (dma_regs->channels[dma->ch_num].source_addr) & BCM_PHYS_ADDR_MASK;
    return (mx_paddr_t)address;
}

mx_status_t bcm_dma_paddr_to_offset(bcm_dma_t* dma, mx_paddr_t paddr, uint32_t* offset) {

    // This call only works if an index was created for the memory object
    if (!dma->mem_idx) {
        return MX_ERR_BAD_STATE;
    }

    for (uint32_t i = 0; i < dma->mem_idx_len; i++) {
        if ((paddr >= dma->mem_idx[i].paddr) && (paddr < (dma->mem_idx[i].paddr + dma->mem_idx[i].len))) {
            *offset = dma->mem_idx[i].offset + (paddr - dma->mem_idx[i].paddr);
            return MX_OK;
        }
    }
    return MX_ERR_OUT_OF_RANGE;
}

/* Builds index of vmo pages.  This is used to translate physical addresses
    reported by the dma status into offsets into the memory object used for the
    transaction.
*/
static mx_status_t bcm_dma_build_mem_index(bcm_dma_t* dma, mx_paddr_t* page_list, uint32_t len) {

    dma->mem_idx = calloc(len, sizeof(bcm_dma_vmo_index_t)); //Allocate worst case sized array
    MX_DEBUG_ASSERT(dma->mem_idx);
    if (!dma->mem_idx)
        return MX_ERR_NO_MEMORY;

    dma->mem_idx_len = 0;

    for (uint32_t i = 0; i < len; i++) {
        uint32_t j;

        for (j = 0; ((page_list[i] > dma->mem_idx[j].paddr) && (j < dma->mem_idx_len)); j++)
            ;

        if ((j != 0) &&
            ((i * BCM_DMA_PAGE_SIZE) == (dma->mem_idx[j - 1].offset + dma->mem_idx[j - 1].len)) &&
            ((page_list[i] == (dma->mem_idx[j - 1].paddr + dma->mem_idx[j - 1].len)))) {

            dma->mem_idx[j - 1].len += BCM_DMA_PAGE_SIZE;

        } else {

            if (j < dma->mem_idx_len) {
                memmove(&dma->mem_idx[j + 1], &dma->mem_idx[j],
                        (dma->mem_idx_len - j)*sizeof(bcm_dma_vmo_index_t));
            }
            dma->mem_idx[j].paddr = page_list[i];
            dma->mem_idx[j].offset = i * BCM_DMA_PAGE_SIZE;
            dma->mem_idx[j].len = BCM_DMA_PAGE_SIZE;
            dma->mem_idx_len++;
        }
    }
    return MX_OK;
}

mx_status_t bcm_dma_init_vmo_to_fifo_trans(bcm_dma_t* dma, mx_handle_t vmo, uint32_t t_info,
                                           mx_paddr_t dest, uint32_t flags) {
    mx_paddr_t* buf_pages = NULL;
    xprintf("Linking vmo to fifo...\n");
    mtx_lock(&dma->dma_lock);

    if (dma->state < BCM_DMA_STATE_INITIALIZED) {
        mtx_unlock(&dma->dma_lock);
        return MX_ERR_BAD_STATE;
    }

    size_t buffsize;
    mx_status_t status = mx_vmo_get_size(vmo, &buffsize);
    if (status != MX_OK) {
        goto dma_link_err;
    }

    uint32_t num_pages = (buffsize + BCM_DMA_PAGE_SIZE - 1) / BCM_DMA_PAGE_SIZE;

    MX_DEBUG_ASSERT(num_pages <= BCM_DMA_NUM_CONTROL_BLOCKS);
    if (num_pages > BCM_DMA_NUM_CONTROL_BLOCKS) {
        status = MX_ERR_NO_MEMORY;
        goto dma_link_err;
    }

    buf_pages = calloc(num_pages, sizeof(mx_paddr_t));
    if (!buf_pages) {
        status = MX_ERR_NO_MEMORY;
        goto dma_link_err;
    }

    status = mx_vmo_op_range(vmo, MX_VMO_OP_LOOKUP, 0, buffsize,
                             buf_pages, sizeof(mx_paddr_t) * num_pages);
    if (status != MX_OK)
        goto dma_link_err;

    if (flags & BCM_DMA_FLAGS_USE_MEM_INDEX) {
        status = bcm_dma_build_mem_index(dma, buf_pages, num_pages);
        if (status != MX_OK)
            goto dma_link_err;
    }

    ssize_t total_bytes = buffsize;

    // Create the control blocks for this vmo.  control block iobuffer was inited when
    //   dma object was inited.  Currently creates one control block for each page of
    //   memory in the memory object.

    bcm_dma_cb_t* cb = (bcm_dma_cb_t*)io_buffer_virt(&dma->ctl_blks);
    // bus address of the control block buffer
    uint32_t cb_bus_addr = (uint32_t)io_buffer_phys(&dma->ctl_blks) | BCM_SDRAM_BUS_ADDR_BASE;

    for (uint32_t i = 0; i < num_pages; i++) {

        cb[i].transfer_info = t_info;

        cb[i].source_addr = (uint32_t)(buf_pages[i] | BCM_SDRAM_BUS_ADDR_BASE);
        cb[i].dest_addr = (uint32_t)dest;

        uint32_t tfer_len = (total_bytes > BCM_DMA_PAGE_SIZE) ? BCM_DMA_PAGE_SIZE : total_bytes;
        cb[i].transfer_len = tfer_len;
        total_bytes -= tfer_len;

        if ((total_bytes > 0) && (i < (num_pages - 1))) {
            cb[i].next_ctl_blk_addr = cb_bus_addr + (sizeof(bcm_dma_cb_t) * (i + 1));
        } else {
            cb[i].next_ctl_blk_addr = (flags & BCM_DMA_FLAGS_CIRCULAR) ? cb_bus_addr : 0;
            if (dma->callback)
                cb[i].transfer_info |= BCM_DMA_TI_INTEN;
        }
    }

    io_buffer_cache_op(&dma->ctl_blks, MX_VMO_OP_CACHE_CLEAN, 0, num_pages * sizeof(bcm_dma_cb_t));

    dma->state = BCM_DMA_STATE_READY;

    goto dma_link_ret;

dma_link_err:
    if (dma->mem_idx) {
        free(dma->mem_idx);
        dma->mem_idx_len = 0;
    }
    dma->mem_idx = NULL;
dma_link_ret:
    if (buf_pages)
        free(buf_pages);
    mtx_unlock(&dma->dma_lock);
    return status;
}

mx_status_t bcm_dma_start(bcm_dma_t* dma) {

    xprintf("starting dma channel %u\n", dma->ch_num);
    mtx_lock(&dma->dma_lock);
    if ((dma_regs == NULL) || (dma->state != BCM_DMA_STATE_READY)) {
        mtx_unlock(&dma->dma_lock);
        return MX_ERR_BAD_STATE;
    }

    dma_regs->channels[dma->ch_num].ctl_blk_addr =
        (uint32_t)(io_buffer_phys(&dma->ctl_blks) | BCM_SDRAM_BUS_ADDR_BASE);

    dma_regs->channels[dma->ch_num].cs |= (BCM_DMA_CS_ACTIVE | BCM_DMA_CS_WAIT);

    dma->state = BCM_DMA_STATE_RUNNING;
    mtx_unlock(&dma->dma_lock);
    return MX_OK;
}

mx_status_t bcm_dma_stop(bcm_dma_t* dma) {
    xprintf("Stopping dma channel %u\n", dma->ch_num);
    mtx_lock(&dma->dma_lock);

    if ((dma_regs == NULL) || (dma->state < BCM_DMA_STATE_READY)) {
        mtx_unlock(&dma->dma_lock);
        return MX_ERR_BAD_STATE;
    }

    dma_regs->channels[dma->ch_num].cs &= ~BCM_DMA_CS_ACTIVE;
    dma->state = BCM_DMA_STATE_READY;

    mtx_unlock(&dma->dma_lock);
    return MX_OK;
}

void bcm_dma_deinit(bcm_dma_t* dma) {

    MX_DEBUG_ASSERT(dma);
    xprintf("Deiniting dma channel %u\n", dma->ch_num);

    mtx_lock(&dma->dma_lock);

    if (dma->irq_handle != MX_HANDLE_INVALID) {
        //shut down the irq thread
        xprintf("Shutting down irq thread\n");
        dma->irq_thrd_stop = true;
        //Signal the interrupt since the thread is waiting on it.
        mx_interrupt_signal(dma->irq_handle);
        thrd_join(dma->irq_thrd, NULL);
        xprintf("irq thread shut down\n");

        //Release the irq handle
        mx_handle_close(dma->irq_handle);
        dma->irq_handle = MX_HANDLE_INVALID;
    }

    dma_regs->channels[dma->ch_num].cs &= ~BCM_DMA_CS_ACTIVE;

    //Reset the hardware
    dma_regs->channels[dma->ch_num].cs = BCM_DMA_CS_RESET;
    dma_regs->channels[dma->ch_num].ctl_blk_addr = (uint32_t)(0);

    //Release whatever memory we are sitting on
    if (dma->mem_idx) {
        free(dma->mem_idx);
        dma->mem_idx = NULL;
    }
    dma->mem_idx_len = 0;

    if (io_buffer_is_valid(&dma->ctl_blks)) {
        io_buffer_release(&dma->ctl_blks);
    }

    dma->state = BCM_DMA_STATE_SHUTDOWN;

    mtx_unlock(&dma->dma_lock);

}
