/*
 * Copyright (c) 2011 Broadcom Corporation
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY
 * SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION
 * OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN
 * CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

//#include <linux/firmware.h>
//#include <linux/kernel.h>
//#include <linux/module.h>
//#include <linux/usb.h>
//#include <linux/vmalloc.h>

#include "usb.h"

#include <ddk/protocol/usb.h>
#include <driver/usb.h>
#include <sync/completion.h>
#include <threads.h>

#include "bcdc.h"
#include "brcm_hw_ids.h"
#include "brcmu_utils.h"
#include "brcmu_wifi.h"
#include "bus.h"
#include "common.h"
#include "core.h"
#include "debug.h"
#include "device.h"
#include "firmware.h"
#include "linuxisms.h"
#include "netbuf.h"

#define IOCTL_RESP_TIMEOUT_MSEC (2000)

#define BRCMF_USB_RESET_GETVER_SPINWAIT_MSEC 100 /* in unit of ms */
#define BRCMF_USB_RESET_GETVER_LOOP_CNT 10

#define BRCMF_POSTBOOT_ID 0xA123 /* ID to detect if dongle has boot up */
#define BRCMF_USB_NRXQ 50
#define BRCMF_USB_NTXQ 50

#define BRCMF_USB_CBCTL_WRITE 0
#define BRCMF_USB_CBCTL_READ 1
#define BRCMF_USB_MAX_PKT_SIZE 1600

BRCMF_FW_DEF(43143, "brcmfmac43143.bin");
BRCMF_FW_DEF(43236B, "brcmfmac43236b.bin");
BRCMF_FW_DEF(43242A, "brcmfmac43242a.bin");
BRCMF_FW_DEF(43569, "brcmfmac43569.bin");
BRCMF_FW_DEF(4373, "brcmfmac4373.bin");

static struct brcmf_firmware_mapping brcmf_usb_fwnames[] = {
    BRCMF_FW_ENTRY(BRCM_CC_43143_CHIP_ID, 0xFFFFFFFF, 43143),
    BRCMF_FW_ENTRY(BRCM_CC_43235_CHIP_ID, 0x00000008, 43236B),
    BRCMF_FW_ENTRY(BRCM_CC_43236_CHIP_ID, 0x00000008, 43236B),
    BRCMF_FW_ENTRY(BRCM_CC_43238_CHIP_ID, 0x00000008, 43236B),
    BRCMF_FW_ENTRY(BRCM_CC_43242_CHIP_ID, 0xFFFFFFFF, 43242A),
    BRCMF_FW_ENTRY(BRCM_CC_43566_CHIP_ID, 0xFFFFFFFF, 43569),
    BRCMF_FW_ENTRY(BRCM_CC_43569_CHIP_ID, 0xFFFFFFFF, 43569),
    BRCMF_FW_ENTRY(CY_CC_4373_CHIP_ID, 0xFFFFFFFF, 4373)
};

#define TRX_MAGIC 0x30524448  /* "HDR0" */
#define TRX_MAX_OFFSET 3      /* Max number of file offsets */
#define TRX_UNCOMP_IMAGE 0x20 /* Trx holds uncompressed img */
#define TRX_RDL_CHUNK 1500    /* size of each dl transfer */
#define TRX_OFFSETS_DLFWLEN_IDX 0

/* Control messages: bRequest values */
#define DL_GETSTATE 0  /* returns the rdl_state_t struct */
#define DL_CHECK_CRC 1 /* currently unused */
#define DL_GO 2        /* execute downloaded image */
#define DL_START 3     /* initialize dl state */
#define DL_REBOOT 4    /* reboot the device in 2 seconds */
#define DL_GETVER 5    /* returns the bootrom_id_t struct */
/* execute the downloaded code and set reset
 * event to occur in 2 seconds.  It is the
 * responsibility of the downloaded code to
 * clear this event
 */
#define DL_GO_PROTECTED 6
#define DL_EXEC 7 /* jump to a supplied address */
/* To support single enum on dongle
 * - Not used by bootloader
 */
#define DL_RESETCFG 8
/* Potentially defer the response to setup
 * if resp unavailable
 */
#define DL_DEFER_RESP_OK 9

/* states */
#define DL_WAITING 0      /* waiting to rx first pkt */
#define DL_READY 1        /* hdr was good, waiting for more of the compressed image */
#define DL_BAD_HDR 2      /* hdr was corrupted */
#define DL_BAD_CRC 3      /* compressed image was corrupted */
#define DL_RUNNABLE 4     /* download was successful,waiting for go cmd */
#define DL_START_FAIL 5   /* failed to initialize correctly */
#define DL_NVRAM_TOOBIG 6 /* host specified nvram data exceeds DL_NVRAM value */
#define DL_IMAGE_TOOBIG 7 /* firmware image too big */

struct trx_header_le {
    uint32_t magic;                   /* "HDR0" */
    uint32_t len;                     /* Length of file including header */
    uint32_t crc32;                   /* CRC from flag_version to end of file */
    uint32_t flag_version;            /* 0:15 flags, 16:31 version */
    uint32_t offsets[TRX_MAX_OFFSET]; /* Offsets of partitions from start of
                                     * header
                                     */
};

struct rdl_state_le {
    uint32_t state;
    uint32_t bytes;
};

struct bootrom_id_le {
    uint32_t chip;      /* Chip id */
    uint32_t chiprev;   /* Chip rev */
    uint32_t ramsize;   /* Size of  RAM */
    uint32_t remapbase; /* Current remap base address */
    uint32_t boardtype; /* Type of board */
    uint32_t boardrev;  /* Board revision */
};

struct brcmf_usb_image {
    struct list_node list;
    int8_t* fwname;
    uint8_t* image;
    int image_len;
};

struct brcmf_usbdev_info {
    struct brcmf_usbdev bus_pub; /* MUST BE FIRST */
    usb_protocol_t* protocol;
    //spinlock_t qlock;
    struct list_node rx_freeq;
    struct list_node rx_postq;
    struct list_node tx_freeq;
    struct list_node tx_postq;
    uint8_t rx_endpoint, tx_endpoint;

    int rx_low_watermark;
    int tx_low_watermark;
    int tx_high_watermark;
    int tx_freecount;
    bool tx_flowblock;
    //spinlock_t tx_flowblock_lock;

    struct brcmf_usbreq* tx_reqs;
    struct brcmf_usbreq* rx_reqs;

    char fw_name[BRCMF_FW_NAME_LEN];
    const uint8_t* image; /* buffer for combine fw and nvram */
    int image_len;

    struct brcmf_usb_device* usbdev;
    struct brcmf_device* dev;
    mtx_t dev_init_lock;

    int ctl_in_pipe, ctl_out_pipe;
    struct urb* ctl_urb; /* URB for control endpoint */
    struct usb_ctrlrequest ctl_write;
    struct usb_ctrlrequest ctl_read;
    uint32_t ctl_urb_actual_length;
    int ctl_urb_status;
    completion_t ioctl_resp_wait;
    atomic_ulong ctl_op;
    uint8_t ifnum;

    struct brcmf_urb* bulk_urb; /* used for FW download */

    bool wowl_enabled;
    struct brcmf_mp_device* settings;
};

static void brcmf_usb_rx_refill(struct brcmf_usbdev_info* devinfo, struct brcmf_usbreq* req);

static struct brcmf_usbdev* brcmf_usb_get_buspub(struct brcmf_device* dev) {
    struct brcmf_bus* bus_if = dev_get_drvdata(dev);
    return bus_if->bus_priv.usb;
}

