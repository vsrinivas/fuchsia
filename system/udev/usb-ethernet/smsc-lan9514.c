// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/binding.h>
#include <ddk/common/usb.h>
#include <ddk/completion.h>
#include <ddk/device.h>
#include <ddk/driver.h>
#include <ddk/protocol/ethernet.h>
#include <magenta/listnode.h>

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <threads.h>
#include <unistd.h>

#include "smsc-lan9514.h"

#define ETH_HEADER_SIZE 4
#define ETH_RX_HEADER_SIZE 4

#define READ_REQ_COUNT 8
#define WRITE_REQ_COUNT 4
#define INTR_REQ_COUNT 4
#define USB_BUF_SIZE 2048
#define INTR_REQ_SIZE 4
//#define ETH_HEADER_SIZE 4

typedef struct {
    mx_device_t* device;
    mx_device_t* usb_device;
    mx_driver_t* driver;

    uint8_t phy_id;
    uint8_t mac_addr[6];
    uint8_t status[INTR_REQ_SIZE];
    bool online;
    bool dead;

    // pool of free USB requests
    list_node_t free_read_reqs;
    list_node_t free_write_reqs;
    list_node_t free_intr_reqs;

    // list of received packets not yet read by upper layer
    list_node_t completed_reads;
    // offset of next packet to process from completed_reads head
    size_t read_offset;

    // the last signals we reported
    mx_signals_t signals;

    completion_t phy_state_completion;

    mtx_t mutex;
} lan9514_t;

#define get_lan9514(dev) ((lan9514_t*)dev->ctx)

static mx_status_t lan9514_write_register(lan9514_t* eth, uint16_t reg, uint32_t value) {

    mx_status_t status = usb_control(eth->usb_device, USB_DIR_OUT | USB_TYPE_VENDOR | USB_RECIP_DEVICE,
                                     LAN9514_REQ_REG_WRITE, 0, reg, &value, sizeof(value));

    return status;
}

static mx_status_t lan9514_read_register(lan9514_t* eth, uint16_t reg, uint32_t* value) {

    mx_status_t status = usb_control(eth->usb_device, USB_DIR_IN | USB_TYPE_VENDOR | USB_RECIP_DEVICE,
                                     LAN9514_REQ_REG_READ, 0, reg, value, sizeof(*value));

    return status;
}

static mx_status_t lan9514_mdio_wait_not_busy(lan9514_t* eth) {
    uint32_t retval;
    mx_time_t timecheck = mx_time_get(MX_CLOCK_MONOTONIC);
    do {
        mx_status_t status = lan9514_read_register(eth, LAN9514_MII_ACCESS_REG, &retval);
        if (status < 0)
            return status;
        if ((mx_time_get(MX_CLOCK_MONOTONIC) - timecheck) > MX_SEC(1))
            return ERR_TIMED_OUT;
    } while (retval & LAN9514_MII_ACCESS_MIIBZY);
    return NO_ERROR;
}

static mx_status_t lan9514_mdio_read(lan9514_t* eth, uint8_t idx, uint16_t* retval) {
    mx_status_t status;
    uint32_t value;

    mtx_lock(&eth->mutex);

    status = lan9514_mdio_wait_not_busy(eth);
    if (status < 0)
        goto done;

    value = (LAN9514_PHY_ID << 11) | (idx << 6) | LAN9514_MII_ACCESS_MIIBZY;
    status = lan9514_write_register(eth, LAN9514_MII_ACCESS_REG, value);
    if (status < 0)
        goto done;

    status = lan9514_mdio_wait_not_busy(eth);
    if (status < 0)
        goto done;

    status = lan9514_read_register(eth, LAN9514_MII_DATA_REG, &value);
    *retval = (uint16_t)(value & 0xffff);

done:
    mtx_unlock(&eth->mutex);
    return status;
}

