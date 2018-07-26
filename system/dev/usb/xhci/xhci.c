// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/debug.h>
#include <hw/arch_ops.h>
#include <hw/reg.h>
#include <zircon/types.h>
#include <zircon/syscalls.h>
#include <zircon/process.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <threads.h>
#include <unistd.h>

#include "xdc.h"
#include "xhci.h"
#include "xhci-device-manager.h"
#include "xhci-root-hub.h"
#include "xhci-transfer.h"
#include "xhci-util.h"

#define ROUNDUP_TO(x, multiple) ((x + multiple - 1) & ~(multiple - 1))
#define PAGE_ROUNDUP(x) ROUNDUP_TO(x, PAGE_SIZE)

// The Interrupter Moderation Interval prevents the controller from sending interrupts too often.
// According to XHCI Rev 1.1 4.17.2, the default is 4000 (= 1 ms). We set it to 1000 (= 250 us) to
// get better latency on completions for bulk transfers; setting it too low seems to destabilize the
// system.
#define XHCI_IMODI_VAL      1000

uint8_t xhci_endpoint_index(uint8_t ep_address) {
    if (ep_address == 0) return 0;
    uint32_t index = 2 * (ep_address & ~USB_ENDPOINT_DIR_MASK);
    if ((ep_address & USB_ENDPOINT_DIR_MASK) == USB_ENDPOINT_OUT)
        index--;
    return index;
}

// returns index into xhci->root_hubs[], or -1 if not a root hub
int xhci_get_root_hub_index(xhci_t* xhci, uint32_t device_id) {
    // regular devices have IDs 1 through xhci->max_slots
    // root hub IDs start at xhci->max_slots + 1
    int index = device_id - (xhci->max_slots + 1);
    if (index < 0 || index >= XHCI_RH_COUNT) return -1;
    return index;
}

static void xhci_read_extended_caps(xhci_t* xhci) {
    uint32_t* cap_ptr = NULL;
    while ((cap_ptr = xhci_get_next_ext_cap(xhci->mmio, cap_ptr, NULL))) {
        uint32_t cap_id = XHCI_GET_BITS32(cap_ptr, EXT_CAP_CAPABILITY_ID_START,
                                          EXT_CAP_CAPABILITY_ID_BITS);

        if (cap_id == EXT_CAP_SUPPORTED_PROTOCOL) {
            uint32_t rev_major = XHCI_GET_BITS32(cap_ptr, EXT_CAP_SP_REV_MAJOR_START,
                                                 EXT_CAP_SP_REV_MAJOR_BITS);
            uint32_t rev_minor = XHCI_GET_BITS32(cap_ptr, EXT_CAP_SP_REV_MINOR_START,
                                                 EXT_CAP_SP_REV_MINOR_BITS);
            zxlogf(TRACE, "EXT_CAP_SUPPORTED_PROTOCOL %d.%d\n", rev_major, rev_minor);

            uint32_t psic = XHCI_GET_BITS32(&cap_ptr[2], EXT_CAP_SP_PSIC_START,
                                            EXT_CAP_SP_PSIC_BITS);
            // psic = count of PSI registers
            uint32_t compat_port_offset = XHCI_GET_BITS32(&cap_ptr[2],
                                                          EXT_CAP_SP_COMPAT_PORT_OFFSET_START,
                                                          EXT_CAP_SP_COMPAT_PORT_OFFSET_BITS);
            uint32_t compat_port_count = XHCI_GET_BITS32(&cap_ptr[2],
                                                         EXT_CAP_SP_COMPAT_PORT_COUNT_START,
                                                         EXT_CAP_SP_COMPAT_PORT_COUNT_BITS);

            zxlogf(TRACE, "compat_port_offset: %d compat_port_count: %d psic: %d\n",
                    compat_port_offset, compat_port_count, psic);

            int rh_index;
            if (rev_major == 3) {
                rh_index = XHCI_RH_USB_3;
            } else if (rev_major == 2) {
                rh_index = XHCI_RH_USB_2;
            } else {
                zxlogf(ERROR, "unsupported rev_major in XHCI extended capabilities\n");
                rh_index = -1;
            }
            for (off_t i = 0; i < compat_port_count; i++) {
                off_t index = compat_port_offset + i - 1;
                if (index >= xhci->rh_num_ports) {
                    zxlogf(ERROR, "port index out of range in xhci_read_extended_caps\n");
                    break;
                }
                xhci->rh_map[index] = rh_index;
            }

            uint32_t* psi = &cap_ptr[4];
            for (uint32_t i = 0; i < psic; i++, psi++) {
                uint32_t psiv = XHCI_GET_BITS32(psi, EXT_CAP_SP_PSIV_START, EXT_CAP_SP_PSIV_BITS);
                uint32_t psie = XHCI_GET_BITS32(psi, EXT_CAP_SP_PSIE_START, EXT_CAP_SP_PSIE_BITS);
                uint32_t plt = XHCI_GET_BITS32(psi, EXT_CAP_SP_PLT_START, EXT_CAP_SP_PLT_BITS);
                uint32_t psim = XHCI_GET_BITS32(psi, EXT_CAP_SP_PSIM_START, EXT_CAP_SP_PSIM_BITS);
                zxlogf(TRACE, "PSI[%d] psiv: %d psie: %d plt: %d psim: %d\n", i, psiv, psie, plt, psim);
            }
        } else if (cap_id == EXT_CAP_USB_LEGACY_SUPPORT) {
            xhci->usb_legacy_support_cap = (xhci_usb_legacy_support_cap_t*)cap_ptr;
        }
    }
}

