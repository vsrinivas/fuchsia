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

#include <assert.h>
#include <ddk/device.h>
#include <ddk/driver.h>
#include <ddk/protocol/i2c.h>
#include <ddk/protocol/pci.h>
#include <hw/pci.h>
#include <intel-serialio/reg.h>
#include <intel-serialio/serialio.h>
#include <magenta/syscalls.h>
#include <magenta/types.h>
#include <mxu/list.h>
#include <runtime/mutex.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "controller.h"
#include "slave.h"

// Implement the functionality of the i2c bus device.

static uint32_t chip_addr_mask(int width) {
    return ((1 << width) - 1);
}

static mx_status_t intel_serialio_i2c_find_slave(
    intel_serialio_i2c_slave_device_t** slave,
    intel_serialio_i2c_device_t* device, uint16_t address) {
    assert(slave);

    list_for_every_entry (&device->slave_list, *slave,
                          intel_serialio_i2c_slave_device_t,
                          slave_list_node) {
        if ((*slave)->chip_address == address)
            return NO_ERROR;
    }

    return ERR_NOT_FOUND;
}

static mx_status_t intel_serialio_i2c_add_slave(
    mx_device_t* dev, uint8_t width, uint16_t address) {
    mx_status_t status;

    if ((width != I2C_7BIT_ADDRESS && width != I2C_10BIT_ADDRESS) ||
        (address & ~chip_addr_mask(width)) != 0) {
        return ERR_INVALID_ARGS;
    }

    intel_serialio_i2c_device_t* device = get_intel_serialio_i2c_device(dev);

    intel_serialio_i2c_slave_device_t* slave;

    mxr_mutex_lock(&device->mutex);

    // Make sure a slave with the given address doesn't already exist.
    status = intel_serialio_i2c_find_slave(&slave, device, address);
    if (status == NO_ERROR) {
        status = ERR_ALREADY_EXISTS;
        goto fail2;
    } else if (status != ERR_NOT_FOUND) {
        goto fail2;
    }

    slave = malloc(sizeof(*slave));
    if (!slave) {
        status = ERR_NO_MEMORY;
        goto fail1;
    }

    status = intel_serialio_i2c_slave_device_init(dev, slave, width, address);
    if (status < 0)
        goto fail1;

    list_add_head(&device->slave_list, &slave->slave_list_node);

    status = device_add(&slave->device, dev);
    if (status < 0)
        goto fail1;

    mxr_mutex_unlock(&device->mutex);
    return NO_ERROR;
fail1:
    free(slave);
fail2:
    mxr_mutex_unlock(&device->mutex);
    return status;
}

static mx_status_t intel_serialio_i2c_remove_slave(
    mx_device_t* dev, uint8_t width, uint16_t address) {
    mx_status_t status;

    if ((width != I2C_7BIT_ADDRESS && width != I2C_10BIT_ADDRESS) ||
        (address & ~chip_addr_mask(width)) != 0) {
        return ERR_INVALID_ARGS;
    }

    intel_serialio_i2c_device_t* device = get_intel_serialio_i2c_device(dev);

    intel_serialio_i2c_slave_device_t* slave;

    mxr_mutex_lock(&device->mutex);

    // Find the slave we're trying to remove.
    status = intel_serialio_i2c_find_slave(&slave, device, address);
    if (status < 0)
        goto remove_slave_finish;
    if (slave->chip_address_width != width) {
        xprintf("Chip address width mismatch.\n");
        status = ERR_NOT_FOUND;
        goto remove_slave_finish;
    }

    status = device_remove(&slave->device);
    if (status < 0)
        goto remove_slave_finish;

    list_delete(&slave->slave_list_node);
    free(slave);

remove_slave_finish:
    mxr_mutex_unlock(&device->mutex);
    return NO_ERROR;
}

static mx_status_t intel_serialio_i2c_set_bus_frequency_locked(
    mx_device_t *dev, uint32_t frequency) {
    if (frequency > I2C_MAX_FAST_SPEED_HZ)
        return ERR_INVALID_ARGS;

    intel_serialio_i2c_device_t* device = get_intel_serialio_i2c_device(dev);

    // Assume the base clock_frequency is 100 MHz, as alluded to in the docs.
    uint32_t clock_frequency = 100 * 1000 * 1000;

    // Compute high and low counts in multiples of the clock frequency. Make
    // them approximately equal.

    uint32_t period = clock_frequency / frequency;
    uint32_t high_count = period / 2;
    uint32_t low_count = period - high_count;

    // Make sure the counts are within bounds.
    if (high_count >= (1 << 16) || high_count < 6 ||
        low_count >= (1 << 16) || low_count < 8) {
        return ERR_OUT_OF_RANGE;
    }

    // Disable the controller before changing the frequency.
    uint32_t orig_en = *REG32(&device->regs->i2c_en);
    RMWREG32(&device->regs->i2c_en, I2C_EN_ENABLE, 1, 0);

    // Write the computed high and low counts into the fast speed registers.
    RMWREG32(&device->regs->fs_scl_hcnt, 0, 16, high_count);
    RMWREG32(&device->regs->fs_scl_lcnt, 0, 16, low_count);

    // Reenable the controller, if it was originally enabled.
    *REG32(&device->regs->i2c_en) = orig_en;

    device->frequency = frequency;

    return NO_ERROR;
}