static mx_status_t lan9514_mdio_write(lan9514_t* eth, uint8_t idx, uint16_t value) {
    mx_status_t status;
    uint32_t writeval;

    mtx_lock(&eth->mutex);

    status = lan9514_mdio_wait_not_busy(eth);
    if (status < 0)
        goto done;

    status = lan9514_write_register(eth, LAN9514_MII_DATA_REG, (uint32_t)value);
    if (status < 0)
        goto done;

    writeval = (LAN9514_PHY_ID << 11) | (idx << 6) | LAN9514_MII_ACCESS_MIIBZY | LAN9514_MII_ACCESS_MIIWnR;
    status = lan9514_write_register(eth, LAN9514_MII_ACCESS_REG, writeval);
    if (status < 0)
        goto done;

    status = lan9514_mdio_wait_not_busy(eth);

done:
    mtx_unlock(&eth->mutex);
    return status;
}

mx_status_t lan9514_nway_restart(lan9514_t* eth) {
    mx_status_t status;
    uint16_t value;

    status = lan9514_mdio_read(eth, MII_PHY_BMCR_REG, &value);
    if (status < 0)
        return status;

    if (value & MII_PHY_BMCR_ANENABLE) {
        value |= MII_PHY_BMCR_ANRESTART;
        return lan9514_mdio_write(eth, MII_PHY_BMCR_REG, value);
    }
    return -1;
}

mx_status_t lan9514_multicast_init(lan9514_t* eth) {
    mx_status_t status;
    uint32_t value;

    status = lan9514_read_register(eth, LAN9514_MAC_CR_REG, &value);
    if (status < 0)
        return status;

    value |= LAN9514_MAC_CR_MCPAS | LAN9514_MAC_CR_RXALL;
    value &= ~(LAN9514_MAC_CR_HPFILT | LAN9514_MAC_CR_PRMS);

    status = lan9514_write_register(eth, LAN9514_MAC_CR_REG, value);
    if (status < 0)
        return status;

    return status;
}

mx_status_t lan9514_phy_init(lan9514_t* eth) {
    mx_status_t status;
    uint16_t value;

    status = lan9514_mdio_write(eth, MII_PHY_BMCR_REG, MII_PHY_BMCR_RESET);
    if (status < 0)
        goto done;

    do {
        status = lan9514_mdio_read(eth, MII_PHY_BMCR_REG, &value);
        if (status < 0)
            goto done;
    } while (value & MII_PHY_BMCR_RESET);

    status = lan9514_mdio_write(eth, MII_PHY_ADVERTISE_REG, MII_PHY_ADVERTISE_ALL |
                                                                MII_PHY_ADVERTISE_CSMA |
                                                                MII_PHY_ADVERTISE_PAUSE_CAP |
                                                                MII_PHY_ADVERTISE_PAUSE_ASYM);
    if (status < 0)
        goto done;

    // Iread interrupt source register to clear
    status = lan9514_mdio_read(eth, MII_PHY_LAN9514_INT_SRC_REG, &value);
    if (status < 0)
        goto done;

    status = lan9514_mdio_write(eth, MII_PHY_LAN9514_INT_MASK_REG, MII_PHY_LAN9514_INT_MASK_DEFAULT);
    if (status < 0)
        goto done;

    status = lan9514_nway_restart(eth);
    if (status < 0)
        goto done;

    uint32_t retval;
    status = lan9514_read_register(eth, LAN9514_INT_EP_CTL_REG, &retval);
    if (status < 0)
        goto done;

    retval |= LAN9514_INT_EP_CTL_PHY_INT;
    status = lan9514_write_register(eth, LAN9514_INT_EP_CTL_REG, retval);

done:
    return status;
}

mx_status_t lan9514_read_mac_address(lan9514_t* eth) {
    uint32_t holder_lo, holder_hi;
    mx_status_t status;

    status = lan9514_read_register(eth, LAN9514_ADDR_HI_REG, &holder_hi);
    if (status < 0)
        return status;

    status = lan9514_read_register(eth, LAN9514_ADDR_LO_REG, &holder_lo);
    if (status < 0)
        return status;

    eth->mac_addr[5] = (holder_hi >> 8) & 0xff;
    eth->mac_addr[4] = holder_hi & 0xff;
    eth->mac_addr[3] = (holder_lo >> 24) & 0xff;
    eth->mac_addr[2] = (holder_lo >> 16) & 0xff;
    eth->mac_addr[1] = (holder_lo >> 8) & 0xff;
    eth->mac_addr[0] = (holder_lo >> 0) & 0xff;
    return NO_ERROR;
}