static struct brcmf_usbdev_info* brcmf_usb_get_businfo(struct brcmf_device* dev) {
    return brcmf_usb_get_buspub(dev)->devinfo;
}

static zx_status_t brcmf_usb_ioctl_resp_wait(struct brcmf_usbdev_info* devinfo) {
    return completion_wait(&devinfo->ioctl_resp_wait, ZX_MSEC(IOCTL_RESP_TIMEOUT_MSEC));
}

static void brcmf_usb_ioctl_resp_wake(struct brcmf_usbdev_info* devinfo) {
    completion_signal(&devinfo->ioctl_resp_wait);
}

static void brcmf_usb_ctl_complete(struct brcmf_usbdev_info* devinfo, int type, int status) {
    //brcmf_dbg(USB, "Enter, status=%d\n", status);

    if (unlikely(devinfo == NULL)) {
        return;
    }

    if (type == BRCMF_USB_CBCTL_READ) {
        if (status == 0) {
            devinfo->bus_pub.stats.rx_ctlpkts++;
        } else {
            devinfo->bus_pub.stats.rx_ctlerrs++;
        }
    } else if (type == BRCMF_USB_CBCTL_WRITE) {
        if (status == 0) {
            devinfo->bus_pub.stats.tx_ctlpkts++;
        } else {
            devinfo->bus_pub.stats.tx_ctlerrs++;
        }
    }

    devinfo->ctl_urb_status = status;
    brcmf_usb_ioctl_resp_wake(devinfo);
}

static void brcmf_usb_ctlread_complete(struct urb* urb) {
    struct brcmf_usbdev_info* devinfo = (struct brcmf_usbdev_info*)urb->context;

    brcmf_dbg(USB, "Enter\n");
    pthread_mutex_lock(&irq_callback_lock);
    devinfo->ctl_urb_actual_length = urb->actual_length;
    brcmf_usb_ctl_complete(devinfo, BRCMF_USB_CBCTL_READ, urb->status);
    pthread_mutex_unlock(&irq_callback_lock);
}

static void brcmf_usb_ctlwrite_complete(struct urb* urb) {
    struct brcmf_usbdev_info* devinfo = (struct brcmf_usbdev_info*)urb->context;

    //brcmf_dbg(USB, "Enter\n");
    pthread_mutex_lock(&irq_callback_lock);
    brcmf_usb_ctl_complete(devinfo, BRCMF_USB_CBCTL_WRITE, urb->status);
    pthread_mutex_unlock(&irq_callback_lock);
}

static zx_status_t brcmf_usb_send_ctl(struct brcmf_usbdev_info* devinfo, uint8_t* buf, int len) {
    zx_status_t ret;
    uint16_t size;

    //brcmf_dbg(USB, "Enter\n");
    if (devinfo == NULL || buf == NULL || len == 0 || devinfo->ctl_urb == NULL) {
        return ZX_ERR_INVALID_ARGS;
    }

    size = len;
    devinfo->ctl_write.wLength = size;
    devinfo->ctl_urb->transfer_buffer_length = size;
    devinfo->ctl_urb_status = 0;
    devinfo->ctl_urb_actual_length = 0;

    usb_fill_control_urb(devinfo->ctl_urb, devinfo->usbdev, devinfo->ctl_out_pipe,
                         (unsigned char*)&devinfo->ctl_write, buf, size,
                         (usb_complete_t)brcmf_usb_ctlwrite_complete, devinfo);

    ret = usb_submit_urb(devinfo->ctl_urb, GFP_ATOMIC);
    if (ret != ZX_OK) {
        brcmf_err("usb_submit_urb failed %d\n", ret);
    }

    return ret;
}

static zx_status_t brcmf_usb_recv_ctl(struct brcmf_usbdev_info* devinfo, uint8_t* buf, int len) {
    zx_status_t ret;
    uint16_t size;

    //brcmf_dbg(USB, "Enter\n");
    if ((devinfo == NULL) || (buf == NULL) || (len == 0) || (devinfo->ctl_urb == NULL)) {
        return ZX_ERR_INVALID_ARGS;
    }

    size = len;
    devinfo->ctl_read.wLength = size;
    devinfo->ctl_urb->transfer_buffer_length = size;

    devinfo->ctl_read.bRequestType = USB_DIR_IN | USB_TYPE_CLASS | USB_RECIP_INTERFACE;
    devinfo->ctl_read.bRequest = 1;

    usb_fill_control_urb(devinfo->ctl_urb, devinfo->usbdev, devinfo->ctl_in_pipe,
                         (unsigned char*)&devinfo->ctl_read, buf, size,
                         (usb_complete_t)brcmf_usb_ctlread_complete, devinfo);

    ret = usb_submit_urb(devinfo->ctl_urb, GFP_ATOMIC);
    if (ret != ZX_OK) {
        brcmf_err("usb_submit_urb failed %d\n", ret);
    }

    return ret;
}

static zx_status_t brcmf_usb_tx_ctlpkt(struct brcmf_device* dev, uint8_t* buf, uint32_t len) {
    zx_status_t err = ZX_OK;
    struct brcmf_usbdev_info* devinfo = brcmf_usb_get_businfo(dev);

    //brcmf_dbg(USB, "Enter");
    if (devinfo->bus_pub.state != BRCMFMAC_USB_STATE_UP) {
        return ZX_ERR_IO;
    }

    if (brcmf_test_and_set_bit_in_array(0, &devinfo->ctl_op)) {
        return ZX_ERR_IO;
    }

    completion_reset(&devinfo->ioctl_resp_wait);
    err = brcmf_usb_send_ctl(devinfo, buf, len);
    if (err != ZX_OK) {
        brcmf_err("fail %d bytes: %d\n", err, len);
        brcmf_clear_bit_in_array(0, &devinfo->ctl_op);
        return err;
    }
    err = brcmf_usb_ioctl_resp_wait(devinfo);
    brcmf_clear_bit_in_array(0, &devinfo->ctl_op);
    if (err != ZX_OK) {
        brcmf_err("Txctl wait timed out\n");
        err = ZX_ERR_IO;
    }
    return err;
}

static zx_status_t brcmf_usb_rx_ctlpkt(struct brcmf_device* dev, uint8_t* buf, uint32_t len,
                                       int* urb_len_out) {
    zx_status_t err = ZX_OK;
    bool timeout;
    struct brcmf_usbdev_info* devinfo = brcmf_usb_get_businfo(dev);

    brcmf_dbg(USB, "Enter");
    if (devinfo->bus_pub.state != BRCMFMAC_USB_STATE_UP) {
        return ZX_ERR_IO;
    }

    if (brcmf_test_and_set_bit_in_array(0, &devinfo->ctl_op)) {
        return ZX_ERR_IO;
    }

    completion_reset(&devinfo->ioctl_resp_wait);
    err = brcmf_usb_recv_ctl(devinfo, buf, len);
    if (err != ZX_OK) {
        brcmf_err("fail %d bytes: %d\n", err, len);
        brcmf_clear_bit_in_array(0, &devinfo->ctl_op);
        return err;
    }
    timeout = brcmf_usb_ioctl_resp_wait(devinfo) != ZX_OK;
    err = devinfo->ctl_urb_status;
    brcmf_clear_bit_in_array(0, &devinfo->ctl_op);
    if (timeout) {
        brcmf_err("rxctl wait timed out\n");
        err = ZX_ERR_IO;
    }
    if (err == ZX_OK) {
        if (urb_len_out) {
            *urb_len_out = devinfo->ctl_urb_actual_length;
        }
        return ZX_OK;
    } else {
        return err;
    }
}

