// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/binding.h>
#include <ddk/device.h>
#include <ddk/driver.h>
#include <ddk/protocol/bcm-bus.h>
#include <ddk/protocol/ethernet.h>
#include <ddk/protocol/platform-device.h>
#include <driver/usb.h>
#include <magenta/device/ethernet.h>
#include <magenta/listnode.h>
#include <sync/completion.h>
#include <bcm/ioctl.h>

#include <inttypes.h>
#include <fcntl.h>
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
    usb_protocol_t usb;

    uint8_t phy_id;
    uint8_t mac_addr[6];
    uint8_t status[INTR_REQ_SIZE];
    bool online;
    bool dead;

    // pool of free USB requests
    list_node_t free_read_reqs;
    list_node_t free_write_reqs;
    list_node_t free_intr_reqs;

    completion_t phy_state_completion;

    // callback interface to attached ethernet layer
    ethmac_ifc_t* ifc;
    void* cookie;

    mtx_t mutex;
    mtx_t control_ep_mutex;
} lan9514_t;

static mx_status_t lan9514_write_register_locked(lan9514_t* eth, uint16_t reg, uint32_t value) {

    return usb_control(&eth->usb, USB_DIR_OUT | USB_TYPE_VENDOR | USB_RECIP_DEVICE,
                       LAN9514_REQ_REG_WRITE, 0, reg, &value, sizeof(value), MX_TIME_INFINITE);
}

static mx_status_t lan9514_read_register_locked(lan9514_t* eth, uint16_t reg, uint32_t* value) {

    return usb_control(&eth->usb, USB_DIR_IN | USB_TYPE_VENDOR | USB_RECIP_DEVICE,
                       LAN9514_REQ_REG_READ, 0, reg, value, sizeof(*value), MX_TIME_INFINITE);
}

static mx_status_t lan9514_write_register(lan9514_t* eth, uint16_t reg, uint32_t value) {
    mtx_lock(&eth->control_ep_mutex);

    mx_status_t status = lan9514_write_register_locked(eth, reg, value);

    mtx_unlock(&eth->control_ep_mutex);

    return status;
}

static mx_status_t lan9514_read_register(lan9514_t* eth, uint16_t reg, uint32_t* value) {
    mtx_lock(&eth->control_ep_mutex);

    mx_status_t status = lan9514_read_register_locked(eth, reg, value);

    mtx_unlock(&eth->control_ep_mutex);

    return status;
}

static mx_status_t lan9514_mdio_wait_not_busy_locked(lan9514_t* eth) {
    uint32_t retval;
    mx_time_t timecheck = mx_time_get(MX_CLOCK_MONOTONIC);
    do {
        mx_status_t status = lan9514_read_register_locked(eth, LAN9514_MII_ACCESS_REG, &retval);
        if (status < 0)
            return status;
        if ((mx_time_get(MX_CLOCK_MONOTONIC) - timecheck) > MX_SEC(1))
            return MX_ERR_TIMED_OUT;
    } while (retval & LAN9514_MII_ACCESS_MIIBZY);
    return MX_OK;
}

static mx_status_t lan9514_mdio_read(lan9514_t* eth, uint8_t idx, uint16_t* retval) {
    mx_status_t status;
    uint32_t value;

    mtx_lock(&eth->control_ep_mutex);

    status = lan9514_mdio_wait_not_busy_locked(eth);
    if (status < 0)
        goto done;

    value = (LAN9514_PHY_ID << 11) | (idx << 6) | LAN9514_MII_ACCESS_MIIBZY;
    status = lan9514_write_register_locked(eth, LAN9514_MII_ACCESS_REG, value);
    if (status < 0)
        goto done;

    status = lan9514_mdio_wait_not_busy_locked(eth);
    if (status < 0)
        goto done;

    status = lan9514_read_register_locked(eth, LAN9514_MII_DATA_REG, &value);
    *retval = (uint16_t)(value & 0xffff);

done:
    mtx_unlock(&eth->control_ep_mutex);
    return status;
}

