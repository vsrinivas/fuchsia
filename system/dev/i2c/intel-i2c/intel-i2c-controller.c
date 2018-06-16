// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <assert.h>
#include <ddk/debug.h>
#include <ddk/device.h>
#include <ddk/driver.h>
#include <ddk/protocol/pci.h>
#include <hw/reg.h>
#include <errno.h>
#include <fcntl.h>
#include <hw/pci.h>
#include <zircon/device/i2c.h>
#include <zircon/listnode.h>
#include <zircon/syscalls.h>
#include <zircon/types.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <threads.h>
#include <unistd.h>

#include "binding.h"
#include "intel-i2c-controller.h"
#include "intel-i2c-slave.h"

#define DEVIDLE_CONTROL 0x24c
#define DEVIDLE_CONTROL_CMD_IN_PROGRESS 0
#define DEVIDLE_CONTROL_DEVIDLE 2
#define DEVIDLE_CONTROL_RESTORE_REQUIRED 3

#define ACER_I2C_TOUCH INTEL_SUNRISE_POINT_SERIALIO_I2C1_DID

// Number of entries at which the FIFO level triggers happen
#define DEFAULT_RX_FIFO_TRIGGER_LEVEL 8
#define DEFAULT_TX_FIFO_TRIGGER_LEVEL 8

// Signals used on the controller's event_handle
#define RX_FULL_SIGNAL ZX_USER_SIGNAL_0
#define TX_EMPTY_SIGNAL ZX_USER_SIGNAL_1
#define STOP_DETECTED_SIGNAL ZX_USER_SIGNAL_2
#define ERROR_DETECTED_SIGNAL ZX_USER_SIGNAL_3

// Implement the functionality of the i2c bus device.

static uint32_t chip_addr_mask(int width) {
    return ((1 << width) - 1);
}

static zx_status_t intel_serialio_i2c_find_slave(
    intel_serialio_i2c_slave_device_t** slave,
    intel_serialio_i2c_device_t* device, uint16_t address) {
    assert(slave);

    list_for_every_entry (&device->slave_list, *slave,
                          intel_serialio_i2c_slave_device_t,
                          slave_list_node) {
        if ((*slave)->chip_address == address)
            return ZX_OK;
    }

    return ZX_ERR_NOT_FOUND;
}

