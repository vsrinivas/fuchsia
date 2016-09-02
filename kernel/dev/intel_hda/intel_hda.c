// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#if !WITH_KERNEL_VM
/* TODO(johngro) : Make this work without a VM */
#error Intel HDA Controller driver depends on the kernel VM module!
#endif

#include <arch/ops.h>
#include <debug.h>
#include <dev/pcie.h>
#include <kernel/event.h>
#include <kernel/mutex.h>
#include <kernel/spinlock.h>
#include <kernel/thread.h>
#include <kernel/vm.h>
#include <lk/init.h>
#include <list.h>
#include <malloc.h>
#include <platform.h>
#include <trace.h>

#include "intel_hda.h"

#define LOCAL_TRACE 0

#define INTEL_HDA_VID (0x8086)
#define INTEL_HDA_DID (0x2668)
#define INTEL_HDA_RESET_HOLD_TIME_USEC          (100)   // Section 5.5.1.2
#define INTEL_HDA_RESET_TIMEOUT_USEC            (1000)  // Arbitrary
#define INTEL_HDA_RING_BUF_RESET_TIMEOUT_USEC   (1000)  // Arbitrary
#define INTEL_HDA_CODEC_DISCOVERY_WAIT_USEC     (521)   // Section 4.3
#define INTEL_HDA_WORK_THREAD_NAME "Intel HDA Driver"
#ifndef INTEL_HDA_WORK_THREAD_PRIORITY
#define INTEL_HDA_WORK_THREAD_PRIORITY HIGH_PRIORITY
#endif  // INTEL_HDA_WORK_THREAD_PRIORITY

#ifndef INTEL_HDA_QEMU_QUIRKS
#define INTEL_HDA_QEMU_QUIRKS 0
#endif  // INTEL_HDA_QEMU_QUIRKS

#define INTEL_HDA_RIRB_RESERVED_RESPONSE_SLOTS (8u)

static int intel_hda_work_thread(void *arg);
static void intel_hda_module_init(uint level);

typedef struct intel_hda_module_state {
    mutex_t             device_list_lock;
    struct list_node    device_list;

    spin_lock_t         pending_work_list_lock;
    struct list_node    pending_work_list;

    mutex_t             work_thread_lock;
    event_t             work_thread_wakeup;
    bool                work_thread_quit;
    thread_t*           work_thread;

    int                 dev_id_gen;
} intel_hda_module_state_t;

static intel_hda_module_state_t g_module_state;
LK_INIT_HOOK(intel_hda_init, intel_hda_module_init, LK_INIT_LEVEL_KERNEL);

static void intel_hda_module_init(uint level) {
    intel_hda_module_state_t* mod = &g_module_state;

    mutex_init     (&mod->device_list_lock);
    list_initialize(&mod->device_list);

    spin_lock_init (&mod->pending_work_list_lock);
    list_initialize(&mod->pending_work_list);

    mutex_init     (&mod->work_thread_lock);
    event_init     (&mod->work_thread_wakeup, false, 0);
    mod->work_thread_quit = false;
    mod->dev_id_gen = 0;
    mod->work_thread = thread_create(INTEL_HDA_WORK_THREAD_NAME,
                                     intel_hda_work_thread,
                                     mod,
                                     INTEL_HDA_WORK_THREAD_PRIORITY,
                                     DEFAULT_STACK_SIZE);
    DEBUG_ASSERT(mod->work_thread);
    thread_resume(mod->work_thread);
}

static void intel_hda_module_unload(void) {
    /* TODO(johngro): finish this someday.  Right now, driver modules cannot
     * unload, so this is mostly just a comment which the compiler checks and
     * the linker drops.  In order to even be able to do this properly, we would
     * need to be able to dynamically unregister this driver with the PCIe bus
     * driver, and make sure that all instances of our devices have been shut
     * down before proceeding.
     */
    intel_hda_module_state_t* mod = &g_module_state;
    DEBUG_ASSERT(list_is_empty(&mod->device_list));

    /* shutdown the work thread */
    spin_lock_saved_state_t spinlock_state;
    spin_lock_irqsave(&mod->pending_work_list_lock, spinlock_state);
    mod->work_thread_quit = true;
    event_signal(&mod->work_thread_wakeup, true);
    spin_unlock_irqrestore(&mod->pending_work_list_lock, spinlock_state);

    int retcode;
    status_t res;
    res = thread_join(mod->work_thread, &retcode, 10);
    if (NO_ERROR != res) {
        dprintf(CRITICAL, "Failed to shutdown Intel HDA module work thread (res %d)\n", res);
    }
}

static bool intel_hda_reset(intel_hda_device_t* dev, bool assert) {
    hda_registers_t* r = dev->regs;
    uint32_t expected;
    lk_bigtime_t start;

    if (assert) {
        expected = 0;
        REG_CLR_BITS(32, r, gctl, HDA_REG_GCTL_HWINIT);
    } else {
        expected = HDA_REG_GCTL_HWINIT;
        REG_SET_BITS(32, r, gctl, HDA_REG_GCTL_HWINIT);
    }

    start = current_time_hires();
    while ((REG_RD(32, r, gctl) & HDA_REG_GCTL_HWINIT) != expected) {
        if ((current_time_hires() - start) >= INTEL_HDA_RESET_TIMEOUT_USEC) {
            LTRACEF("Timeout waiting for device to exit reset\n");
            return false;
        }
    }

    return true;
}

