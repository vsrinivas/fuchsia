// Copyright 2016 The Fuchsia Authors
// Copyright (c) 2016, Google, Inc. All rights reserved
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <assert.h>
#include <debug.h>
#include <err.h>
#include <string.h>
#include <trace.h>

#include "pcie_priv.h"

#define LOCAL_TRACE 0

typedef struct pcie_caps_parse_table_entry {
    status_t (*parse)(pcie_device_state_t* dev, void* hdr, uint version, uint space_left);
    uint     cap_id;
} pcie_caps_parse_table_entry_t;

typedef struct pcie_caps_fetch_hdr_params {
    void* hdr;
    uint  cap_id;
    uint  cap_version;
    uint  space_left;
} pcie_caps_fetch_hdr_params_t;

typedef struct pcie_do_parse_caps_params {
    status_t (*fetch_hdr)(pcie_device_state_t*          dev,
                          void*                         prev_hdr,
                          pcie_caps_fetch_hdr_params_t *out_params);
    const pcie_caps_parse_table_entry_t* parse_table;
    const size_t                         parse_table_size;
    const uint                           max_possible_caps;
} pcie_do_parse_caps_params_t;

/*
 * PCI Express Base Specification 3.1a Section 7.8
 */
static status_t pcie_parse_pci_express_caps(struct pcie_device_state* dev,
                                            void*                     hdr,
                                            uint                      version,
                                            uint                      space_left) {
    DEBUG_ASSERT(dev);
    DEBUG_ASSERT(hdr);
    DEBUG_ASSERT(!version);  // Standard capabilities do not have versions

    /* Size sanity check */
    pcie_capabilities_t* ecam = (pcie_capabilities_t*)hdr;
    if (sizeof(*ecam) > space_left) {
        TRACEF("Device %02x:%02x.%01x (%04hx:%04hx) has illegally positioned PCI "
                "Express capability structure.  Structure is %zu bytes long, but "
                "only %u bytes remain in ECAM standard config.\n",
               dev->bus_id, dev->dev_id, dev->func_id,
               dev->vendor_id, dev->device_id,
               sizeof(*ecam), space_left);
        return ERR_NOT_VALID;
    }

    /* Read the caps field, then parse and sanity check the device/port type */
    uint16_t caps = pcie_read16(&ecam->caps);
    pcie_device_type_t devtype = PCS_CAPS_DEVTYPE(caps);
    switch (devtype) {
        // Type 0 config header types
        case PCIE_DEVTYPE_PCIE_ENDPOINT:
        case PCIE_DEVTYPE_LEGACY_PCIE_ENDPOINT:
        case PCIE_DEVTYPE_RC_INTEGRATED_ENDPOINT:
        case PCIE_DEVTYPE_RC_EVENT_COLLECTOR:
            if (dev->is_bridge) {
                TRACEF("Device %02x:%02x.%01x (%04hx:%04hx) has a Type 0 PCIe "
                       "device type (0x%x) in PCIe capabilties structure, but "
                       "does not have a Type 0 config header.\n",
                       dev->bus_id, dev->dev_id, dev->func_id,
                       dev->vendor_id, dev->device_id,
                       devtype);
                return ERR_NOT_VALID;
            }
            break;

        // Type 1 config header types
        case PCIE_DEVTYPE_RC_ROOT_PORT:
        case PCIE_DEVTYPE_SWITCH_UPSTREAM_PORT:
        case PCIE_DEVTYPE_SWITCH_DOWNSTREAM_PORT:
        case PCIE_DEVTYPE_PCIE_TO_PCI_BRIDGE:
        case PCIE_DEVTYPE_PCI_TO_PCIE_BRIDGE:
            if (!dev->is_bridge) {
                TRACEF("Device %02x:%02x.%01x (%04hx:%04hx) has a Type 1 PCIe "
                       "device type (0x%x) in PCIe capabilties structure, but "
                       "does not have a Type 1 config header.\n",
                       dev->bus_id, dev->dev_id, dev->func_id,
                       dev->vendor_id, dev->device_id,
                       devtype);
                return ERR_NOT_VALID;
            }
            break;

        default:
            TRACEF("Device %02x:%02x.%01x (%04hx:%04hx) has an illegal PCIe "
                   "device type (0x%x) in PCIe capabilties structure.\n",
                   dev->bus_id, dev->dev_id, dev->func_id,
                   dev->vendor_id, dev->device_id,
                   devtype);
            return ERR_NOT_VALID;
    }

    /* TODO(johngro): remember to read the MSI/MSI-X interrupt message number
     * field when setting up for MSI/MSI-X.  We almost certainly need to hook
     * this IRQ in order to be aware of any changes to the extended
     * capabilities.  It is unclear whether or not we should allow this IRQ to
     * be passed thru to the device driver or not.
     */

    /* Check device capabilities to see if we support function level reset or
     * not */
    uint32_t devcaps = pcie_read32(&ecam->device.caps);
    bool has_flr = PCS_DEV_CAPS_FUNC_LEVEL_RESET(devcaps) != 0;

    /* Success, stash our results and we are done */
    dev->pcie_caps.ecam    = ecam;
    dev->pcie_caps.devtype = devtype;
    dev->pcie_caps.has_flr = has_flr;
    return NO_ERROR;
}

