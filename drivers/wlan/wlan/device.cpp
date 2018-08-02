// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "device.h"

#include <ddk/device.h>
#include <fbl/limits.h>
#include <fuchsia/wlan/mlme/cpp/fidl.h>
#include <lib/zx/thread.h>
#include <lib/zx/time.h>
#include <wlan/common/channel.h>
#include <wlan/common/logging.h>
#include <wlan/mlme/ap/ap_mlme.h>
#include <wlan/mlme/client/bss.h>
#include <wlan/mlme/client/client_mlme.h>
#include <wlan/mlme/debug.h>
#include <wlan/mlme/service.h>
#include <wlan/mlme/timer.h>
#include <wlan/mlme/wlan.h>
#include <wlan/protocol/ioctl.h>
#include <zircon/assert.h>
#include <zircon/compiler.h>
#include <zircon/syscalls.h>
#include <zircon/syscalls/port.h>

#include <cinttypes>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <utility>

namespace wlan {

namespace wlan_mlme = ::fuchsia::wlan::mlme;

#define DEV(c) static_cast<Device*>(c)
static zx_protocol_device_t wlan_device_ops = {
    .version = DEVICE_OPS_VERSION,
    .unbind = [](void* ctx) { DEV(ctx)->WlanUnbind(); },
    .release = [](void* ctx) { DEV(ctx)->WlanRelease(); },
    .ioctl = [](void* ctx, uint32_t op, const void* in_buf, size_t in_len, void* out_buf,
                size_t out_len, size_t* out_actual) -> zx_status_t {
        return DEV(ctx)->WlanIoctl(op, in_buf, in_len, out_buf, out_len, out_actual);
    },
};

static zx_protocol_device_t eth_device_ops = {
    .version = DEVICE_OPS_VERSION,
    .unbind = [](void* ctx) { DEV(ctx)->EthUnbind(); },
    .release = [](void* ctx) { DEV(ctx)->EthRelease(); },
};

static wlanmac_ifc_t wlanmac_ifc_ops = {
    .status = [](void* cookie, uint32_t status) { DEV(cookie)->WlanmacStatus(status); },
    .recv = [](void* cookie, uint32_t flags, const void* data, size_t length,
               wlan_rx_info_t* info) { DEV(cookie)->WlanmacRecv(flags, data, length, info); },
    .complete_tx = [](void* cookie, wlan_tx_packet_t* pkt,
                      zx_status_t status) { DEV(cookie)->WlanmacCompleteTx(pkt, status); },
    .indication = [](void* cookie, uint32_t ind) { DEV(cookie)->WlanmacIndication(ind); },
};

static ethmac_protocol_ops_t ethmac_ops = {
    .query = [](void* ctx, uint32_t options, ethmac_info_t* info) -> zx_status_t {
        return DEV(ctx)->EthmacQuery(options, info);
    },
    .stop = [](void* ctx) { DEV(ctx)->EthmacStop(); },
    .start = [](void* ctx, ethmac_ifc_t* ifc, void* cookie) -> zx_status_t {
        return DEV(ctx)->EthmacStart(ifc, cookie);
    },
    .queue_tx = [](void* ctx, uint32_t options, ethmac_netbuf_t* netbuf) -> zx_status_t {
        return DEV(ctx)->EthmacQueueTx(options, netbuf);
    },
    .set_param = [](void* ctx, uint32_t param, int32_t value, void* data) -> zx_status_t {
        return DEV(ctx)->EthmacSetParam(param, value, data);
    },
};
#undef DEV

Device::Device(zx_device_t* device, wlanmac_protocol_t wlanmac_proto)
    : parent_(device), wlanmac_proxy_(wlanmac_proto) {
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

    status = wlanmac_proxy_.Query(0, &wlanmac_info_);
    if (status != ZX_OK) {
        errorf("could not query wlanmac device: %d\n", status);
        return status;
    }

    status = ValidateWlanMacInfo(wlanmac_info_);
    if (status != ZX_OK) {
        errorf("could not bind wlanmac device with invalid wlanmac info\n");
        return status;
    }

    state_->set_address(common::MacAddr(wlanmac_info_.ifc_info.mac_addr));

    fbl::unique_ptr<Mlme> mlme;

    // mac_role is a bitfield, but only a single value is supported for an interface
    switch (wlanmac_info_.ifc_info.mac_role) {
    case WLAN_MAC_ROLE_CLIENT:
        mlme.reset(new ClientMlme(this));
        break;
    case WLAN_MAC_ROLE_AP:
        mlme.reset(new ApMlme(this));
        break;
    default:
        errorf("unsupported MAC role: %u\n", wlanmac_info_.ifc_info.mac_role);
        return ZX_ERR_NOT_SUPPORTED;
    }
    ZX_DEBUG_ASSERT(mlme != nullptr);
    status = mlme->Init();
    if (status != ZX_OK) {
        errorf("could not initialize MLME: %d\n", status);
        return status;
    }
    dispatcher_.reset(new Dispatcher(this, std::move(mlme)));
    dispatcher_->CreateAndStartTelemetry();

    work_thread_ = std::thread(&Device::MainLoop, this);

    bool wlan_added = false;

    status = AddWlanDevice();
    if (status == ZX_OK) {
        wlan_added = true;
        status = AddEthDevice();
    }

    // Clean up if either device add failed.
    if (status != ZX_OK) {
        errorf("could not add device err=%d\n", status);
        zx_status_t shutdown_status = QueueDevicePortPacket(DevicePacket::kShutdown);
        if (shutdown_status != ZX_OK) {
            ZX_PANIC("wlan: could not send shutdown loop message: %d\n", shutdown_status);
        }
        if (work_thread_.joinable()) { work_thread_.join(); }

        // Remove the wlan device if it was successfully added.
        if (wlan_added) { device_remove(zxdev_); }
    } else {
        debugf("device added\n");
    }

    return status;
}

zx_status_t Device::AddWlanDevice() {
    device_add_args_t args = {};
    args.version = DEVICE_ADD_ARGS_VERSION;
    args.name = "wlan";
    args.ctx = this;
    args.ops = &wlan_device_ops;
    args.proto_id = ZX_PROTOCOL_WLANIF;
    return device_add(parent_, &args, &zxdev_);
}

zx_status_t Device::AddEthDevice() {
    device_add_args_t args = {};
    args.version = DEVICE_ADD_ARGS_VERSION;
    args.name = "wlan-ethernet";
    args.ctx = this;
    args.ops = &eth_device_ops;
    args.proto_id = ZX_PROTOCOL_ETHERNET_IMPL;
    args.proto_ops = &ethmac_ops;
    return device_add(zxdev_, &args, &ethdev_);
}

fbl::unique_ptr<Packet> Device::PreparePacket(const void* data, size_t length, Packet::Peer peer) {
    fbl::unique_ptr<Buffer> buffer = GetBuffer(length);
    if (buffer == nullptr) {
        errorf("could not get buffer for packet of length %zu\n", length);
        return nullptr;
    }

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
        errorf("could not send packet queued msg err=%d\n", status);
        packet_queue_.UndoEnqueue();
        return status;
    }
    return ZX_OK;
}