static struct brcmf_usbreq* brcmf_usb_deq(struct brcmf_usbdev_info* devinfo, struct list_node* q,
                                          int* counter) {
    struct brcmf_usbreq* req;
    //spin_lock_irqsave(&devinfo->qlock, flags);
    pthread_mutex_lock(&irq_callback_lock);
    if (list_empty(q)) {
        //spin_unlock_irqrestore(&devinfo->qlock, flags);
        pthread_mutex_unlock(&irq_callback_lock);
        return NULL;
    }
    req = list_entry(q->next, struct brcmf_usbreq, list);
    list_del_init(q->next);
    if (counter) {
        (*counter)--;
    }
    //spin_unlock_irqrestore(&devinfo->qlock, flags);
    pthread_mutex_unlock(&irq_callback_lock);
    return req;
}

static void brcmf_usb_enq(struct brcmf_usbdev_info* devinfo, struct list_node* q,
                          struct brcmf_usbreq* req, int* counter) {
    //spin_lock_irqsave(&devinfo->qlock, flags);
    pthread_mutex_lock(&irq_callback_lock);
    list_add_tail(q, &req->list);
    if (counter) {
        (*counter)++;
    }
    //spin_unlock_irqrestore(&devinfo->qlock, flags);
    pthread_mutex_unlock(&irq_callback_lock);
}

static struct brcmf_usbreq* brcmf_usbdev_qinit(struct list_node* q, int qsize) {
    int i;
    struct brcmf_usbreq* req;
    struct brcmf_usbreq* reqs;

    reqs = calloc(qsize, sizeof(struct brcmf_usbreq));
    if (reqs == NULL) {
        return NULL;
    }

    req = reqs;

    for (i = 0; i < qsize; i++) {
        req->urb = usb_alloc_urb(0, GFP_ATOMIC);
        if (!req->urb) {
            goto fail;
        }

        INIT_LIST_HEAD(&req->list);
        list_add_tail(q, &req->list);
        req++;
    }
    return reqs;
fail:
    brcmf_err("fail!\n");
    while (!list_empty(q)) {
        req = list_entry(q->next, struct brcmf_usbreq, list);
        if (req) {
            usb_free_urb(req->urb);
        }
        list_del(q->next);
    }
    return NULL;
}

static void brcmf_usb_free_q(struct list_node* q, bool pending) {
    struct brcmf_usbreq* req;
    struct brcmf_usbreq* next;
    int i = 0;
    list_for_each_entry_safe(req, next, q, list) {
        if (!req->urb) {
            brcmf_err("bad req\n");
            break;
        }
        i++;
        if (pending) {
            usb_kill_urb(req->urb);
        } else {
            usb_free_urb(req->urb);
            list_del_init(&req->list);
        }
    }
}

static void brcmf_usb_del_fromq(struct brcmf_usbdev_info* devinfo, struct brcmf_usbreq* req) {
    //spin_lock_irqsave(&devinfo->qlock, flags);
    pthread_mutex_lock(&irq_callback_lock);
    list_del_init(&req->list);
    //spin_unlock_irqrestore(&devinfo->qlock, flags);
    pthread_mutex_unlock(&irq_callback_lock);
}

static void brcmf_usb_tx_complete(struct urb* urb) {
    struct brcmf_usbreq* req = (struct brcmf_usbreq*)urb->context;
    struct brcmf_usbdev_info* devinfo = req->devinfo;

    pthread_mutex_lock(&irq_callback_lock);
    brcmf_dbg(USB, "Enter, urb->status=%d, skb=%p\n", urb->status, req->skb);
    brcmf_usb_del_fromq(devinfo, req);

    brcmf_proto_bcdc_txcomplete(devinfo->dev, req->skb, urb->status == 0);
    req->skb = NULL;
    brcmf_usb_enq(devinfo, &devinfo->tx_freeq, req, &devinfo->tx_freecount);
    //spin_lock_irqsave(&devinfo->tx_flowblock_lock, flags);
    if (devinfo->tx_freecount > devinfo->tx_high_watermark && devinfo->tx_flowblock) {
        brcmf_proto_bcdc_txflowblock(devinfo->dev, false);
        devinfo->tx_flowblock = false;
    }
    //spin_unlock_irqrestore(&devinfo->tx_flowblock_lock, flags);
    pthread_mutex_unlock(&irq_callback_lock);
}

static void brcmf_usb_rx_complete(struct urb* urb) {
    struct brcmf_usbreq* req = (struct brcmf_usbreq*)urb->context;
    struct brcmf_usbdev_info* devinfo = req->devinfo;
    struct brcmf_netbuf* skb;

    pthread_mutex_lock(&irq_callback_lock);
    brcmf_dbg(USB, "Enter, urb->status=%d\n", urb->status);
    brcmf_usb_del_fromq(devinfo, req);
    skb = req->skb;
    req->skb = NULL;

    /* zero length packets indicate usb "failure". Do not refill */
    if (urb->status != 0 || !urb->actual_length) {
        brcmu_pkt_buf_free_skb(skb);
        brcmf_usb_enq(devinfo, &devinfo->rx_freeq, req, NULL);
        pthread_mutex_unlock(&irq_callback_lock);
        return;
    }

    if (devinfo->bus_pub.state == BRCMFMAC_USB_STATE_UP) {
        brcmf_netbuf_grow_tail(skb, urb->actual_length);
        brcmf_rx_frame(devinfo->dev, skb, true);
        brcmf_usb_rx_refill(devinfo, req);
    } else {
        brcmu_pkt_buf_free_skb(skb);
        brcmf_usb_enq(devinfo, &devinfo->rx_freeq, req, NULL);
    }
    pthread_mutex_unlock(&irq_callback_lock);
    return;
}

static void brcmf_usb_rx_refill(struct brcmf_usbdev_info* devinfo, struct brcmf_usbreq* req) {
    struct brcmf_netbuf* skb;
    zx_status_t ret;

    if (!req || !devinfo) {
        return;
    }

    skb = brcmf_netbuf_allocate(devinfo->bus_pub.bus_mtu);
    if (!skb) {
        brcmf_usb_enq(devinfo, &devinfo->rx_freeq, req, NULL);
        return;
    }
    req->skb = skb;

    usb_fill_bulk_urb(req->urb, devinfo->usbdev, devinfo->rx_endpoint, skb->data,
                      brcmf_netbuf_tail_space(skb), brcmf_usb_rx_complete, req);
    req->devinfo = devinfo;
    brcmf_usb_enq(devinfo, &devinfo->rx_postq, req, NULL);

    ret = usb_submit_urb(req->urb, GFP_ATOMIC);
    if (ret != ZX_OK) {
        brcmf_usb_del_fromq(devinfo, req);
        brcmu_pkt_buf_free_skb(req->skb);
        req->skb = NULL;
        brcmf_usb_enq(devinfo, &devinfo->rx_freeq, req, NULL);
    }
    return;
}

static void brcmf_usb_rx_fill_all(struct brcmf_usbdev_info* devinfo) {
    struct brcmf_usbreq* req;

    if (devinfo->bus_pub.state != BRCMFMAC_USB_STATE_UP) {
        brcmf_err("bus is not up=%d\n", devinfo->bus_pub.state);
        return;
    }
    while ((req = brcmf_usb_deq(devinfo, &devinfo->rx_freeq, NULL)) != NULL) {
        brcmf_usb_rx_refill(devinfo, req);
    }
}