static mx_status_t lan9514_mdio_write(lan9514_t* eth, uint8_t idx, uint16_t value) {
    mx_status_t status;
    uint32_t writeval;

    mtx_lock(&eth->control_ep_mutex);

    status = lan9514_mdio_wait_not_busy_locked(eth);
    if (status < 0)
        goto done;

    status = lan9514_write_register_locked(eth, LAN9514_MII_DATA_REG, (uint32_t)value);
    if (status < 0)
        goto done;

    writeval = (LAN9514_PHY_ID << 11) | (idx << 6) | LAN9514_MII_ACCESS_MIIBZY | LAN9514_MII_ACCESS_MIIWnR;
    status = lan9514_write_register_locked(eth, LAN9514_MII_ACCESS_REG, writeval);
    if (status < 0)
        goto done;

    status = lan9514_mdio_wait_not_busy_locked(eth);

done:
    mtx_unlock(&eth->control_ep_mutex);
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
    return MX_OK;
}

static void queue_interrupt_requests_locked(lan9514_t* eth) {
    list_node_t* node;
    while ((node = list_remove_head(&eth->free_intr_reqs)) != NULL) {
        iotxn_t* req = containerof(node, iotxn_t, node);
        iotxn_queue(eth->usb_device, req);
    }
}

mx_status_t lan9514_recv(lan9514_t* eth, iotxn_t* request) {
    if (eth->dead) {
        printf("lan9514_recv dead\n");
        return MX_ERR_PEER_CLOSED;
    }

    size_t len = request->actual;
    uint8_t* pkt;
    iotxn_mmap(request, (void**) &pkt);

    uint32_t rx_status;
    if (len < sizeof(rx_status)) {
        return MX_ERR_IO;
    }
    memcpy(&rx_status, pkt, sizeof(rx_status));

    uint32_t frame_len = (rx_status & LAN9514_RXSTATUS_FRAME_LEN) >> 16;

    if (rx_status & LAN9514_RXSTATUS_ERROR_MASK) {
        printf("invalid header: 0x%08x\n", rx_status);
        return MX_ERR_INTERNAL;
    }
    if (frame_len > (len - sizeof(rx_status))) {
        printf("LAN9514 recv - buffer too small\n)");
        return MX_ERR_BUFFER_TOO_SMALL;
    }

    eth->ifc->recv(eth->cookie, pkt + sizeof(rx_status), frame_len, 0);
    return MX_OK;
}


static void lan9514_read_complete(iotxn_t* request, void* cookie) {
    lan9514_t* eth = (lan9514_t*)cookie;
    //printf("lan9514 read complete\n");

    if (request->status == MX_ERR_IO_NOT_PRESENT) {
        iotxn_release(request);
        return;
    }

    mtx_lock(&eth->mutex);
    if ((request->status == MX_OK) && eth->ifc) {
        lan9514_recv(eth, request);
    }

    if (eth->online) {
        iotxn_queue(eth->usb_device, request);
    } else {
        list_add_head(&eth->free_read_reqs, &request->node);
    }
    mtx_unlock(&eth->mutex);
}

static void lan9514_write_complete(iotxn_t* request, void* cookie) {
    lan9514_t* eth = (lan9514_t*)cookie;
    if (request->status == MX_ERR_IO_NOT_PRESENT) {
        iotxn_release(request);
        return;
    }

    mtx_lock(&eth->mutex);
    list_add_tail(&eth->free_write_reqs, &request->node);
    mtx_unlock(&eth->mutex);
}

static void lan9514_interrupt_complete(iotxn_t* request, void* cookie) {
    lan9514_t* eth = (lan9514_t*)cookie;
    if ((request->status == MX_ERR_IO_NOT_PRESENT) || (request->status == MX_ERR_IO)) { // MX_ERR_IO = NACK (no status change)
        iotxn_release(request);
        return;
    }

    mtx_lock(&eth->mutex);
    if (request->status == MX_OK && request->actual == sizeof(eth->status)) {
        uint8_t status[INTR_REQ_SIZE];
        iotxn_copyfrom(request, status, sizeof(status), 0);
        memcpy(eth->status, status, sizeof(eth->status));
        completion_signal(&eth->phy_state_completion);
    }

    list_add_head(&eth->free_intr_reqs, &request->node);
    queue_interrupt_requests_locked(eth);

    mtx_unlock(&eth->mutex);
}

