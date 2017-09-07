// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ethertap.h"

#include <ddk/debug.h>
#include <magenta/compiler.h>
#include <fbl/auto_lock.h>
#include <fbl/type_support.h>
#include <pretty/hexdump.h>

#include <stdio.h>
#include <string.h>

// This macro allows for per-device tracing rather than enabling tracing for the whole driver
// TODO(tkilbourn): decide whether this is worth the effort
#define ethertap_trace(args...) \
  do { if (unlikely(options_ & ETHERTAP_OPT_TRACE)) dprintf(INFO, "ethertap: " args); } while (0)

namespace eth {

TapCtl::TapCtl(mx_device_t* device) : ddk::Device<TapCtl, ddk::Ioctlable>(device) {}

void TapCtl::DdkRelease() {
    delete this;
}

mx_status_t TapCtl::DdkIoctl(uint32_t op, const void* in_buf, size_t in_len, void* out_buf,
                             size_t out_len, size_t* out_actual) {
    switch (op) {
    case IOCTL_ETHERTAP_CONFIG: {
        if (in_buf == NULL || in_len != sizeof(ethertap_ioctl_config_t) ||
            out_buf == NULL || out_len != sizeof(mx_handle_t) || out_actual == NULL) {
            return MX_ERR_INVALID_ARGS;
        }

        mx::socket local, remote;
        mx_status_t status = mx::socket::create(MX_SOCKET_DATAGRAM, &local, &remote);
        if (status != MX_OK) {
            return status;
        }

        ethertap_ioctl_config_t config;
        memcpy(&config, in_buf, in_len);
        config.name[ETHERTAP_MAX_NAME_LEN] = '\0';

        auto tap = fbl::unique_ptr<eth::TapDevice>(
                new eth::TapDevice(mxdev(), &config, fbl::move(local)));

        status = tap->DdkAdd(config.name);
        if (status != MX_OK) {
            dprintf(ERROR, "tapctl: could not add tap device: %d\n", status);
        } else {
            // devmgr owns the memory until release is called
            tap.release();

            mx_handle_t* out = reinterpret_cast<mx_handle_t*>(out_buf);
            *out = remote.release();
            *out_actual = sizeof(mx_handle_t);
            dprintf(INFO, "tapctl: created ethertap device '%s'\n", config.name);
        }
        return status;
    }
    default:
        return MX_ERR_NOT_SUPPORTED;
    }
}

int tap_device_thread(void* arg) {
    TapDevice* device = reinterpret_cast<TapDevice*>(arg);
    return device->Thread();
}

#define TAP_SHUTDOWN MX_USER_SIGNAL_7

TapDevice::TapDevice(mx_device_t* device, const ethertap_ioctl_config* config, mx::socket data)
  : ddk::Device<TapDevice, ddk::Unbindable>(device),
    options_(config->options),
    features_(config->features | ETHMAC_FEATURE_SYNTH),
    mtu_(config->mtu),
    data_(fbl::move(data)) {
    MX_DEBUG_ASSERT(data_.is_valid());
    memcpy(mac_, config->mac, 6);
    int ret = thrd_create_with_name(&thread_, tap_device_thread, reinterpret_cast<void*>(this),
                                    "ethertap-thread");
    MX_DEBUG_ASSERT(ret == thrd_success);
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
    mx_status_t status = data_.signal(0, TAP_SHUTDOWN);
    MX_DEBUG_ASSERT(status == MX_OK);
    // When the thread exits after the channel is closed, it will call DdkRemove.
}

mx_status_t TapDevice::EthmacQuery(uint32_t options, ethmac_info_t* info) {
    memset(info, 0, sizeof(*info));
    info->features = features_;
    info->mtu = mtu_;
    memcpy(info->mac, mac_, 6);
    return MX_OK;
}

void TapDevice::EthmacStop() {
    ethertap_trace("EthmacStop\n");
    fbl::AutoLock lock(&lock_);
    ethmac_proxy_.reset();
}

mx_status_t TapDevice::EthmacStart(fbl::unique_ptr<ddk::EthmacIfcProxy> proxy) {
    ethertap_trace("EthmacStart\n");
    fbl::AutoLock lock(&lock_);
    if (ethmac_proxy_ != nullptr) {
        return MX_ERR_ALREADY_BOUND;
    } else {
        ethmac_proxy_.swap(proxy);
        ethmac_proxy_->Status(online_ ? ETH_STATUS_ONLINE : 0u);
    }
    return MX_OK;
}

void TapDevice::EthmacSend(uint32_t options, void* data, size_t length) {
    MX_DEBUG_ASSERT(length <= mtu_);
    if (unlikely(options_ & ETHERTAP_OPT_TRACE_PACKETS)) {
        fbl::AutoLock lock(&lock_);
        ethertap_trace("sending %zu bytes\n", length);
        hexdump8_ex(data, length, 0);
    }
    mx_status_t status = data_.write(0u, data, length, nullptr);
    if (status != MX_OK) {
        dprintf(ERROR, "ethertap: EthmacSend error writing: %d\n", status);
    }
}

int TapDevice::Thread() {
    ethertap_trace("starting main thread\n");
    mx_signals_t pending;
    fbl::unique_ptr<uint8_t[]> buf(new uint8_t[mtu_]);

    mx_status_t status = MX_OK;
    const mx_signals_t wait = MX_SOCKET_READABLE | MX_SOCKET_PEER_CLOSED | ETHERTAP_SIGNAL_ONLINE
        | ETHERTAP_SIGNAL_OFFLINE | TAP_SHUTDOWN;
    while (true) {
        status = data_.wait_one(wait, MX_TIME_INFINITE, &pending);
        if (status != MX_OK) {
            ethertap_trace("error waiting on data: %d\n", status);
            break;
        }

        if (pending & (ETHERTAP_SIGNAL_OFFLINE|ETHERTAP_SIGNAL_ONLINE)) {
            status = UpdateLinkStatus(pending);
            if (status != MX_OK) {
                break;
            }
        }

        if (pending & MX_SOCKET_READABLE) {
            status = Recv(buf.get(), mtu_);
            if (status != MX_OK) {
                break;
            }
        }
        if (pending & MX_SOCKET_PEER_CLOSED) {
            ethertap_trace("socket closed (peer)\n");
            break;
        }
        if (pending & TAP_SHUTDOWN) {
            ethertap_trace("socket closed (self)\n");
            break;
        }
    }

    dprintf(INFO, "ethertap: device '%s' destroyed\n", name());
    data_.reset();
    DdkRemove();

    return static_cast<int>(status);
}

static inline bool observed_online(mx_signals_t obs) {
    return obs & ETHERTAP_SIGNAL_ONLINE;
}

static inline bool observed_offline(mx_signals_t obs) {
    return obs & ETHERTAP_SIGNAL_OFFLINE;
}

mx_status_t TapDevice::UpdateLinkStatus(mx_signals_t observed) {
    bool was_online = online_;
    mx_signals_t clear = 0;

    if (observed_online(observed) && observed_offline(observed)) {
        dprintf(ERROR, "ethertap: error asserting both online and offline\n");
        return MX_ERR_BAD_STATE;
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
        mx_status_t status = data_.signal(clear, 0);
        if (status != MX_OK) {
            dprintf(ERROR, "ethertap: could not clear status signals: %d\n", status);
            return status;
        }
    }
    return MX_OK;
}

mx_status_t TapDevice::Recv(uint8_t* buffer, uint32_t capacity) {
    size_t actual = 0;
    mx_status_t status = data_.read(0u, buffer, capacity, &actual);
    if (status != MX_OK) {
        dprintf(ERROR, "ethertap: error reading data: %d\n", status);
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
    return MX_OK;
}

}  // namespace eth

extern "C" mx_status_t tapctl_bind(void* ctx, mx_device_t* device, void** cookie) {
    auto dev = fbl::unique_ptr<eth::TapCtl>(new eth::TapCtl(device));
    mx_status_t status = dev->DdkAdd("tapctl");
    if (status != MX_OK) {
        dprintf(ERROR, "%s: could not add device: %d\n", __func__, status);
    } else {
        // devmgr owns the memory now
        dev.release();
    }
    return status;
}
