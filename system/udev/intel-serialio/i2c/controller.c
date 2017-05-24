// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <assert.h>
#include <ddk/device.h>
#include <ddk/driver.h>
#include <ddk/protocol/pci.h>
#include <errno.h>
#include <fcntl.h>
#include <hw/pci.h>
#include <intel-serialio/reg.h>
#include <intel-serialio/serialio.h>
#include <magenta/device/i2c.h>
#include <magenta/listnode.h>
#include <magenta/syscalls.h>
#include <magenta/types.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <threads.h>
#include <unistd.h>

#include "controller.h"
#include "slave.h"

#define DEVIDLE_CONTROL 0x24c
#define DEVIDLE_CONTROL_CMD_IN_PROGRESS 0
#define DEVIDLE_CONTROL_DEVIDLE 2
#define DEVIDLE_CONTROL_RESTORE_REQUIRED 3

#define ACER_I2C_TOUCH INTEL_SUNRISE_POINT_SERIALIO_I2C1_DID

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
    intel_serialio_i2c_device_t* device, uint8_t width, uint16_t address) {
    mx_status_t status;

    if ((width != I2C_7BIT_ADDRESS && width != I2C_10BIT_ADDRESS) ||
        (address & ~chip_addr_mask(width)) != 0) {
        return ERR_INVALID_ARGS;
    }

    intel_serialio_i2c_slave_device_t* slave;

    mtx_lock(&device->mutex);

    // Make sure a slave with the given address doesn't already exist.
    status = intel_serialio_i2c_find_slave(&slave, device, address);
    if (status == NO_ERROR) {
        status = ERR_ALREADY_EXISTS;
    }
    if (status != ERR_NOT_FOUND) {
        mtx_unlock(&device->mutex);
        return status;
    }

    slave = calloc(1, sizeof(*slave));
    if (!slave) {
        status = ERR_NO_MEMORY;
        mtx_unlock(&device->mutex);
        return status;
    }
    slave->chip_address_width = width;
    slave->chip_address = address;
    slave->controller = device;

    list_add_head(&device->slave_list, &slave->slave_list_node);
    mtx_unlock(&device->mutex);

    // Temporarily add binding support for the i2c slave. The real way to do
    // this will involve ACPI/devicetree enumeration, but for now we publish PCI
    // VID/DID and i2c ADDR as binding properties.

    // Retrieve pci_config (again)
    pci_protocol_t* pci;
    status = device_op_get_protocol(device->pcidev, MX_PROTOCOL_PCI, (void**)&pci);
    if (status != NO_ERROR) {
        goto fail2;
    }

    const pci_config_t* pci_config;
    mx_handle_t config_handle;
    status = pci->get_config(device->pcidev, &pci_config, &config_handle);
    if (status != NO_ERROR) {
        goto fail2;
    }

    int count = 0;
    slave->props[count++] = (mx_device_prop_t){BIND_PCI_VID, 0, pci_config->vendor_id};
    slave->props[count++] = (mx_device_prop_t){BIND_PCI_DID, 0, pci_config->device_id};
    slave->props[count++] = (mx_device_prop_t){BIND_I2C_ADDR, 0, address};

    char name[sizeof(address) * 2 + 2] = {
            [sizeof(name) - 1] = '\0',
    };
    snprintf(name, sizeof(name) - 1, "%04x", address);

   device_add_args_t args = {
        .version = DEVICE_ADD_ARGS_VERSION,
        .name = name,
        .ctx = slave,
        .ops = &intel_serialio_i2c_slave_device_proto,
        .props = slave->props,
        .prop_count = count,
    };

    status = device_add(device->mxdev, &args, &slave->mxdev);
    if (status != NO_ERROR) {
        goto fail1;
    }

    return NO_ERROR;

fail1:
    mx_handle_close(config_handle);
fail2:
    mtx_lock(&device->mutex);
    list_delete(&slave->slave_list_node);
    mtx_unlock(&device->mutex);
    free(slave);
    return status;
}

static mx_status_t intel_serialio_i2c_remove_slave(
    intel_serialio_i2c_device_t* device, uint8_t width, uint16_t address) {
    mx_status_t status;

    if ((width != I2C_7BIT_ADDRESS && width != I2C_10BIT_ADDRESS) ||
        (address & ~chip_addr_mask(width)) != 0) {
        return ERR_INVALID_ARGS;
    }

    intel_serialio_i2c_slave_device_t* slave;

    mtx_lock(&device->mutex);

    // Find the slave we're trying to remove.
    status = intel_serialio_i2c_find_slave(&slave, device, address);
    if (status < 0)
        goto remove_slave_finish;
    if (slave->chip_address_width != width) {
        xprintf("Chip address width mismatch.\n");
        status = ERR_NOT_FOUND;
        goto remove_slave_finish;
    }

    status = device_remove(slave->mxdev);
    if (status < 0)
        goto remove_slave_finish;

    list_delete(&slave->slave_list_node);
    free(slave);

remove_slave_finish:
    mtx_unlock(&device->mutex);
    return status;
}