static void lan9514_send(void* ctx, uint32_t options, void* buffer, size_t length) {
    lan9514_t* eth = ctx;

    if (eth->dead) {
        return;
    }

    mtx_lock(&eth->mutex);

    list_node_t* node = list_remove_head(&eth->free_write_reqs);
    if (!node) {
        goto out;
    }
    iotxn_t* request = containerof(node, iotxn_t, node);

    if (length + ETH_HEADER_SIZE > USB_BUF_SIZE) {
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

    iotxn_copyto(request, header, 8, 0);
    iotxn_copyto(request, buffer, length, 8);
    request->length = length + 8;
    iotxn_queue(eth->usb_device, request);

out:
    mtx_unlock(&eth->mutex);
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

static void lan9514_free(lan9514_t* eth) {
    iotxn_t* txn;

    mtx_lock(&eth->mutex);
    while ((txn = list_remove_head_type(&eth->free_read_reqs, iotxn_t, node)) != NULL) {
        iotxn_release(txn);
    }
    while ((txn = list_remove_head_type(&eth->free_write_reqs, iotxn_t, node)) != NULL) {
        iotxn_release(txn);
    }
    while ((txn = list_remove_head_type(&eth->free_intr_reqs, iotxn_t, node)) != NULL) {
        iotxn_release(txn);
    }
    mtx_unlock(&eth->mutex);

    free(eth);
}

static void lan9514_release(void* ctx) {
    lan9514_t* eth = ctx;
    lan9514_free(eth);
}

static void lan9514_unbind(void* ctx) {
    lan9514_t* eth = ctx;

    mtx_lock(&eth->mutex);
    eth->dead = true;
    mtx_unlock(&eth->mutex);

    // this must be last since this can trigger releasing the device
    device_remove(eth->device);
}

static mx_protocol_device_t lan9514_device_proto = {
    .version = DEVICE_OPS_VERSION,
    .unbind = lan9514_unbind,
    .release = lan9514_release,
};

static mx_status_t lan9514_query(void* ctx, uint32_t options, ethmac_info_t* info) {
    lan9514_t* eth = ctx;

    if (options) {
        return MX_ERR_INVALID_ARGS;
    }

    memset(info, 0, sizeof(*info));
    info->mtu = USB_BUF_SIZE - ETH_HEADER_SIZE;
    memcpy(info->mac, eth->mac_addr, sizeof(eth->mac_addr));

    return MX_OK;
}

static void lan9514_stop(void* ctx) {
    lan9514_t* eth = ctx;
    mtx_lock(&eth->mutex);
    eth->ifc = NULL;
    mtx_unlock(&eth->mutex);
}

static mx_status_t lan9514_start(void* ctx, ethmac_ifc_t* ifc, void* cookie) {
    lan9514_t* eth = ctx;
    mx_status_t status = MX_OK;

    mtx_lock(&eth->mutex);
    if (eth->ifc) {
        status = MX_ERR_BAD_STATE;
    } else {
        eth->ifc = ifc;
        eth->cookie = cookie;
        eth->ifc->status(eth->cookie, eth->online ? ETH_STATUS_ONLINE : 0);
    }
    mtx_unlock(&eth->mutex);

    return status;
}

static ethmac_protocol_ops_t ethmac_ops = {
    .query = lan9514_query,
    .stop = lan9514_stop,
    .start = lan9514_start,
    .send = lan9514_send,
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

    status = lan9514_write_register(eth, LAN9514_PM_CTRL_REG, LAN9514_PM_CTRL_PHY_RST);
    if (status < 0)
        goto fail;
    do {
        if (lan9514_read_register(eth, LAN9514_PM_CTRL_REG, &retval) < 0)
            goto fail;
    } while (retval & LAN9514_PM_CTRL_PHY_RST);

    // if we are on rpi, then try to find BCM bus device to fetch MAC address
    // TODO(voydanoff) come up with a better way of accessing the bus protocol
    mx_device_t* dev = eth->usb_device;
    bcm_bus_protocol_t bus_proto = { NULL, NULL };
    while (dev) {
        platform_device_protocol_t pdev;

        if (device_get_protocol(dev, MX_PROTOCOL_PLATFORM_DEV, &pdev) == MX_OK &&
                pdev_get_protocol(&pdev, MX_PROTOCOL_BCM_BUS, &bus_proto) == MX_OK) {
            break;
        }
        dev = device_get_parent(dev);
    }

    if (bus_proto.ops) {
        uint8_t temp_mac[6];
        if (bcm_bus_get_macid(&bus_proto, temp_mac) == MX_OK) {
            uint32_t macword = (temp_mac[5] << 8) + temp_mac[4];
            if (lan9514_write_register(eth, LAN9514_ADDR_HI_REG, macword) < 0)
                goto fail;
            macword =   (temp_mac[3] << 24) +
                        (temp_mac[2] << 16) +
                        (temp_mac[1] << 8 ) +
                         temp_mac[0];
            if (lan9514_write_register(eth, LAN9514_ADDR_LO_REG, macword) < 0)
                goto fail;
        }
    } else {
        printf("lan9514_reset could not find MX_PROTOCOL_BCM_BUS\n");
    }

    if (lan9514_read_mac_address(eth) < 0) {
        goto fail;
    }
    printf("LAN9514 MAC Address %02x:%02x:%02x:%02x:%02x:%02x\n", eth->mac_addr[0],
           eth->mac_addr[1], eth->mac_addr[2], eth->mac_addr[3], eth->mac_addr[4],
           eth->mac_addr[5]);

    // Set Bulk IN empty response to 1=NAK   (0=ZLP)
    if (lan9514_read_register(eth, LAN9514_HW_CFG_REG, &retval) < 0)
        goto fail;
    retval |= LAN9514_HW_CFG_BIR;
    if (lan9514_write_register(eth, LAN9514_HW_CFG_REG, retval) < 0)
        goto fail;

    if (lan9514_write_register(eth, LAN9514_BULK_IN_DLY_REG, LAN9514_BULK_IN_DLY_DEFAULT) < 0)
        goto fail;

    if (lan9514_read_register(eth, LAN9514_HW_CFG_REG, &retval) < 0)
        goto fail;
    retval &= ~LAN9514_HW_CFG_RXDOFF;
    if (lan9514_write_register(eth, LAN9514_HW_CFG_REG, retval) < 0)
        goto fail;

    if (lan9514_write_register(eth, LAN9514_INT_STS_REG, LAN9514_INT_STS_REG_CLEAR_ALL) < 0)
        goto fail;

    retval = LAN9514_LED_GPIO_CFG_SPD_LED | LAN9514_LED_GPIO_CFG_LNK_LED | LAN9514_LED_GPIO_CFG_FDX_LED;
    if (lan9514_write_register(eth, LAN9514_LED_GPIO_CFG_REG, retval) < 0)
        goto fail;

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
    return MX_OK;

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

    device_add_args_t args = {
        .version = DEVICE_ADD_ARGS_VERSION,
        .name = "smsc-lan9514",
        .ctx = eth,
        .ops = &lan9514_device_proto,
        .proto_id = MX_PROTOCOL_ETHERMAC,
        .proto_ops = &ethmac_ops,
    };

    status = device_add(eth->usb_device, &args, &eth->device);
    if (status < 0) {
        printf("lan9514: failed to create device: %d\n", status);
        lan9514_free(eth);
        return status;
    }

    mtx_lock(&eth->mutex);
    queue_interrupt_requests_locked(eth);
    mtx_unlock(&eth->mutex);

    while (true) {
        uint16_t temp;
        status = completion_wait(&eth->phy_state_completion, MX_MSEC(500)); //MX_TIME_INFINITE);
        if (status == MX_ERR_TIMED_OUT) {
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
                    if (eth->ifc) {
                        eth->ifc->status(eth->cookie, 0);
                    }
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
                    mx_nanosleep(mx_deadline_after(MX_MSEC(100)));
                    if ((mx_time_get(MX_CLOCK_MONOTONIC - timecheck) > MX_SEC(1))) {
                        status = MX_ERR_TIMED_OUT;
                        goto teardown;
                    }
                }
                status = lan9514_mdio_write(eth, MII_PHY_LAN9514_INT_MASK_REG, MII_PHY_LAN9514_INT_MASK_LINK_DOWN);
                if (status < 0)

                    goto teardown;
                mtx_lock(&eth->mutex);
                eth->online = true;
                printf("lan9514: Link is up - %04x\n", temp);
                if (eth->ifc) {
                    eth->ifc->status(eth->cookie, ETH_STATUS_ONLINE);
                }
                iotxn_t* req;
                iotxn_t* prev;
                list_for_every_entry_safe (&eth->free_read_reqs, req, prev, iotxn_t, node) {
                    list_delete(&req->node);
                    iotxn_queue(eth->usb_device, req);
                }
                mtx_unlock(&eth->mutex);

            }
            completion_reset(&eth->phy_state_completion);
        }
    }
teardown:
    lan9514_unbind(eth->device);
    printf("LAN9514: driver failing with status=%d\n", status);
    return status;
}