static void brcmf_usb_state_change(struct brcmf_usbdev_info* devinfo, int state) {
    struct brcmf_bus* bcmf_bus = devinfo->bus_pub.bus;
    int old_state;

    brcmf_dbg(USB, "Enter, current state=%d, new state=%d\n", devinfo->bus_pub.state, state);

    if ((int)devinfo->bus_pub.state == state) {
        return;
    }

    old_state = devinfo->bus_pub.state;
    devinfo->bus_pub.state = state;

    /* update state of upper layer */
    if (state == BRCMFMAC_USB_STATE_DOWN) {
        brcmf_dbg(USB, "DBUS is down\n");
        brcmf_bus_change_state(bcmf_bus, BRCMF_BUS_DOWN);
    } else if (state == BRCMFMAC_USB_STATE_UP) {
        brcmf_dbg(USB, "DBUS is up\n");
        brcmf_bus_change_state(bcmf_bus, BRCMF_BUS_UP);
    } else {
        brcmf_dbg(USB, "DBUS current state=%d\n", state);
    }
    brcmf_dbg(TEMP, "Exit");
}

static zx_status_t brcmf_usb_tx(struct brcmf_device* dev, struct brcmf_netbuf* skb) {
    struct brcmf_usbdev_info* devinfo = brcmf_usb_get_businfo(dev);
    struct brcmf_usbreq* req;
    zx_status_t ret;

    brcmf_dbg(USB, "Enter, skb=%p\n", skb);
    if (devinfo->bus_pub.state != BRCMFMAC_USB_STATE_UP) {
        ret = ZX_ERR_IO;
        goto fail;
    }

    req = brcmf_usb_deq(devinfo, &devinfo->tx_freeq, &devinfo->tx_freecount);
    if (!req) {
        brcmf_err("no req to send\n");
        ret = ZX_ERR_NO_MEMORY;
        goto fail;
    }

    req->skb = skb;
    req->devinfo = devinfo;
    usb_fill_bulk_urb(req->urb, devinfo->usbdev, devinfo->tx_endpoint, skb->data, skb->len,
                      brcmf_usb_tx_complete, req);
    brcmf_usb_enq(devinfo, &devinfo->tx_postq, req, NULL);
    ret = usb_submit_urb(req->urb, GFP_ATOMIC);
    if (ret != ZX_OK) {
        brcmf_err("brcmf_usb_tx usb_submit_urb FAILED\n");
        brcmf_usb_del_fromq(devinfo, req);
        req->skb = NULL;
        brcmf_usb_enq(devinfo, &devinfo->tx_freeq, req, &devinfo->tx_freecount);
        goto fail;
    }

    //spin_lock_irqsave(&devinfo->tx_flowblock_lock, flags);
    pthread_mutex_lock(&irq_callback_lock);
    if (devinfo->tx_freecount < devinfo->tx_low_watermark && !devinfo->tx_flowblock) {
        brcmf_proto_bcdc_txflowblock(dev, true);
        devinfo->tx_flowblock = true;
    }
    //spin_unlock_irqrestore(&devinfo->tx_flowblock_lock, flags);
    pthread_mutex_unlock(&irq_callback_lock);
    return ZX_OK;

fail:
    return ret;
}

static zx_status_t brcmf_usb_up(struct brcmf_device* dev) {
    struct brcmf_usbdev_info* devinfo = brcmf_usb_get_businfo(dev);

    brcmf_dbg(USB, "Enter\n");
    if (devinfo->bus_pub.state == BRCMFMAC_USB_STATE_UP) {
        return ZX_OK;
    }

    /* Success, indicate devinfo is fully up */
    brcmf_usb_state_change(devinfo, BRCMFMAC_USB_STATE_UP);
    if (devinfo->ctl_urb) {
        devinfo->ctl_in_pipe = usb_rcvctrlpipe(devinfo->usbdev, 0);
        devinfo->ctl_out_pipe = usb_sndctrlpipe(devinfo->usbdev, 0);

        /* CTL Write */
        devinfo->ctl_write.bRequestType = USB_DIR_OUT | USB_TYPE_CLASS | USB_RECIP_INTERFACE;
        devinfo->ctl_write.bRequest = 0;
        devinfo->ctl_write.wValue = 0;
        devinfo->ctl_write.wIndex = devinfo->ifnum;

        /* CTL Read */
        devinfo->ctl_read.bRequestType = USB_DIR_IN | USB_TYPE_CLASS | USB_RECIP_INTERFACE;
        devinfo->ctl_read.bRequest = 1;
        devinfo->ctl_read.wValue = 0;
        devinfo->ctl_read.wIndex = devinfo->ifnum;
    }
    brcmf_usb_rx_fill_all(devinfo);
    return ZX_OK;
}

static void brcmf_cancel_all_urbs(struct brcmf_usbdev_info* devinfo) {
    brcmf_dbg(TEMP, "* * Entered cancel_all_urbs");
    if (devinfo->ctl_urb) {
        usb_kill_urb(devinfo->ctl_urb);
    }
    if (devinfo->bulk_urb) {
        usb_kill_urb(devinfo->bulk_urb);
    }
    brcmf_usb_free_q(&devinfo->tx_postq, true);
    brcmf_usb_free_q(&devinfo->rx_postq, true);
}

static void brcmf_usb_down(struct brcmf_device* dev) {
    struct brcmf_usbdev_info* devinfo = brcmf_usb_get_businfo(dev);

    brcmf_dbg(USB, "Enter\n");
    if (devinfo == NULL) {
        return;
    }

    if (devinfo->bus_pub.state == BRCMFMAC_USB_STATE_DOWN) {
        return;
    }

    brcmf_usb_state_change(devinfo, BRCMFMAC_USB_STATE_DOWN);

    brcmf_cancel_all_urbs(devinfo);
}

static void brcmf_usb_sync_complete(struct urb* urb) {
    struct brcmf_usbdev_info* devinfo = (struct brcmf_usbdev_info*)urb->context;

    pthread_mutex_lock(&irq_callback_lock);
    brcmf_usb_ioctl_resp_wake(devinfo);
    pthread_mutex_unlock(&irq_callback_lock);
}

static zx_status_t brcmf_usb_dl_cmd(struct brcmf_usbdev_info* devinfo, uint8_t cmd, void* buffer,
                                    int buflen) {
    zx_status_t ret;
    char* tmpbuf;
    uint16_t size;

    if ((!devinfo) || (devinfo->ctl_urb == NULL)) {
        return ZX_ERR_INVALID_ARGS;
    }

    tmpbuf = malloc(buflen);
    if (!tmpbuf) {
        return ZX_ERR_NO_MEMORY;
    }

    size = buflen;
    devinfo->ctl_urb->transfer_buffer_length = size;

    devinfo->ctl_read.wLength = size;
    devinfo->ctl_read.bRequestType = USB_DIR_IN | USB_TYPE_VENDOR | USB_RECIP_INTERFACE;
    devinfo->ctl_read.bRequest = cmd;

    usb_fill_control_urb(devinfo->ctl_urb, devinfo->usbdev, usb_rcvctrlpipe(devinfo->usbdev, 0),
                         (unsigned char*)&devinfo->ctl_read, (void*)tmpbuf, size,
                         (usb_complete_t)brcmf_usb_sync_complete, devinfo);

    completion_reset(&devinfo->ioctl_resp_wait);
    ret = usb_submit_urb(devinfo->ctl_urb, GFP_ATOMIC);
    if (ret != ZX_OK) {
        brcmf_err("usb_submit_urb failed %d\n", ret);
        goto finalize;
    }

    if (brcmf_usb_ioctl_resp_wait(devinfo) != ZX_OK) {
        brcmf_dbg(TEMP, "Timed out. Canceling endpoint 0.");
        usb_kill_urb(devinfo->ctl_urb);
        ret = ZX_ERR_SHOULD_WAIT;
    } else {
        memcpy(buffer, tmpbuf, buflen);
    }

finalize:
    free(tmpbuf);
    return ret;
}