static bool intel_hda_do_reset_cycle(intel_hda_device_t* dev) {
    if (!intel_hda_reset(dev, true))
        return false;

    spin(INTEL_HDA_RESET_HOLD_TIME_USEC);

    if (!intel_hda_reset(dev, false))
        return false;

    spin(INTEL_HDA_CODEC_DISCOVERY_WAIT_USEC);

    return true;
}

static inline status_t intel_hda_reset_corbrp(intel_hda_device_t* dev) {
    DEBUG_ASSERT(dev);
    hda_registers_t* r = dev->regs;
    DEBUG_ASSERT(r);

#if INTEL_HDA_QEMU_QUIRKS
    /* See Section 3.3.21
     *
     * QEMU does not properly implement the emulated hardware behavior for
     * resetting the pretend CORB read pointer.  Just write a 0 to the register
     * and leave it at that.
     */
    REG_WR(16, r, corbrp, 0);
#else
    lk_bigtime_t start;

    /* Set the reset bit, wait for the hardware to respond by setting the bit in
     * the readback. */
    REG_WR(16, r, corbrp, HDA_REG_CORBRP_RST);
    start = current_time_hires();
    while (!(REG_RD(16, r, corbrp) & HDA_REG_CORBRP_RST)) {
        if ((current_time_hires() - start) >= INTEL_HDA_RING_BUF_RESET_TIMEOUT_USEC) {
            LTRACEF("Timeout waiting for ring buffer reset ack\n");
            return ERR_TIMED_OUT;
        }
    }

    /* Clear the reset bit, wait for the hardware to respond by clearing the bit
     * in the readback. */
    REG_WR(16, r, corbrp, 0);
    start = current_time_hires();
    while (REG_RD(16, r, corbrp) & HDA_REG_CORBRP_RST) {
        if ((current_time_hires() - start) >= INTEL_HDA_RING_BUF_RESET_TIMEOUT_USEC) {
            LTRACEF("Timeout waiting for ring buffer reset clear\n");
            return ERR_TIMED_OUT;
        }
    }
#endif  // INTEL_HDA_QEMU_QUIRKS

    return NO_ERROR;
}

static inline status_t intel_hda_setup_command_buffer_size(volatile uint8_t* size_reg,
                                                           uint* entry_count) {
    // Note: this method takes advantage of the fact that the TX and RX ring
    // buffer size register bitfield definitions are identical.
    uint8_t tmp = REG_RD_ADDR(8, size_reg);
    uint8_t cmd;

    if (tmp & HDA_REG_CORBSIZE_CAP_256ENT) {
        *entry_count = 256;
        cmd = HDA_REG_CORBSIZE_CFG_256ENT;
    } else if (tmp & HDA_REG_CORBSIZE_CAP_16ENT) {
        *entry_count = 16;
        cmd = HDA_REG_CORBSIZE_CFG_16ENT;
    } else if (tmp & HDA_REG_CORBSIZE_CAP_2ENT) {
        *entry_count = 2;
        cmd = HDA_REG_CORBSIZE_CFG_2ENT;
    } else {
        LTRACEF("Invalid ring buffer capabilities! (0x%02x @ %p)\n", tmp, size_reg);
        return ERR_INTERNAL;
    }

    REG_WR_ADDR(8, size_reg, cmd);
    return NO_ERROR;
}