/*
 * PCI Local Bus Specification 3.0 Section 6.8.1
 */
static status_t pcie_parse_msi_caps(pcie_device_state_t* dev,
                                    void*                hdr,
                                    uint                 version,
                                    uint                 space_left) {
    DEBUG_ASSERT(dev);

    /* Zero out the devices MSI IRQ state */
    memset(&dev->irq.msi, 0, sizeof(dev->irq.msi));

    /* Make sure we have at least enough space to hold the common header. */
    size_t min_size = PCIE_CAP_MSI_CAP_HDR_SIZE;
    if (space_left < min_size) {
        TRACEF("Device %02x:%02x.%01x (%04hx:%04hx) has illegally positioned MSI "
               "capability structure.  Structure header is at least %zu bytes "
               "long, but only %u bytes remain in ECAM standard config.\n",
               dev->bus_id, dev->dev_id, dev->func_id,
               dev->vendor_id, dev->device_id,
               min_size, space_left);
        return ERR_NOT_VALID;
    }

    /* Extract the control field and figure out what sub-type of MSI capability
     * struture we are dealing with. */
    pcie_cap_msi_t* msi_cap = (pcie_cap_msi_t*)hdr;
    uint16_t        ctrl    = pcie_read16(&msi_cap->ctrl);
    bool            pvm     = PCIE_CAP_MSI_CTRL_PVM_SUPPORTED(ctrl);
    bool            is64bit = PCIE_CAP_MSI_CTRL_64BIT_SUPPORTED(ctrl);

    if (pvm) min_size += is64bit ? sizeof(msi_cap->pvm_64bit)   : sizeof(msi_cap->pvm_32bit);
    else     min_size += is64bit ? sizeof(msi_cap->nopvm_64bit) : sizeof(msi_cap->nopvm_32bit);

    if (space_left < min_size) {
        TRACEF("Device %02x:%02x.%01x (%04hx:%04hx) has illegally positioned MSI "
               "capability structure.  Structure %s 64-bit addressing and %s "
               "per-vector masking and should be %zu bytes long, but only %u "
               "bytes remain in ECAM standard config.\n",
               dev->bus_id, dev->dev_id, dev->func_id,
               dev->vendor_id, dev->device_id,
               is64bit ? "supports" : "does not support",
               pvm     ? "supports" : "does not support",
               min_size, space_left);
        return ERR_NOT_VALID;
    }

    /* Sanity check the Multi-Message Capable field */
    uint max_irqs = 0x1u << PCIE_CAP_MSI_CTRL_GET_MMC(ctrl);
    if (max_irqs > PCIE_MAX_MSI_IRQS) {
        TRACEF("Device %02x:%02x.%01x (%04hx:%04hx) has invalid Multi-Message "
               "Capable value in MSI capability structure (%u).  Structure "
               "claims to support %u vectors, but %u is the maximum allowed.\n",
               dev->bus_id, dev->dev_id, dev->func_id,
               dev->vendor_id, dev->device_id,
               PCIE_CAP_MSI_CTRL_GET_MMC(ctrl), max_irqs, PCIE_MAX_MSI_IRQS);
        return ERR_NOT_VALID;
    }

    /* Success!
     *
     * Make sure that MSI is disabled and that the Multi-Message Enable field is
     * set to 1-vector (multi-message disabled).  Then record our capabilities
     * in the device's bookkeeping and we are done.
     */
    pcie_write16(&msi_cap->ctrl, PCIE_CAP_MSI_CTRL_SET_MME(0,
                                 PCIE_CAP_MSI_CTRL_SET_ENB(0, ctrl)));

    dev->irq.msi.cfg      = msi_cap;
    dev->irq.msi.max_irqs = max_irqs;
    dev->irq.msi.is64bit  = is64bit;
    if (pvm) {
        /*
         * If we support PVM, cache a pointer to the mask register (so we don't
         * have to keep constantly checking if we are 64 bit or 32 bit MSI to
         * figure out where our register is).  Also, mask sure that all vectors
         * are currently masked.
         */
        dev->irq.msi.pvm_mask_reg = is64bit
                                     ? &msi_cap->pvm_64bit.mask_bits
                                     : &msi_cap->pvm_32bit.mask_bits;
        pcie_write32(dev->irq.msi.pvm_mask_reg, 0xFFFFFFFF);
    }

    return NO_ERROR;
}

