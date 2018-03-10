// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ethertap.h"

#include <ddk/debug.h>
#include <fbl/auto_lock.h>
#include <fbl/type_support.h>
#include <pretty/hexdump.h>
#include <zircon/compiler.h>

#include <stdio.h>
#include <string.h>

// This macro allows for per-device tracing rather than enabling tracing for the whole driver
// TODO(tkilbourn): decide whether this is worth the effort
#define ethertap_trace(args...) \
  do { if (unlikely(options_ & ETHERTAP_OPT_TRACE)) zxlogf(INFO, "ethertap: " args); } while (0)

namespace eth {

TapCtl::TapCtl(zx_device_t* device) : ddk::Device<TapCtl, ddk::Ioctlable>(device) {}

void TapCtl::DdkRelease() {
    delete this;
}

zx_status_t TapCtl::DdkIoctl(uint32_t op, const void* in_buf, size_t in_len, void* out_buf,
                             size_t out_len, size_t* out_actual) {
    switch (op) {
    case IOCTL_ETHERTAP_CONFIG: {
        if (in_buf == NULL || in_len != sizeof(ethertap_ioctl_config_t) ||
            out_buf == NULL || out_len != sizeof(zx_handle_t) || out_actual == NULL) {
            return ZX_ERR_INVALID_ARGS;
        }

        ethertap_ioctl_config_t config;
        memcpy(&config, in_buf, in_len);

        if (config.mtu > ETHERTAP_MAX_MTU) {
            return ZX_ERR_INVALID_ARGS;
        }

        zx::socket local, remote;
        uint32_t sockopt = ZX_SOCKET_DATAGRAM |
                           ((config.options & ETHERTAP_OPT_REPORT_PARAM) ? ZX_SOCKET_HAS_CONTROL : 0);
        zx_status_t status = zx::socket::create(sockopt, &local, &remote);
        if (status != ZX_OK) {
            return status;
        }

        config.name[ETHERTAP_MAX_NAME_LEN] = '\0';

        auto tap = fbl::unique_ptr<eth::TapDevice>(
                new eth::TapDevice(zxdev(), &config, fbl::move(local)));

        status = tap->DdkAdd(config.name);
        if (status != ZX_OK) {
            zxlogf(ERROR, "tapctl: could not add tap device: %d\n", status);
        } else {
            // devmgr owns the memory until release is called
            __UNUSED auto ptr = tap.release();

            zx_handle_t* out = reinterpret_cast<zx_handle_t*>(out_buf);
            *out = remote.release();
            *out_actual = sizeof(zx_handle_t);
            zxlogf(INFO, "tapctl: created ethertap device '%s'\n", config.name);
        }
        return status;
    }
    default:
        return ZX_ERR_NOT_SUPPORTED;
    }
}

int tap_device_thread(void* arg) {
    TapDevice* device = reinterpret_cast<TapDevice*>(arg);
    return device->Thread();
}

#define TAP_SHUTDOWN ZX_USER_SIGNAL_7

TapDevice::TapDevice(zx_device_t* device, const ethertap_ioctl_config* config, zx::socket data)
  : ddk::Device<TapDevice, ddk::Unbindable>(device),
    options_(config->options),
    features_(config->features | ETHMAC_FEATURE_SYNTH),
    mtu_(config->mtu),
    data_(fbl::move(data)) {
    ZX_DEBUG_ASSERT(data_.is_valid());
    memcpy(mac_, config->mac, 6);

    int ret = thrd_create_with_name(&thread_, tap_device_thread, reinterpret_cast<void*>(this),
                                    "ethertap-thread");
    ZX_DEBUG_ASSERT(ret == thrd_success);
}

void TapDevice::DdkRelease() {
    ethertap_trace("DdkRelease\n");
    // Only the thread can call DdkRemove(), which means the thread is exiting on its own. No need
    // to join the thread.
    delete this;
}

void TapDevice::DdkUnbind() {
    ethertap_trace("DdkUnbind\n");
    fbl::AutoLock lock(&lock_);
    zx_status_t status = data_.signal(0, TAP_SHUTDOWN);
    ZX_DEBUG_ASSERT(status == ZX_OK);
    // When the thread exits after the channel is closed, it will call DdkRemove.
}

zx_status_t TapDevice::EthmacQuery(uint32_t options, ethmac_info_t* info) {
    memset(info, 0, sizeof(*info));
    info->features = features_;
    info->mtu = mtu_;
    memcpy(info->mac, mac_, 6);
    return ZX_OK;
}

void TapDevice::EthmacStop() {
    ethertap_trace("EthmacStop\n");
    fbl::AutoLock lock(&lock_);
    ethmac_proxy_.reset();
}

zx_status_t TapDevice::EthmacStart(fbl::unique_ptr<ddk::EthmacIfcProxy> proxy) {
    ethertap_trace("EthmacStart\n");
    fbl::AutoLock lock(&lock_);
    if (ethmac_proxy_ != nullptr) {
        return ZX_ERR_ALREADY_BOUND;
    } else {
        ethmac_proxy_.swap(proxy);
        ethmac_proxy_->Status(online_ ? ETH_STATUS_ONLINE : 0u);
    }
    return ZX_OK;
}

zx_status_t TapDevice::EthmacQueueTx(uint32_t options, ethmac_netbuf_t* netbuf) {
    fbl::AutoLock lock(&lock_);
    if (dead_) {
        return ZX_ERR_PEER_CLOSED;
    }
    uint8_t temp_buf[ETHERTAP_MAX_MTU + sizeof(ethertap_socket_header_t)];
    auto header = reinterpret_cast<ethertap_socket_header*>(temp_buf);
    uint8_t* data = temp_buf + sizeof(ethertap_socket_header_t);
    size_t length = netbuf->len;
    ZX_DEBUG_ASSERT(length <= mtu_);
    memcpy(data, netbuf->data, length);
    header->type = ETHERTAP_MSG_PACKET;

    if (unlikely(options_ & ETHERTAP_OPT_TRACE_PACKETS)) {
        ethertap_trace("sending %zu bytes\n", length);
        hexdump8_ex(data, length, 0);
    }
    zx_status_t status = data_.write(0u, temp_buf, length + sizeof(ethertap_socket_header_t),
                                     nullptr);
    if (status != ZX_OK) {
        zxlogf(ERROR, "ethertap: EthmacQueueTx error writing: %d\n", status);
    }
    // returning ZX_ERR_SHOULD_WAIT indicates that we will call complete_tx(), which we will not
    return status == ZX_ERR_SHOULD_WAIT ? ZX_ERR_UNAVAILABLE : status;
}

zx_status_t TapDevice::EthmacSetParam(uint32_t param, int32_t value, void* data) {
    fbl::AutoLock lock(&lock_);
    if (!(options_ & ETHERTAP_OPT_REPORT_PARAM) || dead_) {
        return ZX_ERR_NOT_SUPPORTED;
    }

    struct {
        ethertap_socket_header_t header;
        ethertap_setparam_report_t report;
    } send_buf = {};

    send_buf.header.type = ETHERTAP_MSG_PARAM_REPORT;
    send_buf.report.param = param;
    send_buf.report.value = value;
    send_buf.report.data_length = 0;
    switch (param) {
    case ETHMAC_SETPARAM_MULTICAST_FILTER:
        if (value == ETHMAC_MULTICAST_FILTER_OVERFLOW) {
            break;
        }
        // Send the final byte of each address, sorted lowest-to-highest.
        uint32_t i;
        for (i = 0; i < static_cast<uint32_t>(value) && i < sizeof(send_buf.report.data); i++) {
            send_buf.report.data[i] = static_cast<uint8_t*>(data)[i * ETH_MAC_SIZE + 5];
        }
        send_buf.report.data_length = i;
        qsort(send_buf.report.data, send_buf.report.data_length, 1,
              [](const void* ap, const void* bp) {
                  int a = *static_cast<const uint8_t*>(ap);
                  int b = *static_cast<const uint8_t*>(bp);
                  return a < b ? -1 : (a > 1 ? 1 : 0);
              });
        break;
    default:
        break;
    }
    zx_status_t status = data_.write(0, &send_buf, sizeof(send_buf), nullptr);
    if (status != ZX_OK) {
        ethertap_trace("error writing SetParam info to socket: %d\n", status);
    }
    // A failure of data_.write is not a simulated failure of hardware under test, so log it but
    // don't report failure on the SetParam attempt.
    return ZX_OK;
}

zx_handle_t TapDevice::EthmacGetBti() {
    return ZX_HANDLE_INVALID;
}

int TapDevice::Thread() {
    ethertap_trace("starting main thread\n");
    zx_signals_t pending;
    fbl::unique_ptr<uint8_t[]> buf(new uint8_t[mtu_]);

    zx_status_t status = ZX_OK;
    const zx_signals_t wait = ZX_SOCKET_READABLE | ZX_SOCKET_PEER_CLOSED | ETHERTAP_SIGNAL_ONLINE
        | ETHERTAP_SIGNAL_OFFLINE | TAP_SHUTDOWN;
    while (true) {
        status = data_.wait_one(wait, zx::time::infinite(), &pending);
        if (status != ZX_OK) {
            ethertap_trace("error waiting on data: %d\n", status);
            break;
        }

        if (pending & (ETHERTAP_SIGNAL_OFFLINE | ETHERTAP_SIGNAL_ONLINE)) {
            status = UpdateLinkStatus(pending);
            if (status != ZX_OK) {
                break;
            }
        }

        if (pending & ZX_SOCKET_READABLE) {
            status = Recv(buf.get(), mtu_);
            if (status != ZX_OK) {
                break;
            }
        }
        if (pending & ZX_SOCKET_PEER_CLOSED) {
            ethertap_trace("socket closed (peer)\n");
            break;
        }
        if (pending & TAP_SHUTDOWN) {
            ethertap_trace("socket closed (self)\n");
            break;
        }
    }
    {
        fbl::AutoLock lock(&lock_);
        dead_ = true;
        zxlogf(INFO, "ethertap: device '%s' destroyed\n", name());
        data_.reset();
    }
    DdkRemove();

    return static_cast<int>(status);
}

static inline bool observed_online(zx_signals_t obs) {
    return obs & ETHERTAP_SIGNAL_ONLINE;
}

static inline bool observed_offline(zx_signals_t obs) {
    return obs & ETHERTAP_SIGNAL_OFFLINE;
}

zx_status_t TapDevice::UpdateLinkStatus(zx_signals_t observed) {
    bool was_online = online_;
    zx_signals_t clear = 0;

    if (observed_online(observed) && observed_offline(observed)) {
        zxlogf(ERROR, "ethertap: error asserting both online and offline\n");
        return ZX_ERR_BAD_STATE;
    }

    if (observed_offline(observed)) {
        ethertap_trace("offline asserted\n");
        online_ = false;
        clear |= ETHERTAP_SIGNAL_OFFLINE;
    }
    if (observed_online(observed)) {
        ethertap_trace("online asserted\n");
        online_ = true;
        clear |= ETHERTAP_SIGNAL_ONLINE;
    }

    if (was_online != online_) {
        fbl::AutoLock lock(&lock_);
        if (ethmac_proxy_ != nullptr) {
            ethmac_proxy_->Status(online_ ? ETH_STATUS_ONLINE : 0u);
        }
        ethertap_trace("device '%s' is now %s\n", name(), online_ ? "online" : "offline");
    }
    if (clear) {
        zx_status_t status = data_.signal(clear, 0);
        if (status != ZX_OK) {
            zxlogf(ERROR, "ethertap: could not clear status signals: %d\n", status);
            return status;
        }
    }
    return ZX_OK;
}

zx_status_t TapDevice::Recv(uint8_t* buffer, uint32_t capacity) {
    size_t actual = 0;
    zx_status_t status = data_.read(0u, buffer, capacity, &actual);
    if (status != ZX_OK) {
        zxlogf(ERROR, "ethertap: error reading data: %d\n", status);
        return status;
    }

    fbl::AutoLock lock(&lock_);
    if (unlikely(options_ & ETHERTAP_OPT_TRACE_PACKETS)) {
        ethertap_trace("received %zu bytes\n", actual);
        hexdump8_ex(buffer, actual, 0);
    }
    if (ethmac_proxy_ != nullptr) {
        ethmac_proxy_->Recv(buffer, actual, 0u);
    }
    return ZX_OK;
}

}  // namespace eth

extern "C" zx_status_t tapctl_bind(void* ctx, zx_device_t* device, void** cookie) {
    auto dev = fbl::unique_ptr<eth::TapCtl>(new eth::TapCtl(device));
    zx_status_t status = dev->DdkAdd("tapctl");
    if (status != ZX_OK) {
        zxlogf(ERROR, "%s: could not add device: %d\n", __func__, status);
    } else {
        // devmgr owns the memory now
        __UNUSED auto ptr = dev.release();
    }
    return status;
}