void Device::WlanUnbind() {
    debugfn();
    {
        std::lock_guard<std::mutex> lock(lock_);
        channel_.reset();
        dead_ = true;
        if (port_.is_valid()) {
            zx_status_t status = QueueDevicePortPacket(DevicePacket::kShutdown);
            if (status != ZX_OK) {
                ZX_PANIC("wlan: could not send shutdown loop message: %d\n", status);
            }
        }
    }
    device_remove(zxdev_);
}

void Device::WlanRelease() {
    debugfn();
    if (work_thread_.joinable()) { work_thread_.join(); }
    delete this;
}

zx_status_t Device::WlanIoctl(uint32_t op, const void* in_buf, size_t in_len, void* out_buf,
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

void Device::EthUnbind() {
    debugfn();
    device_remove(ethdev_);
}

void Device::EthRelease() {
    debugfn();
    // NOTE: we reuse the same ctx for the wlanif and the ethmac, so we do NOT free the memory here.
    // Since ethdev_ is a child of zxdev_, this release will be called first, followed by
    // WlanRelease. There's nothing else to clean up here.
}

zx_status_t Device::EthmacQuery(uint32_t options, ethmac_info_t* info) {
    debugfn();
    if (info == nullptr) return ZX_ERR_INVALID_ARGS;

    memset(info, 0, sizeof(*info));
    memcpy(info->mac, wlanmac_info_.ifc_info.mac_addr, ETH_MAC_SIZE);
    info->features = ETHMAC_FEATURE_WLAN;
    if (wlanmac_info_.ifc_info.driver_features & WLAN_DRIVER_FEATURE_SYNTH) {
        info->features |= ETHMAC_FEATURE_SYNTH;
    }
    info->mtu = 1500;

    return ZX_OK;
}

zx_status_t Device::EthmacStart(ethmac_ifc_t* ifc, void* cookie) {
    debugfn();
    ZX_DEBUG_ASSERT(ifc != nullptr);

    std::lock_guard<std::mutex> lock(lock_);
    if (ethmac_proxy_ != nullptr) { return ZX_ERR_ALREADY_BOUND; }
    zx_status_t status = wlanmac_proxy_.Start(&wlanmac_ifc_ops, this);
    if (status != ZX_OK) {
        errorf("could not start wlanmac: %d\n", status);
    } else {
        ethmac_proxy_.reset(new EthmacIfcProxy(ifc, cookie));
    }
    return status;
}

void Device::EthmacStop() {
    debugfn();

    std::lock_guard<std::mutex> lock(lock_);
    if (ethmac_proxy_ == nullptr) { warnf("ethmac not started\n"); }
    ethmac_proxy_.reset();
}

zx_status_t Device::EthmacQueueTx(uint32_t options, ethmac_netbuf_t* netbuf) {
    // no debugfn() because it's too noisy
    auto packet = PreparePacket(netbuf->data, netbuf->len, Packet::Peer::kEthernet);
    if (packet == nullptr) {
        warnf("could not prepare Ethernet packet with len %u\n", netbuf->len);
        return ZX_ERR_NO_RESOURCES;
    }
    zx_status_t status = QueuePacket(std::move(packet));
    if (status != ZX_OK) { warnf("could not queue Ethernet packet err=%d\n", status); }
    return status;
}

zx_status_t Device::EthmacSetParam(uint32_t param, int32_t value, void* data) {
    debugfn();
    return ZX_ERR_NOT_SUPPORTED;
}

void Device::WlanmacStatus(uint32_t status) {
    debugf("WlanmacStatus %u\n", status);

    std::lock_guard<std::mutex> lock(lock_);
    SetStatusLocked(status);
}

void Device::WlanmacRecv(uint32_t flags, const void* data, size_t length, wlan_rx_info_t* info) {
    // no debugfn() because it's too noisy
    auto packet = PreparePacket(data, length, Packet::Peer::kWlan, *info);
    if (packet == nullptr) {
        errorf("could not prepare outbound Ethernet packet with len %zu\n", length);
        return;
    }

    zx_status_t status = QueuePacket(std::move(packet));
    if (status != ZX_OK) {
        warnf("could not queue inbound packet with len %zu err=%d\n", length, status);
    }
}

void Device::WlanmacCompleteTx(wlan_tx_packet_t* pkt, zx_status_t status) {
    // TODO(tkilbourn): free memory and complete the ethernet tx (if necessary). For now, we aren't
    // doing any async transmits in the wlan drivers, so this method shouldn't be called yet.
    ZX_PANIC("not implemented yet!");
}

void Device::WlanmacIndication(uint32_t ind) {
    debugf("WlanmacIndication %u\n", ind);
    auto status = QueueDevicePortPacket(DevicePacket::kIndication, ind);
    if (status != ZX_OK) { warnf("could not queue driver indication packet err=%d\n", status); }
}

zx_status_t Device::GetTimer(uint64_t id, fbl::unique_ptr<Timer>* timer) {
    ZX_DEBUG_ASSERT(timer != nullptr);
    ZX_DEBUG_ASSERT(timer->get() == nullptr);
    ZX_DEBUG_ASSERT(port_.is_valid());

    zx::timer t;
    zx_status_t status = zx::timer::create(0u, ZX_CLOCK_MONOTONIC, &t);
    if (status != ZX_OK) { return status; }

    status = t.wait_async(port_, id, ZX_TIMER_SIGNALED, ZX_WAIT_ASYNC_REPEATING);
    if (status != ZX_OK) { return status; }
    timer->reset(new SystemTimer(id, std::move(t)));

    return ZX_OK;
}

zx_status_t Device::SendEthernet(fbl::unique_ptr<Packet> packet) {
    ZX_DEBUG_ASSERT(packet != nullptr);
    ZX_DEBUG_ASSERT(packet->len() <= ETH_FRAME_MAX_SIZE);

    if (packet->len() > ETH_FRAME_MAX_SIZE) {
        errorf("SendEthernet drops Ethernet frame of invalid length: %zu\n", packet->len());
        return ZX_ERR_INVALID_ARGS;
    }

    if (ethmac_proxy_ != nullptr) { ethmac_proxy_->Recv(packet->mut_data(), packet->len(), 0u); }
    return ZX_OK;
}

zx_status_t Device::SendWlan(fbl::unique_ptr<Packet> packet) {
    ZX_DEBUG_ASSERT(packet->len() <= fbl::numeric_limits<uint16_t>::max());

    wlan_tx_packet_t tx_pkt;
    auto status = packet->AsWlanTxPacket(&tx_pkt);
    if (status != ZX_OK) {
        errorf("could not convert packet to wlan_tx_packet when sending wlan frame: %d\n", status);
        return status;
    }

    status = wlanmac_proxy_.QueueTx(0u, &tx_pkt);
    // TODO(tkilbourn): remove this once we implement WlanmacCompleteTx and allow wlanmac drivers to
    // complete transmits asynchronously.
    ZX_DEBUG_ASSERT(status != ZX_ERR_SHOULD_WAIT);

    return status;
}

// Disable thread safety analysis, since these methods are called through an interface from an
// object that we know is holding the lock. So taking the lock would be wrong, but there's no way to
// convince the compiler that the lock is held.

// This *should* be safe, since the worst case is that
// the syscall fails, and we return an error.
// TODO(tkilbourn): consider refactoring this so we don't have to abandon the safety analysis.
zx_status_t Device::SendService(fbl::unique_ptr<Packet> packet) __TA_NO_THREAD_SAFETY_ANALYSIS {
    if (channel_.is_valid()) {
        return channel_.write(0u, packet->data(), packet->len(), nullptr, 0);
    }
    return ZX_OK;
}

// TODO(tkilbourn): figure out how to make sure we have the lock for accessing dispatcher_.
zx_status_t Device::SetChannel(wlan_channel_t chan) __TA_NO_THREAD_SAFETY_ANALYSIS {
    // TODO(porce): Implement == operator for wlan_channel_t, or an equality test function.

    char buf[80];
    snprintf(buf, sizeof(buf), "channel set: from %s to %s",
             common::ChanStr(state_->channel()).c_str(), common::ChanStr(chan).c_str());

    if (chan.primary == state_->channel().primary && chan.cbw == state_->channel().cbw) {
        warnf("%s suppressed\n", buf);
        return ZX_OK;
    }

    zx_status_t status = dispatcher_->PreChannelChange(chan);
    if (status != ZX_OK) {
        errorf("%s prechange failed (status %d)\n", buf, status);
        return status;
    }

    status = wlanmac_proxy_.SetChannel(0u, &chan);
    if (status != ZX_OK) {
        // TODO(porce): Revert the successful PreChannelChange()
        errorf("%s change failed (status %d)\n", buf, status);
        return status;
    }

    state_->set_channel(chan);

    status = dispatcher_->PostChannelChange();
    if (status != ZX_OK) {
        // TODO(porce): Revert the successful PreChannelChange(), wlanmac_proxy_.SetChannel(),
        // and state_->set_channel()
        errorf("%s postchange failed (status %d)\n", buf, status);
        return status;
    }

    verbosef("%s succeeded\n", buf);
    return ZX_OK;
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

zx_status_t Device::ConfigureBss(wlan_bss_config_t* cfg) {
    return wlanmac_proxy_.ConfigureBss(0u, cfg);
}

zx_status_t Device::EnableBeaconing(bool enabled) {
    return wlanmac_proxy_.EnableBeaconing(0u, enabled);
}

zx_status_t Device::ConfigureBeacon(fbl::unique_ptr<Packet> beacon) {
    ZX_DEBUG_ASSERT(beacon.get() != nullptr);
    if (beacon.get() == nullptr) { return ZX_ERR_INVALID_ARGS; }

    wlan_tx_packet_t tx_packet;
    auto status = beacon->AsWlanTxPacket(&tx_packet);
    if (status != ZX_OK) {
        errorf("error turning Beacon into wlan_tx_packet: %d\n", status);
        return status;
    }
    return wlanmac_proxy_.ConfigureBeacon(0u, &tx_packet);
}

zx_status_t Device::SetKey(wlan_key_config_t* key_config) {
    return wlanmac_proxy_.SetKey(0u, key_config);
}

fbl::RefPtr<DeviceState> Device::GetState() {
    return state_;
}

const wlanmac_info_t& Device::GetWlanInfo() const {
    return wlanmac_info_;
}

void Device::MainLoop() {
    infof("starting MainLoop\n");
    const char kThreadName[] = "wlan-mainloop";
    zx::thread::self()->set_property(ZX_PROP_NAME, kThreadName, sizeof(kThreadName));

    zx_port_packet_t pkt;
    bool running = true;
    while (running) {
        zx::time timeout = zx::deadline_after(zx::sec(30));
        zx_status_t status = port_.wait(timeout, &pkt);
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
            case to_enum_type(DevicePacket::kIndication):
                dispatcher_->HwIndication(pkt.status);
                break;
            case to_enum_type(DevicePacket::kPacketQueued): {
                fbl::unique_ptr<Packet> packet;
                {
                    std::lock_guard<std::mutex> lock(packet_queue_lock_);
                    packet = packet_queue_.Dequeue();
                    ZX_DEBUG_ASSERT(packet != nullptr);
                }
                zx_status_t status = dispatcher_->HandlePacket(fbl::move(packet));
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
                dispatcher_->HandlePortPacket(pkt.key);
                break;
            default:
                errorf("unknown port key: %" PRIu64 "\n", pkt.key);
                break;
            }
            break;
        case ZX_PKT_TYPE_SIGNAL_ONE:
            switch (ToPortKeyType(pkt.key)) {
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
    for (size_t i = 0; i < pkt.signal.count; ++i) {
        auto buffer = LargeBufferAllocator::New();
        if (buffer == nullptr) {
            errorf("no free buffers available!\n");
            // TODO: reply on the channel
            channel_.reset();
            return;
        }
        uint32_t read = 0;
        zx_status_t status =
            channel_.read(0, buffer->data(), buffer->capacity(), &read, nullptr, 0, nullptr);
        if (status == ZX_ERR_SHOULD_WAIT) {
            break;
        }
        if (status != ZX_OK) {
            if (status == ZX_ERR_PEER_CLOSED) {
                infof("channel closed\n");
            } else {
                errorf("could not read channel: %d\n", status);
            }
            channel_.reset();
            return;
        }

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
                channel_.reset();
                return;
            }
        }
    }
    RegisterChannelWaitLocked();
}

zx_status_t Device::RegisterChannelWaitLocked() {
    zx_signals_t sigs = ZX_CHANNEL_READABLE | ZX_CHANNEL_PEER_CLOSED;
    return channel_.wait_async(port_, ToPortKey(PortKeyType::kService, 0u), sigs,
                               ZX_WAIT_ASYNC_ONCE);
}

zx_status_t Device::QueueDevicePortPacket(DevicePacket id, uint32_t status) {
    debugfn();
    zx_port_packet_t pkt = {};
    pkt.key = ToPortKey(PortKeyType::kDevice, to_enum_type(id));
    pkt.type = ZX_PKT_TYPE_USER;
    pkt.status = status;
    return port_.queue(&pkt);
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

zx_status_t ValidateWlanMacInfo(const wlanmac_info& wlanmac_info) {
    for (uint8_t i = 0; i < wlanmac_info.ifc_info.num_bands; i++) {
        auto bandinfo = wlanmac_info.ifc_info.bands[i];

        // Validate channels
        auto& supported_channels = bandinfo.supported_channels;
        switch (supported_channels.base_freq) {
        case common::kBaseFreq5Ghz:
            for (auto c : supported_channels.channels) {
                if (c == 0) {  // End of the valid channel
                    break;
                }
                auto chan = wlan_channel_t{.primary = c, .cbw = CBW20};
                if (!common::IsValidChan5Ghz(chan)) {
                    errorf("wlanmac band info for %u MHz has invalid channel %u\n",
                           supported_channels.base_freq, c);
                    errorf("wlanmac info: %s\n", debug::DescribeWlanMacInfo(wlanmac_info).c_str());
                    return ZX_ERR_NOT_SUPPORTED;
                }
            }
            break;
        case common::kBaseFreq2Ghz:
            for (auto c : supported_channels.channels) {
                if (c == 0) {  // End of the valid channel
                    break;
                }
                auto chan = wlan_channel_t{.primary = c, .cbw = CBW20};
                if (!common::IsValidChan2Ghz(chan)) {
                    errorf("wlanmac band info for %u MHz has invalid cahnnel %u\n",
                           supported_channels.base_freq, c);
                    errorf("wlanmac info: %s\n", debug::DescribeWlanMacInfo(wlanmac_info).c_str());
                    return ZX_ERR_NOT_SUPPORTED;
                }
            }
            break;
        default:
            errorf("wlanmac band info for %u MHz not supported\n", supported_channels.base_freq);
            errorf("wlanmac info: %s\n", debug::DescribeWlanMacInfo(wlanmac_info).c_str());
            return ZX_ERR_NOT_SUPPORTED;
        }
    }
    // Add more sanity check here

    return ZX_OK;
}

}  // namespace wlan