/*
 * Advanced Capabilities for Conventional PCI ECN
 */
static status_t pcie_parse_pci_advanced_features(struct pcie_device_state* dev,
                                                 void*                     hdr,
                                                 uint                      version,
                                                 uint                      space_left) {
    DEBUG_ASSERT(dev);
    DEBUG_ASSERT(hdr);
    DEBUG_ASSERT(!version);  // Standard capabilities do not have versions

    /* Size sanity check */
    pcie_cap_adv_caps_t* ecam = (pcie_cap_adv_caps_t*)hdr;
    if (sizeof(*ecam) > space_left) {
        TRACEF("Device %02x:%02x.%01x (%04hx:%04hx) has illegally positioned PCI "
                "Advanced capability structure.  Structure is %zu bytes long, but "
                "only %u bytes remain in ECAM standard config.\n",
               dev->bus_id, dev->dev_id, dev->func_id,
               dev->vendor_id, dev->device_id,
               sizeof(*ecam), space_left);
        return ERR_NOT_VALID;
    }

    uint8_t length = pcie_read8(&ecam->length);
    if (sizeof(*ecam) > length) {
        TRACEF("Device %02x:%02x.%01x (%04hx:%04hx) has an invalid PCI Advanced "
               "capability structure length.  Expected %zu, Actual %u\n",
               dev->bus_id, dev->dev_id, dev->func_id,
               dev->vendor_id, dev->device_id,
               sizeof(*ecam), length);
        return ERR_NOT_VALID;
    }

    /* Read the caps field and sanity check it */
    uint8_t caps = pcie_read8(&ecam->af_caps);
    if (PCS_ADVCAPS_CAP_HAS_FUNC_LEVEL_RESET(caps) !=
        PCS_ADVCAPS_CAP_HAS_TRANS_PENDING(caps)) {
        TRACEF("Device %02x:%02x.%01x (%04hx:%04hx) has illegal PCI Advanced "
               "capability structure.  Structure %s a Function Level Reset bit, "
               "but %s a Transaction Pending Bit (caps = 0x%02x)\n",
               dev->bus_id, dev->dev_id, dev->func_id,
               dev->vendor_id, dev->device_id,
               PCS_ADVCAPS_CAP_HAS_FUNC_LEVEL_RESET(caps) ? "has" : "does not have",
               PCS_ADVCAPS_CAP_HAS_TRANS_PENDING(caps)    ? "has" : "does not have",
               caps);
        return ERR_NOT_VALID;
    }

    /* Success, stash our results and we are done */
    dev->pcie_adv_caps.ecam    = ecam;
    dev->pcie_adv_caps.has_flr = PCS_ADVCAPS_CAP_HAS_FUNC_LEVEL_RESET(caps);

    return NO_ERROR;
}