static uint32_t intel_serialio_compute_scl_hcnt(
    uint32_t controller_freq,
    uint32_t t_high_nanos,
    uint32_t t_r_nanos) {

    uint32_t clock_freq_kilohz = controller_freq / 1000;

    // We need high count to satisfy highcount + 3 >= clock * (t_HIGH + t_r_max)
    // Apparently the counter starts as soon as the controller releases SCL, so
    // include t_r to account for potential delay in rising.
    //
    // In terms of units, the division should really be thought of as a
    // (1 s)/(1000000000 ns) factor to get this into the right scale.
    uint32_t high_count =
        (clock_freq_kilohz * (t_high_nanos + t_r_nanos) + 500000);
    return high_count / 1000000 - 3;
}

static uint32_t intel_serialio_compute_scl_lcnt(
    uint32_t controller_freq,
    uint32_t t_low_nanos,
    uint32_t t_f_nanos) {

    uint32_t clock_freq_kilohz = controller_freq / 1000;

    // We need low count to satisfy lowcount + 1 >= clock * (t_LOW + t_f_max)
    // Apparently the counter starts as soon as the controller pulls SCL low, so
    // include t_f to account for potential delay in falling.
    //
    // In terms of units, the division should really be thought of as a
    // (1 s)/(1000000000 ns) factor to get this into the right scale.
    uint32_t low_count =
        (clock_freq_kilohz * (t_low_nanos + t_f_nanos) + 500000);
    return low_count / 1000000 - 1;
}

static mx_status_t intel_serialio_configure_bus_timing(
    intel_serialio_i2c_device_t* device) {

    uint32_t clock_frequency = device->controller_freq;

    // These constants are from the i2c timing requirements
    uint32_t fs_hcnt = intel_serialio_compute_scl_hcnt(
        clock_frequency, 600, 300);
    uint32_t fs_lcnt = intel_serialio_compute_scl_lcnt(
        clock_frequency, 1300, 300);
    uint32_t ss_hcnt = intel_serialio_compute_scl_hcnt(
        clock_frequency, 4000, 300);
    uint32_t ss_lcnt = intel_serialio_compute_scl_lcnt(
        clock_frequency, 4700, 300);

    // Make sure the counts are within bounds.
    if (fs_hcnt >= (1 << 16) || fs_hcnt < 6 ||
        fs_lcnt >= (1 << 16) || fs_lcnt < 8) {
        return ERR_OUT_OF_RANGE;
    }
    if (ss_hcnt >= (1 << 16) || ss_hcnt < 6 ||
        ss_lcnt >= (1 << 16) || ss_lcnt < 8) {
        return ERR_OUT_OF_RANGE;
    }

    RMWREG32(&device->regs->fs_scl_hcnt, 0, 16, fs_hcnt);
    RMWREG32(&device->regs->fs_scl_lcnt, 0, 16, fs_lcnt);
    RMWREG32(&device->regs->ss_scl_hcnt, 0, 16, ss_hcnt);
    RMWREG32(&device->regs->ss_scl_lcnt, 0, 16, ss_lcnt);
    return NO_ERROR;
}

static mx_status_t intel_serialio_i2c_set_bus_frequency(intel_serialio_i2c_device_t* device,
                                                        uint32_t frequency) {
    if (frequency != I2C_MAX_FAST_SPEED_HZ &&
        frequency != I2C_MAX_STANDARD_SPEED_HZ) {
        return ERR_INVALID_ARGS;
    }

    mtx_lock(&device->mutex);
    device->bus_freq = frequency;

    unsigned int speed = CTL_SPEED_STANDARD;
    if (device->bus_freq == I2C_MAX_FAST_SPEED_HZ) {
        speed = CTL_SPEED_FAST;
    }
    RMWREG32(&device->regs->ctl, CTL_SPEED, 2, speed);
    mtx_unlock(&device->mutex);

    return NO_ERROR;
}