static status_t intel_hda_setup_command_buffers(intel_hda_device_t* dev) {
    status_t ret;
    DEBUG_ASSERT(dev);
    hda_registers_t* r = dev->regs;
    DEBUG_ASSERT(r);

    /* Start by making sure that the output and response ring buffers are being
     * held in the stopped state */
    REG_WR(8, r, corbctl, 0);
    REG_WR(8, r, rirbctl, 0);

    /* Reset the read and write pointers for both ring buffers */
    REG_WR(16, r, corbwp, 0);
    ret = intel_hda_reset_corbrp(dev);
    if (ret != NO_ERROR)
        return ret;

    /* Note; the HW does not expose a Response Input Ring Buffer Read Pointer,
     * we have to maintain our own. */
    dev->rirb_rd_ptr = 0;
    REG_WR(16, r, rirbwp, HDA_REG_RIRBWP_RST);

    /* Grab a page from the physical memory manager.
     *
     * TODO(johngro) : Depending on the architechture we are running on, and
     * snoop capabilities of this device, we might want to make sure that the
     * command buffer memory is mapped with an uncached policy in the MMU.  This
     * will need to be dealt with more formally as we migrate to Magenta and
     * start to run more on non-emulated hardware with actual cache HW.  */
    if (pmm_alloc_pages(1, PMM_ALLOC_FLAG_ANY, &dev->codec_cmd_buf_pages) != 1)
        return ERR_NO_MEMORY;

    /* Determine the ring buffer sizes.  If there are options, make them as
     * large as possible. Even the largest buffers permissible should fit within
     * a single 4k page. */
    static_assert(PAGE_SIZE >= (HDA_CORB_MAX_BYTES + HDA_RIRB_MAX_BYTES),
                  "A page must be large enough to hold the CORB and RIRB buffers!");

    ret = intel_hda_setup_command_buffer_size(&r->corbsize, &dev->corb_entry_count);
    if (ret != NO_ERROR)
        return ret;

    ret = intel_hda_setup_command_buffer_size(&r->rirbsize, &dev->rirb_entry_count);
    if (ret != NO_ERROR)
        return ret;

    /* Stash these so we don't have to constantly recalculate then */
    dev->corb_mask = dev->corb_entry_count - 1;
    dev->rirb_mask = dev->rirb_entry_count - 1;
    dev->corb_max_in_flight = dev->rirb_mask > INTEL_HDA_RIRB_RESERVED_RESPONSE_SLOTS
                            ? dev->rirb_mask - INTEL_HDA_RIRB_RESERVED_RESPONSE_SLOTS
                            : 1;
    dev->corb_max_in_flight = MIN(dev->corb_max_in_flight, dev->corb_mask);

    /* Program the base address registers for the TX/RX ring buffers, and set up the virtual
     * pointers to the ring buffer entries */
    paddr_t   cmd_buf_paddr;
    uintptr_t cmd_buf_vaddr;
    uint64_t  cmd_buf_paddr_64;

    vm_page_t* page  = containerof(list_peek_head(&dev->codec_cmd_buf_pages), vm_page_t, node);
    cmd_buf_paddr    = vm_page_to_paddr(page);
    cmd_buf_vaddr    = (uintptr_t)paddr_to_kvaddr(cmd_buf_paddr);
    cmd_buf_paddr_64 = (uint64_t)cmd_buf_paddr;

    /* TODO(johngro) : If the controller does not support 64 bit phys
     * addressing, we need to make sure to get a page from low memory to use for
     * our command buffers. */
    bool gcap_64bit_ok = (REG_RD(16, r, gcap) & HDA_REG_GCAP_64OK) != 0;
    if ((cmd_buf_paddr_64 >> 32) && !gcap_64bit_ok) {
        LTRACEF("Intel HDA controller does not support 64-bit physical addressing!\n");
        return ERR_NOT_SUPPORTED;
    }

    /* Section 4.4.1.1; corb ring buffer base address must be 128 byte aligned. */
    DEBUG_ASSERT(!(cmd_buf_paddr_64 & 0x7F));
    REG_WR(32, r, corblbase, ((uint32_t)(cmd_buf_paddr_64 & 0xFFFFFFFF)));
    REG_WR(32, r, corbubase, ((uint32_t)(cmd_buf_paddr_64 >> 32)));
    dev->corb = (hda_corb_entry_t*)cmd_buf_vaddr;

    cmd_buf_paddr_64 += HDA_CORB_MAX_BYTES;
    cmd_buf_vaddr    += HDA_CORB_MAX_BYTES;

    /* Section 4.4.2.2; rirb ring buffer base address must be 128 byte aligned. */
    DEBUG_ASSERT(!(cmd_buf_paddr_64 & 0x7F));
    REG_WR(32, r, rirblbase, ((uint32_t)(cmd_buf_paddr_64 & 0xFFFFFFFF)));
    REG_WR(32, r, rirbubase, ((uint32_t)(cmd_buf_paddr_64 >> 32)));
    dev->rirb = (hda_rirb_entry_t*)cmd_buf_vaddr;

    /* Set the response interrupt count threshold.  The RIRB IRQ will fire any
     * time all of the SDATA_IN lines stop having codec responses to transmit,
     * or when RINTCNT responses have been received, whichever happens
     * first.  We would like to batch up responses to minimize IRQ load, but we
     * also need to make sure to...
     * 1) Not configure the threshold to be larger than the available space in
     *    the ring buffer.
     * 2) Reserve some space (if we can) at the end of the ring buffer so the
     *    hardware has space to write while we are servicing our IRQ.  If we
     *    reserve no space, then the ring buffer is going to fill up and
     *    potentially overflow before we can get in there and process responses.
     */
    uint16_t thresh = dev->rirb_entry_count - 1;
    if (thresh > INTEL_HDA_RIRB_RESERVED_RESPONSE_SLOTS)
        thresh -= INTEL_HDA_RIRB_RESERVED_RESPONSE_SLOTS;
    DEBUG_ASSERT(thresh);
    REG_WR(16, r, rintcnt, thresh);

    /* Clear out any lingering interrupt status */
    REG_WR(8, r, corbsts, HDA_REG_CORBSTS_MEI);
    REG_WR(8, r, rirbsts, HDA_REG_RIRBSTS_INTFL | HDA_REG_RIRBSTS_OIS);

    /* Enable the TX/RX IRQs and DMA engines. */
    REG_WR(8, r, corbctl, HDA_REG_CORBCTL_MEIE | HDA_REG_CORBCTL_DMA_EN);
    REG_WR(8, r, rirbctl, HDA_REG_RIRBCTL_INTCTL | HDA_REG_RIRBCTL_DMA_EN | HDA_REG_RIRBCTL_OIC);

    return NO_ERROR;
}

static void intel_hda_activate_device(intel_hda_device_t* dev) {
    intel_hda_module_state_t* mod = &g_module_state;
    mutex_acquire(&mod->device_list_lock);
    list_add_tail(&mod->device_list, &dev->device_list_node);
    mutex_release(&mod->device_list_lock);
}