static zx_status_t intel_serialio_i2c_add_slave(intel_serialio_i2c_device_t* device,
                                                uint8_t width, uint16_t address,
                                                uint32_t protocol_id,
                                                zx_device_prop_t* moreprops, uint32_t propcount) {
    zx_status_t status;

    if ((width != I2C_7BIT_ADDRESS && width != I2C_10BIT_ADDRESS) ||
        (address & ~chip_addr_mask(width)) != 0) {
        return ZX_ERR_INVALID_ARGS;
    }

    intel_serialio_i2c_slave_device_t* slave;

    mtx_lock(&device->mutex);

    // Make sure a slave with the given address doesn't already exist.
    status = intel_serialio_i2c_find_slave(&slave, device, address);
    if (status == ZX_OK) {
        status = ZX_ERR_ALREADY_EXISTS;
    }
    if (status != ZX_ERR_NOT_FOUND) {
        mtx_unlock(&device->mutex);
        return status;
    }

    slave = calloc(1, sizeof(*slave));
    if (!slave) {
        status = ZX_ERR_NO_MEMORY;
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
    pci_protocol_t pci;
    status = device_get_protocol(device->pcidev, ZX_PROTOCOL_PCI, &pci);
    if (status != ZX_OK) {
        goto fail;
    }

    uint16_t vendor_id;
    uint16_t device_id;
    int count = 0;

    pci_config_read16(&pci, PCI_CONFIG_VENDOR_ID, &vendor_id);
    pci_config_read16(&pci, PCI_CONFIG_DEVICE_ID, &device_id);

    zx_device_prop_t props[8];
    if (countof(props) < 3 + propcount) {
        zxlogf(ERROR, "i2c: slave at 0x%02x has too many props! (%u)\n", address, propcount);
        status = ZX_ERR_INVALID_ARGS;
        goto fail;
    }
    props[count++] = (zx_device_prop_t){BIND_PCI_VID, 0, vendor_id};
    props[count++] = (zx_device_prop_t){BIND_PCI_DID, 0, device_id};
    props[count++] = (zx_device_prop_t){BIND_I2C_ADDR, 0, address};
    memcpy(&props[count], moreprops, sizeof(zx_device_prop_t) * propcount);
    count += propcount;

    char name[sizeof(address) * 2 + 2] = {
            [sizeof(name) - 1] = '\0',
    };
    snprintf(name, sizeof(name) - 1, "%04x", address);

   device_add_args_t args = {
        .version = DEVICE_ADD_ARGS_VERSION,
        .name = name,
        .ctx = slave,
        .ops = &intel_serialio_i2c_slave_device_proto,
        .proto_id = protocol_id,
        .props = props,
        .prop_count = count,
    };

    status = device_add(device->zxdev, &args, &slave->zxdev);
    if (status != ZX_OK) {
        goto fail;
    }

    return ZX_OK;

fail:
    mtx_lock(&device->mutex);
    list_delete(&slave->slave_list_node);
    mtx_unlock(&device->mutex);
    free(slave);
    return status;
}

static zx_status_t intel_serialio_i2c_remove_slave(
    intel_serialio_i2c_device_t* device, uint8_t width, uint16_t address) {
    zx_status_t status;

    if ((width != I2C_7BIT_ADDRESS && width != I2C_10BIT_ADDRESS) ||
        (address & ~chip_addr_mask(width)) != 0) {
        return ZX_ERR_INVALID_ARGS;
    }

    intel_serialio_i2c_slave_device_t* slave;

    mtx_lock(&device->mutex);

    // Find the slave we're trying to remove.
    status = intel_serialio_i2c_find_slave(&slave, device, address);
    if (status < 0)
        goto remove_slave_finish;
    if (slave->chip_address_width != width) {
        zxlogf(ERROR, "Chip address width mismatch.\n");
        status = ZX_ERR_NOT_FOUND;
        goto remove_slave_finish;
    }

    status = device_remove(slave->zxdev);
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

static zx_status_t intel_serialio_compute_bus_timing(
    intel_serialio_i2c_device_t* device) {

    uint32_t clock_frequency = device->controller_freq;

    // These constants are from the i2c timing requirements
    uint32_t fmp_hcnt = intel_serialio_compute_scl_hcnt(
        clock_frequency, 260, 120);
    uint32_t fmp_lcnt = intel_serialio_compute_scl_lcnt(
        clock_frequency, 500, 120);
    uint32_t fs_hcnt = intel_serialio_compute_scl_hcnt(
        clock_frequency, 600, 300);
    uint32_t fs_lcnt = intel_serialio_compute_scl_lcnt(
        clock_frequency, 1300, 300);
    uint32_t ss_hcnt = intel_serialio_compute_scl_hcnt(
        clock_frequency, 4000, 300);
    uint32_t ss_lcnt = intel_serialio_compute_scl_lcnt(
        clock_frequency, 4700, 300);

    // Make sure the counts are within bounds.
    if (fmp_hcnt >= (1 << 16) || fmp_hcnt < 6 ||
        fmp_lcnt >= (1 << 16) || fmp_lcnt < 8) {
        return ZX_ERR_OUT_OF_RANGE;
    }
    if (fs_hcnt >= (1 << 16) || fs_hcnt < 6 ||
        fs_lcnt >= (1 << 16) || fs_lcnt < 8) {
        return ZX_ERR_OUT_OF_RANGE;
    }
    if (ss_hcnt >= (1 << 16) || ss_hcnt < 6 ||
        ss_lcnt >= (1 << 16) || ss_lcnt < 8) {
        return ZX_ERR_OUT_OF_RANGE;
    }

    device->fmp_scl_hcnt = fmp_hcnt;
    device->fmp_scl_lcnt = fmp_lcnt;
    device->fs_scl_hcnt = fs_hcnt;
    device->fs_scl_lcnt = fs_lcnt;
    device->ss_scl_hcnt = ss_hcnt;
    device->ss_scl_lcnt = ss_lcnt;
    device->sda_hold = 1;
    return ZX_OK;
}

static zx_status_t intel_serialio_i2c_set_bus_frequency(intel_serialio_i2c_device_t* device,
                                                        uint32_t frequency) {
    if (frequency != I2C_MAX_FAST_SPEED_HZ &&
        frequency != I2C_MAX_STANDARD_SPEED_HZ &&
        frequency != I2C_MAX_FAST_PLUS_SPEED_HZ) {
        return ZX_ERR_INVALID_ARGS;
    }

    mtx_lock(&device->mutex);
    device->bus_freq = frequency;

    zx_status_t status = intel_serialio_i2c_reset_controller(device);
    if (status != ZX_OK) {
        mtx_unlock(&device->mutex);
        return status;
    }

    mtx_unlock(&device->mutex);
    return ZX_OK;
}

static zx_status_t intel_serialio_i2c_ioctl(
    void* ctx, uint32_t op, const void* in_buf, size_t in_len,
    void* out_buf, size_t out_len, size_t* out_actual) {
    intel_serialio_i2c_device_t* device = ctx;
    switch (op) {
    case IOCTL_I2C_BUS_ADD_SLAVE: {
        const i2c_ioctl_add_slave_args_t* args = in_buf;
        if (in_len < sizeof(*args))
            return ZX_ERR_INVALID_ARGS;

        return intel_serialio_i2c_add_slave(device, args->chip_address_width,
                                            args->chip_address, ZX_PROTOCOL_I2C, NULL, 0);
    }
    case IOCTL_I2C_BUS_REMOVE_SLAVE: {
        const i2c_ioctl_remove_slave_args_t* args = in_buf;
        if (in_len < sizeof(*args))
            return ZX_ERR_INVALID_ARGS;

        return intel_serialio_i2c_remove_slave(device, args->chip_address_width,
                                              args->chip_address);
    }
    case IOCTL_I2C_BUS_SET_FREQUENCY: {
        const i2c_ioctl_set_bus_frequency_args_t* args = in_buf;
        if (in_len < sizeof(*args)) {
            return ZX_ERR_INVALID_ARGS;
        }
        return intel_serialio_i2c_set_bus_frequency(device, args->frequency);
    }
    default:
        return ZX_ERR_INVALID_ARGS;
    }
}

static int intel_serialio_i2c_irq_thread(void* arg) {
    intel_serialio_i2c_device_t* dev = (intel_serialio_i2c_device_t*)arg;
    zx_status_t status;
    for (;;) {
        status = zx_interrupt_wait(dev->irq_handle, NULL);
        if (status != ZX_OK) {
            zxlogf(ERROR, "i2c: error waiting for interrupt: %d\n", status);
            continue;
        }
        uint32_t intr_stat = readl(&dev->regs->intr_stat);
        zxlogf(SPEW, "Received i2c interrupt: %x %x\n",
               intr_stat, readl(&dev->regs->raw_intr_stat));
        if (intr_stat & (1u << INTR_RX_UNDER)) {
            // If we hit an underflow, it's a bug.
            zx_object_signal(dev->event_handle, 0, ERROR_DETECTED_SIGNAL);
            readl(&dev->regs->clr_rx_under);
            zxlogf(ERROR, "i2c: rx underflow detected!\n");
        }
        if (intr_stat & (1u << INTR_RX_OVER)) {
            // If we hit an overflow, it's a bug.
            zx_object_signal(dev->event_handle, 0, ERROR_DETECTED_SIGNAL);
            readl(&dev->regs->clr_rx_over);
            zxlogf(ERROR, "i2c: rx overflow detected!\n");
        }
        if (intr_stat & (1u << INTR_RX_FULL)) {
            mtx_lock(&dev->irq_mask_mutex);
            zx_object_signal(dev->event_handle, 0, RX_FULL_SIGNAL);
            RMWREG32(&dev->regs->intr_mask, INTR_RX_FULL, 1, 0);
            mtx_unlock(&dev->irq_mask_mutex);
        }
        if (intr_stat & (1u << INTR_TX_OVER)) {
            // If we hit an overflow, it's a bug.
            zx_object_signal(dev->event_handle, 0, ERROR_DETECTED_SIGNAL);
            readl(&dev->regs->clr_tx_over);
            zxlogf(ERROR, "i2c: tx overflow detected!\n");
        }
        if (intr_stat & (1u << INTR_TX_EMPTY)) {
            mtx_lock(&dev->irq_mask_mutex);
            zx_object_signal(dev->event_handle, 0, TX_EMPTY_SIGNAL);
            RMWREG32(&dev->regs->intr_mask, INTR_TX_EMPTY, 1, 0);
            mtx_unlock(&dev->irq_mask_mutex);
        }
        if (intr_stat & (1u << INTR_TX_ABORT)) {
            zxlogf(ERROR, "i2c: tx abort detected: 0x%08x\n",
                   readl(&dev->regs->tx_abrt_source));
            zx_object_signal(dev->event_handle, 0, ERROR_DETECTED_SIGNAL);
            readl(&dev->regs->clr_tx_abort);
        }
        if (intr_stat & (1u << INTR_ACTIVITY)) {
            // Should always be masked...remask it.
            mtx_lock(&dev->irq_mask_mutex);
            RMWREG32(&dev->regs->intr_mask, INTR_ACTIVITY, 1, 0);
            mtx_unlock(&dev->irq_mask_mutex);
            zxlogf(INFO, "i2c: spurious activity irq\n");
        }
        if (intr_stat & (1u << INTR_STOP_DETECTION)) {
            zx_object_signal(dev->event_handle, 0, STOP_DETECTED_SIGNAL);
            readl(&dev->regs->clr_stop_det);
        }
        if (intr_stat & (1u << INTR_START_DETECTION)) {
            readl(&dev->regs->clr_start_det);
        }
        if (intr_stat & (1u << INTR_GENERAL_CALL)) {
            // Should always be masked...remask it.
            mtx_lock(&dev->irq_mask_mutex);
            RMWREG32(&dev->regs->intr_mask, INTR_GENERAL_CALL, 1, 0);
            mtx_unlock(&dev->irq_mask_mutex);
            zxlogf(INFO, "i2c: spurious general call irq\n");
        }
    }
    return 0;
}

zx_status_t intel_serialio_i2c_wait_for_rx_full(
    intel_serialio_i2c_device_t* controller,
    zx_time_t deadline) {

    uint32_t observed;
    zx_status_t status = zx_object_wait_one(controller->event_handle,
                                            RX_FULL_SIGNAL | ERROR_DETECTED_SIGNAL,
                                            deadline, &observed);
    if (status != ZX_OK) {
        return status;
    }
    if (observed & ERROR_DETECTED_SIGNAL) {
        return ZX_ERR_IO;
    }
    return ZX_OK;
}

zx_status_t intel_serialio_i2c_wait_for_tx_empty(
    intel_serialio_i2c_device_t* controller,
    zx_time_t deadline) {

    uint32_t observed;
    zx_status_t status = zx_object_wait_one(controller->event_handle,
                                            TX_EMPTY_SIGNAL | ERROR_DETECTED_SIGNAL,
                                            deadline, &observed);
    if (status != ZX_OK) {
        return status;
    }
    if (observed & ERROR_DETECTED_SIGNAL) {
        return ZX_ERR_IO;
    }
    return ZX_OK;
}

zx_status_t intel_serialio_i2c_wait_for_stop_detect(
    intel_serialio_i2c_device_t* controller,
    zx_time_t deadline) {

    uint32_t observed;
    zx_status_t status = zx_object_wait_one(controller->event_handle,
                                            STOP_DETECTED_SIGNAL | ERROR_DETECTED_SIGNAL,
                                            deadline, &observed);
    if (status != ZX_OK) {
        return status;
    }
    if (observed & ERROR_DETECTED_SIGNAL) {
        return ZX_ERR_IO;
    }
    return ZX_OK;
}

zx_status_t intel_serialio_i2c_check_for_error(intel_serialio_i2c_device_t* controller) {

    uint32_t observed;
    zx_status_t status = zx_object_wait_one(controller->event_handle, ERROR_DETECTED_SIGNAL, 0,
                                            &observed);
    if (status != ZX_OK && status != ZX_ERR_TIMED_OUT) {
        return status;
    }
    if (observed & ERROR_DETECTED_SIGNAL) {
        return ZX_ERR_IO;
    }
    return ZX_OK;
}

zx_status_t intel_serialio_i2c_clear_stop_detect(intel_serialio_i2c_device_t* controller) {
    return zx_object_signal(controller->event_handle, STOP_DETECTED_SIGNAL, 0);
}

// Perform a write to the DATA_CMD register, and clear
// interrupt masks as appropriate
zx_status_t intel_serialio_i2c_issue_rx(
    intel_serialio_i2c_device_t* controller,
    uint32_t data_cmd) {

    writel(data_cmd, &controller->regs->data_cmd);
    return ZX_OK;
}

zx_status_t intel_serialio_i2c_read_rx(
    intel_serialio_i2c_device_t* controller,
    uint8_t* data) {

    *data = readl(&controller->regs->data_cmd);

    uint32_t rx_tl;
    intel_serialio_i2c_get_rx_fifo_threshold(controller, &rx_tl);
    const uint32_t rxflr = readl(&controller->regs->rxflr) & 0x1ff;
    // If we've dropped the RX queue level below the threshold, clear the signal
    // and unmask the interrupt.
    if (rxflr < rx_tl) {
        mtx_lock(&controller->irq_mask_mutex);
        zx_status_t status = zx_object_signal(controller->event_handle, RX_FULL_SIGNAL, 0);
        RMWREG32(&controller->regs->intr_mask, INTR_RX_FULL, 1, 1);
        mtx_unlock(&controller->irq_mask_mutex);
        return status;
    }
    return ZX_OK;
}

zx_status_t intel_serialio_i2c_issue_tx(
    intel_serialio_i2c_device_t* controller,
    uint32_t data_cmd) {

    writel(data_cmd, &controller->regs->data_cmd);
    uint32_t tx_tl;
    intel_serialio_i2c_get_tx_fifo_threshold(controller, &tx_tl);
    const uint32_t txflr = readl(&controller->regs->txflr) & 0x1ff;
    // If we've raised the TX queue level above the threshold, clear the signal
    // and unmask the interrupt.
    if (txflr > tx_tl) {
        mtx_lock(&controller->irq_mask_mutex);
        zx_status_t status = zx_object_signal(controller->event_handle, TX_EMPTY_SIGNAL, 0);
        RMWREG32(&controller->regs->intr_mask, INTR_TX_EMPTY, 1, 1);
        mtx_unlock(&controller->irq_mask_mutex);
        return status;
    }
    return ZX_OK;
}

void intel_serialio_i2c_get_rx_fifo_threshold(
    intel_serialio_i2c_device_t* controller,
    uint32_t* threshold) {

    *threshold = (readl(&controller->regs->rx_tl) & 0xff) + 1;
}

// Get an RX interrupt whenever the RX FIFO size is >= the threshold.
zx_status_t intel_serialio_i2c_set_rx_fifo_threshold(
    intel_serialio_i2c_device_t* controller,
    uint32_t threshold) {

    if (threshold - 1 > UINT8_MAX) {
        return ZX_ERR_INVALID_ARGS;
    }

    RMWREG32(&controller->regs->rx_tl, 0, 8, threshold - 1);
    return ZX_OK;
}

void intel_serialio_i2c_get_tx_fifo_threshold(
    intel_serialio_i2c_device_t* controller,
    uint32_t* threshold) {

    *threshold = (readl(&controller->regs->tx_tl) & 0xff) + 1;
}

// Get a TX interrupt whenever the TX FIFO size is <= the threshold.
zx_status_t intel_serialio_i2c_set_tx_fifo_threshold(
    intel_serialio_i2c_device_t* controller,
    uint32_t threshold) {

    if (threshold - 1 > UINT8_MAX) {
        return ZX_ERR_INVALID_ARGS;
    }

    RMWREG32(&controller->regs->tx_tl, 0, 8, threshold - 1);
    return ZX_OK;
}

static void intel_serialio_i2c_unbind(void* ctx) {
    intel_serialio_i2c_device_t* dev = ctx;
    if (dev) {
        zxlogf(INFO, "intel-i2c: unbind irq_handle %d irq_thread %p\n", dev->irq_handle,
                dev->irq_thread);
        if ((dev->irq_handle != ZX_HANDLE_INVALID) && dev->irq_thread) {
            zx_interrupt_destroy(dev->irq_handle);
            thrd_join(dev->irq_thread, NULL);
        }
        if (dev->zxdev) {
            device_remove(dev->zxdev);
        }
    }
}

static void intel_serialio_i2c_release(void* ctx) {
    intel_serialio_i2c_device_t* dev = ctx;
    if (dev) {
        zx_handle_close(dev->regs_handle);
        zx_handle_close(dev->irq_handle);
        zx_handle_close(dev->event_handle);
    }
    free(dev);
}

static zx_protocol_device_t intel_serialio_i2c_device_proto = {
    .version = DEVICE_OPS_VERSION,
    .ioctl = intel_serialio_i2c_ioctl,
    .unbind = intel_serialio_i2c_unbind,
    .release = intel_serialio_i2c_release,
};

// The controller lock should already be held when entering this function.
zx_status_t intel_serialio_i2c_reset_controller(
    intel_serialio_i2c_device_t* device) {
    zx_status_t status = ZX_OK;

    // The register will only return valid values if the ACPI _PS0 has been
    // evaluated.
    if (readl((void*)device->regs + DEVIDLE_CONTROL) != 0xffffffff) {
        // Wake up device if it is in DevIdle state
        RMWREG32((void*)device->regs + DEVIDLE_CONTROL, DEVIDLE_CONTROL_DEVIDLE, 1, 0);

        // Wait for wakeup to finish processing
        int retry = 10;
        while (retry-- &&
               (readl((void*)device->regs + DEVIDLE_CONTROL) &
                (1 << DEVIDLE_CONTROL_CMD_IN_PROGRESS))) {
            usleep(10);
        }
        if (!retry) {
            printf("i2c-controller: timed out waiting for device idle\n");
            return ZX_ERR_TIMED_OUT;
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
    if (device->bus_freq == I2C_MAX_FAST_PLUS_SPEED_HZ) {
        RMWREG32(&device->regs->fs_scl_hcnt, 0, 16, device->fmp_scl_hcnt);
        RMWREG32(&device->regs->fs_scl_lcnt, 0, 16, device->fmp_scl_lcnt);
    } else {
        RMWREG32(&device->regs->fs_scl_hcnt, 0, 16, device->fs_scl_hcnt);
        RMWREG32(&device->regs->fs_scl_lcnt, 0, 16, device->fs_scl_lcnt);
    }
    RMWREG32(&device->regs->ss_scl_hcnt, 0, 16, device->ss_scl_hcnt);
    RMWREG32(&device->regs->ss_scl_lcnt, 0, 16, device->ss_scl_lcnt);
    RMWREG32(&device->regs->sda_hold, 0, 16, device->sda_hold);

    unsigned int speed = CTL_SPEED_STANDARD;
    if (device->bus_freq == I2C_MAX_FAST_SPEED_HZ ||
        device->bus_freq == I2C_MAX_FAST_PLUS_SPEED_HZ) {

        speed = CTL_SPEED_FAST;
    }

    writel((0x1 << CTL_SLAVE_DISABLE) |
           (0x1 << CTL_RESTART_ENABLE) |
           (speed << CTL_SPEED) |
           (CTL_MASTER_MODE_ENABLED << CTL_MASTER_MODE),
           &device->regs->ctl);

    mtx_lock(&device->irq_mask_mutex);
    // Mask all interrupts
    writel(0, &device->regs->intr_mask);

    status = intel_serialio_i2c_set_rx_fifo_threshold(device, DEFAULT_RX_FIFO_TRIGGER_LEVEL);
    if (status != ZX_OK) {
        goto cleanup;
    }
    status = intel_serialio_i2c_set_tx_fifo_threshold(device, DEFAULT_TX_FIFO_TRIGGER_LEVEL);
    if (status != ZX_OK) {
        goto cleanup;
    }

    // Clear the signals
    status = zx_object_signal(device->event_handle,
                              RX_FULL_SIGNAL | TX_EMPTY_SIGNAL | STOP_DETECTED_SIGNAL |
                              ERROR_DETECTED_SIGNAL, 0);
    if (status != ZX_OK) {
        goto cleanup;
    }

    // Reading this register clears all interrupts.
    readl(&device->regs->clr_intr);

    // Unmask the interrupts we care about
    writel((1u<<INTR_STOP_DETECTION) | (1u<<INTR_TX_ABORT) |
           (1u<<INTR_TX_EMPTY) | (1u<<INTR_TX_OVER) | (1u<<INTR_RX_FULL) |
           (1u<<INTR_RX_OVER) | (1u<<INTR_RX_UNDER),
           &device->regs->intr_mask);

cleanup:
    mtx_unlock(&device->irq_mask_mutex);
    return status;
}

static zx_status_t intel_serialio_i2c_device_specific_init(
    intel_serialio_i2c_device_t* device,
    uint16_t device_id) {

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
                INTEL_SUNRISE_POINT_SERIALIO_I2C4_DID,
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
            return ZX_OK;
        }
    }

    return ZX_ERR_NOT_SUPPORTED;
}

static void intel_serialio_add_devices(intel_serialio_i2c_device_t* parent,
                                       pci_protocol_t* pci) {
    // get child info from aux data, max 4
    // TODO: this seems nonstandard to device model
    uint8_t childdata[sizeof(auxdata_i2c_device_t) * 4];
    memset(childdata, 0, sizeof(childdata));

    uint32_t actual;
    zx_status_t status = pci_get_auxdata(pci, "i2c-child", childdata, sizeof(childdata),
                             &actual);
    if (status != ZX_OK) {
        return;
    }

    auxdata_i2c_device_t* child = (auxdata_i2c_device_t*)childdata;
    uint32_t count = actual / sizeof(auxdata_i2c_device_t);
    uint32_t bus_speed = 0;
    while (count--) {
        zxlogf(TRACE, "i2c: got child[%u] bus_master=%d ten_bit=%d address=0x%x bus_speed=%u"
                     " protocol_id=0x%08x\n",
               count, child->bus_master, child->ten_bit, child->address, child->bus_speed,
               child->protocol_id);

        if (bus_speed && bus_speed != child->bus_speed) {
            zxlogf(ERROR, "i2c: cannot add devices with different bus speeds (%u, %u)\n",
                    bus_speed, child->bus_speed);
        }
        if (!bus_speed) {
            intel_serialio_i2c_set_bus_frequency(parent, child->bus_speed);
            bus_speed = child->bus_speed;
        }
        intel_serialio_i2c_add_slave(parent,
                child->ten_bit ? I2C_10BIT_ADDRESS : I2C_7BIT_ADDRESS,
                child->address, child->protocol_id, child->props, child->propcount);
        child += 1;
    }
}

zx_status_t intel_i2c_bind(void* ctx, zx_device_t* dev) {
    pci_protocol_t pci;
    if (device_get_protocol(dev, ZX_PROTOCOL_PCI, &pci))
        return ZX_ERR_NOT_SUPPORTED;

    intel_serialio_i2c_device_t* device = calloc(1, sizeof(*device));
    if (!device)
        return ZX_ERR_NO_MEMORY;

    list_initialize(&device->slave_list);
    mtx_init(&device->mutex, mtx_plain);
    mtx_init(&device->irq_mask_mutex, mtx_plain);
    device->pcidev = dev;

    uint16_t vendor_id;
    uint16_t device_id;
    pci_config_read16(&pci, PCI_CONFIG_VENDOR_ID, &vendor_id);
    pci_config_read16(&pci, PCI_CONFIG_DEVICE_ID, &device_id);

    zx_status_t status = pci_map_bar(&pci, 0u, ZX_CACHE_POLICY_UNCACHED_DEVICE,
                                   (void**)&device->regs, &device->regs_size, &device->regs_handle);
    if (status != ZX_OK) {
        zxlogf(ERROR,"i2c: failed to mape pci bar 0: %d\n", status);
        goto fail;
    }

    // set msi irq mode
    status = pci_set_irq_mode(&pci, ZX_PCIE_IRQ_MODE_LEGACY, 1);
    if (status < 0) {
        zxlogf(ERROR,"i2c: failed to set irq mode: %d\n", status);
        goto fail;
    }

    // get irq handle
    status = pci_map_interrupt(&pci, 0, &device->irq_handle);
    if (status != ZX_OK) {
        zxlogf(ERROR,"i2c: failed to get irq handle: %d\n", status);
        goto fail;
    }

    status = zx_event_create(0, &device->event_handle);
    if (status != ZX_OK) {
        zxlogf(ERROR,"i2c: failed to create event handle: %d\n", status);
        goto fail;
    }

    // start irq thread
    int ret = thrd_create_with_name(&device->irq_thread, intel_serialio_i2c_irq_thread, device, "i2c-irq");
    if (ret != thrd_success) {
        zxlogf(ERROR,"i2c: failed to create irq thread: %d\n", ret);
        goto fail;
    }

    // Run the bus at standard speed by default.
    device->bus_freq = I2C_MAX_STANDARD_SPEED_HZ;

    status = intel_serialio_i2c_device_specific_init(device, device_id);
    if (status != ZX_OK) {
        zxlogf(ERROR, "i2c: device specific init failed: %d\n", status);
        goto fail;
    }

    status = intel_serialio_compute_bus_timing(device);
    if (status < 0) {
        zxlogf(ERROR, "i2c: compute bus timing failed: %d\n", status);
        goto fail;
    }

    // Temporary hack until we have routed through the FMCN ACPI tables.
    if (vendor_id == INTEL_VID &&
        device_id == INTEL_SUNRISE_POINT_SERIALIO_I2C0_DID) {
        // TODO: These should all be extracted from FPCN in the ACPI tables.
        device->fmp_scl_lcnt = 0x0042;
        device->fmp_scl_hcnt = 0x001b;
        device->sda_hold = 0x24;
    } else if (vendor_id == INTEL_VID &&
        device_id == INTEL_SUNRISE_POINT_SERIALIO_I2C1_DID) {
        // TODO(yky): These should all be extracted from FMCN in the ACPI tables.
        device->fs_scl_lcnt = 0x00b6;
        device->fs_scl_hcnt = 0x0059;
        device->sda_hold = 0x24;
    } else if (vendor_id == INTEL_VID &&
               device_id == INTEL_SUNRISE_POINT_SERIALIO_I2C2_DID) {
        // TODO: These should all be extracted from FMCN in the ACPI tables.
        device->fs_scl_lcnt = 0x00ba;
        device->fs_scl_hcnt = 0x005d;
        device->sda_hold = 0x24;
    } else if (vendor_id == INTEL_VID &&
               device_id == INTEL_SUNRISE_POINT_SERIALIO_I2C4_DID) {
        // TODO: These should all be extracted from FMCN in the ACPI tables.
        device->fs_scl_lcnt = 0x005a;
        device->fs_scl_hcnt = 0x00a6;
        device->sda_hold = 0x24;
    }

    // Configure the I2C controller. We don't need to hold the lock because
    // nobody else can see this controller yet.
    status = intel_serialio_i2c_reset_controller(device);
    if (status < 0) {
        zxlogf(ERROR, "i2c: reset controller failed: %d\n", status);
        goto fail;
    }

    char name[ZX_DEVICE_NAME_MAX];
    snprintf(name, sizeof(name), "i2c-bus-%04x", device_id);

   device_add_args_t args = {
        .version = DEVICE_ADD_ARGS_VERSION,
        .name = name,
        .ctx = device,
        .ops = &intel_serialio_i2c_device_proto,
    };

    status = device_add(dev, &args, &device->zxdev);
    if (status < 0) {
        zxlogf(ERROR, "device add failed: %d\n", status);
        goto fail;
    }

    zxlogf(INFO,
        "initialized intel serialio i2c driver, "
        "reg=%p regsize=%ld\n",
        device->regs, device->regs_size);

    intel_serialio_add_devices(device, &pci);
    return ZX_OK;

fail:
    intel_serialio_i2c_unbind(device);
    intel_serialio_i2c_release(device);
    return status;
}

static zx_driver_ops_t intel_i2c_driver_ops = {
    .version = DRIVER_OPS_VERSION,
    .bind = intel_i2c_bind,
};

ZIRCON_DRIVER_BEGIN(intel_i2c, intel_i2c_driver_ops, "zircon", "0.1", 9)
    BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_PCI),
    BI_ABORT_IF(NE, BIND_PCI_VID, 0x8086),
    BI_MATCH_IF(EQ, BIND_PCI_DID, INTEL_WILDCAT_POINT_SERIALIO_I2C0_DID),
    BI_MATCH_IF(EQ, BIND_PCI_DID, INTEL_WILDCAT_POINT_SERIALIO_I2C1_DID),
    BI_MATCH_IF(EQ, BIND_PCI_DID, INTEL_SUNRISE_POINT_SERIALIO_I2C0_DID),
    BI_MATCH_IF(EQ, BIND_PCI_DID, INTEL_SUNRISE_POINT_SERIALIO_I2C1_DID),
    BI_MATCH_IF(EQ, BIND_PCI_DID, INTEL_SUNRISE_POINT_SERIALIO_I2C2_DID),
    BI_MATCH_IF(EQ, BIND_PCI_DID, INTEL_SUNRISE_POINT_SERIALIO_I2C3_DID),
    BI_MATCH_IF(EQ, BIND_PCI_DID, INTEL_SUNRISE_POINT_SERIALIO_I2C4_DID),
ZIRCON_DRIVER_END(intel_i2c)
