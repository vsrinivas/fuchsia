// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "endpoint.h"

#include <fcntl.h>
#include <fidl/fuchsia.hardware.ethertap/cpp/wire.h>
#include <fuchsia/netemul/internal/cpp/fidl.h>
#include <lib/fdio/directory.h>
#include <lib/fdio/fd.h>
#include <lib/fdio/fdio.h>
#include <zircon/status.h>
#include <zircon/types.h>

#include <random>
#include <unordered_set>
#include <vector>

#include "ethernet_client.h"
#include "ethertap_client.h"
#include "network_context.h"
#include "src/lib/fostr/hex_dump.h"
#include "src/lib/fxl/strings/join_strings.h"

namespace netemul {

namespace impl {

static const char kNetworkDeviceFakeTopoPathRoot[] = "/netemul";

class EndpointImpl : public data::Consumer {
 public:
  explicit EndpointImpl(Endpoint::Config config)
      : config_(std::move(config)), weak_ptr_factory_(this) {}

  decltype(auto) GetPointer() { return weak_ptr_factory_.GetWeakPtr(); }

  void CloneConfig(Endpoint::Config* config) { config_.Clone(config); }

  const Endpoint::Config& config() const { return config_; }

  std::vector<data::BusConsumer::Ptr>& sinks() { return sinks_; }

  void SetClosedCallback(fit::closure cb) { closed_callback_ = std::move(cb); }

  virtual zx_status_t Setup(const std::string& name, bool start_online,
                            const NetworkContext& context, size_t id) = 0;

  virtual void SetLinkUp(bool up, fit::callback<void()> done) = 0;

  virtual fuchsia::netemul::network::DeviceConnection GetDevice() = 0;

  virtual void ServeDevice(zx::channel req) = 0;

  static fuchsia::net::MacAddress RandomMac(const std::string& str_seed) {
    fuchsia::net::MacAddress ret;
    std::vector<uint8_t> sseed(str_seed.begin(), str_seed.end());
    std::seed_seq seed(sseed.begin(), sseed.end());
    std::independent_bits_engine<std::default_random_engine, CHAR_BIT, uint8_t> rnd(seed);
    std::generate(ret.octets.begin(), ret.octets.end(), rnd);
    // Force mac to be unicast (bit 0 is 0) and locally administered (bit 1 is 1).
    ret.octets[0] = (ret.octets[0] & 0xFC) | (0x02);
    return ret;
  }

 protected:
  void Closed() {
    if (closed_callback_) {
      closed_callback_();
    }
  }

  void ForwardData(const void* data, size_t len) {
    auto self = weak_ptr_factory_.GetWeakPtr();
    for (auto i = sinks_.begin(); i != sinks_.end();) {
      if (*i) {
        (*i)->Consume(data, len, self);
        ++i;
      } else {
        // The sink was freed, remove it from the list.
        i = sinks_.erase(i);
      }
    }
  }

 private:
  Endpoint::Config config_;
  std::vector<data::BusConsumer::Ptr> sinks_;
  fxl::WeakPtrFactory<data::Consumer> weak_ptr_factory_;
  fit::callback<void()> closed_callback_;
};

class EthertapImpl : public EndpointImpl {
 public:
  explicit EthertapImpl(Endpoint::Config config) : EndpointImpl(std::move(config)) {}