static void intel_hda_deactivate_device(intel_hda_device_t* dev) {
    intel_hda_module_state_t* mod = &g_module_state;

    /* If we are in the list of active devices, remove ourselves.  We are no
     * longer active. */
    mutex_acquire(&mod->device_list_lock);
    if (list_in_list(&dev->device_list_node))
        list_delete(&dev->device_list_node);
    mutex_release(&mod->device_list_lock);

    /* Block the hardware from directly accessing main system memory */
    pcie_enable_bus_master(dev->pci_device, false);

    /* TODO(johngro): Disengage from and synchronize with any upward facing API
     * layers we are currently registered with. */

    /* Shut off our IRQ at the PCIe level and synchronize with the PCIe bus
     * driver's IRQ dispatcher.  After this point, we are certain that we can no
     * longer be added to the pending work list by the IRQ handler.  */
    pcie_set_irq_mode_disabled(dev->pci_device);

    /* Purge ourselves from the pending work list if we are currently on it.
     * Keep track of whether or not we were on the pending work list.  If we
     * were, then we know that the work thread was not servicing us when we
     * removed ourselves, therefor there is no reason to synchronize with the
     * work thread.*/
    spin_lock_saved_state_t spinlock_state;
    bool was_pending;

    spin_lock_irqsave(&mod->pending_work_list_lock, spinlock_state);
    was_pending = list_in_list(&dev->pending_work_list_node);
    if (was_pending)
        list_delete(&dev->pending_work_list_node);
    spin_unlock_irqrestore(&mod->pending_work_list_lock, spinlock_state);

    /* If we were not in the pending work list, then there is a chance that the
     * work thread is servicing our device right now.  Bouncing through the work
     * thread mutex will ensure that the work thread is finished with any job
     * it's currently processing.
     */
    if (!was_pending) {
        mutex_acquire(&mod->work_thread_lock);
        mutex_release(&mod->work_thread_lock);
    }

    /* Finished.  We are now certain that we have cleanly disengaged from any
     * executional context which may have been aware of us when we started to
     * de-active.
     */
}

static pcie_irq_handler_retval_t intel_hda_pci_irq_handler(struct pcie_device_state* pci_device,
                                                    uint  irq_id,
                                                    void* ctx) {
    DEBUG_ASSERT(pci_device && ctx);
    intel_hda_module_state_t* mod = &g_module_state;
    intel_hda_device_t*       dev = (intel_hda_device_t*)ctx;
    hda_registers_t*          r   = dev->regs;

    /* start by shutting off our interrupt at the top level of the device's
     * interrupt tree.  We will re-enable it once the work thread is finished
     * servicing us. */
    DEBUG_ASSERT(r);
    REG_CLR_BITS(32, r, intctl, HDA_REG_INTCTL_GIE);

    /* Add this device to the work thread's pending work list, and make certain
     * that it is signaled to wake up.  If the pending work list was not
     * already empty, then we should be able to assert that the thread is
     * currently being signaled and that there is no need to force an immediate
     * reschedule.  If we just went from 0 devices to 1 device on the pending
     * work list, we need to make sure to wake up the work thread and request a
     * resched. */
    bool need_resched;
    spin_lock(&mod->pending_work_list_lock);

    DEBUG_ASSERT(!list_in_list(&dev->pending_work_list_node));
    need_resched = list_is_empty(&mod->pending_work_list);
    list_add_tail(&mod->pending_work_list, &dev->pending_work_list_node);
    if (need_resched)
        event_signal(&mod->work_thread_wakeup, false);

    spin_unlock(&mod->pending_work_list_lock);

    return need_resched ? PCIE_IRQRET_RESCHED : PCIE_IRQRET_NO_ACTION;
}

