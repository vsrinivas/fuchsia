// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "device.h"
#include "logging.h"

#include <magenta/assert.h>
#include <magenta/compiler.h>
#include <magenta/device/wlan.h>
#include <magenta/syscalls.h>
#include <magenta/syscalls/port.h>
#include <mx/time.h>

#include <cinttypes>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <utility>


namespace wlan {

Device::Device(mx_device_t* device, wlanmac_protocol_t* wlanmac_ops)
  : WlanBaseDevice("wlan"),
    parent_(device),
    mlme_(ddk::WlanmacProtocolProxy(wlanmac_ops, device)),
    buffer_alloc_(kNumSlabs, true) {
    debugfn();
}

Device::~Device() {
    debugfn();
    MX_DEBUG_ASSERT(!work_thread_.joinable());
}

// Disable thread safety analysis, as this is a part of device initialization. All thread-unsafe
// work should occur before multiple threads are possible (e.g., before MainLoop is started and
// before Add() is called), or locks should be held.
mx_status_t Device::Bind() __TA_NO_THREAD_SAFETY_ANALYSIS {
    debugfn();

    mx_status_t status = mlme_.Init();
    if (status != NO_ERROR) {
        warnf("could not initialize mlme: %d\n", status);
    }

    status = mx::port::create(MX_PORT_OPT_V2, &port_);
    if (status != NO_ERROR) {
        errorf("could not create port: %d\n", status);
        return status;
    }
    work_thread_ = std::thread(&Device::MainLoop, this);

    status = Add(parent_);
    if (status != NO_ERROR) {
        errorf("could not add device err=%d\n", status);
        auto shutdown_status = SendShutdown();
        if (shutdown_status != NO_ERROR) {
            MX_PANIC("wlan: could not send shutdown loop message: %d\n", shutdown_status);
        }
        if (work_thread_.joinable()) {
            work_thread_.join();
        }
    } else {
        debugf("device added\n");
    }

    return status;
}

mx_status_t Device::QueuePacket(const void* data, size_t length, Packet::Source src) {
    if (length > kBufferSize) {
        return ERR_BUFFER_TOO_SMALL;
    }
    auto buffer = buffer_alloc_.New();
    if (buffer == nullptr) {
        return ERR_NO_RESOURCES;
    }
    auto packet = mxtl::unique_ptr<Packet>(new Packet(std::move(buffer), length));
    packet->set_src(src);
    packet->CopyFrom(data, length, 0);
    {
        std::lock_guard<std::mutex> lock(packet_queue_lock_);
        packet_queue_.push_front(std::move(packet));
        LoopMessage msg(kPacketQueued);
        mx_status_t status = port_.queue(&msg, 0);
        if (status != NO_ERROR) {
            warnf("could not send packet queued msg err=%d\n", status);
            packet_queue_.pop_front();
            return status;
        }
    }
    return NO_ERROR;
}

void Device::DdkUnbind() {
    debugfn();
    device_remove(mxdev());
}

void Device::DdkRelease() {
    debugfn();
    if (port_.is_valid()) {
        mx_status_t status = SendShutdown();
        if (status != NO_ERROR) {
            MX_PANIC("wlan: could not send shutdown loop message: %d\n", status);
        }
        if (work_thread_.joinable()) {
            work_thread_.join();
        }
    }
    delete this;
}

mx_status_t Device::DdkIoctl(uint32_t op, const void* in_buf, size_t in_len, void* out_buf,
                             size_t out_len, size_t* out_actual) {
    debugfn();
    if (op != IOCTL_WLAN_GET_CHANNEL) {
        return ERR_NOT_SUPPORTED;
    }
    if (out_buf == nullptr || out_actual == nullptr || out_len < sizeof(mx_handle_t)) {
        return ERR_BUFFER_TOO_SMALL;
    }

    mx::channel out;
    mx_status_t status = GetChannel(&out);
    if (status != NO_ERROR) {
        return status;
    }

    mx_handle_t* outh = static_cast<mx_handle_t*>(out_buf);
    *outh = out.release();
    *out_actual = sizeof(mx_handle_t);
    return NO_ERROR;
}

mx_status_t Device::EthmacQuery(uint32_t options, ethmac_info_t* info) {
    debugfn();
    if (info == nullptr) return ERR_INVALID_ARGS;

    std::lock_guard<std::mutex> lock(lock_);
    mlme_.GetDeviceInfo(info);
    return NO_ERROR;
}

mx_status_t Device::EthmacStart(mxtl::unique_ptr<ddk::EthmacIfcProxy> proxy) {
    debugfn();
    MX_DEBUG_ASSERT(proxy != nullptr);

    std::lock_guard<std::mutex> lock(lock_);
    return mlme_.Start(mxtl::move(proxy), this);
}

void Device::EthmacStop() {
    debugfn();

    std::lock_guard<std::mutex> lock(lock_);
    mlme_.Stop();
}

void Device::EthmacSend(uint32_t options, void* data, size_t length) {
    // no debugfn() because it's too noisy
    mx_status_t status = QueuePacket(data, length, Packet::Source::kEthernet);
    if (status != NO_ERROR) {
        warnf("could not queue outbound packet err=%d\n", status);
    }
}

void Device::WlanmacStatus(uint32_t status) {
    debugf("WlanmacStatus %u\n", status);
}

void Device::WlanmacRecv(void* data, size_t length, uint32_t flags) {
    // no debugfn() because it's too noisy
    mx_status_t status = QueuePacket(data, length, Packet::Source::kWlan);
    if (status != NO_ERROR) {
        warnf("could not queue inbound packet err=%d\n", status);
    }
}

Device::LoopMessage::LoopMessage(MsgKey key, uint32_t extra) {
    debugf("LoopMessage key=%" PRIu64 ", extra=%u\n", key, extra);
    hdr.key = key;
    hdr.extra = extra;
}

void Device::MainLoop() {
    infof("starting MainLoop\n");
    mx_port_packet_t pkt;
    uint64_t this_key = reinterpret_cast<uint64_t>(this);
    mx_time_t timeout = mx::deadline_after(MX_SEC(5));
    bool running = true;
    while (running) {
        mx_status_t status = port_.wait(timeout, &pkt, 0);
        std::lock_guard<std::mutex> lock(lock_);
        if (status == ERR_TIMED_OUT) {
            mx_status_t status = mlme_.HandleTimeout(&timeout);
            if (status != NO_ERROR) {
                errorf("could not handle timeout err=%d\n", status);
                // TODO: decide whether to continue
            }
            continue;
        } else if (status != NO_ERROR) {
            if (status == ERR_BAD_HANDLE) {
                debugf("port closed, exiting\n");
            } else {
                errorf("error waiting on port: %d\n", status);
            }
            break;
        }

        bool todo = false;
        switch (pkt.type) {
        case MX_PKT_TYPE_USER:
            switch (pkt.key) {
            case kShutdown:
                running = false;
                continue;
            case kPacketQueued:
                todo = true;
                break;
            default:
                errorf("unknown user port key: %" PRIu64 "\n", pkt.key);
                break;
            }
            break;
        case MX_PKT_TYPE_SIGNAL_ONE:
            if (pkt.key != this_key) {
                errorf("unknown signal key: %" PRIu64 ", this_key=%" PRIu64 "\n",
                        pkt.key, this_key);
                break;
            }
            if (ProcessChannelPacketLocked(pkt)) {
                todo = true;
            }
            break;
        default:
            errorf("unknown port packet type: %u\n", pkt.type);
            break;
        }

        if (todo) {
            mxtl::unique_ptr<Packet> packet;
            {
                std::lock_guard<std::mutex> lock(packet_queue_lock_);
                packet = packet_queue_.pop_back();
                MX_DEBUG_ASSERT(packet != nullptr);
            }
            mx_status_t status = mlme_.HandlePacket(packet.get(), &timeout);
            if (status != NO_ERROR) {
                errorf("could not handle packet err=%d\n", status);
            }
        }
    }

    infof("exiting MainLoop\n");
    std::lock_guard<std::mutex> lock(lock_);
    mlme_.Stop();
    port_.reset();
    ResetChannelLocked();
}

bool Device::ProcessChannelPacketLocked(const mx_port_packet_t& pkt) {
    debugf("%s pkt{key=%" PRIu64 ", type=%u, status=%d\n",
            __func__, pkt.key, pkt.type, pkt.status);

    bool packet_queued = false;
    const auto& sig = pkt.signal;
    debugf("signal trigger=%u observed=%u count=%" PRIu64 "\n", sig.trigger, sig.observed,
            sig.count);
    if (sig.observed & MX_CHANNEL_PEER_CLOSED) {
        infof("channel closed\n");
        ResetChannelLocked();
    } else if (sig.observed & MX_CHANNEL_READABLE) {
        auto buffer = buffer_alloc_.New();
        if (buffer == nullptr) {
            errorf("no free buffers available!\n");
            // TODO: reply on the channel
            return packet_queued;
        }
        uint32_t read = 0;
        mx_status_t status =
            channel_.read(0, buffer->data, sizeof(buffer->data), &read, nullptr, 0, nullptr);
        if (status != NO_ERROR) {
            errorf("could not read channel: %d\n", status);
            ResetChannelLocked();
            return packet_queued;
        }
        debugf("read %u bytes from channel_\n", read);

        auto packet = mxtl::unique_ptr<Packet>(new Packet(std::move(buffer), read));
        packet->set_src(Packet::Source::kService);
        {
            std::lock_guard<std::mutex> lock(packet_queue_lock_);
            packet_queue_.push_front(std::move(packet));
        }
        packet_queued = true;

        status = RegisterChannelWaitLocked();
        if (status != NO_ERROR) {
            errorf("could not wait on channel: %d\n", status);
            ResetChannelLocked();
        }
    }
    return packet_queued;
}

mx_status_t Device::RegisterChannelWaitLocked() {
    mx_signals_t sigs = MX_CHANNEL_READABLE | MX_CHANNEL_PEER_CLOSED;
    return channel_.wait_async(port_, reinterpret_cast<uintptr_t>(this),
            sigs, MX_WAIT_ASYNC_ONCE);
}

mx_status_t Device::SendShutdown() {
    debugfn();
    LoopMessage msg(kShutdown);
    return port_.queue(&msg, 0);
}

mx_status_t Device::GetChannel(mx::channel* out) {
    MX_DEBUG_ASSERT(out != nullptr);

    std::lock_guard<std::mutex> lock(lock_);
    if (!port_.is_valid()) {
        return ERR_BAD_STATE;
    }
    if (channel_.is_valid()) {
        return ERR_ALREADY_BOUND;
    }

    mx_status_t status = mx::channel::create(0, &channel_, out);
    if (status != NO_ERROR) {
        errorf("could not create channel: %d\n", status);
        return status;
    }

    status = RegisterChannelWaitLocked();
    if (status != NO_ERROR) {
        errorf("could not wait on channel: %d\n", status);
        out->reset();
        channel_.reset();
        return status;
    }

    infof("channel opened\n");
    mlme_.SetServiceChannel(channel_.get());
    return NO_ERROR;
}

void Device::ResetChannelLocked() {
    mlme_.SetServiceChannel(MX_HANDLE_INVALID);
    channel_.reset();
}

}  // namespace wlan
