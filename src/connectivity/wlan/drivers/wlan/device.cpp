// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "device.h"

#include <ddk/device.h>
#include <lib/zx/thread.h>
#include <lib/zx/time.h>
#include <wlan/common/channel.h>
#include <wlan/common/logging.h>
#include <wlan/mlme/ap/ap_mlme.h>
#include <wlan/mlme/client/bss.h>
#include <wlan/mlme/client/client_mlme.h>
#include <wlan/mlme/debug.h>
#include <wlan/mlme/mesh/mesh_mlme.h>
#include <wlan/mlme/service.h>
#include <wlan/mlme/timer.h>
#include <wlan/mlme/timer_manager.h>
#include <wlan/mlme/validate_frame.h>
#include <wlan/mlme/wlan.h>
#include <wlan/protocol/ioctl.h>
#include <zircon/assert.h>
#include <zircon/compiler.h>
#include <zircon/status.h>
#include <zircon/syscalls.h>
#include <zircon/syscalls/port.h>

#include <cinttypes>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>
#include <utility>

#include "probe_sequence.h"

namespace wlan {

namespace wlan_minstrel = ::fuchsia::wlan::minstrel;

// Remedy for FLK-24 (DNO-389)
// See |MINSTREL_DATA_FRAME_INTERVAL_NANOS| in //garnet/bin/wlan-hw-sim/src/main.rs
// For the test, ensure at least one probe frame (generated every 16 data frames) in every cycle,
// 16 <= (kMinstrelUpdateIntervalForHwSim / MINSTREL_DATA_FRAME_INTERVAL_NANOS * 1e6) < 32.
static constexpr zx::duration kMinstrelUpdateIntervalForHwSim = zx::msec(83);
static constexpr zx::duration kMinstrelUpdateIntervalNormal = zx::msec(100);

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
    .report_tx_status =
        [](void* cookie, const wlan_tx_status_t* tx_status) {
            DEV(cookie)->WlanmacReportTxStatus(tx_status);
        },
    .hw_scan_complete =
        [](void* cookie, const wlan_hw_scan_result_t* result) {
            DEV(cookie)->WlanmacHwScanComplete(result);
        },
};

static ethmac_protocol_ops_t ethmac_ops = {
    .query = [](void* ctx, uint32_t options, ethmac_info_t* info) -> zx_status_t {
        return DEV(ctx)->EthmacQuery(options, info);
    },
    .stop = [](void* ctx) { DEV(ctx)->EthmacStop(); },
    .start = [](void* ctx, const ethmac_ifc_protocol_t* ifc) -> zx_status_t {
        return DEV(ctx)->EthmacStart(ifc);
    },
    .queue_tx = [](void* ctx, uint32_t options, ethmac_netbuf_t* netbuf) -> zx_status_t {
        return DEV(ctx)->EthmacQueueTx(options, netbuf);
    },
    .set_param = [](void* ctx, uint32_t param, int32_t value, const void* data, size_t data_size)
        -> zx_status_t { return DEV(ctx)->EthmacSetParam(param, value, data, data_size); },
};
#undef DEV