  zx_status_t Setup(const std::string& name, bool start_online, const NetworkContext& context,
                    size_t id) override {
    EthertapConfig tap_cfg;
    if (config().mac) {
      tap_cfg.tap_cfg.mac.octets = config().mac->octets;
    } else {
      // if mac is not provided, random mac is assigned with a seed based on
      // name
      tap_cfg.RandomLocalUnicast(name);
    }

    tap_cfg.name = name;
    // Because device cleanup is asynchronous for ethertap devices, it's
    // possible that an existing device with this name has been removed by the
    // client but not yet fully cleaned up. This would lead to a name collision.
    //
    // To avoid this issue, append a unique integer ID to the endpoint name
    // (overwriting the end of the name if necessary to avoid surpassing the
    // maximum ethertap interface name length).
    AppendSuffix(tap_cfg.name, id);
    tap_cfg.tap_cfg.mtu = config().mtu;
    fuchsia::hardware::ethernet::MacAddress mac;
    tap_cfg.tap_cfg.mac.Clone(&mac);
    tap_cfg.tap_cfg.options = fuchsia::hardware::ethertap::OPT_REPORT_PARAM;
    if (start_online) {
      tap_cfg.tap_cfg.options |= fuchsia::hardware::ethertap::OPT_ONLINE;
    }
    tap_cfg.devfs_root = context.ConnectDevfs();
    zx_status_t status = EthertapClient::Create(std::move(tap_cfg), &ethertap_);
    if (status != ZX_OK) {
      return status;
    }

    auto devfs_root = context.ConnectDevfs();
    if (devfs_root.is_valid()) {
      ethernet_factory_ = std::make_unique<EthernetClientFactory>(
          EthernetClientFactory::kDevfsEthernetRoot, std::move(devfs_root));
    } else {
      ethernet_factory_ = std::make_unique<EthernetClientFactory>();
    }

    // We add a 2 min timeout to this so that it's easier to debug problems with enumerating devices
    // from devfs. By default, MountPointWithMAC would hang forever and would make debugging
    // problems here really hard.
    ethernet_mount_path_ = ethernet_factory_->MountPointWithMAC(mac, zx::sec(120));

    // can't find mount path for ethernet!!
    if (ethernet_mount_path_.empty()) {
      fprintf(stderr, "Failed to locate ethertap device %s %02X:%02X:%02X:%02X:%02X:%02X\n",
              name.c_str(), mac.octets[0], mac.octets[1], mac.octets[2], mac.octets[3],
              mac.octets[4], mac.octets[5]);
      return ZX_ERR_INTERNAL;
    }

    ethertap_->SetPacketCallback(
        [this](std::vector<uint8_t> data) { ForwardData(data.data(), data.size()); });

    ethertap_->SetPeerClosedCallback([this]() { Closed(); });
    return ZX_OK;
  }

  void SetLinkUp(bool up, fit::callback<void()> done) override {
    ethertap_->SetLinkUp(up);
    done();
  }

  fuchsia::netemul::network::DeviceConnection GetDevice() override {
    fidl::InterfaceHandle<fuchsia::hardware::ethernet::Device> ret;
    if (ethernet_factory_) {
      if (ethernet_factory_->Connect(ethernet_mount_path_, ret.NewRequest()) != ZX_OK) {
        // if it didn't succeed, release the request and reset:
        ret = nullptr;
      }
    }
    return fuchsia::netemul::network::DeviceConnection::WithEthernet(std::move(ret));
  }

  void ServeDevice(zx::channel channel) override {
    if (ethernet_factory_) {
      ethernet_factory_->Connect(
          ethernet_mount_path_,
          fidl::InterfaceRequest<fuchsia::hardware::ethernet::Device>(std::move(channel)));
    }
  }

  void Consume(const void* data, size_t len) override {
    auto status = ethertap_->Send(data, len);
    if (status != ZX_OK) {
      fprintf(stderr, "ethertap couldn't push data: %s\n", zx_status_get_string(status));
    }
  }

 private:
  static void AppendSuffix(std::string& name, size_t id) {
    std::string suffix = std::to_string(id);
    if (name.size() <= fuchsia_hardware_ethertap::wire::kMaxNameLength - suffix.size()) {
      name.append(suffix);
    } else {
      // The endpoint name is too long to append the entire suffix without
      // overflowing the maximum ethertap name length. Overwrite the end of the
      // endpoint name and then append any remaining characters in the suffix.
      auto it = suffix.begin();
      for (size_t i = fuchsia_hardware_ethertap::wire::kMaxNameLength - suffix.size();
           i < name.size(); i++) {
        name[i] = *it;
        it++;
      }
      name.append(it, suffix.end());
    }
  }