static zx_status_t xhci_claim_ownership(xhci_t* xhci) {
    xhci_usb_legacy_support_cap_t* cap = xhci->usb_legacy_support_cap;
    if (cap == NULL) {
        return ZX_OK;
    }

    // The XHCI spec defines this handoff protocol.  We need to wait at most one
    // second for the BIOS to respond.
    //
    // Note that bios_owned_sem and os_owned_sem are adjacent 1-byte fields, so
    // must be written to as single bytes to prevent the OS from modifying the
    // BIOS semaphore.  Additionally, all bits besides bit 0 in the OS semaphore
    // are RsvdP, so we need to preserve them on modification.
    cap->os_owned_sem |= 1;
    zx_time_t now = zx_clock_get_monotonic();
    zx_time_t deadline = now + ZX_SEC(1);
    while ((cap->bios_owned_sem & 1) && now < deadline) {
        zx_nanosleep(zx_deadline_after(ZX_MSEC(10)));
        now = zx_clock_get_monotonic();
    }

    if (cap->bios_owned_sem & 1) {
        cap->os_owned_sem &= ~1;
        return ZX_ERR_TIMED_OUT;
    }
    return ZX_OK;
}

static void xhci_vmo_release(zx_handle_t handle, zx_vaddr_t virt) {
    uint64_t size;
    zx_vmo_get_size(handle, &size);
    zx_vmar_unmap(zx_vmar_root_self(), virt, size);
    zx_handle_close(handle);
}