static mx_status_t intel_serialio_i2c_ioctl(
    void* ctx, uint32_t op, const void* in_buf, size_t in_len,
    void* out_buf, size_t out_len, size_t* out_actual) {
    intel_serialio_i2c_device_t* device = ctx;
    switch (op) {
    case IOCTL_I2C_BUS_ADD_SLAVE: {
        const i2c_ioctl_add_slave_args_t* args = in_buf;
        if (in_len < sizeof(*args))
            return ERR_INVALID_ARGS;

        return intel_serialio_i2c_add_slave(device, args->chip_address_width,
                                            args->chip_address);
    }
    case IOCTL_I2C_BUS_REMOVE_SLAVE: {
        const i2c_ioctl_remove_slave_args_t* args = in_buf;
        if (in_len < sizeof(*args))
            return ERR_INVALID_ARGS;

        return intel_serialio_i2c_remove_slave(device, args->chip_address_width,
                                              args->chip_address);
    }
    case IOCTL_I2C_BUS_SET_FREQUENCY: {
        const i2c_ioctl_set_bus_frequency_args_t* args = in_buf;
        if (in_len < sizeof(*args)) {
            return ERR_INVALID_ARGS;
        }
        intel_serialio_i2c_set_bus_frequency(device, args->frequency);
    }
    default:
        return ERR_INVALID_ARGS;
    }
}

static void intel_serialio_i2c_release(void* ctx) {
    intel_serialio_i2c_device_t* cont = ctx;
    free(cont);
}

static mx_protocol_device_t intel_serialio_i2c_device_proto = {
    .version = DEVICE_OPS_VERSION,
    .ioctl = intel_serialio_i2c_ioctl,
    .release = intel_serialio_i2c_release,
};

// The controller lock should already be held when entering this function.
mx_status_t intel_serialio_i2c_reset_controller(
    intel_serialio_i2c_device_t* device) {
    mx_status_t status = NO_ERROR;

    // The register will only return valid values if the ACPI _PS0 has been
    // evaluated.
    if (*REG32((void*)device->regs + DEVIDLE_CONTROL) != 0xffffffff) {
        // Wake up device if it is in DevIdle state
        RMWREG32((void*)device->regs + DEVIDLE_CONTROL, DEVIDLE_CONTROL_DEVIDLE, 1, 0);

        // Wait for wakeup to finish processing
        int retry = 10;
        while (retry-- &&
               (*REG32((void*)device->regs + DEVIDLE_CONTROL) & (1 << DEVIDLE_CONTROL_CMD_IN_PROGRESS))) {
            usleep(10);
        }
        if (!retry) {
            printf("i2c-controller: timed out waiting for device idle\n");
            return ERR_TIMED_OUT;
        }
    }

    // Reset the device.
    RMWREG32(device->soft_reset, 0, 2, 0x0);
    RMWREG32(device->soft_reset, 0, 2, 0x3);

    // Clear the "Restore Required" flag
    RMWREG32((void*)device->regs + DEVIDLE_CONTROL, DEVIDLE_CONTROL_RESTORE_REQUIRED, 1, 0);

    // Disable the controller.
    RMWREG32(&device->regs->i2c_en, I2C_EN_ENABLE, 1, 0);

    // Reconfigure the bus timing
    status = intel_serialio_configure_bus_timing(device);
    if (status < 0)
        return status;

    unsigned int speed = CTL_SPEED_STANDARD;
    if (device->bus_freq == I2C_MAX_FAST_SPEED_HZ) {
        speed = CTL_SPEED_FAST;
    }

    *REG32(&device->regs->ctl) =
        (0x1 << CTL_SLAVE_DISABLE) |
        (0x1 << CTL_RESTART_ENABLE) |
        (speed << CTL_SPEED) |
        (CTL_MASTER_MODE_ENABLED << CTL_MASTER_MODE);

    //XXX Do we need this?
    *REG32(&device->regs->intr_mask) = INTR_STOP_DETECTION;

    *REG32(&device->regs->rx_tl) = 0;
    *REG32(&device->regs->tx_tl) = 0;

    return status;
}