static void update_signals_locked(lan9514_t* eth) {
    mx_signals_t new_signals = 0;

    if (eth->dead)
        new_signals |= (DEV_STATE_READABLE | DEV_STATE_ERROR);
    if (!list_is_empty(&eth->completed_reads))
        new_signals |= DEV_STATE_READABLE;
    if (!list_is_empty(&eth->free_write_reqs) && eth->online)
        new_signals |= DEV_STATE_WRITABLE;
    if (new_signals != eth->signals) {
        device_state_set_clr(eth->device, new_signals & ~eth->signals, eth->signals & ~new_signals);
        eth->signals = new_signals;
    }
}

static void requeue_read_request_locked(lan9514_t* eth, iotxn_t* req) {
    if (eth->online) {
        iotxn_queue(eth->usb_device, req);
    }
}

static void queue_interrupt_requests_locked(lan9514_t* eth) {
    list_node_t* node;
    while ((node = list_remove_head(&eth->free_intr_reqs)) != NULL) {
        iotxn_t* req = containerof(node, iotxn_t, node);
        iotxn_queue(eth->usb_device, req);
    }
}

static void lan9514_read_complete(iotxn_t* request, void* cookie) {
    lan9514_t* eth = (lan9514_t*)cookie;
    //printf("lan9514 read complete\n");

    if (request->status == ERR_REMOTE_CLOSED) {
        request->ops->release(request);
        return;
    }

    mtx_lock(&eth->mutex);
    if (request->status == NO_ERROR) {
        list_add_tail(&eth->completed_reads, &request->node);
    } else {
        requeue_read_request_locked(eth, request);
    }
    update_signals_locked(eth);
    mtx_unlock(&eth->mutex);
}

static void lan9514_write_complete(iotxn_t* request, void* cookie) {
    lan9514_t* eth = (lan9514_t*)cookie;
    if (request->status == ERR_REMOTE_CLOSED) {
        request->ops->release(request);
        return;
    }

    mtx_lock(&eth->mutex);
    list_add_tail(&eth->free_write_reqs, &request->node);
    update_signals_locked(eth);
    mtx_unlock(&eth->mutex);
}

static void lan9514_interrupt_complete(iotxn_t* request, void* cookie) {
    lan9514_t* eth = (lan9514_t*)cookie;
    if ((request->status == ERR_REMOTE_CLOSED) || (request->status == ERR_IO)) { // ERR_IO = NACK (no status change)
        request->ops->release(request);
        return;
    }

    mtx_lock(&eth->mutex);
    if (request->status == NO_ERROR && request->actual == sizeof(eth->status)) {
        uint8_t status[INTR_REQ_SIZE];
        request->ops->copyfrom(request, status, sizeof(status), 0);
        memcpy(eth->status, status, sizeof(eth->status));
        completion_signal(&eth->phy_state_completion);
    }

    list_add_head(&eth->free_intr_reqs, &request->node);
    queue_interrupt_requests_locked(eth);

    mtx_unlock(&eth->mutex);
}

mx_status_t lan9514_recv(mx_device_t* device, void* buffer, size_t length) {
    lan9514_t* eth = get_lan9514(device);
    if (eth->dead) {
        printf("lan9514_recv dead\n");
        return ERR_REMOTE_CLOSED;
    }

    mx_status_t status = NO_ERROR;

    mtx_lock(&eth->mutex);

    list_node_t* node = list_peek_head(&eth->completed_reads);
    if (!node) {
        status = ERR_BAD_STATE;
        goto out;
    }
    iotxn_t* request = containerof(node, iotxn_t, node);

    uint32_t rx_status;

    request->ops->copyfrom(request, &rx_status, sizeof(rx_status), 0);

    uint32_t frame_len = (rx_status & LAN9514_RXSTATUS_FRAME_LEN) >> 16;

    if (rx_status & LAN9514_RXSTATUS_ERROR_MASK) {
        printf("invalid header: 0x%08x\n", rx_status);
        status = ERR_INTERNAL;
        list_remove_head(&eth->completed_reads);
        requeue_read_request_locked(eth, request);
        goto out;
    }
    if (frame_len > length) {
        status = ERR_BUFFER_TOO_SMALL;
        printf("LAN9514 recv - buffer too small\n)");
        goto out;
    }

    request->ops->copyfrom(request, buffer, frame_len, sizeof(rx_status));
    status = frame_len;

    list_remove_head(&eth->completed_reads);
out:
    eth->read_offset = 0;
    update_signals_locked(eth);
    mtx_unlock(&eth->mutex);
    return status;
}