zx_status_t xhci_init(xhci_t* xhci, xhci_mode_t mode, uint32_t num_interrupts) {
    zx_status_t result = ZX_OK;

    list_initialize(&xhci->command_queue);
    mtx_init(&xhci->command_ring_lock, mtx_plain);
    mtx_init(&xhci->command_queue_mutex, mtx_plain);
    mtx_init(&xhci->mfindex_mutex, mtx_plain);
    mtx_init(&xhci->input_context_lock, mtx_plain);
    sync_completion_reset(&xhci->command_queue_completion);

    usb_request_pool_init(&xhci->free_reqs);

    xhci->cap_regs = (xhci_cap_regs_t*)xhci->mmio;
    xhci->op_regs = (xhci_op_regs_t*)((uint8_t*)xhci->cap_regs + xhci->cap_regs->length);
    xhci->doorbells = (uint32_t*)((uint8_t*)xhci->cap_regs + xhci->cap_regs->dboff);
    xhci->runtime_regs = (xhci_runtime_regs_t*)((uint8_t*)xhci->cap_regs + xhci->cap_regs->rtsoff);
    volatile uint32_t* hcsparams1 = &xhci->cap_regs->hcsparams1;
    volatile uint32_t* hcsparams2 = &xhci->cap_regs->hcsparams2;
    volatile uint32_t* hccparams1 = &xhci->cap_regs->hccparams1;
    volatile uint32_t* hccparams2 = &xhci->cap_regs->hccparams2;

    xhci->mode = mode;
    xhci->num_interrupts = num_interrupts;

    xhci->max_slots = XHCI_GET_BITS32(hcsparams1, HCSPARAMS1_MAX_SLOTS_START,
                                      HCSPARAMS1_MAX_SLOTS_BITS);
    xhci->rh_num_ports = XHCI_GET_BITS32(hcsparams1, HCSPARAMS1_MAX_PORTS_START,
                                         HCSPARAMS1_MAX_PORTS_BITS);
    xhci->context_size = (XHCI_READ32(hccparams1) & HCCPARAMS1_CSZ ? 64 : 32);
    xhci->large_esit = !!(XHCI_READ32(hccparams2) & HCCPARAMS2_LEC);

    uint32_t scratch_pad_bufs = XHCI_GET_BITS32(hcsparams2, HCSPARAMS2_MAX_SBBUF_HI_START,
                                                HCSPARAMS2_MAX_SBBUF_HI_BITS);
    scratch_pad_bufs <<= HCSPARAMS2_MAX_SBBUF_LO_BITS;
    scratch_pad_bufs |= XHCI_GET_BITS32(hcsparams2, HCSPARAMS2_MAX_SBBUF_LO_START,
                                        HCSPARAMS2_MAX_SBBUF_LO_BITS);
    xhci->page_size = XHCI_READ32(&xhci->op_regs->pagesize) << 12;

    // allocate array to hold our slots
    // add 1 to allow 1-based indexing of slots
    xhci->slots = (xhci_slot_t*)calloc(xhci->max_slots + 1, sizeof(xhci_slot_t));
    if (!xhci->slots) {
        result = ZX_ERR_NO_MEMORY;
        goto fail;
    }

    xhci->rh_map = (uint8_t *)calloc(xhci->rh_num_ports, sizeof(uint8_t));
    if (!xhci->rh_map) {
        result = ZX_ERR_NO_MEMORY;
        goto fail;
    }
    xhci->rh_port_map = (uint8_t *)calloc(xhci->rh_num_ports, sizeof(uint8_t));
    if (!xhci->rh_port_map) {
        result = ZX_ERR_NO_MEMORY;
        goto fail;
    }
    xhci_read_extended_caps(xhci);

    // We need to claim before we write to any other registers on the
    // controller, but after we've read the extended capabilities.
    result = xhci_claim_ownership(xhci);
    if (result != ZX_OK) {
        zxlogf(ERROR, "xhci_claim_ownership failed\n");
        goto fail;
    }

    // Allocate DMA memory for various things
    result = io_buffer_init(&xhci->dcbaa_erst_buffer, xhci->bti_handle, PAGE_SIZE,
                            IO_BUFFER_RW | IO_BUFFER_CONTIG | XHCI_IO_BUFFER_UNCACHED);
    if (result != ZX_OK) {
        zxlogf(ERROR, "io_buffer_init failed for xhci->dcbaa_erst_buffer\n");
        goto fail;
    }
    result = io_buffer_init(&xhci->input_context_buffer, xhci->bti_handle, PAGE_SIZE,
                            IO_BUFFER_RW | IO_BUFFER_CONTIG | XHCI_IO_BUFFER_UNCACHED);
    if (result != ZX_OK) {
        zxlogf(ERROR, "io_buffer_init failed for xhci->input_context_buffer\n");
        goto fail;
    }

    bool scratch_pad_is_contig = false;
    if (scratch_pad_bufs > 0) {
        // map scratchpad buffers read-only
        uint32_t flags = IO_BUFFER_RO;
        if (xhci->page_size > PAGE_SIZE) {
            flags |= IO_BUFFER_CONTIG;
            scratch_pad_is_contig = true;
        }
        size_t scratch_pad_pages_size = scratch_pad_bufs * xhci->page_size;
        result = io_buffer_init(&xhci->scratch_pad_pages_buffer, xhci->bti_handle,
                                scratch_pad_pages_size, flags);
        if (result != ZX_OK) {
            zxlogf(ERROR, "io_buffer_init failed for xhci->scratch_pad_pages_buffer\n");
            goto fail;
        }
        if (!scratch_pad_is_contig) {
            result = io_buffer_physmap(&xhci->scratch_pad_pages_buffer);
            if (result != ZX_OK) {
                zxlogf(ERROR, "io_buffer_physmap failed for xhci->scratch_pad_pages_buffer\n");
                goto fail;
            }
        }
        size_t scratch_pad_index_size = PAGE_ROUNDUP(scratch_pad_bufs * sizeof(uint64_t));
        result = io_buffer_init(&xhci->scratch_pad_index_buffer, xhci->bti_handle,
                                scratch_pad_index_size,
                                IO_BUFFER_RW | IO_BUFFER_CONTIG | XHCI_IO_BUFFER_UNCACHED);
        if (result != ZX_OK) {
            zxlogf(ERROR, "io_buffer_init failed for xhci->scratch_pad_index_buffer\n");
            goto fail;
        }
    }

    // set up DCBAA, ERST array and input context
    xhci->dcbaa = (uint64_t *)io_buffer_virt(&xhci->dcbaa_erst_buffer);
    xhci->dcbaa_phys = io_buffer_phys(&xhci->dcbaa_erst_buffer);
    xhci->input_context = (uint8_t *)io_buffer_virt(&xhci->input_context_buffer);
    xhci->input_context_phys = io_buffer_phys(&xhci->input_context_buffer);

    // DCBAA can only be 256 * sizeof(uint64_t) = 2048 bytes, so we have room for ERST array after DCBAA
    zx_off_t erst_offset = 256 * sizeof(uint64_t);

    size_t array_bytes = ERST_ARRAY_SIZE * sizeof(erst_entry_t);
    // MSI only supports up to 32 interupts, so the required ERST arrays will fit
    // within the page. Potentially more pages will need to be allocated for MSI-X.
    for (uint32_t i = 0; i < xhci->num_interrupts; i++) {
        // Ran out of space in page.
        if (erst_offset + array_bytes > PAGE_SIZE) {
            zxlogf(ERROR, "only have space for %u ERST arrays, want %u\n", i,
                    xhci->num_interrupts);
            goto fail;
        }
        xhci->erst_arrays[i] = (void *)xhci->dcbaa + erst_offset;
        xhci->erst_arrays_phys[i] = xhci->dcbaa_phys + erst_offset;
        // ERST arrays must be 64 byte aligned - see Table 54 in XHCI spec.
        // dcbaa_phys is already page (and hence 64 byte) aligned, so only
        // need to round the offset.
        erst_offset = ROUNDUP_TO(erst_offset + array_bytes, 64);
    }

    if (scratch_pad_bufs > 0) {
        uint64_t* scratch_pad_index = (uint64_t *)io_buffer_virt(&xhci->scratch_pad_index_buffer);
        off_t offset = 0;
        for (uint32_t i = 0; i < scratch_pad_bufs; i++) {
            zx_paddr_t scratch_pad_phys;
            if (scratch_pad_is_contig) {
                scratch_pad_phys = io_buffer_phys(&xhci->scratch_pad_pages_buffer) + offset;
            } else {
                size_t index = offset / PAGE_SIZE;
                size_t suboffset = offset & (PAGE_SIZE - 1);
                scratch_pad_phys = xhci->scratch_pad_pages_buffer.phys_list[index] + suboffset;
            }

            scratch_pad_index[i] = scratch_pad_phys;
            offset += xhci->page_size;
        }

        zx_paddr_t scratch_pad_index_phys = io_buffer_phys(&xhci->scratch_pad_index_buffer);
        xhci->dcbaa[0] = scratch_pad_index_phys;
    } else {
        xhci->dcbaa[0] = 0;
    }

    result = xhci_transfer_ring_init(&xhci->command_ring, xhci->bti_handle, COMMAND_RING_SIZE);
    if (result != ZX_OK) {
        zxlogf(ERROR, "xhci_command_ring_init failed\n");
        goto fail;
    }

    for (uint32_t i = 0; i < xhci->num_interrupts; i++) {
        result = xhci_event_ring_init(&xhci->event_rings[i], xhci->bti_handle,
                                      xhci->erst_arrays[i], EVENT_RING_SIZE);
        if (result != ZX_OK) {
            zxlogf(ERROR, "xhci_event_ring_init failed\n");
            goto fail;
        }
    }

    // initialize slots and endpoints
    for (uint32_t i = 1; i <= xhci->max_slots; i++) {
        xhci_slot_t* slot = &xhci->slots[i];
        xhci_endpoint_t* eps = slot->eps;
        for (int j = 0; j < XHCI_NUM_EPS; j++) {
            xhci_endpoint_t* ep = &eps[j];
            mtx_init(&ep->lock, mtx_plain);
            list_initialize(&ep->queued_reqs);
            list_initialize(&ep->pending_reqs);
            ep->current_req = NULL;
        }
    }

    // initialize virtual root hub devices
    for (int i = 0; i < XHCI_RH_COUNT; i++) {
        result = xhci_root_hub_init(xhci, i);
        if (result != ZX_OK) goto fail;
    }

    return ZX_OK;

fail:
    xhci_free(xhci);
    return result;
}