  std::string ethernet_mount_path_;
  std::unique_ptr<EthernetClientFactory> ethernet_factory_;
  std::unique_ptr<EthertapClient> ethertap_;
};

class NetworkDeviceImpl : public EndpointImpl,
                          public fuchsia::netemul::internal::NetworkDeviceInstance {
 public:
  explicit NetworkDeviceImpl(Endpoint::Config config) : EndpointImpl(std::move(config)) {}

  zx_status_t Setup(const std::string& name, bool start_online, const NetworkContext& context,
                    /* unused by NetworkDevice endpoints */ size_t it) override {
    device_name_ = name;

    auto tun_ctl = context.ConnectNetworkTun().BindSync();
    if (!tun_ctl.is_bound()) {
      return ZX_ERR_INTERNAL;
    }

    fuchsia::net::tun::DeviceConfig tun_config;
    tun_config.set_blocking(true);
    zx_status_t status = tun_ctl->CreateDevice(std::move(tun_config), tun_device_.NewRequest());
    if (status != ZX_OK) {
      return status;
    }
    tun_device_.set_error_handler([this](zx_status_t err) { Closed(); });

    fuchsia::net::tun::DevicePortConfig port_config;
    port_config.mutable_base()->set_id(Endpoint::kPortId);
    port_config.mutable_base()->set_mtu(config().mtu);
    port_config.mutable_base()->set_rx_types({Endpoint::kFrameType});
    port_config.mutable_base()->set_tx_types({fuchsia::hardware::network::FrameTypeSupport{
        .type = Endpoint::kFrameType,
        .features = fuchsia::hardware::network::FRAME_FEATURES_RAW,
        .supported_flags = fuchsia::hardware::network::TxFlags()}});
    if (config().mac) {
      status = config().mac->Clone(port_config.mutable_mac());
      if (status != ZX_OK) {
        return status;
      }
    } else {
      port_config.set_mac(RandomMac(name));
    }
    port_config.set_online(start_online);
    tun_device_->AddPort(std::move(port_config), tun_port_.NewRequest());

    ListenForFrames();
    return ZX_OK;
  }

  void SetLinkUp(bool up, fit::callback<void()> done) override {
    tun_port_->SetOnline(up, [cb = std::move(done)]() mutable { cb(); });
  }

  fuchsia::netemul::network::DeviceConnection GetDevice() override {
    fidl::InterfaceHandle<fuchsia::hardware::network::DeviceInstance> dev;
    ServeDevice(dev.NewRequest().TakeChannel());
    return fuchsia::netemul::network::DeviceConnection::WithNetworkDevice(std::move(dev));
  }

  void ServeDevice(zx::channel req) override {
    instance_bindings_.AddBinding(
        this,
        fidl::InterfaceRequest<fuchsia::netemul::internal::NetworkDeviceInstance>(std::move(req)));
  }

  void Consume(const void* data, size_t len) override {
    if (!tun_device_.is_bound()) {
      return;
    }
    auto* p = static_cast<const uint8_t*>(data);
    fuchsia::net::tun::Frame frame;
    frame.set_port(Endpoint::kPortId);
    frame.set_frame_type(fuchsia::hardware::network::FrameType::ETHERNET);
    frame.set_data(std::vector<uint8_t>(p, p + len));
    // Copy some of the data so we get decent errors if the packet is dropped.
    // Pick only the first few bytes that will contain important headers to help
    // debug packet drops.
    constexpr size_t kMaxSaveBytes = 64;
    std::vector<uint8_t> partial_data(p, p + std::min(len, kMaxSaveBytes));
    tun_device_->WriteFrame(std::move(frame), [this, data = std::move(partial_data),
                                               len](fpromise::result<void, zx_status_t> status) {
      if (status.is_error()) {
        FX_LOGS(WARNING) << "Failed to send " << len << "-byte frame to network device '"
                         << device_name_ << "': " << zx_status_get_string(status.error()) << " "
                         << fostr::HexDump(data.data(), data.size(), 0);
      }
    });
  }

  /* fuchsia.hardware.network/DeviceInstance */

  void GetDevice(::fidl::InterfaceRequest<fuchsia::hardware::network::Device> device) override {
    if (!tun_device_.is_bound()) {
      return;
    }
    tun_device_->GetDevice(std::move(device));
  }

  /* fuchsia.device/Controller */

  void ConnectToDeviceFidl(zx::channel server) override {}

  void Bind(std::string driver, BindCallback callback) override {
    callback(fuchsia::device::Controller_Bind_Result::WithErr(ZX_ERR_NOT_SUPPORTED));
  }

  void Rebind(std::string driver, RebindCallback callback) override {
    callback(fuchsia::device::Controller_Rebind_Result::WithErr(ZX_ERR_NOT_SUPPORTED));
  }

  void UnbindChildren(UnbindChildrenCallback callback) override {
    callback(fuchsia::device::Controller_UnbindChildren_Result::WithErr(ZX_ERR_NOT_SUPPORTED));
  }

  void ScheduleUnbind(ScheduleUnbindCallback callback) override {
    callback(fuchsia::device::Controller_ScheduleUnbind_Result::WithErr(ZX_ERR_NOT_SUPPORTED));
  }

  // Returns a fake topological path.
  //
  // Network devices in netemul are backed by network-tun, which does not provide
  // fuchsia.device/Controller. We provide a fake implementation so netemul-backed
  // devfs looks similar to the real one.
  //
  // This method is only implemented so network managers can successfully query
  // a device's topological path.
  void GetTopologicalPath(GetTopologicalPathCallback callback) override {
    std::vector<std::string> parts = {kNetworkDeviceFakeTopoPathRoot, device_name_};
    callback(fuchsia::device::Controller_GetTopologicalPath_Result::WithResponse(
        fuchsia::device::Controller_GetTopologicalPath_Response(fxl::JoinStrings(parts, "/"))));
  }

  void GetMinDriverLogSeverity(GetMinDriverLogSeverityCallback callback) override {
    callback(ZX_ERR_NOT_SUPPORTED, fuchsia::logger::LogLevelFilter::NONE);
  }

  void SetMinDriverLogSeverity(fuchsia::logger::LogLevelFilter severity,
                               SetMinDriverLogSeverityCallback callback) override {
    callback(ZX_ERR_NOT_SUPPORTED);
  }

  void GetCurrentPerformanceState(GetCurrentPerformanceStateCallback callback) override {
    callback(0);
  }

  void SetPerformanceState(uint32_t requested_state,
                           SetPerformanceStateCallback callback) override {
    callback(ZX_ERR_NOT_SUPPORTED, 0);
  }

 private:
  void ListenForFrames() {
    if (!tun_device_.is_bound()) {
      return;
    }
    tun_device_->ReadFrame([this](fuchsia::net::tun::Device_ReadFrame_Result result) {
      if (result.is_response()) {
        auto& data = result.response().frame.data();
        ForwardData(data.data(), data.size());
      } else {
        FX_LOGS(ERROR) << "ReadFrame from Tun Device failed " << zx_status_get_string(result.err());
      }

      // Listen again for the next frame.
      ListenForFrames();
    });
  }

  std::string device_name_;
  fuchsia::net::tun::DevicePtr tun_device_;
  fuchsia::net::tun::PortPtr tun_port_;
  fidl::BindingSet<fuchsia::netemul::internal::NetworkDeviceInstance> instance_bindings_;
};

std::unique_ptr<EndpointImpl> MakeImpl(Endpoint::Config config) {
  switch (config.backing) {
    case Endpoint::Backing::ETHERTAP:
      return std::make_unique<EthertapImpl>(std::move(config));
    case Endpoint::Backing::NETWORK_DEVICE:
      return std::make_unique<NetworkDeviceImpl>(std::move(config));
  }
}

}  // namespace impl

Endpoint::Endpoint(NetworkContext* context, std::string name, Endpoint::Config config)
    : impl_(impl::MakeImpl(std::move(config))), parent_(context), name_(std::move(name)) {
  auto close_handler = [this]() {
    if (closed_callback_) {
      // bubble up the problem
      closed_callback_(*this);
    }
  };

  impl_->SetClosedCallback(close_handler);
  bindings_.set_empty_set_handler(close_handler);
}

zx_status_t Endpoint::Startup(const NetworkContext& context, bool start_online, size_t id) {
  return impl_->Setup(name_, start_online, context, id);
}

Endpoint::~Endpoint() = default;

void Endpoint::GetConfig(Endpoint::GetConfigCallback callback) {
  Config config;
  impl_->CloneConfig(&config);
  callback(std::move(config));
}

void Endpoint::GetName(Endpoint::GetNameCallback callback) { callback(name_); }

void Endpoint::SetLinkUp(bool up, Endpoint::SetLinkUpCallback callback) {
  impl_->SetLinkUp(up, std::move(callback));
}

void Endpoint::GetDevice(Endpoint::GetDeviceCallback callback) { callback(impl_->GetDevice()); }

void Endpoint::GetProxy(fidl::InterfaceRequest<FProxy> proxy) {
  proxy_bindings_.AddBinding(this, std::move(proxy), parent_->dispatcher());
}

void Endpoint::ServeDevice(zx::channel channel) { impl_->ServeDevice(std::move(channel)); }

void Endpoint::Bind(fidl::InterfaceRequest<FEndpoint> req) {
  bindings_.AddBinding(this, std::move(req), parent_->dispatcher());
}

zx_status_t Endpoint::InstallSink(data::BusConsumer::Ptr sink, data::Consumer::Ptr* src) {
  // just forbid install if sink is already in our list:
  auto& sinks = impl_->sinks();
  for (auto& i : sinks) {
    if (i.get() == sink.get()) {
      return ZX_ERR_ALREADY_BOUND;
    }
  }
  impl_->sinks().emplace_back(std::move(sink));
  *src = impl_->GetPointer();
  return ZX_OK;
}

zx_status_t Endpoint::RemoveSink(data::BusConsumer::Ptr sink, data::Consumer::Ptr* src) {
  auto& sinks = impl_->sinks();
  for (auto i = sinks.begin(); i != sinks.end(); i++) {
    if (i->get() == sink.get()) {
      sinks.erase(i);
      *src = impl_->GetPointer();
      return ZX_OK;
    }
  }
  return ZX_ERR_NOT_FOUND;
}

void Endpoint::SetClosedCallback(Endpoint::EndpointClosedCallback cb) {
  closed_callback_ = std::move(cb);
}

}  // namespace netemul