mx_status_t lan9514_send(mx_device_t* device, const void* buffer, size_t length) {
    lan9514_t* eth = get_lan9514(device);

    if (eth->dead) {
        return ERR_REMOTE_CLOSED;
    }

    mx_status_t status = NO_ERROR;

    mtx_lock(&eth->mutex);

    list_node_t* node = list_remove_head(&eth->free_write_reqs);
    if (!node) {
        status = ERR_BUFFER_TOO_SMALL;
        goto out;
    }
    iotxn_t* request = containerof(node, iotxn_t, node);

    if (length + ETH_HEADER_SIZE > USB_BUF_SIZE) {
        status = ERR_INVALID_ARGS;
        goto out;
    }

    uint8_t header[8];
    uint32_t command_a = (1 << 13) | (1 << 12) | (length);
    uint32_t command_b = (0 << 14) | (length);

    header[0] = command_a & 0xff;
    header[1] = (command_a >> 8) & 0xff;
    header[2] = (command_a >> 16) & 0xff;
    header[3] = (command_a >> 24) & 0xff;
    header[4] = command_b & 0xff;
    header[5] = (command_b >> 8) & 0xff;
    header[6] = (command_b >> 16) & 0xff;
    header[7] = (command_b >> 24) & 0xff;

    request->ops->copyto(request, header, 8, 0);
    request->ops->copyto(request, buffer, length, 8);
    request->length = length + 8;
    iotxn_queue(eth->usb_device, request);

out:
    update_signals_locked(eth);
    mtx_unlock(&eth->mutex);
    return status;
}

static mx_status_t lan9514_stop_xcvr(lan9514_t* eth) {

    mx_status_t status;
    uint32_t value;

    status = lan9514_read_register(eth, LAN9514_MAC_CR_REG, &value);
    if (status < 0)
        return status;

    value &= ~(LAN9514_MAC_CR_TXEN | LAN9514_MAC_CR_RXEN);

    return lan9514_write_register(eth, LAN9514_MAC_CR_REG, value);
}

static mx_status_t lan9514_start_xcvr(lan9514_t* eth) {

    mx_status_t status;
    uint32_t value;

    status = lan9514_read_register(eth, LAN9514_MAC_CR_REG, &value);
    if (status < 0)
        return status;

    value |= LAN9514_MAC_CR_TXEN;

    status = lan9514_write_register(eth, LAN9514_MAC_CR_REG, value);
    if (status < 0)
        return status;

    status = lan9514_write_register(eth, LAN9514_TX_CFG_REG, LAN9514_TX_CFG_ON);
    if (status < 0)
        return status;

    value |= LAN9514_MAC_CR_RXEN;

    return lan9514_write_register(eth, LAN9514_MAC_CR_REG, value);
}

static ssize_t lan9514_read(mx_device_t* dev, void* data, size_t len, mx_off_t off) {
    // special case reading MAC address
    uint8_t* buff = data;
    if (len == ETH_MAC_SIZE) {
        lan9514_t* eth = get_lan9514(dev);
        for (int i = 0; i < 6; i++)
            buff[i] = eth->mac_addr[i];
        return len;
    }
    if (len < (USB_BUF_SIZE - ETH_HEADER_SIZE)) {
        return ERR_BUFFER_TOO_SMALL;
    }
    return lan9514_recv(dev, data, len);
}

static ssize_t lan9514_write(mx_device_t* dev, const void* data, size_t len, mx_off_t off) {
    return lan9514_send(dev, data, len);
}