uint32_t xhci_get_max_interrupters(xhci_t* xhci) {
    xhci_cap_regs_t* cap_regs = (xhci_cap_regs_t*)xhci->mmio;
    volatile uint32_t* hcsparams1 = &cap_regs->hcsparams1;
    return XHCI_GET_BITS32(hcsparams1, HCSPARAMS1_MAX_INTRS_START,
                           HCSPARAMS1_MAX_INTRS_BITS);
}

int xhci_get_slot_ctx_state(xhci_slot_t* slot) {
    return XHCI_GET_BITS32(&slot->sc->sc3, SLOT_CTX_SLOT_STATE_START,
                           SLOT_CTX_CONTEXT_ENTRIES_BITS);
}

int xhci_get_ep_ctx_state(xhci_slot_t* slot, xhci_endpoint_t* ep) {
    if (!ep->epc) {
        return EP_CTX_STATE_DISABLED;
    }
    return XHCI_GET_BITS32(&ep->epc->epc0, EP_CTX_EP_STATE_START, EP_CTX_EP_STATE_BITS);
}

static void xhci_update_erdp(xhci_t* xhci, int interrupter) {
    xhci_event_ring_t* er = &xhci->event_rings[interrupter];
    xhci_intr_regs_t* intr_regs = &xhci->runtime_regs->intr_regs[interrupter];

    uint64_t erdp = xhci_event_ring_current_phys(er);
    erdp |= ERDP_EHB; // clear event handler busy
    XHCI_WRITE64(&intr_regs->erdp, erdp);
}