static status_t intel_hda_pci_startup(struct pcie_device_state* pci_device) {
    DEBUG_ASSERT(pci_device && pci_device->driver_ctx);
    intel_hda_device_t* dev = (intel_hda_device_t*)(pci_device->driver_ctx);
    hda_registers_t*    r   = NULL;
    status_t            ret = ERR_INTERNAL;

    DEBUG_ASSERT(dev->pci_device == pci_device);
    LTRACEF("Starting %s @ %02x:%02x.%01x\n",
            pcie_driver_name(pci_device->driver),
            pci_device->bus_id,
            pci_device->dev_id,
            pci_device->func_id);

    /* Fetch the information about where our registers have been mapped for us,
     * then sanity check. */
    const pcie_bar_info_t* info = pcie_get_bar_info(pci_device, 0);
    if (!info || !info->is_allocated || !info->is_mmio) {
        TRACEF("Failed to fetch base address register info!\n");
        ret = ERR_BAD_STATE;
        goto finished;
    }

    if (sizeof(hda_all_registers_t) != info->size) {
        TRACEF("Unexpected register window size!  (Got %" PRIu64 "; expected %zu)\n",
                info->size, sizeof(hda_all_registers_t));
        ret = ERR_INTERNAL;
        goto finished;
    }

    /* Map in the device registers */
    vmm_aspace_t *aspace = vmm_get_kernel_aspace();
    DEBUG_ASSERT(aspace);
    ret = vmm_alloc_physical(aspace,
                             "iHDA_reg",
                             info->size,
                             (void**)&r,
                             PAGE_SIZE_SHIFT,
                             0,
                             info->bus_addr,
                             0,
                             ARCH_MMU_FLAG_UNCACHED_DEVICE | ARCH_MMU_FLAG_PERM_READ |
                                 ARCH_MMU_FLAG_PERM_WRITE);

    if (ret != NO_ERROR) {
        TRACEF("Failed to map register window (0x%" PRIx64 " @ 0x%" PRIx64 ") Status = %d\n",
               info->size, info->bus_addr, ret);
        goto finished;
    }
    DEBUG_ASSERT(r);

    dev->regs = r;
    pcie_enable_mmio(pci_device, true);

    /* Check our hardware version */
    uint8_t major = pcie_read8(&r->vmaj);
    uint8_t minor = pcie_read8(&r->vmin);

    if ((1 != major) || (0 != minor)) {
        TRACEF("Unexpected HW revision %d.%d!\n", major, minor);
        ret = ERR_INTERNAL;
        goto finished;
    }

    /* setup our pointers to our stream descriptor registers */
    uint16_t gcap = pcie_read16(&r->gcap);
    dev->input_strm_cnt  = HDA_REG_GCAP_ISS(gcap);
    dev->output_strm_cnt = HDA_REG_GCAP_OSS(gcap);
    dev->bidir_strm_cnt  = HDA_REG_GCAP_BSS(gcap);

    if ((dev->input_strm_cnt +
         dev->output_strm_cnt +
         dev->bidir_strm_cnt) > countof(r->stream_desc)) {
        TRACEF("Invalid stream counts in GCAP register (In %zu Out %zu Bidir %zu; Max %zu)\n",
               dev->input_strm_cnt,
               dev->output_strm_cnt,
               dev->bidir_strm_cnt,
               countof(r->stream_desc));
        ret = ERR_INTERNAL;
        goto finished;
    }

    if (dev->input_strm_cnt)
        dev->input_strm_regs = r->stream_desc;

    if (dev->output_strm_cnt)
        dev->output_strm_regs = r->stream_desc + dev->input_strm_cnt;

    if (dev->bidir_strm_cnt)
        dev->bidir_strm_regs = r->stream_desc + dev->input_strm_cnt + dev->output_strm_cnt;

    /* TODO(johngro) : figure out the proper behavior here.
     *
     * So, there are a few confusing things which it would be good to clear up
     * surrounding the issue of the reset sequence involving what the spec says,
     * observed QEMU virtual Intel HDA behavior, and actual Intel HDA controller
     * behavior.  Note: At the time of writing this, actual Intel HDA controller
     * behavior has not been observed.
     *
     * What the spec says:
     * 1) Register writes will have no effect while controller reset (CRST) is
     *    asserted (GCTL[0] == 0).  See section 4.2.2
     * 2) Among other things, asserting CRST will cause the physical link RST#
     *    line to become asserted.  See section 5.5.1
     * 3) The controller begins the process of codec address asssignment and
     *    initialization in response to a codec initialization request.
     *    Ignoring hotplug events, codecs must request initialization within 25
     *    frame syncs (521 uSec) of the deassertion of RST#.  See seciton
     *    5.5.1.2.
     * 4) After successfully assigning an address to a codec, the relevant bit
     *    in the STATESTS register will be set to indicate a codec state change
     *    event.  See Section 4.3
     * 5) Bits in the WAKEEN and STATESTS registers are preserved across both
     *    low power states and reset.  WAKEEN bits must be set to a proper
     *    value, and STATESTS bits must be cleared following a reset operation.
     *    See section 4.2.2
     *
     * Already, there is a race condition issue here.  The host cannot clear
     * STATESTS bits until after CRST has been de-asserted.  After CRST has been
     * de-asserted, the controller will listen for codec initialization requests
     * and set bits in the STATESTS register.  If...
     *
     * 1) Host releases CRST
     * 2) Codec is assigned and address and STATESTS[x] is set.
     * 3) Host clears the contents of STATESTS.
     *
     * Then the host will miss the presence of the codec in the system.
     *
     * To make things more confusing, the behavior of the QEMU virtual Intel HDA
     * controller does not seem to obey the spec.  In particular...
     *
     * 1) Writes used to clear bits STATESTS have an effect while CRST is
     *    asserted.  This contradicts Spec(1) above.
     * 2) Virtual codecs appear in the STATESTS register when CRST is asserted.
     *    Specifically, they appear when a 0 is written to CRST, regardless of
     *    whether or not CRST was already asserted.  This contradicts Spec(3-4)
     *    above.
     * 3) Virtual codecs do not appear in the STATESTS register when CRST is
     *    de-asserted.  This also contradicts Spec(3-4) above.
     *
     * What actual hardware does is currently unclear.  For now, the following
     * behavior is implemented at startup in order to work around the apparent
     * race condition in the spec, as well as the apparent spec violation of the
     * QEMU virtual Intel HDA controller.
     *
     * 1) A full reset cycle is conducted, including the 521 uSec wait for codec
     *    enumeration and address assignment.  At this point, regardless of the
     *    initial state of STATESTS and whether or not this is a real or virtual
     *    controller, codecs present in the system should have their STATESTS
     *    bits properly set.
     * 2) WAKEEN and STATESTS are cleared.  Disregarding hot-plug for the moment,
     *    we should be certain that there are no codecs reported in the STATESTS
     *    register.
     * 3) Another full reset cycle is conducted.  As there were no "ghost"
     *    codecs present at the start of this operation, the only codecs we
     *    should see at the end of the operation should be codecs actually
     *    connected to the system (virtual or otherwise).
     */
    intel_hda_do_reset_cycle(dev);
    REG_CLR_BITS(16, r, wakeen,   0x7FFF);
    REG_WR      (16, r, statests, 0x7FFF);
    intel_hda_do_reset_cycle(dev);

    /* Setup the codec command and control transmit and receive buffers.  */
    ret = intel_hda_setup_command_buffers(dev);
    if (ret != NO_ERROR)
        goto finished;

    /* Allow the device to act as a bus master */
    pcie_enable_bus_master(dev->pci_device, true);

    /* Add ourselves to the list of active devices */
    intel_hda_activate_device(dev);

    /* Select our IRQ mode and register our handler. Try to use MSI, but if we
     * can't, fall back on legacy. */
    ret = pcie_set_irq_mode(pci_device, PCIE_IRQ_MODE_MSI, 1);
    if (ret != NO_ERROR) {
        TRACEF("Failed to configure PCIe device for MSI IRQ mode (err = %d), "
               "falling back on Legacy mode\n", ret);

        ret = pcie_set_irq_mode(pci_device, PCIE_IRQ_MODE_LEGACY, 1);
        if (ret != NO_ERROR) {
            TRACEF("Failed to configure PCIe device for Legacy IRQ mode (err = %d)\n", ret);
            goto finished;
        }
    }

    /* Register our handler; if the mode we are operating in does not support
     * masking, we might start to receive interrupts as soon as we register. */
    ret = pcie_register_irq_handler(pci_device, 0, intel_hda_pci_irq_handler, dev);
    if (ret != NO_ERROR) {
        TRACEF("Failed to register IRQ handler (err = %d)\n", ret);
        goto finished;
    }

    ret = pcie_unmask_irq(pci_device, 0);
    if (ret != NO_ERROR) {
        TRACEF("Failed to unmask IRQ (err = %d)\n", ret);
        goto finished;
    }

    /* Enable the controller IRQ, and unmask all of the codec wake IRQs in order
     * to start the process of codec discovery. */
    REG_SET_BITS(16, r, wakeen, 0x7FFF);
    REG_SET_BITS(32, r, intctl, (HDA_REG_INTCTL_GIE | HDA_REG_INTCTL_CIE));

    ret = NO_ERROR;

finished:
    if (ret != NO_ERROR) {
        /* Something went wrong.  Shutdown is not going to get called, so make
         * sure that we have undone any partially completed startup tasks. */
        intel_hda_deactivate_device(dev);

        if (r) {
            REG_WR(32, r, intctl, 0);   // Dissable all interrupts
            intel_hda_reset(dev, true); // Place the device into reset
        }
    }

    return ret;
}

