// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "netsvc.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include <inet6/inet6.h>
#include <inet6/netifc.h>

#include <launchpad/launchpad.h>
#include <magenta/process.h>
#include <magenta/processargs.h>
#include <magenta/syscalls.h>

#include <magenta/boot/netboot.h>

static uint32_t last_cookie = 0;
static uint32_t last_cmd = 0;
static uint32_t last_arg = 0;
static uint32_t last_ack_cmd = 0;
static uint32_t last_ack_arg = 0;

#define PAGE_ROUNDUP(x) ((x + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1))
#define MAX_ADVERTISE_DATA_LEN 256

static bool xfer_active = false;

typedef struct nbfilecontainer {
    nbfile file;
    mx_handle_t data;   // handle to vmo that backs netbootfile.
} nbfilecontainer_t;

static nbfilecontainer_t nbkernel;
static nbfilecontainer_t nbbootdata;

// Pointer to the currently active transfer.
static nbfile* active;

mx_status_t nbfilecontainer_init(size_t size, nbfilecontainer_t* target) {
    mx_status_t st = MX_OK;

    assert(target);

    // De-init the container if it's already initialized.
    if (target->file.data) {
        // For now, I can't see a valid reason that a client would send the same
        // filename twice.
        // We'll handle this case gracefully, but we'll print a warning to the
        // console just in case it was a mistake.
        printf("netbootloader: warning, reusing a previously initialized container\n");

        // Unmap the vmo from the address space.
        st = mx_vmar_unmap(mx_vmar_root_self(), (uintptr_t)target->file.data, target->file.size);
        if (st != MX_OK) {
            printf("netbootloader: failed to unmap existing vmo, st = %d\n", st);
            return st;
        }

        mx_handle_close(target->data);

        target->file.offset = 0;
        target->file.size = 0;
        target->file.data = 0;
    }

    size = PAGE_ROUNDUP(size);
    st = mx_vmo_create(size, 0, &target->data);
    if (st != MX_OK) {
        printf("netbootloader: Could not create a netboot vmo of size = %lu "
               "retcode = %d\n", size, st);
        return st;
    }

    uintptr_t buffer;
    st = mx_vmar_map(mx_vmar_root_self(), 0, target->data, 0, size,
                     MX_VM_FLAG_PERM_READ | MX_VM_FLAG_PERM_WRITE, &buffer);
    if (st != MX_OK) {
        printf("netbootloader: failed to map data vmo for buffer, st = %d\n", st);
        mx_handle_close(target->data);
        return st;
    }

    target->file.offset = 0;
    target->file.size = size;
    target->file.data = (uint8_t*)buffer;

    return MX_OK;
}

nbfile* netboot_get_buffer(const char* name, size_t size) {
    mx_status_t st = MX_OK;
    nbfilecontainer_t* result;

    if (!strncmp(name, "kernel.bin", 11)) {
        result = &nbkernel;
    } else if (!strncmp(name, "ramdisk.bin", 12)) {
        result = &nbbootdata;
    } else {
        return NULL;
    }

    st = nbfilecontainer_init(size, result);
    if (st != MX_OK) {
        printf("netbootloader: failed to initialize file container for "
               "file = '%s', retcode = %d\n", name, st);
        return NULL;
    }

    return &result->file;
}

void netboot_advertise(const char* nodename) {
    // Don't advertise if a transfer is active.
    if (xfer_active) return;

    uint8_t buffer[sizeof(nbmsg) + MAX_ADVERTISE_DATA_LEN];
    nbmsg* msg = (void*)buffer;
    msg->magic = NB_MAGIC;
    msg->cookie = 0;
    msg->cmd = NB_ADVERTISE;
    msg->arg = NB_VERSION_CURRENT;

    snprintf((char*)msg->data, MAX_ADVERTISE_DATA_LEN, "version=%s;nodename=%s",
             BOOTLOADER_VERSION, nodename);
    const size_t data_len = strlen((char*)msg->data) + 1;
    udp6_send(buffer, sizeof(nbmsg) + data_len, &ip6_ll_all_nodes,
              NB_ADVERT_PORT, NB_SERVER_PORT);
}

void netboot_recv(void* data, size_t len,
                  const ip6_addr_t* daddr, uint16_t dport,
                  const ip6_addr_t* saddr, uint16_t sport) {
    nbmsg* msg = data;
    nbmsg ack;

    bool do_transmit = true;
    bool do_boot = false;

    if (dport != NB_SERVER_PORT)
        return;

    if (len < sizeof(nbmsg))
        return;
    len -= sizeof(nbmsg);

    if ((last_cookie == msg->cookie) &&
        (last_cmd == msg->cmd) && (last_arg == msg->arg)) {
        // host must have missed the ack. resend
        ack.magic = NB_MAGIC;
        ack.cookie = last_cookie;
        ack.cmd = last_ack_cmd;
        ack.arg = last_ack_arg;
        goto transmit;
    }

    ack.cmd = NB_ACK;
    ack.arg = 0;

    switch (msg->cmd) {
    case NB_COMMAND:
        if (len == 0)
            return;
        msg->data[len - 1] = 0;
        break;
    case NB_SEND_FILE:
        xfer_active = true;
        if (len == 0)
            return;
        msg->data[len - 1] = 0;
        for (size_t i = 0; i < (len - 1); i++) {
            if ((msg->data[i] < ' ') || (msg->data[i] > 127)) {
                msg->data[i] = '.';
            }
        }
        active = netboot_get_buffer((const char*)msg->data, msg->arg);
        if (active) {
            active->offset = 0;
            ack.arg = msg->arg;
            printf("netboot: Receive File '%s'...\n", (char*) msg->data);
        } else {
            printf("netboot: Rejected File '%s'...\n", (char*) msg->data);
            ack.cmd = NB_ERROR_BAD_FILE;
        }
        break;

    case NB_DATA:
    case NB_LAST_DATA:
        xfer_active = true;
        if (active == 0) {
            printf("netboot: > received chunk before NB_FILE\n");
            return;
        }
        if (msg->arg != active->offset) {
            // printf("netboot: < received chunk at offset %d but current offset is %zu\n", msg->arg, active->offset);
            ack.arg = active->offset;
            ack.cmd = NB_ACK;
        } else if ((active->offset + len) > active->size) {
            ack.cmd = NB_ERROR_TOO_LARGE;
            ack.arg = msg->arg;
        } else {
            memcpy(active->data + active->offset, msg->data, len);
            active->offset += len;
            ack.cmd = msg->cmd == NB_LAST_DATA ? NB_FILE_RECEIVED : NB_ACK;
            if (msg->cmd != NB_LAST_DATA) {
                do_transmit = false;
            } else {
                xfer_active = false;
            }
        }
        break;
    case NB_BOOT:
        do_boot = true;
        printf("netboot: Boot Kernel...\n");
        break;
    default:
        // We don't have a handler for this command, let netsvc handle it.
        do_transmit = false;
    }

    last_cookie = msg->cookie;
    last_cmd = msg->cmd;
    last_arg = msg->arg;
    last_ack_cmd = ack.cmd;
    last_ack_arg = ack.arg;

    ack.cookie = msg->cookie;
    ack.magic = NB_MAGIC;
transmit:
    if (do_transmit) {
        udp6_send(&ack, sizeof(ack), saddr, sport, NB_SERVER_PORT);
    }

    if (do_boot) {
        mx_system_mexec(nbkernel.data, nbbootdata.data);
    }
}