static mx_status_t intel_serialio_i2c_device_specific_init(
    intel_serialio_i2c_device_t* device,
    const pci_config_t* pci_config) {

    static const struct {
        uint16_t device_ids[16];
        // Offset of the soft reset register
        size_t reset_offset;
        // Internal controller frequency, in hertz
        uint32_t controller_clock_frequency;
    } dev_props[] = {
        {
            .device_ids = {
                INTEL_SUNRISE_POINT_SERIALIO_I2C0_DID,
                INTEL_SUNRISE_POINT_SERIALIO_I2C1_DID,
                INTEL_SUNRISE_POINT_SERIALIO_I2C2_DID,
                INTEL_SUNRISE_POINT_SERIALIO_I2C3_DID,
            },
            .reset_offset = 0x204,
            .controller_clock_frequency = 120 * 1000 * 1000,
        },
        {
            .device_ids = {
                INTEL_WILDCAT_POINT_SERIALIO_I2C0_DID, INTEL_WILDCAT_POINT_SERIALIO_I2C1_DID,
            },
            .reset_offset = 0x804,
            .controller_clock_frequency = 100 * 1000 * 1000,
        },
    };

    uint16_t device_id = pci_config->device_id;

    for (unsigned int i = 0; i < countof(dev_props); ++i) {
        const unsigned int num_dev_ids = countof(dev_props[0].device_ids);
        for (unsigned int dev_idx = 0; dev_idx < num_dev_ids; ++dev_idx) {
            if (!dev_props[i].device_ids[dev_idx]) {
                break;
            }
            if (dev_props[i].device_ids[dev_idx] != device_id) {
                continue;
            }

            device->controller_freq = dev_props[i].controller_clock_frequency;
            device->soft_reset = (void*)device->regs +
                                 dev_props[i].reset_offset;
            return NO_ERROR;
        }
    }

    return ERR_NOT_SUPPORTED;
}

mx_status_t intel_serialio_bind_i2c(mx_device_t* dev) {
    pci_protocol_t* pci;
    if (device_op_get_protocol(dev, MX_PROTOCOL_PCI, (void**)&pci))
        return ERR_NOT_SUPPORTED;

    mx_status_t status = pci->claim_device(dev);
    if (status < 0)
        return status;

    intel_serialio_i2c_device_t* device = calloc(1, sizeof(*device));
    if (!device)
        return ERR_NO_MEMORY;

    list_initialize(&device->slave_list);
    mtx_init(&device->mutex, mtx_plain);
    device->pcidev = dev;

    const pci_config_t* pci_config;
    mx_handle_t config_handle;
    status = pci->get_config(dev, &pci_config, &config_handle);
    if (status != NO_ERROR) {
        goto fail;
    }

    status = pci->map_mmio(
        dev, 0, MX_CACHE_POLICY_UNCACHED_DEVICE,
        (void**)&device->regs, &device->regs_size, &device->regs_handle);
    if (status != NO_ERROR) {
        goto fail;
    }

    // Run the bus at standard speed by default.
    device->bus_freq = I2C_MAX_STANDARD_SPEED_HZ;

    status = intel_serialio_i2c_device_specific_init(device, pci_config);
    if (status < 0)
        goto fail;

    // This is a temporary workaround until we have full ACPI device
    // enumeration. If this is the I2C1 bus, we run _PS0 so the controller is
    // active.
    if (pci_config->vendor_id == INTEL_VID &&
        pci_config->device_id == ACER_I2C_TOUCH) {
        int dmctlfd = open("/dev/misc/dmctl", O_RDWR);
        if (dmctlfd < 0) {
            printf("could not open dmctl: %d\n", errno);
        } else {
            const char* i2c1 = "acpi-ps0:\\_SB.PCI0.I2C1";
            ssize_t wr = write(dmctlfd, i2c1, strlen(i2c1));
            if (wr < 0) {
                printf("could not run ps0 for %s: %zd\n", i2c1, wr);
            }
            close(dmctlfd);
        }
    }

    // Configure the I2C controller. We don't need to hold the lock because
    // nobody else can see this controller yet.
    status = intel_serialio_i2c_reset_controller(device);
    if (status < 0)
        goto fail;

    char name[MX_DEVICE_NAME_MAX];
    snprintf(name, sizeof(name), "i2c-bus-%04x", pci_config->device_id);

   device_add_args_t args = {
        .version = DEVICE_ADD_ARGS_VERSION,
        .name = name,
        .ctx = device,
        .ops = &intel_serialio_i2c_device_proto,
    };

    status = device_add(dev, &args, &device->mxdev);
    if (status < 0) {
        goto fail;
    }

    xprintf(
        "initialized intel serialio i2c driver, "
        "reg=%p regsize=%ld\n",
        device->regs, device->regs_size);

    // Temporarily setup the controller for the Acer12 touch panel. This will
    // eventually be done by enumerating the device via ACPI, but for now we
    // hardcode it.
    if (pci_config->vendor_id == INTEL_VID &&
        pci_config->device_id == ACER_I2C_TOUCH) {
        intel_serialio_i2c_set_bus_frequency(device, 400000);
        intel_serialio_i2c_add_slave(device, I2C_7BIT_ADDRESS, 0x0010);
    }
    mx_handle_close(config_handle);
    return NO_ERROR;

fail:
    if (device->regs_handle > 0)
        mx_handle_close(device->regs_handle);
    if (config_handle)
        mx_handle_close(config_handle);
    free(device);

    return status;
}
