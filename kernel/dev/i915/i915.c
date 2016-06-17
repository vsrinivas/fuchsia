// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <trace.h>
#include <reg.h>
#include <string.h>
#include <err.h>
#include <list.h>
#include <dev/display.h>
#include <dev/pcie.h>
#include <kernel/vm.h>
#include <lib/gfxconsole.h>

extern uint32_t bootloader_fb_base;
extern uint32_t bootloader_fb_width;
extern uint32_t bootloader_fb_height;
extern uint32_t bootloader_fb_stride;
extern uint32_t bootloader_fb_format;
extern uint32_t bootloader_i915_reg_base;
extern uint32_t bootloader_fb_window_size;

#define LOCAL_TRACE 0

#define INTEL_I915_VID (0x8086)
#define INTEL_I915_DID (0x1616)

#define INTEL_I915_REG_WINDOW_SIZE (0x1000000u)
#define INTEL_I915_FB_WINDOW_SIZE  (0x10000000u)

#define BACKLIGHT_CTRL_OFFSET (0xc8250)
#define BACKLIGHT_CTRL_BIT    ((uint32_t)(1u << 31))

typedef struct intel_i915_device {
    void*                regs;
    size_t               regs_size;
    void*                framebuffer;
    size_t               framebuffer_size;
    vmm_aspace_t*        aspace;
    struct display_info  disp;
    pcie_device_state_t* pci_device;
} intel_i915_device_t;

static intel_i915_device_t* g_i915_device;

static void intel_i915_unmap_windows(intel_i915_device_t* dev)
{
    DEBUG_ASSERT(dev);

    if (dev->aspace) {
        if (dev->regs) {
            vmm_free_region(dev->aspace, (vaddr_t)dev->regs);
            dev->regs = NULL;
        }

        if (dev->framebuffer) {
            vmm_free_region(dev->aspace, (vaddr_t)dev->framebuffer);
            dev->framebuffer = NULL;
        }

        dev->aspace = NULL;
    } else {
        DEBUG_ASSERT(!dev->regs);
        DEBUG_ASSERT(!dev->framebuffer);
    }
}

static status_t intel_i915_map_reg_window(intel_i915_device_t* dev,
                                          paddr_t reg_phys,
                                          size_t  size)
{
    DEBUG_ASSERT(dev);
    DEBUG_ASSERT(dev->aspace);

    if (!dev->regs) {
        status_t ret;
        DEBUG_ASSERT(!dev->regs_size);

        ret = vmm_alloc_physical(
               dev->aspace,
               "i915_reg",
               size,
               &dev->regs,
               PAGE_SIZE_SHIFT,
               reg_phys,
               0 /* vmm flags */,
               ARCH_MMU_FLAG_UNCACHED_DEVICE | ARCH_MMU_FLAG_PERM_NO_EXECUTE);

        if (ret == NO_ERROR)
            dev->regs_size = size;

        return ret;
    } else {
        DEBUG_ASSERT(dev->regs_size);

        if (size != dev->regs_size) {
            LTRACEF("Size mismatch when moving i915 register window.  New size "
                    "(%zu) does not match old size (%zu)\n",
                    size, dev->regs_size);
            return ERR_NOT_VALID;
        }

        return vmm_move_region_phys(dev->aspace,
                                    (vaddr_t)dev->regs,
                                    reg_phys);
    }
}

static status_t intel_i915_map_fb_window(intel_i915_device_t* dev,
                                         paddr_t fb_phys,
                                         size_t  size)
{
    DEBUG_ASSERT(dev);
    DEBUG_ASSERT(dev->aspace);

    if (!dev->framebuffer) {
        status_t ret;
        DEBUG_ASSERT(!dev->framebuffer_size);

        ret = vmm_alloc_physical(
               dev->aspace,
               "i915_fb",
               INTEL_I915_FB_WINDOW_SIZE,
               &dev->framebuffer,
               PAGE_SIZE_SHIFT,
               fb_phys,
               0 /* vmm flags */,
               ARCH_MMU_FLAG_WRITE_COMBINING | ARCH_MMU_FLAG_PERM_NO_EXECUTE);

        if (ret == NO_ERROR)
            dev->framebuffer_size = size;

        return ret;
    } else {
        DEBUG_ASSERT(dev->framebuffer_size);

        if (size != dev->framebuffer_size) {
            LTRACEF("Size mismatch when moving i915 framebuffer window.  New size "
                    "(%zu) does not match old size (%zu)\n",
                    size, dev->framebuffer_size);
            return ERR_NOT_VALID;
        }

        return vmm_move_region_phys(dev->aspace,
                                    (vaddr_t)dev->framebuffer,
                                    fb_phys);
    }
}

static void intel_i915_cleanup_device(intel_i915_device_t* dev)
{
    if (!dev) {
        DEBUG_ASSERT(!g_i915_device);
        return;
    }

    DEBUG_ASSERT(!dev->regs);
    DEBUG_ASSERT(!dev->framebuffer);
    DEBUG_ASSERT(!dev->pci_device);
    DEBUG_ASSERT(dev == g_i915_device);

    intel_i915_unmap_windows(dev);
    free(dev);
    g_i915_device = NULL;
}