static status_t pcie_fetch_standard_cap_hdr(pcie_device_state_t*          dev,
                                            void*                         prev_hdr,
                                            pcie_caps_fetch_hdr_params_t *out_params) {
    DEBUG_ASSERT(dev);
    DEBUG_ASSERT(dev->cfg);
    DEBUG_ASSERT(out_params);

    /* By default, return a NULL next header and stop searching */
    memset(out_params, 0, sizeof(*out_params));

    /* Determine the position of the next header based on the previous header. */
    uint8_t cptr;
    if (!prev_hdr) {
        /* If there was no previous header at all, check to see if this device
         * even has a standard capability list */
        uint16_t status = pcie_read16(&dev->cfg->base.status);
        if (!(status & PCI_STATUS_NEW_CAPS))
            return NO_ERROR;

        /* Start of the standard config list comes from the capabilities ptr
         * member of the config header */
        cptr = pcie_read8(&dev->cfg->base.capabilities_ptr)
             & ~(PCIE_CAPABILITY_ALIGNMENT - 1);
    } else {
        /* Extract the next header pointer from the previous header */
        cptr = pcie_cap_hdr_get_next_ptr(pcie_read16((pcie_cap_hdr_t*)prev_hdr));
    }

    /* NULL next pointer means that we are done */
    if (cptr == PCIE_CAP_PTR_NULL)
        return NO_ERROR;

    /* Sanity check the capability pointer. */
    if (!((cptr >= PCIE_CAP_PTR_MIN_VALID) && (cptr <= PCIE_CAP_PTR_MAX_VALID))) {
        TRACEF("Device %02x:%02x.%01x (%04hx:%04hx) has illegal capability "
               "pointer (0x%02x) in standard capability list.\n",
               dev->bus_id, dev->dev_id, dev->func_id,
               dev->vendor_id, dev->device_id,
               cptr);
        return ERR_NOT_VALID;
    }

    /* Read the next header */
    static_assert(PCIE_CAP_PTR_MAX_VALID <= PCIE_BASE_CONFIG_SIZE);
    uint space = PCIE_BASE_CONFIG_SIZE - cptr;
    DEBUG_ASSERT(space >= sizeof(pcie_cap_hdr_t));

    pcie_cap_hdr_t* hdr_ptr = (pcie_cap_hdr_t*)((uintptr_t)(dev->cfg) + cptr);
    pcie_cap_hdr_t  hdr_val = pcie_read16(hdr_ptr);

    /* Things look good.  Fill out our output params and we are done */
    out_params->hdr         = hdr_ptr;
    out_params->cap_id      = pcie_cap_hdr_get_type(hdr_val);
    out_params->cap_version = 0x0;
    out_params->space_left  = space;
    return NO_ERROR;
}