static bool brcmf_usb_dlneeded(struct brcmf_usbdev_info* devinfo) {
    struct bootrom_id_le id;
    uint32_t chipid, chiprev;

    brcmf_dbg(USB, "Enter\n");

    if (devinfo == NULL) {
        return false;
    }

    /* Check if firmware downloaded already by querying runtime ID */
    id.chip = 0xDEAD;
    zx_status_t result = brcmf_usb_dl_cmd(devinfo, DL_GETVER, &id, sizeof(id));
    brcmf_dbg(TEMP, "result from dl_cmd %d", result);

    chipid = id.chip;
    chiprev = id.chiprev;

    if ((chipid & 0x4300) == 0x4300) {
        brcmf_dbg(USB, "chip 0x%x rev 0x%x\n", chipid, chiprev);
    } else {
        brcmf_dbg(USB, "chip %d rev 0x%x\n", chipid, chiprev);
    }
    if (chipid == BRCMF_POSTBOOT_ID) {
        brcmf_dbg(USB, "firmware already downloaded\n");
        brcmf_usb_dl_cmd(devinfo, DL_RESETCFG, &id, sizeof(id));
        return false;
    } else {
        devinfo->bus_pub.devid = chipid;
        devinfo->bus_pub.chiprev = chiprev;
    }
    return true;
}

static zx_status_t brcmf_usb_resetcfg(struct brcmf_usbdev_info* devinfo) {
    struct bootrom_id_le id;
    uint32_t loop_cnt;
    zx_status_t err;

    brcmf_dbg(USB, "Enter\n");

    loop_cnt = 0;
    do {
        msleep(BRCMF_USB_RESET_GETVER_SPINWAIT_MSEC);
        loop_cnt++;
        id.chip = 0xDEAD; /* Get the ID */
        err = brcmf_usb_dl_cmd(devinfo, DL_GETVER, &id, sizeof(id));
        if ((err != ZX_OK) && (err != ZX_ERR_SHOULD_WAIT)) {
            brcmf_dbg(USB, "Returning err %d from DL_GETVER", err);
            return err;
        }
        if (id.chip == BRCMF_POSTBOOT_ID) {
            break;
        }
    } while (loop_cnt < BRCMF_USB_RESET_GETVER_LOOP_CNT);

    if (id.chip == BRCMF_POSTBOOT_ID) {
        brcmf_dbg(USB, "postboot chip 0x%x/rev 0x%x\n", id.chip,
                  id.chiprev);

        brcmf_usb_dl_cmd(devinfo, DL_RESETCFG, &id, sizeof(id));
        return ZX_OK;
    } else {
        brcmf_err("Cannot talk to Dongle. Firmware is not UP, %d ms\n",
                  BRCMF_USB_RESET_GETVER_SPINWAIT_MSEC * loop_cnt);
        return ZX_ERR_INVALID_ARGS;
    }
}

static zx_status_t brcmf_usb_dl_send_bulk(struct brcmf_usbdev_info* devinfo, void* buffer,
                                          int len) {
    zx_status_t ret;

    if ((devinfo == NULL) || (devinfo->bulk_urb == NULL)) {
        return ZX_ERR_INVALID_ARGS;
    }

    /* Prepare the URB */
    usb_fill_bulk_urb(devinfo->bulk_urb, devinfo->usbdev, devinfo->tx_endpoint, buffer, len,
                      (usb_complete_t)brcmf_usb_sync_complete, devinfo);

    completion_reset(&devinfo->ioctl_resp_wait);
    ret = usb_submit_urb(devinfo->bulk_urb, GFP_ATOMIC);
    if (ret != ZX_OK) {
        brcmf_err("usb_submit_urb failed %d\n", ret);
        return ret;
    }
    ret = brcmf_usb_ioctl_resp_wait(devinfo);
    return ret;
}

static zx_status_t brcmf_usb_dl_writeimage(struct brcmf_usbdev_info* devinfo, uint8_t* fw,
                                           int fwlen) {
    unsigned int sendlen, sent, dllen;
    char* bulkchunk = NULL;
    char* dlpos;
    struct rdl_state_le state;
    uint32_t rdlstate, rdlbytes;
    zx_status_t err = ZX_OK;

    brcmf_dbg(USB, "Enter, fw %p, len %d\n", fw, fwlen);

    bulkchunk = malloc(TRX_RDL_CHUNK);
    if (bulkchunk == NULL) {
        err = ZX_ERR_NO_MEMORY;
        goto fail;
    }

    /* 1) Prepare USB boot loader for runtime image */
    brcmf_usb_dl_cmd(devinfo, DL_START, &state, sizeof(state));

    rdlstate = state.state;
    rdlbytes = state.bytes;

    /* 2) Check we are in the Waiting state */
    if (rdlstate != DL_WAITING) {
        brcmf_err("Failed to DL_START\n");
        err = ZX_ERR_BAD_STATE;
        goto fail;
    }
    sent = 0;
    dlpos = (char*)fw;
    dllen = fwlen;

    /* Get chip id and rev */
    while (rdlbytes != dllen) {
        /* Wait until the usb device reports it received all
         * the bytes we sent */
        if ((rdlbytes == sent) && (rdlbytes != dllen)) {
            if ((dllen - sent) < TRX_RDL_CHUNK) {
                sendlen = dllen - sent;
            } else {
                sendlen = TRX_RDL_CHUNK;
            }

            /* simply avoid having to send a ZLP by ensuring we
             * never have an even
             * multiple of 64
             */
            if (!(sendlen % 64)) {
                sendlen -= 4;
            }

            /* send data */
            memcpy(bulkchunk, dlpos, sendlen);
            if (brcmf_usb_dl_send_bulk(devinfo, bulkchunk, sendlen) != ZX_OK) {
                brcmf_err("send_bulk failed\n");
                err = ZX_ERR_INTERNAL;
                goto fail;
            }
            dlpos += sendlen;
            sent += sendlen;
        }
        err = brcmf_usb_dl_cmd(devinfo, DL_GETSTATE, &state, sizeof(state));
        if (err != ZX_OK) {
            brcmf_err("DL_GETSTATE Failed\n");
            goto fail;
        }

        rdlstate = state.state;
        rdlbytes = state.bytes;

        /* restart if an error is reported */
        if (rdlstate == DL_BAD_HDR || rdlstate == DL_BAD_CRC) {
            brcmf_err("Bad Hdr or Bad CRC state %d\n", rdlstate);
            err = ZX_ERR_IO_DATA_INTEGRITY;
            goto fail;
        }
    }

fail:
    free(bulkchunk);
    brcmf_dbg(USB, "Exit, err=%d\n", err);
    return err;
}

static zx_status_t brcmf_usb_dlstart(struct brcmf_usbdev_info* devinfo, uint8_t* fw, int len) {
    zx_status_t err;

    brcmf_dbg(USB, "Enter\n");

    if (devinfo == NULL) {
        return ZX_ERR_INVALID_ARGS;
    }

    if (devinfo->bus_pub.devid == 0xDEAD) {
        return ZX_ERR_IO_NOT_PRESENT;
    }

    err = brcmf_usb_dl_writeimage(devinfo, fw, len);
    if (err == ZX_OK) {
        devinfo->bus_pub.state = BRCMFMAC_USB_STATE_DL_DONE;
    } else {
        devinfo->bus_pub.state = BRCMFMAC_USB_STATE_DL_FAIL;
    }
    brcmf_dbg(USB, "Exit, err=%d\n", err);

    return err;
}