static void intel_hda_pci_shutdown(struct pcie_device_state* pci_device) {
    DEBUG_ASSERT(pci_device && pci_device->driver_ctx);
    intel_hda_device_t* dev = (intel_hda_device_t*)(pci_device->driver_ctx);
    hda_registers_t*    r   = dev->regs;

    LTRACEF("Shutting down %s @ %02x:%02x.%01x\n",
            pcie_driver_name(pci_device->driver),
            pci_device->bus_id,
            pci_device->dev_id,
            pci_device->func_id);

    /* Deactivate the device.  This will ensure that...
     *
     * 1) Our IRQ is disabled at the PCIe level.
     * 2) No PCIe IRQ dispatches for this device are currently in flight.
     * 3) No work for this device is either scheduled or currently being performed by the module's
     *    work thread.
     * 4) The device no longer exists on the module's list of active devices.
     */
    intel_hda_deactivate_device(dev);

    /* Shut down all interrupt sources in the device's interrupt tree.  Clear out any sticky pending
     * interrupt status bits. */
    REG_WR      (32, r, intctl,   0x0);
    REG_CLR_BITS(16, r, wakeen,   0x7FFF);
    REG_WR      (16, r, statests, 0x7FFF);
    /* TODO(johngro): shut down all stream IRQs as well */

    /* Place the device into reset */
    intel_hda_reset(dev, true);

    dev->pci_device = NULL;
}

static void intel_hda_pci_release(void* ctx) {
    DEBUG_ASSERT(ctx);
    intel_hda_device_t* dev = (intel_hda_device_t*)(ctx);

    DEBUG_ASSERT(!list_in_list(&dev->device_list_node));
    intel_hda_release(dev);
}

