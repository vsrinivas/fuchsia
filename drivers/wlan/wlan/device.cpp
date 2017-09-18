// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "device.h"
#include "logging.h"
#include "serialize.h"
#include "timer.h"
#include "wlan.h"

#include <zircon/assert.h>
#include <zircon/compiler.h>
#include <zircon/device/wlan.h>
#include <zircon/syscalls.h>
#include <zircon/syscalls/port.h>
#include <zx/thread.h>
#include <zx/time.h>

#include <cinttypes>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <utility>

namespace wlan {

Device::Device(zx_device_t* device, wlanmac_protocol_t* wlanmac_proto)
    : WlanBaseDevice(device), wlanmac_proxy_(wlanmac_proto), mlme_(this) {
    debugfn();
    state_ = fbl::AdoptRef(new DeviceState);
}

Device::~Device() {
    debugfn();
    ZX_DEBUG_ASSERT(!work_thread_.joinable());
}

// Disable thread safety analysis, as this is a part of device initialization. All thread-unsafe
// work should occur before multiple threads are possible (e.g., before MainLoop is started and
// before DdkAdd() is called), or locks should be held.
zx_status_t Device::Bind() __TA_NO_THREAD_SAFETY_ANALYSIS {
    debugfn();

    zx_status_t status = zx::port::create(0, &port_);
    if (status != ZX_OK) {
        errorf("could not create port: %d\n", status);
        return status;
    }

    status = mlme_.Init();
    if (status != ZX_OK) {
        errorf("could not initialize mlme: %d\n", status);
        return status;
    }

    status = wlanmac_proxy_.Query(0, &ethmac_info_);
    if (status != ZX_OK) {
        errorf("could not query wlanmac device: %d\n", status);
        return status;
    }
    state_->set_address(DeviceAddress(ethmac_info_.mac));

    work_thread_ = std::thread(&Device::MainLoop, this);

    status = DdkAdd("wlan");
    if (status != ZX_OK) {
        errorf("could not add device err=%d\n", status);
        zx_status_t shutdown_status = QueueDevicePortPacket(DevicePacket::kShutdown);
        if (shutdown_status != ZX_OK) {
            ZX_PANIC("wlan: could not send shutdown loop message: %d\n", shutdown_status);
        }
        if (work_thread_.joinable()) { work_thread_.join(); }
    } else {
        debugf("device added\n");
    }

    return status;
}

fbl::unique_ptr<Packet> Device::PreparePacket(const void* data, size_t length, Packet::Peer peer) {
    if (length > kLargeBufferSize) { return nullptr; }

    fbl::unique_ptr<Buffer> buffer = GetBuffer(length);
    if (buffer == nullptr) { return nullptr; }

    auto packet = fbl::unique_ptr<Packet>(new Packet(std::move(buffer), length));
    packet->set_peer(peer);
    zx_status_t status = packet->CopyFrom(data, length, 0);
    if (status != ZX_OK) {
        errorf("could not copy to packet: %d\n", status);
        return nullptr;
    }
    return packet;
}

zx_status_t Device::QueuePacket(fbl::unique_ptr<Packet> packet) {
    if (packet == nullptr) { return ZX_ERR_NO_RESOURCES; }
    std::lock_guard<std::mutex> lock(packet_queue_lock_);
    packet_queue_.Enqueue(std::move(packet));

    zx_status_t status = QueueDevicePortPacket(DevicePacket::kPacketQueued);
    if (status != ZX_OK) {
        warnf("could not send packet queued msg err=%d\n", status);
        packet_queue_.UndoEnqueue();
        return status;
    }
    return ZX_OK;
}

void Device::DdkUnbind() {
    debugfn();
    {
        std::lock_guard<std::mutex> lock(lock_);
        channel_.reset();
        dead_ = true;
    }
    device_remove(zxdev());
}

void Device::DdkRelease() {
    debugfn();
    if (port_.is_valid()) {
        zx_status_t status = QueueDevicePortPacket(DevicePacket::kShutdown);
        if (status != ZX_OK) {
            ZX_PANIC("wlan: could not send shutdown loop message: %d\n", status);
        }
        if (work_thread_.joinable()) { work_thread_.join(); }
    }
    delete this;
}

zx_status_t Device::DdkIoctl(uint32_t op, const void* in_buf, size_t in_len, void* out_buf,
                             size_t out_len, size_t* out_actual) {
    debugfn();
    if (op != IOCTL_WLAN_GET_CHANNEL) { return ZX_ERR_NOT_SUPPORTED; }
    if (out_buf == nullptr || out_actual == nullptr || out_len < sizeof(zx_handle_t)) {
        return ZX_ERR_BUFFER_TOO_SMALL;
    }

    zx::channel out;
    zx_status_t status = GetChannel(&out);
    if (status != ZX_OK) { return status; }

    zx_handle_t* outh = static_cast<zx_handle_t*>(out_buf);
    *outh = out.release();
    *out_actual = sizeof(zx_handle_t);
    return ZX_OK;
}

zx_status_t Device::EthmacQuery(uint32_t options, ethmac_info_t* info) {
    debugfn();
    if (info == nullptr) return ZX_ERR_INVALID_ARGS;

    *info = ethmac_info_;
    // Make sure this device is reported as a wlan device
    info->features |= ETHMAC_FEATURE_WLAN;
    return ZX_OK;
}

zx_status_t Device::EthmacStart(fbl::unique_ptr<ddk::EthmacIfcProxy> proxy) {
    debugfn();
    ZX_DEBUG_ASSERT(proxy != nullptr);

    std::lock_guard<std::mutex> lock(lock_);
    if (ethmac_proxy_ != nullptr) { return ZX_ERR_ALREADY_BOUND; }
    zx_status_t status = wlanmac_proxy_.Start(this);
    if (status != ZX_OK) {
        errorf("could not start wlanmac: %d\n", status);
    } else {
        ethmac_proxy_.swap(proxy);
    }
    return status;
}

void Device::EthmacStop() {
    debugfn();

    std::lock_guard<std::mutex> lock(lock_);
    if (ethmac_proxy_ == nullptr) { warnf("ethmac not started\n"); }
    ethmac_proxy_.reset();
}

void Device::EthmacSend(uint32_t options, void* data, size_t length) {
    // no debugfn() because it's too noisy
    auto packet = PreparePacket(data, length, Packet::Peer::kEthernet);
    zx_status_t status = QueuePacket(std::move(packet));
    if (status != ZX_OK) { warnf("could not queue outbound packet err=%d\n", status); }
}

void Device::WlanmacStatus(uint32_t status) {
    debugf("WlanmacStatus %u\n", status);

    std::lock_guard<std::mutex> lock(lock_);
    SetStatusLocked(status);
}

void Device::WlanmacRecv(uint32_t flags, const void* data, size_t length, wlan_rx_info_t* info) {
    // no debugfn() because it's too noisy
    auto packet = PreparePacket(data, length, Packet::Peer::kWlan, *info);
    zx_status_t status = QueuePacket(std::move(packet));
    if (status != ZX_OK) { warnf("could not queue inbound packet err=%d\n", status); }
}

zx_status_t Device::GetTimer(uint64_t id, fbl::unique_ptr<Timer>* timer) {
    ZX_DEBUG_ASSERT(timer != nullptr);
    ZX_DEBUG_ASSERT(timer->get() == nullptr);
    zx::timer t;
    zx_status_t status = zx::timer::create(0u, ZX_CLOCK_MONOTONIC, &t);
    if (status != ZX_OK) { return status; }

    status = t.wait_async(port_, id, ZX_TIMER_SIGNALED, ZX_WAIT_ASYNC_REPEATING);
    if (status != ZX_OK) { return status; }
    timer->reset(new SystemTimer(id, std::move(t)));

    return ZX_OK;
}

zx_status_t Device::SendEthernet(fbl::unique_ptr<Packet> packet) {
    if (ethmac_proxy_ != nullptr) { ethmac_proxy_->Recv(packet->mut_data(), packet->len(), 0u); }
    return ZX_OK;
}

zx_status_t Device::SendWlan(fbl::unique_ptr<Packet> packet) {
    wlanmac_proxy_.Tx(0u, packet->data(), packet->len());
    return ZX_OK;
}

// Disable thread safety analysis, since these methods are called through an interface from an
// object that we know is holding the lock. So taking the lock would be wrong, but there's no way to
// convince the compiler that the lock is held.

// This *should* be safe, since the worst case is that
// the syscall fails, and we return an error.
// TODO(tkilbourn): consider refactoring this so we don't have to abandon the safety analysis.
zx_status_t Device::SendService(fbl::unique_ptr<Packet> packet) __TA_NO_THREAD_SAFETY_ANALYSIS {
    return channel_.write(0u, packet->data(), packet->len(), nullptr, 0);
}

// TODO(tkilbourn): figure out how to make sure we have the lock for accessing mlme_.
zx_status_t Device::SetChannel(wlan_channel_t chan) __TA_NO_THREAD_SAFETY_ANALYSIS {
    debugf("%s chan=%u\n", __PRETTY_FUNCTION__, chan.channel_num);
    if (chan.channel_num == state_->channel().channel_num) { return ZX_OK; }

    zx_status_t status = mlme_.PreChannelChange(chan);
    if (status != ZX_OK) { return status; }
    status = wlanmac_proxy_.SetChannel(0u, &chan);
    if (status == ZX_OK) { state_->set_channel(chan); }
    zx_status_t post_status = mlme_.PostChannelChange();
    if (status != ZX_OK) { return status; }
    return post_status;
}

zx_status_t Device::SetStatus(uint32_t status) {
    // Lock is already held when MLME is asked to handle assoc/deassoc packets, which caused this
    // link status change.
    SetStatusLocked(status);
    return ZX_OK;
}

void Device::SetStatusLocked(uint32_t status) {
    state_->set_online(status == ETH_STATUS_ONLINE);
    if (ethmac_proxy_ != nullptr) { ethmac_proxy_->Status(status); }
}

fbl::RefPtr<DeviceState> Device::GetState() {
    return state_;
}

void Device::MainLoop() {
    infof("starting MainLoop\n");
    const char kThreadName[] = "wlan-mainloop";
    zx::thread::self().set_property(ZX_PROP_NAME, kThreadName, sizeof(kThreadName));

    zx_port_packet_t pkt;
    bool running = true;
    while (running) {
        zx_time_t timeout = zx::deadline_after(ZX_SEC(30));
        zx_status_t status = port_.wait(timeout, &pkt, 0);
        std::lock_guard<std::mutex> lock(lock_);
        if (status == ZX_ERR_TIMED_OUT) {
            // TODO(tkilbourn): more watchdog checks here?
            ZX_DEBUG_ASSERT(running);
            continue;
        } else if (status != ZX_OK) {
            if (status == ZX_ERR_BAD_HANDLE) {
                debugf("port closed, exiting\n");
            } else {
                errorf("error waiting on port: %d\n", status);
            }
            break;
        }

        switch (pkt.type) {
        case ZX_PKT_TYPE_USER:
            ZX_DEBUG_ASSERT(ToPortKeyType(pkt.key) == PortKeyType::kDevice);
            switch (ToPortKeyId(pkt.key)) {
            case to_enum_type(DevicePacket::kShutdown):
                running = false;
                continue;
            case to_enum_type(DevicePacket::kPacketQueued): {
                fbl::unique_ptr<Packet> packet;
                {
                    std::lock_guard<std::mutex> lock(packet_queue_lock_);
                    packet = packet_queue_.Dequeue();
                    ZX_DEBUG_ASSERT(packet != nullptr);
                }
                zx_status_t status = mlme_.HandlePacket(packet.get());
                if (status != ZX_OK) { errorf("could not handle packet err=%d\n", status); }
                break;
            }
            default:
                errorf("unknown device port key subtype: %" PRIu64 "\n", pkt.user.u64[0]);
                break;
            }
            break;
        case ZX_PKT_TYPE_SIGNAL_REP:
            switch (ToPortKeyType(pkt.key)) {
            case PortKeyType::kMlme:
                mlme_.HandlePortPacket(pkt.key);
                break;
            case PortKeyType::kService:
                ProcessChannelPacketLocked(pkt);
                break;
            default:
                errorf("unknown port key: %" PRIu64 "\n", pkt.key);
                break;
            }
            break;
        default:
            errorf("unknown port packet type: %u\n", pkt.type);
            break;
        }
    }

    infof("exiting MainLoop\n");
    std::lock_guard<std::mutex> lock(lock_);
    port_.reset();
    channel_.reset();
}

void Device::ProcessChannelPacketLocked(const zx_port_packet_t& pkt) {
    debugf("%s pkt{key=%" PRIu64 ", type=%u, status=%d}\n", __func__, pkt.key, pkt.type,
           pkt.status);

    const auto& sig = pkt.signal;
    debugf("signal trigger=%u observed=%u count=%" PRIu64 "\n", sig.trigger, sig.observed,
           sig.count);
    if (sig.observed & ZX_CHANNEL_PEER_CLOSED) {
        infof("channel closed\n");
        channel_.reset();
    } else if (sig.observed & ZX_CHANNEL_READABLE) {
        auto buffer = LargeBufferAllocator::New();
        if (buffer == nullptr) {
            errorf("no free buffers available!\n");
            // TODO: reply on the channel
            return;
        }
        uint32_t read = 0;
        zx_status_t status =
            channel_.read(0, buffer->data(), buffer->capacity(), &read, nullptr, 0, nullptr);
        if (status != ZX_OK) {
            errorf("could not read channel: %d\n", status);
            channel_.reset();
            return;
        }
        debugf("read %u bytes from channel_\n", read);

        auto packet = fbl::unique_ptr<Packet>(new Packet(std::move(buffer), read));
        packet->set_peer(Packet::Peer::kService);
        {
            std::lock_guard<std::mutex> lock(packet_queue_lock_);
            packet_queue_.Enqueue(std::move(packet));
            status = QueueDevicePortPacket(DevicePacket::kPacketQueued);
            if (status != ZX_OK) {
                warnf("could not send packet queued msg err=%d\n", status);
                packet_queue_.UndoEnqueue();
                // TODO(tkilbourn): recover as gracefully as possible
            }
        }
    }
}

zx_status_t Device::RegisterChannelWaitLocked() {
    zx_signals_t sigs = ZX_CHANNEL_READABLE | ZX_CHANNEL_PEER_CLOSED;
    // TODO(tkilbourn): ZX_WAIT_ASYNC_REPEATING can go horribly wrong with multiple threads waiting
    // on the port. If we ever go to a multi-threaded event loop, fix the channel wait.
    return channel_.wait_async(port_, ToPortKey(PortKeyType::kService, 0u), sigs,
                               ZX_WAIT_ASYNC_REPEATING);
}

zx_status_t Device::QueueDevicePortPacket(DevicePacket id) {
    debugfn();
    zx_port_packet_t pkt = {};
    pkt.key = ToPortKey(PortKeyType::kDevice, to_enum_type(id));
    pkt.type = ZX_PKT_TYPE_USER;
    return port_.queue(&pkt, 0);
}

zx_status_t Device::GetChannel(zx::channel* out) {
    ZX_DEBUG_ASSERT(out != nullptr);

    std::lock_guard<std::mutex> lock(lock_);
    if (dead_) { return ZX_ERR_PEER_CLOSED; }
    if (!port_.is_valid()) { return ZX_ERR_BAD_STATE; }
    if (channel_.is_valid()) { return ZX_ERR_ALREADY_BOUND; }

    zx_status_t status = zx::channel::create(0, &channel_, out);
    if (status != ZX_OK) {
        errorf("could not create channel: %d\n", status);
        return status;
    }

    status = RegisterChannelWaitLocked();
    if (status != ZX_OK) {
        errorf("could not wait on channel: %d\n", status);
        out->reset();
        channel_.reset();
        return status;
    }

    infof("channel opened\n");
    return ZX_OK;
}

}  // namespace wlan