mx_status_t lan9514_get_mac_addr(mx_device_t* device, uint8_t* out_addr) {
    lan9514_t* eth = get_lan9514(device);
    printf("someone called get mac\n");
    for (int i = 0; i < 6; i++)
        out_addr[i] = eth->mac_addr[i];
    return NO_ERROR;
}

bool lan9514_is_online(mx_device_t* device) {
    printf("someone called is online\n");

    lan9514_t* eth = get_lan9514(device);
    return eth->online;
}

size_t lan9514_get_mtu(mx_device_t* device) {
    return USB_BUF_SIZE - ETH_HEADER_SIZE;
}

static void lan9514_free(lan9514_t* eth) {
    iotxn_t* txn;

    mtx_lock(&eth->mutex);
    while ((txn = list_remove_head_type(&eth->free_read_reqs, iotxn_t, node)) != NULL) {
        txn->ops->release(txn);
    }
    while ((txn = list_remove_head_type(&eth->free_write_reqs, iotxn_t, node)) != NULL) {
        txn->ops->release(txn);
    }
    while ((txn = list_remove_head_type(&eth->free_intr_reqs, iotxn_t, node)) != NULL) {
        txn->ops->release(txn);
    }
    mtx_unlock(&eth->mutex);

    free(eth->device);
    free(eth);
}

static mx_status_t lan9514_release(mx_device_t* device) {
    lan9514_t* eth = get_lan9514(device);
    lan9514_free(eth);
    return NO_ERROR;
}

static void lan9514_unbind(mx_device_t* device) {
    lan9514_t* eth = get_lan9514(device);

    mtx_lock(&eth->mutex);
    eth->dead = true;
    update_signals_locked(eth);
    mtx_unlock(&eth->mutex);

    // this must be last since this can trigger releasing the device
    device_remove(eth->device);
}

static ethernet_protocol_t lan9514_proto = {
    .send = lan9514_send,
    .recv = lan9514_recv,
    .get_mac_addr = lan9514_get_mac_addr,
    .is_online = lan9514_is_online,
    .get_mtu = lan9514_get_mtu,
};

static mx_protocol_device_t lan9514_device_proto = {
    .unbind = lan9514_unbind,
    .release = lan9514_release,
    .read = lan9514_read,
    .write = lan9514_write,
};