static void intel_hda_work_thread_service_device(intel_hda_device_t* dev) {
    /* Note: the module's work thread lock is being held at the moment. */
    DEBUG_ASSERT(dev && dev->regs);
    hda_registers_t* r = dev->regs;

    /* Read the top level interrupt status and figure out what we are supposed
     * to be doing. */
    uint32_t intctl = REG_RD(32, r, intctl);
    uint32_t intsts = REG_RD(32, r, intsts) & intctl;

    /* If the global interrupt enable bit is set, then something is seriously
     * wrong.  The IRQ handler was supposed to have turned it off. */
    DEBUG_ASSERT(!(intctl & HDA_REG_INTCTL_GIE));

    /* Take a snapshot of any pending responses ASAP in order to minimize the
     * chance of an RIRB overflow.  We will process the responses which we
     * snapshot-ed in a short while after we are done handling other important
     * IRQ tasks. */
    intel_hda_codec_snapshot_rirb(dev);

    /* Set up the CORB bookkeeping for this cycle before we start to create
     * codecs and dispatch responses */
    intel_hda_codec_snapshot_corb(dev);

    /* Do we have a pending controller interrupt?  If so, something has either
     * changed with current codec status, or something is going on with the
     * command ring buffers.  Figure out what is going on and take care of it.
     */
    if (intsts & HDA_REG_INTCTL_CIE) {
        intsts &= ~HDA_REG_INTCTL_CIE;

        /* Read and ack the pending state interrupts */
        uint16_t statests = REG_RD(16, r, statests);
        REG_WR(16, r, statests, statests);

        /* Take care of each codec which has delivered a wake interrupt */
        for (size_t codec_ndx = 0;
             statests && (codec_ndx < countof(dev->codecs));
             codec_ndx++, statests >>= 1) {
            /* TODO(johngro) : Right now, we do not deal with hotplugging or
             * sleep/wake.  The only codec interrupts we should be receiving
             * should be during initial codec enumeration.  If we receive an IRQ
             * for a codec we have already discovered, it's probably an unplug
             * event.  Log a warning, but ignore the IRQ.
             */
            if (dev->codecs[codec_ndx]) {
                dprintf(INFO, "Received wake IRQ for a codec (id %zu) we already know about!\n",
                        codec_ndx);
                continue;
            }

            dev->codecs[codec_ndx] = intel_hda_create_codec(dev, (uint8_t)codec_ndx);
            if (!dev->codecs[codec_ndx]) {
                dprintf(CRITICAL,
                        "Failed to allocate control structure for codec (id %zu).  Codec will be "
                        "non-functional\n",
                        codec_ndx);
            }
        }

        /* Check IRQ status for the two command ring buffers */
        uint8_t corbsts = REG_RD(8, r, corbsts);
        uint8_t rirbsts = REG_RD(8, r, rirbsts);

        if (corbsts & HDA_REG_CORBSTS_MEI) {
            /* TODO(johngro) : Implement proper controller reset behavior.
             *
             * The MEI bit in CORBSTS indicates some form memory error detected
             * by the controller while attempting to read from system memory.
             * This is Extremely Bad and should never happen.  If it does, the
             * TRM suggests that all bets are off, and the only reasonable
             * action is to completely shutdown and reset the controller.
             *
             * Right now, we do not implement this behavior.  Instead we log,
             * then assert in debug builds.  In release builds, we simply ack
             * the interrupt and move on.
             */
            dprintf(CRITICAL,
                    "CRITICAL ERROR: controller encountered an unrecoverable "
                    "error attempting to read from system memory!\n");
            DEBUG_ASSERT(false);
        }

        if (rirbsts & HDA_REG_RIRBSTS_OIS) {
            /* TODO(johngro) : Implement retry behavior for codec command and
             * control.
             *
             * The OIS bit in the RIRBSTS register indicates that hardware has
             * encountered a overrun while attempting to write to the Response
             * Input Ring Buffer.  IOW - responses were received, but the
             * controller was unable to write to system memory in time, and some
             * of the responses were lost.  This should *really* never happen.
             * If it does, all bets are pretty much off.  Every command verb
             * sent is supposed to receive a response from the codecs; if a
             * response is dropped it can easily wedge a codec's command and
             * control state machine.
             *
             * This problem is not limited to HW being unable to write to system
             * memory in time.  There is no HW read pointer for the RIRB.  The
             * implication of this is that HW has no way to know that it has
             * overrun SW if SW is not keeping up.  If this was to happen, there
             * would be no way for the system to know, it would just look like a
             * large number of responses were lost.
             *
             * In either case, the only mitigation we could possibly implement
             * would be a reasonable retry system at the codec driver level.
             *
             * Right now, we just log the error, ack the IRQ and move on.
             */
            dprintf(CRITICAL,
                    "CRITICAL ERROR: controller overrun detected while "
                    "attempting to write to response input ring buffer.\n");
        }

        /* Ack the command ring buffer IRQs.  No need to have an explicit
         * handler for RIRB:INTFL, we are going to process any pending codec
         * responses no matter what. */
        REG_WR(8, r, corbsts, corbsts);
        REG_WR(8, r, rirbsts, rirbsts);
    }

    /* Process any snapshot-ed codec responses */
    intel_hda_codec_process_rirb(dev);

    /* Give any codecs who have pending work a chance to talk on the link. */
    intel_hda_codec_process_pending_work(dev);

    /* Commit any commands which were queued by codecs during this cycle */
    intel_hda_codec_commit_corb(dev);

    /* Currently, we have not support for streams or stream interrupts.  If we
     * have a pending unmasked stream interrupt, something is seriously wrong.
     */
    DEBUG_ASSERT(!intsts);

    /* Turn interrupts back on at the global level. */
    REG_SET_BITS(32, r, intctl, HDA_REG_INTCTL_GIE);
}