static void xhci_interrupter_init(xhci_t* xhci, int interrupter) {
    xhci_intr_regs_t* intr_regs = &xhci->runtime_regs->intr_regs[interrupter];

    xhci_update_erdp(xhci, interrupter);

    XHCI_SET32(&intr_regs->iman, IMAN_IE, IMAN_IE);
    XHCI_SET32(&intr_regs->imod, IMODI_MASK, XHCI_IMODI_VAL);
    XHCI_SET32(&intr_regs->erstsz, ERSTSZ_MASK, ERST_ARRAY_SIZE);
    XHCI_WRITE64(&intr_regs->erstba, xhci->erst_arrays_phys[interrupter]);
}

void xhci_wait_bits(volatile uint32_t* ptr, uint32_t bits, uint32_t expected) {
    uint32_t value = XHCI_READ32(ptr);
    while ((value & bits) != expected) {
        usleep(1000);
        value = XHCI_READ32(ptr);
    }
}

void xhci_wait_bits64(volatile uint64_t* ptr, uint64_t bits, uint64_t expected) {
    uint64_t value = XHCI_READ64(ptr);
    while ((value & bits) != expected) {
        usleep(1000);
        value = XHCI_READ64(ptr);
    }
}

void xhci_set_dbcaa(xhci_t* xhci, uint32_t slot_id, zx_paddr_t paddr) {
    XHCI_WRITE64(&xhci->dcbaa[slot_id], paddr);
}