static status_t intel_i915_setup_device(void)
{
    DEBUG_ASSERT(!g_i915_device);

    intel_i915_device_t* dev;
    dev = g_i915_device = (intel_i915_device_t*)calloc(1, sizeof(*g_i915_device));
    if (!dev) {
        LTRACEF("Failed to allocate %zu bytes for Intel i915 device\n",
                sizeof(intel_i915_device_t));
        return ERR_NO_MEMORY;
    }

    dev->aspace = vmm_get_kernel_aspace();
    if (!dev->aspace) {
        LTRACEF("Failed to fetch Intel i915 device's address space!\n");
        intel_i915_cleanup_device(dev);
        return ERR_NO_RESOURCES;
    }

    return NO_ERROR;
}

static void intel_i915_enable_backlight(intel_i915_device_t* dev, bool enable) {
    DEBUG_ASSERT(dev);

    if (!dev->regs)
        return;

    void*    backlight_ctrl = (uint8_t*)dev->regs + BACKLIGHT_CTRL_OFFSET;
    uint32_t tmp = pcie_read32(backlight_ctrl);

    if (enable)
        tmp |= BACKLIGHT_CTRL_BIT;
    else
        tmp &= ~BACKLIGHT_CTRL_BIT;

    pcie_write32(backlight_ctrl, tmp);
}

static status_t intel_i915_pci_startup(struct pcie_device_state* pci_device) {
    DEBUG_ASSERT(pci_device && pci_device->driver_ctx);
    intel_i915_device_t*   dev = (intel_i915_device_t*)pci_device->driver_ctx;
    status_t status = NO_ERROR;

    /* Figure our where the bus driver has placed our register window and our
     * framebuffer window */
    const pcie_bar_info_t* info;

    info = pcie_get_bar_info(pci_device, 0);
    if (!info || !info->is_allocated || !info->is_mmio) {
        status = ERR_BAD_STATE;
        goto bailout;
    }

    status = intel_i915_map_reg_window(dev, info->bus_addr, info->size);
    if (status != NO_ERROR)
        goto bailout;

    info = pcie_get_bar_info(pci_device, 2);
    if (!info || !info->is_allocated || !info->is_mmio) {
        status = ERR_BAD_STATE;
        goto bailout;
    }

    status = intel_i915_map_fb_window(dev, info->bus_addr, info->size);
    if (status != NO_ERROR)
        goto bailout;

    pcie_enable_mmio(pci_device, true);
    intel_i915_enable_backlight(dev, true);

    struct display_info di;
    memset(&info, 0, sizeof(info));
    if (bootloader_fb_base) {
        di.format      = bootloader_fb_format;
        di.width       = bootloader_fb_width;
        di.height      = bootloader_fb_height;
        di.stride      = bootloader_fb_stride;
    } else {
        di.format      = DISPLAY_FORMAT_RGB_565;
        di.width       = 2560 / 2;
        di.height      = 1700 / 2;
        di.stride      = 2560 / 2;
    }
    di.flags       = DISPLAY_FLAG_HW_FRAMEBUFFER;
    di.flush       = NULL;
    di.framebuffer = dev->framebuffer;

    gfxconsole_bind_display(&di, NULL);

bailout:
    if (status != NO_ERROR)
        intel_i915_unmap_windows(dev);

    return status;
}

static void intel_i915_pci_shutdown(struct pcie_device_state* pci_device) {
    DEBUG_ASSERT(pci_device && pci_device->driver_ctx);
    intel_i915_device_t* dev = (intel_i915_device_t*)pci_device->driver_ctx;

    intel_i915_enable_backlight(dev, false);
    intel_i915_unmap_windows(dev);
    pcie_enable_mmio(pci_device, false);
}

static void intel_i915_pci_release(void* ctx) {
    DEBUG_ASSERT(ctx);
    DEBUG_ASSERT(g_i915_device == ctx);
    DEBUG_ASSERT(g_i915_device->regs == NULL);
    DEBUG_ASSERT(g_i915_device->framebuffer == NULL);

    intel_i915_cleanup_device(g_i915_device);
}

static void* intel_i915_pci_probe(pcie_device_state_t* pci_device) {
    DEBUG_ASSERT(pci_device);

    /* Is this the droid we are looking for? */
    if ((pci_device->vendor_id != INTEL_I915_VID) ||
        (pci_device->device_id != INTEL_I915_DID)) {
        return NULL;
    }

    if (!g_i915_device) {
        /* Device structure has not been allocated yet.  Attempt to do so now.
         * If we fail, do not claim the device */
        if (intel_i915_setup_device() != NO_ERROR)
            return NULL;
    } else {
        /* If the singleton device structure has already been allocated, check
         * to see if it has already claimed a PCI device.  If it has, do not
         * claim this one. */
        if (g_i915_device->pci_device)
            return NULL;
    }

    /* Stash a reference to our PCI device and claim the device in the bus
     * driver */
    g_i915_device->pci_device = pci_device;
    return g_i915_device;
}

static const pcie_driver_fn_table_t INTEL_I815_FN_TABLE = {
    .pcie_probe_fn    = intel_i915_pci_probe,
    .pcie_startup_fn  = intel_i915_pci_startup,
    .pcie_shutdown_fn = intel_i915_pci_shutdown,
    .pcie_release_fn  = intel_i915_pci_release,
};

#ifdef NO_USER_DISPLAY
STATIC_PCIE_DRIVER(intel_i915, "Intel i915 Display Controller", INTEL_I815_FN_TABLE)
#endif