static mx_status_t lan9514_reset(lan9514_t* eth) {
    mx_status_t status = 0;
    uint32_t retval = 0;

    status = lan9514_write_register(eth, LAN9514_HW_CFG_REG, LAN9514_HW_CFG_LRST);
    if (status < 0)
        goto fail;
    do {
        if (lan9514_read_register(eth, LAN9514_HW_CFG_REG, &retval) < 0)
            goto fail;
    } while (retval & LAN9514_HW_CFG_LRST);
    printf("LAN9514 Lite HW reset complete...\n");

    status = lan9514_write_register(eth, LAN9514_PM_CTRL_REG, LAN9514_PM_CTRL_PHY_RST);
    if (status < 0)
        goto fail;
    do {
        if (lan9514_read_register(eth, LAN9514_PM_CTRL_REG, &retval) < 0)
            goto fail;
    } while (retval & LAN9514_PM_CTRL_PHY_RST);
    printf("LAN9514 PHY reset complete...\n");

    if (lan9514_read_mac_address(eth) < 0)
        goto fail;
    printf("Current MAC Address %02x:%02x:%02x:%02x:%02x:%02x\n", eth->mac_addr[5], eth->mac_addr[4],
           eth->mac_addr[3], eth->mac_addr[2],
           eth->mac_addr[1], eth->mac_addr[0]);

    if (lan9514_write_register(eth, LAN9514_ADDR_HI_REG, 0x00004a1c) < 0)
        goto fail;
    if (lan9514_write_register(eth, LAN9514_ADDR_LO_REG, 0x17b65000) < 0)
        goto fail;

    if (lan9514_read_mac_address(eth) < 0)
        goto fail;
    printf("Updated MAC Address %02x:%02x:%02x:%02x:%02x:%02x\n", eth->mac_addr[5], eth->mac_addr[4],
           eth->mac_addr[3], eth->mac_addr[2],
           eth->mac_addr[1], eth->mac_addr[0]);

    // Set Bulk IN empty response to 1=NAK   (0=ZLP)
    if (lan9514_read_register(eth, LAN9514_HW_CFG_REG, &retval) < 0)
        goto fail;
    printf("LAN9514 HW_CFG register = 0x%08x\n", retval);
    retval |= LAN9514_HW_CFG_BIR;
    if (lan9514_write_register(eth, LAN9514_HW_CFG_REG, retval) < 0)
        goto fail;
    if (lan9514_read_register(eth, LAN9514_HW_CFG_REG, &retval) < 0)
        goto fail;
    printf("updated LAN9514 HW_CFG register = 0x%08x\n", retval);

    if (lan9514_write_register(eth, LAN9514_BULK_IN_DLY_REG, LAN9514_BULK_IN_DLY_DEFAULT) < 0)
        goto fail;
    if (lan9514_read_register(eth, LAN9514_BULK_IN_DLY_REG, &retval) < 0)
        goto fail;
    printf("LAN9514 Bulk In Delay set to %d\n", retval);

    if (lan9514_read_register(eth, LAN9514_HW_CFG_REG, &retval) < 0)
        goto fail;
    retval &= ~LAN9514_HW_CFG_RXDOFF;
    if (lan9514_write_register(eth, LAN9514_HW_CFG_REG, retval) < 0)
        goto fail;
    if (lan9514_read_register(eth, LAN9514_HW_CFG_REG, &retval) < 0)
        goto fail;
    printf("updated LAN9514 HW_CFG register = 0x%08x\n", retval);

    if (lan9514_write_register(eth, LAN9514_INT_STS_REG, LAN9514_INT_STS_REG_CLEAR_ALL) < 0)
        goto fail;
    printf("LAN9514 Cleared all pending interrupts\n");

    if (lan9514_read_register(eth, LAN9514_ID_REV_REG, &retval) < 0)
        goto fail;
    printf("LAN9514 id/revision register: 0x%08x\n", retval);

    retval = LAN9514_LED_GPIO_CFG_SPD_LED | LAN9514_LED_GPIO_CFG_LNK_LED | LAN9514_LED_GPIO_CFG_FDX_LED;
    if (lan9514_write_register(eth, LAN9514_LED_GPIO_CFG_REG, retval) < 0)
        goto fail;
    printf("LAN9514 LED Configuration = 0x%08x\n", retval);

    if (lan9514_write_register(eth, LAN9514_AFC_CFG_REG, LAN9514_AFC_CFG_DEFAULT) < 0)
        goto fail;

    if (lan9514_read_register(eth, LAN9514_COE_CR_REG, &retval) < 0)
        goto fail;
    retval |= LAN9514_COE_CR_TX_COE_EN | LAN9514_COE_CR_RX_COE_EN;
    if (lan9514_write_register(eth, LAN9514_COE_CR_REG, retval) < 0)
        goto fail;

    if (lan9514_multicast_init(eth) < 0)
        goto fail;

    if (lan9514_phy_init(eth) < 0)
        goto fail;

    //start tx path
    //start rx path
    if (lan9514_start_xcvr(eth) < 0)
        goto fail;

    uint16_t inval;
    lan9514_mdio_read(eth, MII_PHY_BSR_REG, &inval);
    uint16_t bcr;
    lan9514_mdio_read(eth, MII_PHY_BMCR_REG, &bcr);

    printf("LAN9514 Initialized! bmcr=%04x  bsr=%04x\n", bcr, inval);
    return NO_ERROR;

fail:
    lan9514_free(eth);
    printf("LAN9514 Initialization failed.  Exiting with status %d\n", status);
    return status;
}