zx_status_t xhci_start(xhci_t* xhci) {
    volatile uint32_t* usbcmd = &xhci->op_regs->usbcmd;
    volatile uint32_t* usbsts = &xhci->op_regs->usbsts;

    xhci_wait_bits(usbsts, USBSTS_CNR, 0);

    // stop controller
    XHCI_SET32(usbcmd, USBCMD_RS, 0);
    // wait until USBSTS_HCH signals we stopped
    xhci_wait_bits(usbsts, USBSTS_HCH, USBSTS_HCH);

    XHCI_SET32(usbcmd, USBCMD_HCRST, USBCMD_HCRST);
    xhci_wait_bits(usbcmd, USBCMD_HCRST, 0);
    xhci_wait_bits(usbsts, USBSTS_CNR, 0);

    if (xhci->mode == XHCI_PCI_MSI || xhci->mode == XHCI_PCI_LEGACY) {
        // enable bus master
        zx_status_t status = pci_enable_bus_master(&xhci->pci, true);
        if (status < 0) {
            zxlogf(ERROR, "usb_xhci_bind enable_bus_master failed %d\n", status);
            return status;
        }
    }

    // setup operational registers
    xhci_op_regs_t* op_regs = xhci->op_regs;
    // initialize command ring
    uint64_t crcr = xhci_transfer_ring_start_phys(&xhci->command_ring);
    if (xhci->command_ring.pcs) {
        crcr |= CRCR_RCS;
    }
    XHCI_WRITE64(&op_regs->crcr, crcr);

    XHCI_WRITE64(&op_regs->dcbaap, xhci->dcbaa_phys);
    XHCI_SET_BITS32(&op_regs->config, CONFIG_MAX_SLOTS_ENABLED_START,
                    CONFIG_MAX_SLOTS_ENABLED_BITS, xhci->max_slots);

    // initialize interrupters
    for (uint32_t i = 0; i < xhci->num_interrupts; i++) {
        xhci_interrupter_init(xhci, i);
    }

    // start the controller with interrupts and mfindex wrap events enabled
    uint32_t start_flags = USBCMD_RS | USBCMD_INTE | USBCMD_EWE;
    XHCI_SET32(usbcmd, start_flags, start_flags);
    xhci_wait_bits(usbsts, USBSTS_HCH, 0);

    xhci_start_device_thread(xhci);

#if defined(__x86_64__)
    // TODO(jocelyndang): start xdc in a new process.
    zx_status_t status = xdc_bind(xhci->zxdev, xhci->bti_handle, xhci->mmio);
    if (status != ZX_OK) {
        zxlogf(ERROR, "xhci_start: xdc_bind failed %d\n", status);
    }
#endif

    return ZX_OK;
}