static mx_status_t intel_serialio_i2c_set_bus_frequency(mx_device_t *dev,
                                                        uint32_t frequency) {
    mx_status_t status;

    intel_serialio_i2c_device_t *device = get_intel_serialio_i2c_device(dev);

    mxr_mutex_lock(&device->mutex);
    status = intel_serialio_i2c_set_bus_frequency_locked(dev, frequency);
    mxr_mutex_unlock(&device->mutex);

    return status;
}

static ssize_t intel_serialio_i2c_ioctl(
    mx_device_t* dev, uint32_t op, const void* in_buf, size_t in_len,
    void* out_buf, size_t out_len, void* cookie) {
    int ret;
    switch (op) {
    case I2C_BUS_ADD_SLAVE: {
        const i2c_ioctl_add_slave_args_t* args = in_buf;
        if (in_len < sizeof(*args))
            return ERR_INVALID_ARGS;

        ret = intel_serialio_i2c_add_slave(dev, args->chip_address_width,
                                           args->chip_address);
        break;
    }
    case I2C_BUS_REMOVE_SLAVE: {
        const i2c_ioctl_remove_slave_args_t* args = in_buf;
        if (in_len < sizeof(*args))
            return ERR_INVALID_ARGS;

        ret = intel_serialio_i2c_remove_slave(dev, args->chip_address_width,
                                              args->chip_address);
        break;
    }
    case I2C_BUS_SET_FREQUENCY: {
        const i2c_ioctl_set_bus_frequency_args_t* args = in_buf;
        if (in_len < sizeof(*args))
            return ERR_INVALID_ARGS;

        ret = intel_serialio_i2c_set_bus_frequency(dev, args->frequency);
        break;
    }
    default:
        return ERR_INVALID_ARGS;
    }

    if (ret == NO_ERROR)
        return in_len;
    else
        return ret;
}

static mx_protocol_device_t intel_serialio_i2c_device_proto = {
    .ioctl = &intel_serialio_i2c_ioctl,
};

// The controller lock should already be held when entering this function.
mx_status_t intel_serialio_i2c_reset_controller(
    intel_serialio_i2c_device_t *device) {
    mx_status_t status = NO_ERROR;

    // Reset the device. These values are reversed in the docs.
    RMWREG32(&device->regs->resets, 0, 2, 0x0);
    RMWREG32(&device->regs->resets, 0, 2, 0x3);

    status = intel_serialio_i2c_set_bus_frequency_locked(&device->device,
                                                         device->frequency);
    if (status < 0)
        return status;

    // Disable the controller.
    RMWREG32(&device->regs->i2c_en, I2C_EN_ENABLE, 1, 0);

    *REG32(&device->regs->ctl) =
        (0x1 << CTL_SLAVE_DISABLE) |
        (0x1 << CTL_RESTART_ENABLE) |
        (CTL_SPEED_FAST << CTL_SPEED) |
        (CTL_MASTER_MODE_ENABLED << CTL_MASTER_MODE);

    //XXX Do we need this?
    *REG32(&device->regs->intr_mask) = INTR_STOP_DETECTION;

    *REG32(&device->regs->rx_tl) = 0;
    *REG32(&device->regs->tx_tl) = 0;

    return status;
}

mx_status_t intel_serialio_bind_i2c(mx_driver_t* drv, mx_device_t* dev) {
    pci_protocol_t* pci;
    if (device_get_protocol(dev, MX_PROTOCOL_PCI, (void**)&pci))
        return ERR_NOT_SUPPORTED;

    mx_status_t status = pci->claim_device(dev);
    if (status < 0)
        return status;

    intel_serialio_i2c_device_t* device = malloc(sizeof(*device));
    if (!device)
        return ERR_NO_MEMORY;

    // Run the bus at 100KHz by default.
    device->frequency = 100000;

    list_initialize(&device->slave_list);
    memset(&device->mutex, 0, sizeof(device->mutex));

    device->regs_handle = pci->map_mmio(
        dev, 0, MX_CACHE_POLICY_UNCACHED_DEVICE,
        (void**)&device->regs, &device->regs_size);
    if (device->regs_handle < 0) {
        status = device->regs_handle;
        goto fail;
    }

    status = device_init(&device->device, drv, "intel_serialio_i2c",
                         &intel_serialio_i2c_device_proto);
    if (status < 0)
        goto fail;

    // Configure the I2C controller. We don't need to hold the lock because
    // nobody else can see this controller yet.
    status = intel_serialio_i2c_reset_controller(device);
    if (status < 0)
        goto fail;

    status = device_add(&device->device, dev);
    if (status < 0)
        goto fail;

    xprintf(
        "initialized intel serialio i2c driver, "
        "reg=%#x regsize=%#x\n",
        device->regs, device->regs_size);

    return NO_ERROR;

fail:
    if (device->regs_handle >= 0)
        _magenta_handle_close(device->regs_handle);
    free(device);

    return status;
}