static zx_status_t brcmf_usb_dlrun(struct brcmf_usbdev_info* devinfo) {
    struct rdl_state_le state;

    brcmf_dbg(USB, "Enter\n");
    if (!devinfo) {
        return ZX_ERR_INVALID_ARGS;
    }

    if (devinfo->bus_pub.devid == 0xDEAD) {
        return ZX_ERR_IO_NOT_PRESENT;
    }

    /* Check we are runnable */
    state.state = 0;
    brcmf_usb_dl_cmd(devinfo, DL_GETSTATE, &state, sizeof(state));

    /* Start the image */
    if (state.state == DL_RUNNABLE) {
        if (brcmf_usb_dl_cmd(devinfo, DL_GO, &state, sizeof(state)) != ZX_OK) {
            brcmf_err("Dongle not runnable (DL_GO failed)\n");
            return ZX_ERR_IO_NOT_PRESENT;
        }
        if (brcmf_usb_resetcfg(devinfo) != ZX_OK) {
            brcmf_err("Dongle not runnable (resetcfg failed)\n");
            return ZX_ERR_IO_NOT_PRESENT;
        }
        brcmf_dbg(TEMP, "Survived resetcfg");
        /* The Dongle may go for re-enumeration. */
    } else {
        brcmf_err("Dongle not runnable\n");
        return ZX_ERR_IO_NOT_PRESENT;
    }
    brcmf_dbg(USB, "Exit\n");
    return ZX_OK;
}

static zx_status_t brcmf_usb_fw_download(struct brcmf_usbdev_info* devinfo) {
    zx_status_t err;

    brcmf_dbg(USB, "Enter\n");
    if (devinfo == NULL) {
        return ZX_ERR_INVALID_ARGS;
    }

    if (!devinfo->image) {
        brcmf_err("No firmware!\n");
        return ZX_ERR_BAD_STATE;
    }

    err = brcmf_usb_dlstart(devinfo, (uint8_t*)devinfo->image, devinfo->image_len);
    if (err == ZX_OK) {
        err = brcmf_usb_dlrun(devinfo);
    }
    brcmf_dbg(TEMP, "Exit\n");
    return err;
}

static void brcmf_usb_detach(struct brcmf_usbdev_info* devinfo) {
    brcmf_dbg(USB, "Enter, devinfo %p\n", devinfo);

    /* free the URBS */
    brcmf_usb_free_q(&devinfo->rx_freeq, false);
    brcmf_usb_free_q(&devinfo->tx_freeq, false);

    usb_free_urb(devinfo->ctl_urb);
    usb_free_urb(devinfo->bulk_urb);

    free(devinfo->tx_reqs);
    free(devinfo->rx_reqs);

    if (devinfo->settings) {
        brcmf_release_module_param(devinfo->settings);
    }
}

static zx_status_t check_file(const uint8_t* headers) {
    struct trx_header_le* trx;

    brcmf_dbg(USB, "Enter\n");
    /* Extract trx header */
    trx = (struct trx_header_le*)headers;
    if (trx->magic != TRX_MAGIC) {
        return ZX_ERR_INTERNAL;
    }

    headers += sizeof(struct trx_header_le);

    if (trx->flag_version & TRX_UNCOMP_IMAGE) {
        return ZX_OK;
    }
    return ZX_ERR_INTERNAL;
}

static struct brcmf_usbdev* brcmf_usb_attach(struct brcmf_usbdev_info* devinfo, int nrxq,
                                             int ntxq) {
    brcmf_dbg(USB, "Enter\n");

    devinfo->bus_pub.nrxq = nrxq;
    devinfo->rx_low_watermark = nrxq / 2;
    devinfo->bus_pub.devinfo = devinfo;
    devinfo->bus_pub.ntxq = ntxq;
    devinfo->bus_pub.state = BRCMFMAC_USB_STATE_DOWN;

    /* flow control when too many tx urbs posted */
    devinfo->tx_low_watermark = ntxq / 4;
    devinfo->tx_high_watermark = devinfo->tx_low_watermark * 3;

    /* Size of buffer for rx */
    devinfo->bus_pub.bus_mtu = BRCMF_USB_MAX_PKT_SIZE;

    /* Initialize other structure content */
    devinfo->ioctl_resp_wait = COMPLETION_INIT;

    /* Initialize the spinlocks */
    //spin_lock_init(&devinfo->qlock);
    //spin_lock_init(&devinfo->tx_flowblock_lock);

    INIT_LIST_HEAD(&devinfo->rx_freeq);
    INIT_LIST_HEAD(&devinfo->rx_postq);

    INIT_LIST_HEAD(&devinfo->tx_freeq);
    INIT_LIST_HEAD(&devinfo->tx_postq);

    devinfo->tx_flowblock = false;

    devinfo->rx_reqs = brcmf_usbdev_qinit(&devinfo->rx_freeq, nrxq);
    if (!devinfo->rx_reqs) {
        goto error;
    }

    devinfo->tx_reqs = brcmf_usbdev_qinit(&devinfo->tx_freeq, ntxq);
    if (!devinfo->tx_reqs) {
        goto error;
    }
    devinfo->tx_freecount = ntxq;

    devinfo->ctl_urb = usb_alloc_urb(0, GFP_ATOMIC);
    if (!devinfo->ctl_urb) {
        goto error;
    }
    devinfo->bulk_urb = usb_alloc_urb(0, GFP_ATOMIC);
    if (!devinfo->bulk_urb) {
        goto error;
    }

    return &devinfo->bus_pub;

error:
    brcmf_err("failed!\n");
    brcmf_usb_detach(devinfo);
    return NULL;
}

static void brcmf_usb_wowl_config(struct brcmf_device* dev, bool enabled) {
    struct brcmf_usbdev_info* devinfo = brcmf_usb_get_businfo(dev);

    brcmf_dbg(USB, "Configuring WOWL, enabled=%d\n", enabled);
    devinfo->wowl_enabled = enabled;
    if (enabled) {
        device_set_wakeup_enable(devinfo->dev, true);
    } else {
        device_set_wakeup_enable(devinfo->dev, false);
    }
}

static zx_status_t brcmf_usb_get_fwname(struct brcmf_device* dev, uint32_t chip, uint32_t chiprev,
                                        uint8_t* fw_name) {
    struct brcmf_usbdev_info* devinfo = brcmf_usb_get_businfo(dev);
    zx_status_t ret = ZX_OK;

    if (devinfo->fw_name[0] != '\0') {
        strlcpy((char*)fw_name, devinfo->fw_name, BRCMF_FW_NAME_LEN);
    } else
        ret = brcmf_fw_map_chip_to_name(chip, chiprev, brcmf_usb_fwnames,
                                        ARRAY_SIZE(brcmf_usb_fwnames), (char*)fw_name, NULL);

    return ret;
}

static const struct brcmf_bus_ops brcmf_usb_bus_ops = {
    .txdata = brcmf_usb_tx,
    .stop = brcmf_usb_down,
    .txctl = brcmf_usb_tx_ctlpkt,
    .rxctl = brcmf_usb_rx_ctlpkt,
    .wowl_config = brcmf_usb_wowl_config,
    .get_fwname = brcmf_usb_get_fwname,
};