Device::Device(zx_device_t* device, wlanmac_protocol_t wlanmac_proto,
               std::shared_ptr<component::Services> services)
    : parent_(device),
      wlanmac_proxy_(wlanmac_proto),
      services_(services),
      fidl_msg_buf_(ZX_CHANNEL_MAX_MSG_BYTES),
      timer_scheduler_(this) {
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

    status = wlanmac_proxy_.Start(&wlanmac_ifc_ops, this);
    if (status != ZX_OK) {
        errorf("failed to start wlanmac device: %s\n", zx_status_get_string(status));
        return status;
    }

    state_->set_address(common::MacAddr(wlanmac_info_.ifc_info.mac_addr));

    fbl::unique_ptr<Mlme> mlme;

    // mac_role is a bitfield, but only a single value is supported for an interface
    switch (wlanmac_info_.ifc_info.mac_role) {
    case WLAN_MAC_ROLE_CLIENT:
        infof("Initialize a client MLME.\n");
        mlme.reset(new ClientMlme(this));
        break;
    case WLAN_MAC_ROLE_AP:
        infof("Initialize an AP MLME.\n");
        mlme.reset(new ApMlme(this));
        break;
    case WLAN_MAC_ROLE_MESH:
        infof("Initialize a mesh MLME.\n");
        mlme.reset(new MeshMlme(this));
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
    if (ShouldEnableMinstrel()) {
        zx_status_t status = CreateMinstrel(wlanmac_info_.ifc_info.driver_features);
        if (ZX_OK == status) { debugmstl("Minstrel Manager created successfully.\n"); }
    }

    work_thread_ = std::thread(&Device::MainLoop, this);

    if (wlanmac_info_.ifc_info.driver_features & WLAN_DRIVER_FEATURE_TEMP_DIRECT_SME_CHANNEL) {
        infof("iface supports SME channel; not adding wlanif device\n");
        status = AddEthDevice(parent_);
        if (status != ZX_OK) {
            errorf("could not add eth device: %s\n", zx_status_get_string(status));
        }
    } else {
        status = AddWlanDevice();
        if (status == ZX_OK) {
            status = AddEthDevice(zxdev_);
            if (status != ZX_OK) {
                // Remove the wlan device.
                device_remove(zxdev_);
                errorf("could not add eth device: %s\n", zx_status_get_string(status));
            }
        } else {
            errorf("could not add wlan device: %s\n", zx_status_get_string(status));
        }
    }

    // Clean up if either device add failed.
    if (status != ZX_OK) {
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

zx_status_t Device::AddWlanDevice() {
    device_add_args_t args = {};
    args.version = DEVICE_ADD_ARGS_VERSION;
    args.name = "wlan";
    args.ctx = this;
    args.ops = &wlan_device_ops;
    args.proto_id = ZX_PROTOCOL_WLANIF;
    return device_add(parent_, &args, &zxdev_);
}

zx_status_t Device::AddEthDevice(zx_device* parent) {
    device_add_args_t args = {};
    args.version = DEVICE_ADD_ARGS_VERSION;
    args.name = "wlan-ethernet";
    args.ctx = this;
    args.ops = &eth_device_ops;
    args.proto_id = ZX_PROTOCOL_ETHMAC;
    args.proto_ops = &ethmac_ops;
    return device_add(parent, &args, &ethdev_);
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
    bool was_empty = packet_queue_.is_empty();

    if (was_empty) {
        zx_status_t status = QueueDevicePortPacket(DevicePacket::kPacketQueued);
        if (status != ZX_OK) {
            errorf("could not send packet queued msg err=%d\n", status);
            return status;
        }
    }
    packet_queue_.Enqueue(std::move(packet));

    return ZX_OK;
}

void Device::DestroySelf() {
    if (work_thread_.joinable()) { work_thread_.join(); }
    delete this;
}

void Device::ShutdownMainLoop() {
    std::lock_guard<std::mutex> lock(lock_);
    if (dead_) { return; }
    channel_.reset();
    dead_ = true;
    if (port_.is_valid()) {
        zx_status_t status = QueueDevicePortPacket(DevicePacket::kShutdown);
        if (status != ZX_OK) {
            ZX_PANIC("wlan: could not send shutdown loop message: %d\n", status);
        }
    }
}

void Device::WlanUnbind() {
    debugfn();
    ShutdownMainLoop();
    device_remove(zxdev_);
}

void Device::WlanRelease() {
    debugfn();
    DestroySelf();
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

// ddk ethmac_protocol_ops methods

void Device::EthUnbind() {
    debugfn();
    ShutdownMainLoop();
    device_remove(ethdev_);
}

void Device::EthRelease() {
    debugfn();
    // If no wlanif device was added we need to clean-up memory here. Otherwise WlanRelease() will
    // do the clean-up as |ethdev_| is a child of |zxdev_|.
    if (wlanmac_info_.ifc_info.driver_features & WLAN_DRIVER_FEATURE_TEMP_DIRECT_SME_CHANNEL) {
        DestroySelf();
    }
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
    info->netbuf_size = sizeof(ethmac_netbuf_t);

    return ZX_OK;
}

zx_status_t Device::EthmacStart(const ethmac_ifc_protocol_t* ifc) {
    debugfn();
    ZX_DEBUG_ASSERT(ifc != nullptr);

    std::lock_guard<std::mutex> lock(lock_);
    if (ethmac_proxy_ != nullptr) { return ZX_ERR_ALREADY_BOUND; }
    ethmac_proxy_.reset(new ddk::EthmacIfcProtocolClient(ifc));
    return ZX_OK;
}

void Device::EthmacStop() {
    debugfn();

    std::lock_guard<std::mutex> lock(lock_);
    if (ethmac_proxy_ == nullptr) { warnf("ethmac not started\n"); }
    std::lock_guard<std::mutex> guard(packet_queue_lock_);
    PacketQueue queued_packets = packet_queue_.Drain();
    while (!queued_packets.is_empty()) {
        auto packet = queued_packets.Dequeue();
        if (packet->peer() == Packet::Peer::kEthernet) {
            auto netbuf = packet->ext_data();
            ZX_DEBUG_ASSERT(netbuf != nullptr);
            ZX_DEBUG_ASSERT(ethmac_proxy_ != nullptr);
            if (netbuf != nullptr && ethmac_proxy_ != nullptr) {
                ethmac_proxy_->CompleteTx(netbuf, ZX_ERR_CANCELED);
            }
            // Outgoing ethernet frames are dropped.
        } else {
            // Incoming WLAN frames are preserved.
            packet_queue_.Enqueue(std::move(packet));
        }
    }
    ethmac_proxy_.reset();
}

zx_status_t Device::EthmacQueueTx(uint32_t options, ethmac_netbuf_t* netbuf) {
    // no debugfn() because it's too noisy
    auto packet = PreparePacket(netbuf->data_buffer, netbuf->data_size, Packet::Peer::kEthernet);
    if (packet == nullptr) {
        warnf("could not prepare Ethernet packet with len %zu\n", netbuf->data_size);
        return ZX_ERR_NO_RESOURCES;
    }
    packet->set_ext_data(netbuf, 0);
    zx_status_t status = QueuePacket(std::move(packet));
    if (status != ZX_OK) {
        warnf("could not queue Ethernet packet err=%s\n", zx_status_get_string(status));
        ZX_DEBUG_ASSERT(status != ZX_ERR_SHOULD_WAIT);
        return status;
    }
    return ZX_ERR_SHOULD_WAIT;
}

zx_status_t Device::EthmacSetParam(uint32_t param, int32_t value, const void* data,
                                   size_t data_size) {
    debugfn();

    zx_status_t status = ZX_ERR_NOT_SUPPORTED;

    switch (param) {
    case ETHMAC_SETPARAM_PROMISC:
        // See NET-1808: In short, the bridge mode doesn't require WLAN promiscuous mode enabled.
        //               So we give a warning and return OK here to continue the bridging.
        // TODO(NET-1930): To implement the real promiscuous mode.
        if (value == 1) {  // Only warn when enabling.
            warnf("WLAN promiscuous not supported yet. see NET-1930\n");
        }
        status = ZX_OK;
        break;
    }

    return status;
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
        warnf("could not queue inbound packet with len %zu err=%s\n", length,
              zx_status_get_string(status));
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

void Device::WlanmacReportTxStatus(const wlan_tx_status_t* tx_status) {
    ZX_DEBUG_ASSERT(minstrel_ != nullptr);
    if (minstrel_ != nullptr) { minstrel_->HandleTxStatusReport(*tx_status); }
}

void Device::WlanmacHwScanComplete(const wlan_hw_scan_result_t* result) {
    debugf("WlanmacHwScanComplete %u\n", result->code);
    auto status = QueueDevicePortPacket(DevicePacket::kHwScanComplete, result->code);
    if (status != ZX_OK) { errorf("could not queue hw scan complete packet err=%d\n", status); }
}

zx_status_t Device::GetTimer(uint64_t id, fbl::unique_ptr<Timer>* timer) {
    ZX_DEBUG_ASSERT(timer != nullptr);
    ZX_DEBUG_ASSERT(timer->get() == nullptr);
    ZX_DEBUG_ASSERT(port_.is_valid());

    zx::timer t;
    zx_status_t status = zx::timer::create(ZX_TIMER_SLACK_LATE, ZX_CLOCK_MONOTONIC, &t);
    if (status != ZX_OK) { return status; }

    timer->reset(new SystemTimer(&timer_scheduler_, id, std::move(t)));

    return ZX_OK;
}

zx_status_t Device::DeliverEthernet(Span<const uint8_t> eth_frame) {
    if (eth_frame.size() > ETH_FRAME_MAX_SIZE) {
        errorf("Attempted to deliver an ethernet frame of invalid length: %zu\n", eth_frame.size());
        return ZX_ERR_INVALID_ARGS;
    }

    if (ethmac_proxy_ != nullptr) { ethmac_proxy_->Recv(eth_frame.data(), eth_frame.size(), 0u); }
    return ZX_OK;
}

TxVector GetTxVector(const fbl::unique_ptr<MinstrelRateSelector>& minstrel,
                     const fbl::unique_ptr<Packet>& packet, uint32_t flags) {
    const auto fc = packet->field<FrameControl>(0);

    constexpr size_t kAddr1Offset = 4;
    ZX_DEBUG_ASSERT(packet->len() >= kAddr1Offset + common::kMacAddrLen);
    auto peer_addr = common::MacAddr(packet->field<uint8_t>(kAddr1Offset));

    if (minstrel != nullptr) {
        tx_vec_idx_t idx = minstrel->GetTxVectorIdx(*fc, peer_addr, flags);
        TxVector tv;
        zx_status_t status = TxVector::FromIdx(idx, &tv);
        ZX_DEBUG_ASSERT(status == ZX_OK);
        return tv;
    } else {
        // Note: This section has no practical effect on ralink and ath10k. It is only effective if
        // the underlying device meets both criteria below:
        // 1. Does not support tx status report. i.e. WLAN_DRIVER_FEATURE_TX_STATUS_REPORT NOT set
        // 2. Hornors our instruction on tx_vector to use.
        // TODO(NET-645): Choose an optimal MCS for management frames
        const uint8_t mcs = fc->type() == FrameType::kData ? 7 : 3;
        return {
            .phy = WLAN_PHY_ERP,
            .cbw = CBW20,
            .mcs_idx = mcs,
            .nss = 1,
            .gi = WLAN_GI_800NS,
        };
    }
}

wlan_tx_info_t MakeTxInfo(const fbl::unique_ptr<Packet>& packet, const TxVector& tv,
                          bool has_minstrel, uint32_t flags) {
    tx_vec_idx_t idx;
    zx_status_t status = tv.ToIdx(&idx);
    ZX_DEBUG_ASSERT(status == ZX_OK);

    uint32_t valid_fields =
        WLAN_TX_INFO_VALID_PHY | WLAN_TX_INFO_VALID_CHAN_WIDTH | WLAN_TX_INFO_VALID_MCS;
    if (has_minstrel) { valid_fields |= WLAN_TX_INFO_VALID_TX_VECTOR_IDX; }

    const auto fc = packet->field<FrameControl>(0);
    if (fc->protected_frame()) { flags |= WLAN_TX_INFO_FLAGS_PROTECTED; }

    return {
        .phy = static_cast<uint16_t>(tv.phy),
        .cbw = static_cast<uint8_t>(tv.cbw),
        .mcs = tv.mcs_idx,
        .tx_vector_idx = idx,
        .valid_fields = valid_fields,
        .tx_flags = flags,
    };
}

zx_status_t Device::SendWlan(fbl::unique_ptr<Packet> packet, uint32_t flags) {
    ZX_DEBUG_ASSERT(packet->len() <= std::numeric_limits<uint16_t>::max());
    ZX_DEBUG_ASSERT(ValidateFrame("Transmitting a malformed frame", *packet));

    auto tv = GetTxVector(minstrel_, packet, flags);
    packet->CopyCtrlFrom(MakeTxInfo(packet, tv, minstrel_ != nullptr, flags));
    wlan_tx_packet_t tx_pkt = packet->AsWlanTxPacket();
    auto status = wlanmac_proxy_.QueueTx(0u, &tx_pkt);
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
zx_status_t Device::SendService(Span<const uint8_t> span) __TA_NO_THREAD_SAFETY_ANALYSIS {
    if (channel_.is_valid()) { return channel_.write(0u, span.data(), span.size(), nullptr, 0); }
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

    zx_status_t status = wlanmac_proxy_.SetChannel(0u, &chan);
    if (status != ZX_OK) {
        errorf("%s change failed (status %d)\n", buf, status);
        return status;
    }

    state_->set_channel(chan);

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
    state_->set_online(status == ETHMAC_STATUS_ONLINE);
    if (ethmac_proxy_ != nullptr) { ethmac_proxy_->Status(status); }
}

zx_status_t Device::ConfigureBss(wlan_bss_config_t* cfg) {
    return wlanmac_proxy_.ConfigureBss(0u, cfg);
}

zx_status_t Device::EnableBeaconing(wlan_bcn_config_t* bcn_cfg) {
    if (bcn_cfg != nullptr) {
        ZX_DEBUG_ASSERT(
            ValidateFrame("Malformed beacon template",
                          {reinterpret_cast<const uint8_t*>(bcn_cfg->tmpl.packet_head.data_buffer),
                           bcn_cfg->tmpl.packet_head.data_size}));
    }
    return wlanmac_proxy_.EnableBeaconing(0u, bcn_cfg);
}

zx_status_t Device::ConfigureBeacon(fbl::unique_ptr<Packet> beacon) {
    ZX_DEBUG_ASSERT(beacon.get() != nullptr);
    if (beacon.get() == nullptr) { return ZX_ERR_INVALID_ARGS; }

    ZX_DEBUG_ASSERT(ValidateFrame("Malformed beacon template", *beacon));

    wlan_tx_packet_t tx_packet = beacon->AsWlanTxPacket();
    return wlanmac_proxy_.ConfigureBeacon(0u, &tx_packet);
}

zx_status_t Device::SetKey(wlan_key_config_t* key_config) {
    return wlanmac_proxy_.SetKey(0u, key_config);
}

zx_status_t Device::StartHwScan(const wlan_hw_scan_config_t* scan_config) {
    return wlanmac_proxy_.StartHwScan(scan_config);
}

zx_status_t Device::ConfigureAssoc(wlan_assoc_ctx_t* assoc_ctx) {
    ZX_DEBUG_ASSERT(assoc_ctx != nullptr);
    // TODO(NET-1385): Minstrel only supports client mode. Add AP mode support later.
    AddMinstrelPeer(*assoc_ctx);
    return wlanmac_proxy_.ConfigureAssoc(0u, assoc_ctx);
}

zx_status_t Device::ClearAssoc(const wlan::common::MacAddr& peer_addr) {
    if (minstrel_ != nullptr) { minstrel_->RemovePeer(peer_addr); }

    uint8_t mac[wlan::common::kMacAddrLen];
    peer_addr.CopyTo(mac);
    return wlanmac_proxy_.ClearAssoc(0u, mac);
}

fbl::RefPtr<DeviceState> Device::GetState() {
    return state_;
}

const wlanmac_info_t& Device::GetWlanInfo() const {
    return wlanmac_info_;
}

zx_status_t Device::GetMinstrelPeers(wlan_minstrel::Peers* peers_fidl) {
    if (minstrel_ == nullptr) { return ZX_ERR_NOT_SUPPORTED; }
    return minstrel_->GetListToFidl(peers_fidl);
}

zx_status_t Device::GetMinstrelStats(const common::MacAddr& addr, wlan_minstrel::Peer* peer_fidl) {
    if (minstrel_ == nullptr) { return ZX_ERR_NOT_SUPPORTED; }
    return minstrel_->GetStatsToFidl(addr, peer_fidl);
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
            case to_enum_type(DevicePacket::kHwScanComplete):
                dispatcher_->HwScanComplete(pkt.status);
                break;
            case to_enum_type(DevicePacket::kPacketQueued): {
                PacketQueue queued_packets{};
                {
                    std::lock_guard<std::mutex> guard(packet_queue_lock_);
                    queued_packets = packet_queue_.Drain();
                }
                while (!queued_packets.is_empty()) {
                    auto packet = queued_packets.Dequeue();
                    ZX_DEBUG_ASSERT(packet != nullptr);
                    ethmac_netbuf_t* netbuf = nullptr;
                    auto peer = packet->peer();
                    if (peer == Packet::Peer::kEthernet) {
                        // ethernet driver somehow decided to send frame after itself is stopped,
                        // drop them as we cannot return the netbuf via CompleteTx.
                        if (ethmac_proxy_ == nullptr) { continue; }
                        netbuf = packet->ext_data();
                        ZX_ASSERT(netbuf != nullptr);
                    }
                    zx_status_t status = dispatcher_->HandlePacket(std::move(packet));
                    if (peer == Packet::Peer::kEthernet) {
                        ethmac_proxy_->CompleteTx(netbuf, status);
                    }
                }
                break;
            }
            default:
                errorf("unknown device port key subtype: %" PRIu64 "\n", pkt.user.u64[0]);
                break;
            }
            break;
        case ZX_PKT_TYPE_SIGNAL_ONE:
            switch (ToPortKeyType(pkt.key)) {
            case PortKeyType::kMlme:
                dispatcher_->HandlePortPacket(pkt.key);
                break;
            case PortKeyType::kDevice: {
                ZX_DEBUG_ASSERT(minstrel_ != nullptr);
                minstrel_->HandleTimeout();
                break;
            }
            case PortKeyType::kService:
                ProcessChannelPacketLocked(pkt.signal.count);
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

void Device::ProcessChannelPacketLocked(uint64_t signal_count) {
    if (!channel_.is_valid()) {
        // It could be that we closed the channel (e.g., in WlanUnbind()) but there is still
        // a pending packet in the port that indicates read availability on that channel.
        // In that case we simply ignore the packet since we can't read from the channel anyway.
        return;
    }

    for (size_t i = 0; i < signal_count; ++i) {
        uint32_t read = 0;
        zx_status_t status = channel_.read(0, fidl_msg_buf_.data(), fidl_msg_buf_.size(), &read,
                                           nullptr, 0, nullptr);
        if (status == ZX_ERR_SHOULD_WAIT) { break; }
        if (status != ZX_OK) {
            if (status == ZX_ERR_PEER_CLOSED) {
                infof("channel closed\n");
            } else {
                errorf("could not read channel: %s\n", zx_status_get_string(status));
            }
            channel_.reset();
            return;
        }

        status = dispatcher_->HandleAnyMlmeMessage({fidl_msg_buf_.data(), read});
        if (status != ZX_OK) {
            errorf("Could not handle service packet: %s\n", zx_status_get_string(status));
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
    if (!port_.is_valid()) { return ZX_ERR_BAD_STATE; }
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
                    errorf("wlanmac info: %s\n", debug::Describe(wlanmac_info).c_str());
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
                    errorf("wlanmac info: %s\n", debug::Describe(wlanmac_info).c_str());
                    return ZX_ERR_NOT_SUPPORTED;
                }
            }
            break;
        default:
            errorf("wlanmac band info for %u MHz not supported\n", supported_channels.base_freq);
            errorf("wlanmac info: %s\n", debug::Describe(wlanmac_info).c_str());
            return ZX_ERR_NOT_SUPPORTED;
        }
    }
    // Add more sanity check here

    return ZX_OK;
}

bool Device::ShouldEnableMinstrel() {
    const auto& info = wlanmac_info_.ifc_info;
    return (info.driver_features & WLAN_DRIVER_FEATURE_TX_STATUS_REPORT) &&
           !(info.driver_features & WLAN_DRIVER_FEATURE_RATE_SELECTION);
}

zx_status_t Device::CreateMinstrel(uint32_t features) {
    debugfn();
    fbl::unique_ptr<Timer> timer;
    ObjectId timer_id;
    timer_id.set_subtype(to_enum_type(ObjectSubtype::kTimer));
    timer_id.set_target(to_enum_type(ObjectTarget::kMinstrel));
    auto status = GetTimer(ToPortKey(PortKeyType::kDevice, timer_id.val()), &timer);
    if (status != ZX_OK) {
        errorf("could not create minstrel timer: %d\n", status);
        return status;
    }
    const zx::duration minstrel_update_interval = (features & WLAN_DRIVER_FEATURE_SYNTH) != 0
                                                      ? kMinstrelUpdateIntervalForHwSim
                                                      : kMinstrelUpdateIntervalNormal;
    minstrel_.reset(new MinstrelRateSelector(std::move(timer), ProbeSequence::RandomSequence(),
                                             minstrel_update_interval));
    return ZX_OK;
}

void Device::AddMinstrelPeer(const wlan_assoc_ctx_t& assoc_ctx) {
    if (minstrel_ == nullptr) { return; }
    minstrel_->AddPeer(assoc_ctx);
}

zx_status_t Device::TimerSchedulerImpl::Schedule(Timer* timer, zx::time deadline) {
    auto sys_timer = static_cast<SystemTimer*>(timer);
    zx_status_t status = sys_timer->inner()->set(deadline, zx::nsec(0));
    if (status != ZX_OK) { return status; }
    return sys_timer->inner()->wait_async(device_->port_, sys_timer->id(), ZX_TIMER_SIGNALED,
                                          ZX_WAIT_ASYNC_ONCE);
}

zx_status_t Device::TimerSchedulerImpl::Cancel(Timer* timer) {
    auto sys_timer = static_cast<SystemTimer*>(timer);
    return sys_timer->inner()->cancel();
}

}  // namespace wlan