static int intel_hda_work_thread(void *arg) {
    intel_hda_module_state_t* mod = &g_module_state;

    LTRACEF("Work thread started\n");

    do {
        event_wait(&mod->work_thread_wakeup);
        if (mod->work_thread_quit)
            break;

        /* Process devices which the hard IRQ handler has posted to us until we
         * either run out of devices to process, or until it is time to shut
         * down.
         */
        while (!mod->work_thread_quit) {
            struct list_node* pending_device;
            spin_lock_saved_state_t spinlock_state;
            spin_lock_irqsave(&mod->pending_work_list_lock, spinlock_state);

            /* Stop processing if we have run out of work */
            if (list_is_empty(&mod->pending_work_list)) {
                event_unsignal(&mod->work_thread_wakeup);
                spin_unlock_irqrestore(&mod->pending_work_list_lock, spinlock_state);
                break;
            }

            /* Grab the first device which needs service, then exchange the
             * pending work spinlock for the work thread lock and service the
             * device.
             *
             * Note: this lock exchange feels dangerous, but is required to
             * ensure that devices can properly synchronized against the IRQ
             * handler and the work thread during shutdown.  It is safe because
             * no other entity in the system ever attempts to hold both the
             * pending work list spinlock at the same time as holding the work
             * thread lock.
             */
            pending_device = list_remove_head(&mod->pending_work_list);
            mutex_acquire(&mod->work_thread_lock);
            spin_unlock_irqrestore(&mod->pending_work_list_lock, spinlock_state);

            intel_hda_work_thread_service_device(containerof(pending_device,
                                                             intel_hda_device_t,
                                                             pending_work_list_node));

            mutex_release(&mod->work_thread_lock);
        }
    } while (!mod->work_thread_quit);

    LTRACEF("Work thread finished\n");
    return 0;
}

static void* intel_hda_pci_probe(struct pcie_device_state* pci_device) {
    DEBUG_ASSERT(pci_device);
    intel_hda_module_state_t* mod = &g_module_state;

    if ((pci_device->vendor_id != INTEL_HDA_VID) ||
        (pci_device->device_id != INTEL_HDA_DID)) {
        return NULL;
    }

    /* Allocate our device state */
    intel_hda_device_t* dev = (intel_hda_device_t*)calloc(1, sizeof(intel_hda_device_t));
    if (!dev) {
        LTRACEF("Failed to allocate %zu bytes for Intel HDA device\n", sizeof(intel_hda_device_t));
        return NULL;
    }

    /* Initialize our device state. */
    dev->ref_count  = 1;  // The PCI bus will be holding the first ref on us
    dev->dev_id     = atomic_add(&mod->dev_id_gen, 1);  // Generate a unique device ID
    dev->pci_device = pci_device;
    list_initialize(&dev->codec_cmd_buf_pages); // command buffer page list starts as empty

    /* Note: no need to explicitly initialize these list node members.  calloc
     * has done this for us already. */
    // list_clear_node(dev->device_list_node);
    // list_clear_node(dev->pending_work_list_node);

    /* Return a pointer to our device state in order to claim this device as ours */
    return dev;
}

void intel_hda_foreach(intel_hda_foreach_cbk cbk, void* ctx) {
    if (!cbk) return;

    intel_hda_module_state_t* mod = &g_module_state;
    mutex_acquire(&mod->device_list_lock);

    intel_hda_device_t* dev;
    list_for_every_entry(&mod->device_list, dev, intel_hda_device_t, device_list_node) {
        cbk(dev, ctx);
    }

    mutex_release(&mod->device_list_lock);
}

intel_hda_device_t* intel_hda_acquire(int dev_id) {
    intel_hda_module_state_t* mod = &g_module_state;
    intel_hda_device_t* ret = NULL;

    mutex_acquire(&mod->device_list_lock);

    intel_hda_device_t* dev;
    list_for_every_entry(&mod->device_list, dev, intel_hda_device_t, device_list_node) {
        DEBUG_ASSERT(dev->ref_count > 0);
        if (dev_id == dev->dev_id) {
            ret = dev;
            atomic_add(&ret->ref_count, 1);
            break;
        }
    }

    mutex_release(&mod->device_list_lock);
    return ret;
}

void intel_hda_release(intel_hda_device_t* dev) {
    DEBUG_ASSERT(dev);
    DEBUG_ASSERT(dev->ref_count > 0);

    if (atomic_add(&dev->ref_count, -1) == 1) {
        for (size_t i = 0; i < countof(dev->codecs); ++i) {
            intel_hda_destroy_codec(dev->codecs[i]);
            dev->codecs[i] = NULL;
        }

        if (!list_is_empty(&dev->codec_cmd_buf_pages))
            pmm_free(&dev->codec_cmd_buf_pages);

        free(dev);
    }
}


static const pcie_driver_fn_table_t INTEL_HDA_FN_TABLE = {
    .pcie_probe_fn       = intel_hda_pci_probe,
    .pcie_startup_fn     = intel_hda_pci_startup,
    .pcie_shutdown_fn    = intel_hda_pci_shutdown,
    .pcie_release_fn     = intel_hda_pci_release,
};

STATIC_PCIE_DRIVER(intel_hda, "Intel HD Audio", INTEL_HDA_FN_TABLE)