static int lan9514_start_thread(void* arg) {
    lan9514_t* eth = (lan9514_t*)arg;
    mx_status_t status = 0;
    printf("Initializing LAN9514...\n");

    lan9514_reset(eth);

    status = device_create(&eth->device, eth->driver, "usb-ethernet", &lan9514_device_proto);
    if (status < 0) {
        printf("lan9514: failed to create device: %d\n", status);
        goto fail;
    }

    mtx_lock(&eth->mutex);
    queue_interrupt_requests_locked(eth);
    mtx_unlock(&eth->mutex);

    eth->device->ctx = eth;
    eth->device->protocol_id = MX_PROTOCOL_ETHERNET;
    eth->device->protocol_ops = &lan9514_proto;
    status = device_add(eth->device, eth->usb_device);

    while (true) {
        uint16_t temp;
        status = completion_wait(&eth->phy_state_completion, MX_MSEC(500)); //MX_TIME_INFINITE);
        if (status == ERR_TIMED_OUT) {
            // do background maintenance and statistics work here
        } else {
            // do interrupt servicing here

            // Reading the source register allows interrupt state to clear
            lan9514_mdio_read(eth, MII_PHY_LAN9514_INT_SRC_REG, &temp);

            if (eth->online) {                                  // we were online... what hapened?
                if (temp & MII_PHY_LAN9514_INT_SRC_LINK_DOWN) { // Link went down
                    eth->online = false;
                    /*
                        TODO - for power managment, may want to enter suspend1 state here
                                and configure for wake on phy (energy detect)
                    */
                    printf("lan9514: Link is down - %04x\n", temp);
                    status = lan9514_mdio_write(eth, MII_PHY_LAN9514_INT_MASK_REG, MII_PHY_LAN9514_INT_MASK_ANEG_COMP);
                    if (status < 0)
                        goto teardown;
                }
            } else {
                lan9514_mdio_read(eth, MII_PHY_BSR_REG, &temp);
                // Wait for link up, catches condition where there may be delay between aneg and link status change
                mx_time_t timecheck = mx_time_get(MX_CLOCK_MONOTONIC);
                while (!(temp & MII_PHY_BSR_LINK_UP)) {
                    lan9514_mdio_read(eth, MII_PHY_BSR_REG, &temp);
                    mx_nanosleep(MX_MSEC(100));
                    if ((mx_time_get(MX_CLOCK_MONOTONIC - timecheck) > MX_SEC(1))) {
                        status = ERR_TIMED_OUT;
                        goto teardown;
                    }
                }
                status = lan9514_mdio_write(eth, MII_PHY_LAN9514_INT_MASK_REG, MII_PHY_LAN9514_INT_MASK_LINK_DOWN);
                if (status < 0)

                    goto teardown;
                mtx_lock(&eth->mutex);
                eth->online = true;
                printf("lan9514: Link is up - %04x\n", temp);
                iotxn_t* req;
                iotxn_t* prev;
                list_for_every_entry_safe (&eth->free_read_reqs, req, prev, iotxn_t, node) {
                    list_delete(&req->node);
                    requeue_read_request_locked(eth, req);
                }
                update_signals_locked(eth);
                mtx_unlock(&eth->mutex);

            }
            completion_reset(&eth->phy_state_completion);
        }
    }
teardown:
    lan9514_unbind(eth->device);
fail:
    printf("LAN9514: driver failing with status=%d\n", status);
    return status;
}