static status_t pcie_fetch_extended_cap_hdr(pcie_device_state_t*          dev,
                                            void*                         prev_hdr,
                                            pcie_caps_fetch_hdr_params_t *out_params) {
    DEBUG_ASSERT(dev);
    DEBUG_ASSERT(dev->cfg);
    DEBUG_ASSERT(out_params);

    /* By default, return a NULL next header and stop searching */
    memset(out_params, 0, sizeof(*out_params));

    /* Determine the position of the next header based on the previous header. */
    uint16_t cptr = prev_hdr
                  ? pcie_ext_cap_hdr_get_next_ptr(pcie_read32((pcie_ext_cap_hdr_t*)prev_hdr))
                  : PCIE_BASE_CONFIG_SIZE;

    /* NULL next pointer means that we are done */
    if (cptr == PCIE_EXT_CAP_PTR_NULL)
        return NO_ERROR;

    /* Sanity check the capability pointer. */
    if (!((cptr >= PCIE_EXT_CAP_PTR_MIN_VALID) && (cptr <= PCIE_EXT_CAP_PTR_MAX_VALID))) {
        TRACEF("Device %02x:%02x.%01x (%04hx:%04hx) has illegal capability "
               "pointer (0x%02x) in extended capability list.\n",
               dev->bus_id, dev->dev_id, dev->func_id,
               dev->vendor_id, dev->device_id,
               cptr);
        return ERR_NOT_VALID;
    }

    /* Read the next header */
    static_assert(PCIE_EXT_CAP_PTR_MAX_VALID <= PCIE_EXTENDED_CONFIG_SIZE);
    uint space = PCIE_EXTENDED_CONFIG_SIZE - cptr;
    DEBUG_ASSERT(space >= sizeof(pcie_ext_cap_hdr_t));

    pcie_ext_cap_hdr_t* hdr_ptr = (pcie_ext_cap_hdr_t*)((uintptr_t)(dev->cfg) + cptr);
    pcie_ext_cap_hdr_t  hdr_val = pcie_read32(hdr_ptr);

    /*
     * TODO(johngro) : Figure this out.
     *
     * In theory all PCIe device are supposed to have an extended caps list.  If
     * the list is empty, it is supposed to start with a NULL capability ID
     * followed by a 0x000 next pointer.
     *
     * Some devices (such as the PCI-to-ISA bridge in Broadwell, and all of the
     * emulated devices in QEMU) seem to fill their extended capabilities config
     * region with 0xFF instead of zero.  This might be because they are
     * supposed to be considered to be "legacy endpoints", however it is unclear
     * what the proper way to test for this is.
     *
     * Section 7.8 of the PCIe 3.1a spec defines the "PCI Express Capability
     * Structure" and says "The PCI Express Capability structure is required for
     * PCI Express device Functions".  Unfortunately there are devices (the Intel
     * HDA audio controller in Broadwell, for example) which lack this standard
     * header, but possess extended capabilities.
     *
     * For now, we just special case this.  If we see a header in the extended
     * config whose values are all 0xFs, just assume that this device has no
     * extended capabilities and stop looking.
     */
    if (hdr_val == 0xFFFFFFFF)
        return NO_ERROR;

    /* A raw extended header value of 0 indicates the end of the list */
    if (!hdr_val)
        return NO_ERROR;

    /* Things look good.  Fill out our output params and we are done */
    out_params->hdr         = hdr_ptr;
    out_params->cap_id      = pcie_ext_cap_hdr_get_type(hdr_val);
    out_params->cap_version = pcie_ext_cap_hdr_get_cap_version(hdr_val);
    out_params->space_left  = space;

    return NO_ERROR;
}

static status_t pcie_do_parse_caps(pcie_device_state_t* dev,
                                   const pcie_do_parse_caps_params_t* params) {
    DEBUG_ASSERT(dev);
    DEBUG_ASSERT(dev->cfg);
    DEBUG_ASSERT(params);
    DEBUG_ASSERT((params->fetch_hdr == pcie_fetch_standard_cap_hdr) ||
                 (params->fetch_hdr == pcie_fetch_extended_cap_hdr));
    DEBUG_ASSERT(params->parse_table);

    __UNUSED const char* dbg_tag = (params->fetch_hdr == pcie_fetch_standard_cap_hdr)
                                 ? "standard"
                                 : "extended";

    status_t res;
    pcie_caps_fetch_hdr_params_t fp;
    uint caps_found = 0;

    /* Fetch headers using the proper fetch function until we encounter a fatal
     * error, or we run out of capabilities to parse. */
    for (res = params->fetch_hdr(dev, NULL, &fp);
         fp.hdr && (res == NO_ERROR);
         res = params->fetch_hdr(dev, fp.hdr, &fp)) {
        /* Sanity check to make sure we're not looping excessively because of
         * something like a cycle in our list.  Note: this check does not ensure
         * that none of the capability structures are overlapping (which might
         * be a good thing to check as well), it just makes sure that we have
         * not processed more capability headers than it is possible to fit
         * inside of a standard or extended configuration block. */
        caps_found += 1;
        if (caps_found > params->max_possible_caps) {
            TRACEF("Device %02x:%02x.%01x (%04hx:%04hx) has too many %s "
                   "capabilities!  %u capability headers have been found so "
                   "far!\n",
                   dev->bus_id, dev->dev_id, dev->func_id,
                   dev->vendor_id, dev->device_id,
                   dbg_tag, caps_found);
            return ERR_NOT_VALID;
        }

        /* Look through our parse table to find a parse hook */
        const pcie_caps_parse_table_entry_t* entry = NULL;
        for (size_t i = 0; (i < params->parse_table_size) && !entry; ++i) {
            if (params->parse_table[i].cap_id == fp.cap_id) {
                entry = &params->parse_table[i];
                break;
            }
        }

        /* If we failed to find a hook, log a warning and keep going.  A new
         * capability ID may have been added to the standard since this code was
         * written */
        if (!entry) {
            TRACEF("Skipping unnknown %s capability (#%u, id = 0x%02x) for device "
                   "%02x:%02x.%01x (%04hx:%04hx)\n",
                   dbg_tag, caps_found, fp.cap_id,
                   dev->bus_id, dev->dev_id, dev->func_id,
                   dev->vendor_id, dev->device_id);
            continue;
        }

        LTRACEF("Found %s capability (#%u, id = 0x%02x) for device %02x:%02x.%01x (%04hx:%04hx)\n",
                dbg_tag, caps_found, fp.cap_id,
                dev->bus_id, dev->dev_id, dev->func_id,
                dev->vendor_id, dev->device_id);

        /* Process the capability if we have a parse hook.  Stop processing if
         * something goes fatally wrong */
        if (entry->parse) {
            res = entry->parse(dev, fp.hdr, fp.cap_version, fp.space_left);
            if (res != NO_ERROR)
                break;
        }
    }

    return res;
}