static zx_status_t brcmf_usb_bus_setup(struct brcmf_usbdev_info* devinfo) {
    zx_status_t ret;

    /* Attach to the common driver interface */
    ret = brcmf_attach(devinfo->dev, devinfo->settings);
    if (ret != ZX_OK) {
        brcmf_err("brcmf_attach failed\n");
        return ret;
    }

    ret = brcmf_usb_up(devinfo->dev);
    if (ret != ZX_OK) {
        goto fail;
    }

    ret = brcmf_bus_started(devinfo->dev);
    if (ret != ZX_OK) {
        goto fail;
    }

    return ZX_OK;
fail:
    brcmf_detach(devinfo->dev);
    return ret;
}

static void brcmf_usb_probe_phase2(struct brcmf_device* dev, zx_status_t ret,
                                   const struct brcmf_firmware* fw, void* nvram, uint32_t nvlen) {
    struct brcmf_bus* bus = dev_get_drvdata(dev);
    struct brcmf_usbdev_info* devinfo = bus->bus_priv.usb->devinfo;

    if (ret != ZX_OK) {
        goto error;
    }

    brcmf_dbg(USB, "Start fw downloading\n");

    ret = check_file(fw->data);
    if (ret != ZX_OK) {
        ret = ZX_ERR_IO;
        brcmf_err("invalid firmware\n");
        goto error;
    }

    devinfo->image = fw->data;
    devinfo->image_len = fw->size;

    ret = brcmf_usb_fw_download(devinfo);
    if (ret != ZX_OK) {
        goto error;
    }

    ret = brcmf_usb_bus_setup(devinfo);
    if (ret != ZX_OK) {
        goto error;
    }

    mtx_unlock(&devinfo->dev_init_lock);
    return;
error:
    brcmf_dbg(TRACE, "failed: dev=%s, err=%d\n", device_get_name(dev->zxdev), ret);
    mtx_unlock(&devinfo->dev_init_lock);
    brcmf_err("TODO(cphoenix): Used to call device_release_driver(dev);");
}

static zx_status_t brcmf_usb_probe_cb(struct brcmf_usbdev_info* devinfo) {
    struct brcmf_bus* bus = NULL;
    struct brcmf_usbdev* bus_pub = NULL;
    struct brcmf_device* dev = devinfo->dev;
    zx_status_t ret;

    brcmf_dbg(USB, "Enter\n");
    bus_pub = brcmf_usb_attach(devinfo, BRCMF_USB_NRXQ, BRCMF_USB_NTXQ);
    if (!bus_pub) {
        return ZX_ERR_IO_NOT_PRESENT;
    }

    bus = calloc(1, sizeof(struct brcmf_bus));
    if (!bus) {
        ret = ZX_ERR_NO_MEMORY;
        goto fail;
    }

    bus->dev = dev;
    bus_pub->bus = bus;
    bus->bus_priv.usb = bus_pub;
    dev_set_drvdata(dev, bus);
    bus->ops = &brcmf_usb_bus_ops;
    bus->proto_type = BRCMF_PROTO_BCDC;
    bus->always_use_fws_queue = true;
#ifdef CONFIG_PM
    bus->wowl_supported = true;
#endif

    devinfo->settings =
        brcmf_get_module_param(bus->dev, BRCMF_BUSTYPE_USB, bus_pub->devid, bus_pub->chiprev);
    if (!devinfo->settings) {
        ret = ZX_ERR_NO_MEMORY;
        goto fail;
    }

    if (!brcmf_usb_dlneeded(devinfo)) {
        ret = brcmf_usb_bus_setup(devinfo);
        if (ret != ZX_OK) {
            goto fail;
        }
        /* we are done */
        mtx_unlock(&devinfo->dev_init_lock);
        return ZX_OK;
    }
    bus->chip = bus_pub->devid;
    bus->chiprev = bus_pub->chiprev;

    ret = brcmf_fw_map_chip_to_name(bus_pub->devid, bus_pub->chiprev, brcmf_usb_fwnames,
                                    ARRAY_SIZE(brcmf_usb_fwnames), devinfo->fw_name, NULL);
    if (ret != ZX_OK) {
        goto fail;
    }

    /* request firmware here */
    ret = brcmf_fw_get_firmwares(dev, 0, devinfo->fw_name, NULL, brcmf_usb_probe_phase2);
    if (ret != ZX_OK) {
        brcmf_err("firmware request failed: %d\n", ret);
        goto fail;
    }

    return ZX_OK;

fail:
    /* Release resources in reverse order */
    free(bus);
    brcmf_usb_detach(devinfo);
    return ret;
}

static void brcmf_usb_disconnect_cb(struct brcmf_usbdev_info* devinfo) {
    if (!devinfo) {
        return;
    }
    brcmf_dbg(USB, "Enter, bus_pub %p\n", devinfo);

    brcmf_detach(devinfo->dev);
    free(devinfo->bus_pub.bus);
    brcmf_usb_detach(devinfo);
}

static zx_status_t brcmf_usb_probe(struct brcmf_usb_interface* intf,
                                   const struct brcmf_usb_device_id* id) {
    struct brcmf_usb_device* usb = intf_to_usbdev(intf);
    struct brcmf_usbdev_info* devinfo;
    struct brcmf_usb_interface_descriptor* desc;
    usb_endpoint_descriptor_t* endpoint;
    zx_status_t ret = ZX_OK;
    uint32_t num_of_eps;
    uint8_t endpoint_num, ep;

    brcmf_dbg(USB, "Enter 0x%04x:0x%04x\n", id->idVendor, id->idProduct);

    devinfo = calloc(1, sizeof(*devinfo));
    if (devinfo == NULL) {
        return ZX_ERR_NO_MEMORY;
    }

    devinfo->usbdev = usb;
    devinfo->dev = &usb->dev;
    /* Take an init lock, to protect for disconnect while still loading.
     * Necessary because of the asynchronous firmware load construction
     */
    mtx_init(&devinfo->dev_init_lock, mtx_plain);
    mtx_lock(&devinfo->dev_init_lock);

    intf->intfdata = devinfo;

    /* Check that the device supports only one configuration */
    if (usb->descriptor.bNumConfigurations != 1) {
        brcmf_err("Number of configurations: %d not supported\n",
                  usb->descriptor.bNumConfigurations);
        ret = ZX_ERR_WRONG_TYPE;
        goto fail;
    }

    if ((usb->descriptor.bDeviceClass != USB_CLASS_VENDOR) &&
            (usb->descriptor.bDeviceClass != USB_CLASS_MISC) &&
            (usb->descriptor.bDeviceClass != USB_CLASS_WIRELESS)) {
        brcmf_err("Device class: 0x%x not supported\n", usb->descriptor.bDeviceClass);
        ret = ZX_ERR_WRONG_TYPE;
        goto fail;
    }

    desc = &intf->altsetting[0].desc;
    if ((desc->bInterfaceClass != USB_CLASS_VENDOR) || (desc->bInterfaceSubClass != 2) ||
            (desc->bInterfaceProtocol != 0xff)) {
        brcmf_err("non WLAN interface %d: 0x%x:0x%x:0x%x\n", desc->bInterfaceNumber,
                  desc->bInterfaceClass, desc->bInterfaceSubClass, desc->bInterfaceProtocol);
        ret = ZX_ERR_WRONG_TYPE;
        goto fail;
    }

    num_of_eps = desc->bNumEndpoints;
    for (ep = 0; ep < num_of_eps; ep++) {
        endpoint = &intf->altsetting[0].endpoint[ep].desc;
        endpoint_num = endpoint->bEndpointAddress & 0xf;
        if (usb_ep_type(endpoint) != USB_ENDPOINT_BULK) {
            continue;
        }
        if (usb_ep_direction(endpoint) == USB_ENDPOINT_IN) {
            if (!devinfo->rx_endpoint) {
                devinfo->rx_endpoint = usb_rcvbulkpipe(usb, endpoint_num);
            }
        } else {
            if (!devinfo->tx_endpoint) {
                devinfo->tx_endpoint = usb_sndbulkpipe(usb, endpoint_num);
            }
        }
    }
    if (devinfo->rx_endpoint == 0) {
        brcmf_err("No RX (in) Bulk EP found\n");
        ret = ZX_ERR_IO_NOT_PRESENT;
        goto fail;
    }
    if (devinfo->tx_endpoint == 0) {
        brcmf_err("No TX (out) Bulk EP found\n");
        ret = ZX_ERR_IO_NOT_PRESENT;
        goto fail;
    }

    devinfo->ifnum = desc->bInterfaceNumber;

    /* voydanoff@ says ZX USB doesn't distinguish between SUPER and SUPER_PLUS.
    if (usb->speed == USB_SPEED_SUPER_PLUS) {
        brcmf_dbg(USB, "Broadcom super speed plus USB WLAN interface detected\n");
    } else*/
    if (usb->speed == USB_SPEED_SUPER) {
        brcmf_dbg(USB, "Broadcom super speed or super speed plus USB WLAN interface detected\n");
    } else if (usb->speed == USB_SPEED_HIGH) {
        brcmf_dbg(USB, "Broadcom high speed USB WLAN interface detected\n");
    } else {
        brcmf_dbg(USB, "Broadcom full speed USB WLAN interface detected\n");
    }

    ret = brcmf_usb_probe_cb(devinfo);
    if (ret != ZX_OK) {
        goto fail;
    }

    /* Success */
    return ZX_OK;

fail:
    mtx_unlock(&devinfo->dev_init_lock);
    free(devinfo);
    intf->intfdata =  NULL;
    return ret;
}