static mx_status_t lan9514_bind(mx_driver_t* driver, mx_device_t* device) {
    printf("LAN9514 - attempting to bind\n");

    // find our endpoints
    usb_desc_iter_t iter;
    mx_status_t result = usb_desc_iter_init(device, &iter);
    if (result < 0)
        return result;

    usb_descriptor_header_t* header = usb_desc_iter_next(&iter);
    int index = 0;
    while (header) {
        printf("%d: %x\n", index++, header->bDescriptorType);
        header = usb_desc_iter_next(&iter);
    }

    usb_desc_iter_reset(&iter);

    char* outstr;

    for (index = 0; index < 6; index++) {
        usb_get_string_descriptor(device, index, &outstr);
        printf("%d : %s\n", index, outstr);
    }
    usb_desc_iter_reset(&iter);

    usb_interface_descriptor_t* intf = usb_desc_iter_next_interface(&iter, true);
    printf("lan9514 returned %d endpoints\n", intf->bNumEndpoints);
    if (!intf || intf->bNumEndpoints != 3) {
        usb_desc_iter_release(&iter);
        return ERR_NOT_SUPPORTED;
    }

    uint8_t bulk_in_addr = 0;
    uint8_t bulk_out_addr = 0;
    uint8_t intr_addr = 0;

    usb_endpoint_descriptor_t* endp = usb_desc_iter_next_endpoint(&iter);
    while (endp) {
        if (usb_ep_direction(endp) == USB_ENDPOINT_OUT) {
            if (usb_ep_type(endp) == USB_ENDPOINT_BULK) {
                bulk_out_addr = endp->bEndpointAddress;
                printf("lan9514 bulk out endpoint:%x\n", bulk_out_addr);
            }
        } else {
            if (usb_ep_type(endp) == USB_ENDPOINT_BULK) {
                bulk_in_addr = endp->bEndpointAddress;
                printf("lan9514 bulk in endpoint:%x\n", bulk_in_addr);
            } else if (usb_ep_type(endp) == USB_ENDPOINT_INTERRUPT) {
                intr_addr = endp->bEndpointAddress;
                printf("lan9514 interrupt endpoint:%x\n", intr_addr);
            }
        }
        endp = usb_desc_iter_next_endpoint(&iter);
    }
    usb_desc_iter_release(&iter);

    if (!bulk_in_addr || !bulk_out_addr || !intr_addr) {
        printf("lan9514_bind could not find endpoints\n");
        return ERR_NOT_SUPPORTED;
    }

    lan9514_t* eth = calloc(1, sizeof(lan9514_t));
    if (!eth) {
        printf("Not enough memory for lan9514_t\n");
        printf("lan9514_bind failed!\n");
        return ERR_NO_MEMORY;
    }

    list_initialize(&eth->free_read_reqs);
    list_initialize(&eth->free_write_reqs);
    list_initialize(&eth->free_intr_reqs);
    list_initialize(&eth->completed_reads);

    eth->usb_device = device;
    eth->driver = driver;

    mx_status_t status = NO_ERROR;
    for (int i = 0; i < READ_REQ_COUNT; i++) {
        iotxn_t* req = usb_alloc_iotxn(bulk_in_addr, USB_BUF_SIZE, 0);
        if (!req) {
            status = ERR_NO_MEMORY;
            goto fail;
        }
        req->length = USB_BUF_SIZE;
        req->complete_cb = lan9514_read_complete;
        req->cookie = eth;
        list_add_head(&eth->free_read_reqs, &req->node);
    }
    for (int i = 0; i < WRITE_REQ_COUNT; i++) {
        iotxn_t* req = usb_alloc_iotxn(bulk_out_addr, USB_BUF_SIZE, 0);
        if (!req) {
            status = ERR_NO_MEMORY;
            goto fail;
        }
        req->length = USB_BUF_SIZE;
        req->complete_cb = lan9514_write_complete;
        req->cookie = eth;
        list_add_head(&eth->free_write_reqs, &req->node);
    }

    for (int i = 0; i < INTR_REQ_COUNT; i++) {
        iotxn_t* req = usb_alloc_iotxn(intr_addr, INTR_REQ_SIZE, 0);
        if (!req) {
            status = ERR_NO_MEMORY;
            goto fail;
        }
        req->length = INTR_REQ_SIZE;
        req->complete_cb = lan9514_interrupt_complete;
        req->cookie = eth;
        list_add_head(&eth->free_intr_reqs, &req->node);
    }
    thrd_t thread;
    thrd_create_with_name(&thread, lan9514_start_thread, eth, "lan9514_start_thread");
    thrd_detach(thread);
    return NO_ERROR;

fail:
    printf("lan9514_bind failed: %d\n", status);
    lan9514_free(eth);
    return status;
}

mx_driver_t _driver_lan9514 = {
    .ops = {
        .bind = lan9514_bind,
    },
};

MAGENTA_DRIVER_BEGIN(_driver_lan9514, "usb-ethernet-lan9514", "magenta", "0.1", 3)
    BI_ABORT_IF(NE, BIND_PROTOCOL, MX_PROTOCOL_USB),
    BI_ABORT_IF(NE, BIND_USB_VID, SMSC_VID),
    BI_MATCH_IF(EQ, BIND_USB_PID, SMSC_9514_LAN_PID),
MAGENTA_DRIVER_END(_driver_lan9514)