#define PTE(_cap_id, _parse_fn) { .cap_id = _cap_id, .parse = _parse_fn }
static const pcie_caps_parse_table_entry_t PCIE_STANDARD_CAPS_PARSE_TABLE[] = {
    PTE(PCIE_CAP_ID_PCI_PWR_MGMT,             NULL),
    PTE(PCIE_CAP_ID_AGP,                      NULL),
    PTE(PCIE_CAP_ID_VPD,                      NULL),
    PTE(PCIE_CAP_ID_MSI,                      pcie_parse_msi_caps),
    PTE(PCIE_CAP_ID_PCIX,                     NULL),
    PTE(PCIE_CAP_ID_HYPERTRANSPORT,           NULL),
    PTE(PCIE_CAP_ID_VENDOR,                   NULL),
    PTE(PCIE_CAP_ID_DEBUG_PORT,               NULL),
    PTE(PCIE_CAP_ID_COMPACTPCI_CRC,           NULL),
    PTE(PCIE_CAP_ID_PCI_HOTPLUG,              NULL),
    PTE(PCIE_CAP_ID_PCI_BRIDGE_SUBSYSTEM_VID, NULL),
    PTE(PCIE_CAP_ID_AGP_8X,                   NULL),
    PTE(PCIE_CAP_ID_SECURE_DEVICE,            NULL),
    PTE(PCIE_CAP_ID_PCI_EXPRESS,              pcie_parse_pci_express_caps),
    PTE(PCIE_CAP_ID_MSIX,                     NULL),
    PTE(PCIE_CAP_ID_SATA_DATA_NDX_CFG,        NULL),
    PTE(PCIE_CAP_ID_ADVANCED_FEATURES,        pcie_parse_pci_advanced_features),
    PTE(PCIE_CAP_ID_ENHANCED_ALLOCATION,      NULL),
};