static mx_status_t lan9514_bind(void* ctx, mx_device_t* device, void** cookie) {
    printf("LAN9514 - attempting to bind\n");

    usb_protocol_t usb;

    mx_status_t status = device_get_protocol(device, MX_PROTOCOL_USB, &usb);
    if (status != MX_OK) {
        return status;
    }

    // find our endpoints
    usb_desc_iter_t iter;
    mx_status_t result = usb_desc_iter_init(&usb, &iter);
    if (result < 0)
        return result;

    usb_interface_descriptor_t* intf = usb_desc_iter_next_interface(&iter, true);
    printf("lan9514 returned %d endpoints\n", intf->bNumEndpoints);
    if (!intf || intf->bNumEndpoints != 3) {
        usb_desc_iter_release(&iter);
        return MX_ERR_NOT_SUPPORTED;
    }

    uint8_t bulk_in_addr = 0;
    uint8_t bulk_out_addr = 0;
    uint8_t intr_addr = 0;

    usb_endpoint_descriptor_t* endp = usb_desc_iter_next_endpoint(&iter);
    while (endp) {
        if (usb_ep_direction(endp) == USB_ENDPOINT_OUT) {
            if (usb_ep_type(endp) == USB_ENDPOINT_BULK) {
                bulk_out_addr = endp->bEndpointAddress;
                //printf("lan9514 bulk out endpoint:%x\n", bulk_out_addr);
            }
        } else {
            if (usb_ep_type(endp) == USB_ENDPOINT_BULK) {
                bulk_in_addr = endp->bEndpointAddress;
                //printf("lan9514 bulk in endpoint:%x\n", bulk_in_addr);
            } else if (usb_ep_type(endp) == USB_ENDPOINT_INTERRUPT) {
                intr_addr = endp->bEndpointAddress;
                //printf("lan9514 interrupt endpoint:%x\n", intr_addr);
            }
        }
        endp = usb_desc_iter_next_endpoint(&iter);
    }
    usb_desc_iter_release(&iter);

    if (!bulk_in_addr || !bulk_out_addr || !intr_addr) {
        printf("lan9514_bind could not find endpoints\n");
        return MX_ERR_NOT_SUPPORTED;
    }

    lan9514_t* eth = calloc(1, sizeof(lan9514_t));
    if (!eth) {
        printf("Not enough memory for lan9514_t\n");
        printf("lan9514_bind failed!\n");
        return MX_ERR_NO_MEMORY;
    }

    list_initialize(&eth->free_read_reqs);
    list_initialize(&eth->free_write_reqs);
    list_initialize(&eth->free_intr_reqs);

    eth->usb_device = device;
    memcpy(&eth->usb, &usb, sizeof(eth->usb));

    for (int i = 0; i < READ_REQ_COUNT; i++) {
        iotxn_t* req = usb_alloc_iotxn(bulk_in_addr, USB_BUF_SIZE);
        if (!req) {
            status = MX_ERR_NO_MEMORY;
            goto fail;
        }
        req->length = USB_BUF_SIZE;
        req->complete_cb = lan9514_read_complete;
        req->cookie = eth;
        list_add_head(&eth->free_read_reqs, &req->node);
    }
    for (int i = 0; i < WRITE_REQ_COUNT; i++) {
        iotxn_t* req = usb_alloc_iotxn(bulk_out_addr, USB_BUF_SIZE);
        if (!req) {
            status = MX_ERR_NO_MEMORY;
            goto fail;
        }
        req->length = USB_BUF_SIZE;
        req->complete_cb = lan9514_write_complete;
        req->cookie = eth;
        list_add_head(&eth->free_write_reqs, &req->node);
    }

    for (int i = 0; i < INTR_REQ_COUNT; i++) {
        iotxn_t* req = usb_alloc_iotxn(intr_addr, INTR_REQ_SIZE);
        if (!req) {
            status = MX_ERR_NO_MEMORY;
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
    return MX_OK;

fail:
    printf("lan9514_bind failed: %d\n", status);
    lan9514_free(eth);
    return status;
}

static mx_driver_ops_t lan9514_driver_ops = {
    .version = DRIVER_OPS_VERSION,
    .bind = lan9514_bind,
};

MAGENTA_DRIVER_BEGIN(ethernet_lan9514, lan9514_driver_ops, "magenta", "0.1", 3)
    BI_ABORT_IF(NE, BIND_PROTOCOL, MX_PROTOCOL_USB),
    BI_ABORT_IF(NE, BIND_USB_VID, SMSC_VID),
    BI_MATCH_IF(EQ, BIND_USB_PID, SMSC_9514_LAN_PID),
MAGENTA_DRIVER_END(ethernet_lan9514)