static void xhci_slot_stop(xhci_slot_t* slot) {
    for (int i = 0; i < XHCI_NUM_EPS; i++) {
        xhci_endpoint_t* ep = &slot->eps[i];

        mtx_lock(&ep->lock);
        if (ep->state != EP_STATE_DEAD) {
            usb_request_t* req;
            while ((req = list_remove_tail_type(&ep->pending_reqs, usb_request_t, node)) != NULL) {
                usb_request_complete(req, ZX_ERR_IO_NOT_PRESENT, 0);
            }
            while ((req = list_remove_tail_type(&ep->queued_reqs, usb_request_t, node)) != NULL) {
                usb_request_complete(req, ZX_ERR_IO_NOT_PRESENT, 0);
            }
            ep->state = EP_STATE_DEAD;
        }
        mtx_unlock(&ep->lock);
    }
}

void xhci_stop(xhci_t* xhci) {
    volatile uint32_t* usbcmd = &xhci->op_regs->usbcmd;
    volatile uint32_t* usbsts = &xhci->op_regs->usbsts;

    // stop device thread and root hubs before turning off controller
    xhci_stop_device_thread(xhci);
    xhci_stop_root_hubs(xhci);

    // stop controller
    XHCI_SET32(usbcmd, USBCMD_RS, 0);
    // wait until USBSTS_HCH signals we stopped
    xhci_wait_bits(usbsts, USBSTS_HCH, USBSTS_HCH);

    for (uint32_t i = 1; i <= xhci->max_slots; i++) {
        xhci_slot_stop(&xhci->slots[i]);
    }
}

void xhci_free(xhci_t* xhci) {
    for (uint32_t i = 1; i <= xhci->max_slots; i++) {
        xhci_slot_t* slot = &xhci->slots[i];
        io_buffer_release(&slot->buffer);

        for (int j = 0; j < XHCI_NUM_EPS; j++) {
            xhci_endpoint_t* ep = &slot->eps[j];
            xhci_transfer_ring_free(&ep->transfer_ring);
        }
    }
    free(xhci->slots);

     for (int i = 0; i < XHCI_RH_COUNT; i++) {
        xhci_root_hub_free(&xhci->root_hubs[i]);
    }
    free(xhci->rh_map);
    free(xhci->rh_port_map);

    for (uint32_t i = 0; i < xhci->num_interrupts; i++) {
        xhci_event_ring_free(&xhci->event_rings[i]);
    }

    xhci_transfer_ring_free(&xhci->command_ring);
    io_buffer_release(&xhci->dcbaa_erst_buffer);
    io_buffer_release(&xhci->input_context_buffer);
    io_buffer_release(&xhci->scratch_pad_pages_buffer);
    io_buffer_release(&xhci->scratch_pad_index_buffer);

    // this must done after releasing anything that relies
    // on our bti, like our io_buffers
    zx_handle_close(xhci->bti_handle);

    free(xhci);
}

void xhci_post_command(xhci_t* xhci, uint32_t command, uint64_t ptr, uint32_t control_bits,
                       xhci_command_context_t* context) {
    // FIXME - check that command ring is not full?

    mtx_lock(&xhci->command_ring_lock);

    xhci_transfer_ring_t* cr = &xhci->command_ring;
    xhci_trb_t* trb = cr->current;
    int index = trb - cr->start;
    xhci->command_contexts[index] = context;

    XHCI_WRITE64(&trb->ptr, ptr);
    XHCI_WRITE32(&trb->status, 0);
    trb_set_control(trb, command, control_bits);

    xhci_increment_ring(cr);

    hw_mb();
    XHCI_WRITE32(&xhci->doorbells[0], 0);

    mtx_unlock(&xhci->command_ring_lock);
}