static const pcie_caps_parse_table_entry_t PCIE_EXTENDED_CAPS_PARSE_TABLE[] = {
    PTE(PCIE_EXT_CAP_ID_ADVANCED_ERROR_REPORTING,              NULL),
    PTE(PCIE_EXT_CAP_ID_VIRTUAL_CHANNEL_NO_MFVC,               NULL),
    PTE(PCIE_EXT_CAP_ID_DEVICE_SERIAL_NUMBER,                  NULL),
    PTE(PCIE_EXT_CAP_ID_POWER_BUDGETING,                       NULL),
    PTE(PCIE_EXT_CAP_ID_ROOT_COMPLEX_LINK_DECLARATION,         NULL),
    PTE(PCIE_EXT_CAP_ID_ROOT_COMPLEX_INTERNAL_LINK_CONTROL,    NULL),
    PTE(PCIE_EXT_CAP_ID_ROOT_COMPLEX_EVENT_COLLECTOR_EP_ASSOC, NULL),
    PTE(PCIE_EXT_CAP_ID_MULTI_FUNCTION_VIRTUAL_CHANNEL,        NULL),
    PTE(PCIE_EXT_CAP_ID_VIRTUAL_CHANNEL_MFVC,                  NULL),
    PTE(PCIE_EXT_CAP_ID_ROOT_COMPLEX_REGISTER_BLOCK,           NULL),
    PTE(PCIE_EXT_CAP_ID_VENDOR_SPECIFIC,                       NULL),
    PTE(PCIE_EXT_CAP_ID_CONFIGURATION_ACCESS_CORRELATION,      NULL),
    PTE(PCIE_EXT_CAP_ID_ACCESS_CONTROL_SERVICES,               NULL),
    PTE(PCIE_EXT_CAP_ID_ALTERNATIVE_ROUTING_ID_INTERPRETATION, NULL),
    PTE(PCIE_EXT_CAP_ID_ADDRESS_TRANSLATION_SERVICES,          NULL),
    PTE(PCIE_EXT_CAP_ID_SINGLE_ROOT_IO_VIRTUALIZATION,         NULL),
    PTE(PCIE_EXT_CAP_ID_MULTI_ROOT_IO_VIRTUALIZATION,          NULL),
    PTE(PCIE_EXT_CAP_ID_MULTICAST,                             NULL),
    PTE(PCIE_EXT_CAP_ID_PAGE_REQUEST,                          NULL),
    PTE(PCIE_EXT_CAP_ID_RESERVED_FOR_AMD,                      NULL),
    PTE(PCIE_EXT_CAP_ID_RESIZABLE_BAR,                         NULL),
    PTE(PCIE_EXT_CAP_ID_DYNAMIC_POWER_ALLOCATION,              NULL),
    PTE(PCIE_EXT_CAP_ID_TLP_PROCESSING_HINTS,                  NULL),
    PTE(PCIE_EXT_CAP_ID_LATENCY_TOLERANCE_REPORTING,           NULL),
    PTE(PCIE_EXT_CAP_ID_SECONDARY_PCI_EXPRESS,                 NULL),
    PTE(PCIE_EXT_CAP_ID_PROTOCOL_MULTIPLEXING,                 NULL),
    PTE(PCIE_EXT_CAP_ID_PROCESS_ADDRESS_SPACE_ID,              NULL),
    PTE(PCIE_EXT_CAP_ID_LN_REQUESTER,                          NULL),
    PTE(PCIE_EXT_CAP_ID_DOWNSTREAM_PORT_CONTAINMENT,           NULL),
    PTE(PCIE_EXT_CAP_ID_L1_PM_SUBSTATES,                       NULL),
    PTE(PCIE_EXT_CAP_ID_PRECISION_TIME_MEASUREMENT,            NULL),
    PTE(PCIE_EXT_CAP_ID_PCI_EXPRESS_OVER_MPHY,                 NULL),
    PTE(PCIE_EXT_CAP_ID_FRS_QUEUEING,                          NULL),
    PTE(PCIE_EXT_CAP_ID_READINESS_TIME_REPORTING,              NULL),
    PTE(PCIE_EXT_CAP_ID_DESIGNATED_VENDOR_SPECIFIC,            NULL),
};
#undef PTE

static const pcie_do_parse_caps_params_t PCIE_STANDARD_PARSE_CAPS_PARAMS = {
    .fetch_hdr         = pcie_fetch_standard_cap_hdr,
    .parse_table       = PCIE_STANDARD_CAPS_PARSE_TABLE,
    .parse_table_size  = countof(PCIE_STANDARD_CAPS_PARSE_TABLE),
    .max_possible_caps = PCIE_MAX_CAPABILITIES,
};

static const pcie_do_parse_caps_params_t PCIE_EXTENDED_PARSE_CAPS_PARAMS = {
    .fetch_hdr         = pcie_fetch_extended_cap_hdr,
    .parse_table       = PCIE_EXTENDED_CAPS_PARSE_TABLE,
    .parse_table_size  = countof(PCIE_EXTENDED_CAPS_PARSE_TABLE),
    .max_possible_caps = PCIE_MAX_EXT_CAPABILITIES,
};

status_t pcie_parse_capabilities(struct pcie_device_state* dev) {
    status_t ret = pcie_do_parse_caps(dev, &PCIE_STANDARD_PARSE_CAPS_PARAMS);

    if (NO_ERROR != ret)
        return ret;

    return pcie_do_parse_caps(dev, &PCIE_EXTENDED_PARSE_CAPS_PARAMS);
};