static void brcmf_usb_disconnect(struct brcmf_usb_interface* intf) {
    struct brcmf_usbdev_info* devinfo;

    brcmf_dbg(USB, "Enter\n");
    devinfo = (struct brcmf_usbdev_info*)(intf->intfdata);

    if (devinfo) {
        mtx_lock(&devinfo->dev_init_lock);
        /* Make sure that devinfo still exists. Firmware probe routines
         * may have released the device and cleared the intfdata.
         */
        if (intf->intfdata == NULL) {
            goto done;
        }

        brcmf_usb_disconnect_cb(devinfo);
        free(devinfo);
    }
done:
    brcmf_dbg(USB, "Exit\n");
}

/*
 * only need to signal the bus being down and update the state.
 */
static zx_status_t brcmf_usb_suspend(struct brcmf_usb_interface* intf, pm_message_t state) {
    struct brcmf_usb_device* usb = intf_to_usbdev(intf);
    struct brcmf_usbdev_info* devinfo = brcmf_usb_get_businfo(&usb->dev);

    brcmf_dbg(USB, "Enter\n");
    devinfo->bus_pub.state = BRCMFMAC_USB_STATE_SLEEP;
    if (devinfo->wowl_enabled) {
        brcmf_cancel_all_urbs(devinfo);
    } else {
        brcmf_detach(&usb->dev);
    }
    return ZX_OK;
}

/*
 * (re-) start the bus.
 */
static zx_status_t brcmf_usb_resume(struct brcmf_usb_interface* intf) {
    struct brcmf_usb_device* usb = intf_to_usbdev(intf);
    struct brcmf_usbdev_info* devinfo = brcmf_usb_get_businfo(&usb->dev);

    brcmf_dbg(USB, "Enter\n");
    if (!devinfo->wowl_enabled) {
        return brcmf_usb_bus_setup(devinfo);
    }

    devinfo->bus_pub.state = BRCMFMAC_USB_STATE_UP;
    brcmf_usb_rx_fill_all(devinfo);
    return ZX_OK;
}

static zx_status_t brcmf_usb_reset_resume(struct brcmf_usb_interface* intf) {
    struct brcmf_usb_device* usb = intf_to_usbdev(intf);
    struct brcmf_usbdev_info* devinfo = brcmf_usb_get_businfo(&usb->dev);

    brcmf_dbg(USB, "Enter\n");

    return brcmf_fw_get_firmwares(&usb->dev, 0, devinfo->fw_name, NULL, brcmf_usb_probe_phase2);
}

#define BROADCOM_USB_DEVICE(dev_id) \
    { .idVendor=BRCM_USB_VENDOR_ID_BROADCOM, .idProduct=dev_id }

#define LINKSYS_USB_DEVICE(dev_id) \
    { .idVendor=BRCM_USB_VENDOR_ID_LINKSYS, .idProduct=dev_id }

#define CYPRESS_USB_DEVICE(dev_id) \
    { .idVendor=CY_USB_VENDOR_ID_CYPRESS, .idProduct=dev_id }

#define LG_USB_DEVICE(dev_id) \
    { .idVendor=BRCM_USB_VENDOR_ID_LG, .idProduct=dev_id }

static const struct brcmf_usb_device_id brcmf_usb_devid_table[] = {
    BROADCOM_USB_DEVICE(BRCM_USB_43143_DEVICE_ID),
    BROADCOM_USB_DEVICE(BRCM_USB_43236_DEVICE_ID),
    BROADCOM_USB_DEVICE(BRCM_USB_43242_DEVICE_ID),
    BROADCOM_USB_DEVICE(BRCM_USB_43569_DEVICE_ID),
    LINKSYS_USB_DEVICE(BRCM_USB_43235_LINKSYS_DEVICE_ID),
    CYPRESS_USB_DEVICE(CY_USB_4373_DEVICE_ID),
    LG_USB_DEVICE(BRCM_USB_43242_LG_DEVICE_ID),
    /* special entry for device with firmware loaded and running */
    BROADCOM_USB_DEVICE(BRCM_USB_BCMFW_DEVICE_ID),
    CYPRESS_USB_DEVICE(BRCM_USB_BCMFW_DEVICE_ID),
    {/* end: all zeroes */}
};

MODULE_DEVICE_TABLE(usb, brcmf_usb_devid_table);

static struct usb_driver brcmf_usbdrvr = {
    .name = KBUILD_MODNAME,
    .probe = brcmf_usb_probe,
    .disconnect = brcmf_usb_disconnect,
    .id_table = brcmf_usb_devid_table,
    .suspend = brcmf_usb_suspend,
    .resume = brcmf_usb_resume,
    .reset_resume = brcmf_usb_reset_resume,
    .disable_hub_initiated_lpm = 1,
};

static zx_status_t brcmf_usb_reset_device(struct brcmf_device* dev, void* notused) {
    /* device past is the usb interface so we
     * need to use parent here.
     */
    brcmf_dev_reset(dev->parent);
    return ZX_OK;
}

void brcmf_usb_exit(void) {
    struct device_driver* drv = &brcmf_usbdrvr.drvwrap.driver;
    int ret;

    brcmf_dbg(USB, "Enter\n");
    ret = driver_for_each_device(drv, NULL, NULL, brcmf_usb_reset_device);
    usb_deregister(&brcmf_usbdrvr);
}

zx_status_t brcmf_usb_register(zx_device_t* device, usb_protocol_t* usb_proto) {
    brcmf_dbg(USB, "Enter\n");
    return usb_register(&brcmf_usbdrvr);
}