static void xhci_handle_command_complete_event(xhci_t* xhci, xhci_trb_t* event_trb) {
    xhci_trb_t* command_trb = xhci_read_trb_ptr(&xhci->command_ring, event_trb);
    uint32_t cc = XHCI_GET_BITS32(&event_trb->status, EVT_TRB_CC_START, EVT_TRB_CC_BITS);
    zxlogf(TRACE, "xhci_handle_command_complete_event slot_id: %d command: %d cc: %d\n",
            (event_trb->control >> TRB_SLOT_ID_START), trb_get_type(command_trb), cc);

    int index = command_trb - xhci->command_ring.start;

    if (cc == TRB_CC_COMMAND_RING_STOPPED) {
        // TRB_CC_COMMAND_RING_STOPPED is generated after aborting a command.
        // Ignore this, since it is unrelated to the next command in the command ring.
        return;
    }

    mtx_lock(&xhci->command_ring_lock);
    xhci_command_context_t* context = xhci->command_contexts[index];
    xhci->command_contexts[index] = NULL;
    mtx_unlock(&xhci->command_ring_lock);

    context->callback(context->data, cc, command_trb, event_trb);
}

static void xhci_handle_mfindex_wrap(xhci_t* xhci) {
    mtx_lock(&xhci->mfindex_mutex);
    xhci->mfindex_wrap_count++;
    xhci->last_mfindex_wrap = zx_clock_get_monotonic();
    mtx_unlock(&xhci->mfindex_mutex);
}

uint64_t xhci_get_current_frame(xhci_t* xhci) {
    mtx_lock(&xhci->mfindex_mutex);

    uint32_t mfindex = XHCI_READ32(&xhci->runtime_regs->mfindex) & ((1 << XHCI_MFINDEX_BITS) - 1);
    uint64_t wrap_count = xhci->mfindex_wrap_count;
    // try to detect race condition where mfindex has wrapped but we haven't processed wrap event yet
    if (mfindex < 500) {
        if (zx_clock_get_monotonic() - xhci->last_mfindex_wrap > ZX_MSEC(1000)) {
            zxlogf(TRACE, "woah, mfindex wrapped before we got the event!\n");
            wrap_count++;
        }
    }
    mtx_unlock(&xhci->mfindex_mutex);

    // shift three to convert from 125us microframes to 1ms frames
    return ((wrap_count * (1 << XHCI_MFINDEX_BITS)) + mfindex) >> 3;
}

static void xhci_handle_events(xhci_t* xhci, int interrupter) {
    xhci_event_ring_t* er = &xhci->event_rings[interrupter];

    // process all TRBs with cycle bit matching our CCS
    while ((XHCI_READ32(&er->current->control) & TRB_C) == er->ccs) {
        uint32_t type = trb_get_type(er->current);
        switch (type) {
        case TRB_EVENT_COMMAND_COMP:
            xhci_handle_command_complete_event(xhci, er->current);
            break;
        case TRB_EVENT_PORT_STATUS_CHANGE:
            xhci_handle_root_hub_change(xhci);
            break;
        case TRB_EVENT_TRANSFER:
            xhci_handle_transfer_event(xhci, er->current);
            break;
        case TRB_EVENT_MFINDEX_WRAP:
            xhci_handle_mfindex_wrap(xhci);
            break;
        default:
            zxlogf(ERROR, "xhci_handle_events: unhandled event type %d\n", type);
            break;
        }

        er->current++;
        if (er->current == er->end) {
            er->current = er->start;
            er->ccs ^= TRB_C;
        }
    }

    // update event ring dequeue pointer and clear event handler busy flag
    xhci_update_erdp(xhci, interrupter);
}

void xhci_handle_interrupt(xhci_t* xhci, uint32_t interrupter) {
    // clear the interrupt pending flag
    xhci_intr_regs_t* intr_regs = &xhci->runtime_regs->intr_regs[interrupter];
    XHCI_WRITE32(&intr_regs->iman, IMAN_IE | IMAN_IP);

    xhci_handle_events(xhci, interrupter);
}
