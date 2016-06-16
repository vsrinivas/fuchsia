// Copyright 2016 The Fuchsia Authors
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include <ddk/device.h>
#include <ddk/driver.h>
#include <ddk/binding.h>

#include <magenta/syscalls-ddk.h>
#include <magenta/types.h>
#include <assert.h>
#include <limits.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "gpt.h"
#include "sata.h"

#define TRACE 1

#if TRACE
#define xprintf(fmt...) printf(fmt)
#else
#define xprintf(fmt...) \
    do {                \
    } while (0)
#endif

#define sata_devinfo_u32(base, offs) (((uint32_t)(base)[(offs) + 1] << 16) | ((uint32_t)(base)[(offs)]))
#define sata_devinfo_u64(base, offs) (((uint64_t)(base)[(offs) + 3] << 48) | ((uint64_t)(base)[(offs) + 2] << 32) | ((uint64_t)(base)[(offs) + 1] << 16) | ((uint32_t)(base)[(offs)]))

mx_status_t sata_read_block_sync(sata_device_t* dev, uint64_t lba, mx_paddr_t mem, size_t mem_sz) {
    if (!(dev->flags & SATA_FLAG_DMA) || !(dev->flags & SATA_FLAG_LBA48)) return ERR_NOT_SUPPORTED;
    // send READ DMA EXT
    memset(&dev->curr_cmd, 0, sizeof(sata_cmd_t));
    dev->curr_cmd.cmd = SATA_CMD_READ_DMA_EXT;
    dev->curr_cmd.device = 0x40;
    dev->curr_cmd.data_phys = mem;
    if (mem_sz >= 0x400000) { // 4mb hardware limit per prd
        return ERR_INVALID_ARGS;
    }
    bool align = !(mem_sz % dev->sector_sz);
    if (!align) {
        xprintf("%s: mem_sz not aligned to sector size\n", dev->device.name);
    }
    dev->curr_cmd.data_sz = mem_sz;
    dev->curr_cmd.lba = lba;
    dev->curr_cmd.count = align ? mem_sz / dev->sector_sz : mem_sz / dev->sector_sz + 1;
    mx_status_t status = ahci_port_do_cmd_sync(dev->channel, &dev->curr_cmd);
    if (status < 0) return status;
    if (dev->curr_cmd.status != NO_ERROR) return dev->curr_cmd.status;
    return NO_ERROR;
}

static mx_status_t sata_device_identify(sata_device_t* dev) {
    // send IDENTIFY DEVICE
    memset(&dev->curr_cmd, 0, sizeof(sata_cmd_t));
    dev->curr_cmd.cmd = SATA_CMD_IDENTIFY_DEVICE;
    dev->curr_cmd.data_phys = dev->mem_phys;
    dev->curr_cmd.data_sz = 512;
    mx_status_t status = ahci_port_do_cmd_sync(dev->channel, &dev->curr_cmd);
    if (status < 0) return status;
    if (dev->curr_cmd.status != NO_ERROR) return dev->curr_cmd.status;

    // parse results
    int flags = 0;
    uint16_t* devinfo = dev->mem;
    char str[41]; // model id is 40 chars
    xprintf("%s: dev info\n", dev->device.name);
    snprintf(str, SATA_DEVINFO_SERIAL_LEN + 1, "%s", (char*)(devinfo + SATA_DEVINFO_SERIAL));
    xprintf("  serial=%s\n", str);
    snprintf(str, SATA_DEVINFO_FW_REV_LEN + 1, "%s", (char*)(devinfo + SATA_DEVINFO_FW_REV));
    xprintf("  firmware rev=%s\n", str);
    snprintf(str, SATA_DEVINFO_MODEL_ID_LEN + 1, "%s", (char*)(devinfo + SATA_DEVINFO_MODEL_ID));
    xprintf("  model id=%s\n", str);

    uint16_t major = *(devinfo + SATA_DEVINFO_MAJOR_VERS);
    if (major & (1 << 8)) {
        xprintf("  ATA8-ACS");
    } else if (major & (0xf << 4)) {
        xprintf("  ATA/ATAPI");
    }

    uint16_t cap = *(devinfo + SATA_DEVINFO_CAP);
    if (cap & (1 << 8)) {
        xprintf(" DMA\n");
        flags |= SATA_FLAG_DMA;
    } else {
        xprintf(" PIO\n");
    }
    if (cap & (1 << 9)) {
        if (*(devinfo + SATA_DEVINFO_CMD_SET_2) & (1 << 10)) {
            xprintf("  LBA48 %llu sectors", sata_devinfo_u64(devinfo, SATA_DEVINFO_LBA_CAPACITY_2));
            flags |= SATA_FLAG_LBA48;
        } else {
            xprintf("  LBA %u sectors", sata_devinfo_u32(devinfo, SATA_DEVINFO_LBA_CAPACITY));
        }
        dev->sector_sz = 512; // default
        if ((*(devinfo + SATA_DEVINFO_SECTOR_SIZE) & 0xd000) == 0x5000) {
            dev->sector_sz = 2 * sata_devinfo_u32(devinfo, SATA_DEVINFO_LOGICAL_SECTOR_SIZE);
        }
        xprintf(" sector size=%d\n", dev->sector_sz);
    } else {
        xprintf("  CHS unsupported!\n");
    }
    dev->flags = flags;

    return NO_ERROR;
}

// implement device protocol:

static mx_protocol_device_t sata_device_proto = {
};

mx_status_t sata_bind(mx_driver_t* drv, mx_device_t* dev, ahci_port_t* port) {
    // initialize the device
    sata_device_t* device = calloc(1, sizeof(sata_device_t));
    if (!device) {
        xprintf("sata: out of memory\n");
        return ERR_NO_MEMORY;
    }

    char name[8];
    snprintf(name, sizeof(name), "sata%d", port->nr);
    mx_status_t status = device_init(&device->device, drv, name, &sata_device_proto);
    if (status) {
        xprintf("sata: failed to init device\n");
        goto fail;
    }

    // allocate some dma memory
    device->mem_sz = PAGE_SIZE;
    status = mx_alloc_device_memory(device->mem_sz, &device->mem_phys, &device->mem);
    if (status < 0) {
        xprintf("%s: error %d allocating dma memory\n", device->device.name, status);
        goto fail;
    }

    device->channel = port;

    // add the device
    device_add(&device->device, dev);

    // send device identify
    sata_device_identify(device);

    // read partition table
    // FIXME layering?
    gpt_bind(drv, &device->device, device);

    return NO_ERROR;
fail:
    free(device);
    return status;
}
