// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/bringup/bin/netsvc/netifc-discover.h"

#include <fcntl.h>
#include <fidl/fuchsia.device/cpp/wire.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async/cpp/wait.h>
#include <lib/fdio/directory.h>
#include <lib/fit/defer.h>
#include <lib/sys/component/cpp/service_client.h>
#include <stdio.h>

#include <fbl/unique_fd.h>

#include "src/lib/fsl/io/device_watcher.h"

namespace {

cpp17::string_view SkipInstanceSigil(cpp17::string_view v) {
  if (!v.empty() && v.at(0) == '@') {
    return v.substr(1);
  }
  return v;
}

struct Ethernet {
  using Instance = fuchsia_hardware_ethernet::Device;
  using Info = DiscoveredInterface;
  static constexpr const char* kDirectory = "/class/ethernet";

  static std::optional<Info> get_interface_if_matching(fidl::ClientEnd<Instance> dev,
                                                       const std::string& filename) {
    fidl::WireResult result = fidl::WireCall(dev)->GetInfo();
    if (!result.ok()) {
      printf("netifc: failed to get Ethernet device info %s: %s\n", filename.c_str(),
             result.status_string());
      return std::nullopt;
    }
    auto& resp = result.value();
    if (resp.info.features & fuchsia_hardware_ethernet::wire::Features::kWlan) {
      return std::nullopt;
    }
    DiscoveredInterface ret = {.device = std::move(dev)};
    static_assert(sizeof(resp.info.mac.octets) == sizeof(ret.mac.x));
    std::copy(resp.info.mac.octets.begin(), resp.info.mac.octets.end(), std::begin(ret.mac.x));
    return ret;
  }

  static void Process(std::optional<DiscoveredInterface>& discovered,
                      async_dispatcher_t* dispatcher, Info info) {
    discovered = std::move(info);
  }
};

struct Netdevice {
  using Instance = fuchsia_hardware_network::DeviceInstance;
  struct Info {
    fidl::ClientEnd<fuchsia_hardware_network::Device> device;
    fidl::ClientEnd<fuchsia_hardware_network::PortWatcher> port_watcher;
  };
  static constexpr const char* kDirectory = "/class/network";

  static std::optional<Info> get_interface_if_matching(fidl::ClientEnd<Instance> instance,
                                                       const std::string& filename) {
    zx::status endpoints = fidl::CreateEndpoints<fuchsia_hardware_network::Device>();
    if (endpoints.is_error()) {
      printf("netifc: failed to create netdevice endpoints %s: %s\n", filename.c_str(),
             endpoints.status_string());
      return std::nullopt;
    }
    auto& [client_end, server_end] = endpoints.value();

    {
      fidl::WireResult result = fidl::WireCall(instance)->GetDevice(std::move(server_end));
      if (!result.ok()) {
        printf("netifc: failed to get NetworkDevice from instance %s: %s\n", filename.c_str(),
               result.status_string());
        return std::nullopt;
      }
    }

    zx::status watcher_endpoints = fidl::CreateEndpoints<fuchsia_hardware_network::PortWatcher>();
    if (watcher_endpoints.is_error()) {
      printf("netifc: failed to create netdevice port watcher endpoints %s: %s\n", filename.c_str(),
             watcher_endpoints.status_string());
      return std::nullopt;
    }
    auto& [watcher_client_end, watcher_server_end] = watcher_endpoints.value();

    {
      fidl::WireResult result =
          fidl::WireCall(client_end)->GetPortWatcher(std::move(watcher_server_end));
      if (!result.ok()) {
        printf("netifc: failed to get port watcher %s: %s\n", filename.c_str(),
               result.status_string());
        return std::nullopt;
      }
    }

    return Info{
        .device = std::move(client_end),
        .port_watcher = std::move(watcher_client_end),
    };
  }

  static void Process(std::optional<DiscoveredInterface>& discovered,
                      async_dispatcher_t* dispatcher, Info info) {
    auto [device, watcher] = std::move(info);
    std::shared_ptr client_ptr =
        std::make_shared<fidl::WireClient<fuchsia_hardware_network::PortWatcher>>(
            std::move(watcher), dispatcher);

    Watch(discovered, client_ptr, std::move(device));
  }

  static void Watch(
      std::optional<DiscoveredInterface>& discovered,
      const std::shared_ptr<fidl::WireClient<fuchsia_hardware_network::PortWatcher>>& watcher,
      fidl::ClientEnd<fuchsia_hardware_network::Device> dev) {
    (*watcher)->Watch().ThenExactlyOnce(
        [&discovered, watcher, dev = std::move(dev)](
            fidl::WireUnownedResult<fuchsia_hardware_network::PortWatcher::Watch>& r) mutable {
          if (!r.ok()) {
            printf("netifc: failed to watch for netdevice ports: %s\n",
                   r.FormatDescription().c_str());
            return;
          }

          auto defer = fit::defer([&discovered, &watcher, &dev]() {
            // Watch next.
            Watch(discovered, watcher, std::move(dev));
          });

          const fuchsia_hardware_network::wire::DevicePortEvent& event = r.value().event;
          fuchsia_hardware_network::wire::PortId port_id;
          switch (event.Which()) {
            case fuchsia_hardware_network::wire::DevicePortEvent::Tag::kAdded:
              port_id = event.added();
              break;
            case fuchsia_hardware_network::wire::DevicePortEvent::Tag::kExisting:
              port_id = event.existing();
              break;
            case fuchsia_hardware_network::wire::DevicePortEvent::Tag::kIdle:
            case fuchsia_hardware_network::wire::DevicePortEvent::Tag::kRemoved:
              return;
          }

          zx::status port_endpoints = fidl::CreateEndpoints<fuchsia_hardware_network::Port>();
          if (port_endpoints.is_error()) {
            printf("netifc: failed to create port endpoints: %s\n", port_endpoints.status_string());
            return;
          }
          auto [port_client_end, port_server_end] = std::move(port_endpoints.value());
          {
            fidl::WireResult result =
                fidl::WireCall(dev)->GetPort(port_id, std::move(port_server_end));
            if (!result.ok()) {
              printf("netifc: failed to get netdevice port (%d:%d): %s\n", port_id.base,
                     port_id.salt, result.FormatDescription().c_str());
              return;
            }
          }
          {
            fidl::WireResult result = fidl::WireCall(port_client_end)->GetInfo();
            if (!result.ok()) {
              printf("netifc: failed to get netdevice port info (%d:%d): %s\n", port_id.base,
                     port_id.salt, result.FormatDescription().c_str());
              return;
            }
            const fuchsia_hardware_network::wire::PortInfo& port_info = result.value().info;
            if (!port_info.has_class()) {
              printf("netifc: missing port class in  netdevice port info (%d:%d): %s\n",
                     port_id.base, port_id.salt, result.FormatDescription().c_str());
              return;
            }
            switch (port_info.class_()) {
              case fuchsia_hardware_network::DeviceClass::kEthernet:
              case fuchsia_hardware_network::DeviceClass::kVirtual:
                // NB: Historically this only netifc only accepts Ethernet
                // device. We allow virtual interfaces as well which are used in
                // testing.
                break;
              case fuchsia_hardware_network::DeviceClass::kBridge:
              case fuchsia_hardware_network::DeviceClass::kWlan:
              case fuchsia_hardware_network::DeviceClass::kPpp:
              case fuchsia_hardware_network::DeviceClass::kWlanAp:
                printf("netifc: ignoring netdevice port (%d:%d) with class %hu\n", port_id.base,
                       port_id.salt, port_info.class_());
                return;
            }
          }

          // This is a good candidate port, but we need to retrieve the MAC
          // address.
          zx::status mac_endpoints =
              fidl::CreateEndpoints<fuchsia_hardware_network::MacAddressing>();
          if (mac_endpoints.is_error()) {
            printf("netifc: failed to create MacAddressing endpoints: %s\n",
                   mac_endpoints.status_string());
            return;
          }
          auto [mac_client_end, mac_server_end] = std::move(mac_endpoints.value());
          {
            fidl::WireResult result =
                fidl::WireCall(port_client_end)->GetMac(std::move(mac_server_end));
            if (!result.ok()) {
              printf("netifc: failed to get mac addressing for port (%d:%d): %s\n", port_id.base,
                     port_id.salt, result.FormatDescription().c_str());
              return;
            }
          }

          fidl::WireResult result = fidl::WireCall(mac_client_end)->GetUnicastAddress();
          if (!result.ok()) {
            printf("netifc: failed to get mac address for port (%d:%d): %s\n", port_id.base,
                   port_id.salt, result.FormatDescription().c_str());
            return;
          }
          const fuchsia_net::wire::MacAddress& mac = result.value().address;

          // We have our device, store it and stop watching.
          DiscoveredInterface& discovered_interface = discovered.emplace(DiscoveredInterface{
              .device = NetdeviceInterface{.device = std::move(dev), .port_id = port_id},
          });
          static_assert(sizeof(mac.octets) == sizeof(discovered_interface.mac.x));
          std::copy(mac.octets.begin(), mac.octets.end(), std::begin(discovered_interface.mac.x));
          defer.cancel();
        });
  }
};

template <typename D>
std::optional<typename D::Info> netifc_evaluate(cpp17::string_view topological_path,
                                                fidl::UnownedClientEnd<fuchsia_io::Directory> dir,
                                                const std::string& dirname,
                                                const std::string& filename) {
  printf("netifc: ? %s/%s\n", dirname.c_str(), filename.c_str());

  fidl::ClientEnd<typename D::Instance> dev;
  {
    zx::status status = component::ConnectAt<typename D::Instance>(dir, filename.c_str());
    if (status.is_error()) {
      printf("netifc: failed to connect to %s/%s: %s\n", dirname.c_str(), filename.c_str(),
             status.status_string());
      return std::nullopt;
    }
    dev = std::move(status.value());
  }

  // If an interface was specified, check the topological path of this device and reject it if it
  // doesn't match.
  if (!topological_path.empty()) {
    // NB: We need to take a trip through a fuchsia.device ClientEnd here to
    // abide by llcpp endpoint typing.
    fidl::UnownedClientEnd<fuchsia_device::Controller> controller(dev.channel().borrow());
    fidl::WireResult result = fidl::WireCall(controller)->GetTopologicalPath();

    if (!result.ok()) {
      printf("netifc: failed to get topological path %s: %s\n", filename.c_str(),
             result.status_string());
      return std::nullopt;
    }
    auto& resp = result.value();
    if (resp.is_error()) {
      printf("netifc: GetTopologicalPath returned error %s: %s\n", filename.c_str(),
             zx_status_get_string(resp.error_value()));
      return std::nullopt;
    }

    cpp17::string_view topo_path = SkipInstanceSigil(resp.value()->path.get());
    if (topo_path != topological_path) {
      return std::nullopt;
    }
  }

  std::optional result = D::get_interface_if_matching(std::move(dev), filename);
  if (result.has_value()) {
    printf("netsvc: using %s/%s\n", dirname.c_str(), filename.c_str());
  }
  return result;
}

template <typename D>
zx::status<std::unique_ptr<fsl::DeviceWatcher>> CreateWatcher(
    async_dispatcher_t* dispatcher, std::optional<DiscoveredInterface>& selected_ifc,
    const std::string& devdir, cpp17::string_view topological_path) {
  const std::string classdir = devdir + D::kDirectory;
  fbl::unique_fd dir(open(classdir.c_str(), O_DIRECTORY | O_RDONLY));
  if (!dir.is_valid()) {
    printf("failed to open %s: %s\n", classdir.c_str(), strerror(errno));
    return zx::error(ZX_ERR_INVALID_ARGS);
  }

  fidl::ClientEnd<fuchsia_io::Directory> dir_channel;
  {
    zx::status status = fidl::CreateEndpoints<fuchsia_io::Directory>();
    if (status.is_error()) {
      return status.take_error();
    }
    auto& [client, server] = status.value();
    // TODO(https://fxbug.dev/77059): We shouldn't require the writable right
    // below, but component::ConnectAt (and fdio_service_connect_at) connect to
    // protocols with the writable right.
    if (zx_status_t status =
            fdio_open(classdir.c_str(),
                      static_cast<uint32_t>(fuchsia_io::wire::OpenFlags::kRightReadable |
                                            fuchsia_io::wire::OpenFlags::kRightWritable),
                      server.TakeChannel().release());
        status != ZX_OK) {
      return zx::error(status);
    }
    dir_channel = std::move(client);
  }

  std::unique_ptr watcher = fsl::DeviceWatcher::Create(
      classdir,
      [dispatcher, classdir, topological_path, dir_channel = std::move(dir_channel), &selected_ifc](
          int dir_fd, const std::string& filename) {
        std::optional r =
            netifc_evaluate<D>(topological_path, dir_channel.borrow(), classdir, filename);
        if (r.has_value()) {
          D::Process(selected_ifc, dispatcher, std::move(r.value()));
        }
      },
      dispatcher);

  return zx::ok(std::move(watcher));
}

}  // namespace

zx::status<DiscoveredInterface> netifc_discover(const std::string& devdir,
                                                cpp17::string_view topological_path) {
  topological_path = SkipInstanceSigil(topological_path);
  async::Loop loop(&kAsyncLoopConfigNeverAttachToThread);
  std::optional<DiscoveredInterface> selected_ifc;

  // Create watchers for the different device types. There's no deterministic
  // ordering guarantee on a preferred device class. The first device found of
  // either class is the selected device.
  zx::status ethernet_watcher =
      CreateWatcher<Ethernet>(loop.dispatcher(), selected_ifc, devdir, topological_path);
  if (ethernet_watcher.is_error()) {
    return ethernet_watcher.take_error();
  }
  zx::status netdevice_watcher =
      CreateWatcher<Netdevice>(loop.dispatcher(), selected_ifc, devdir, topological_path);
  if (netdevice_watcher.is_error()) {
    return netdevice_watcher.take_error();
  }

  for (;;) {
    zx_status_t status = loop.Run(zx::time::infinite(), /* once */ true);
    if (status != ZX_OK) {
      printf("run loop error: %s\n", zx_status_get_string(status));
      return zx::error(status);
    }
    if (selected_ifc.has_value()) {
      return zx::ok(std::move(selected_ifc.value()));
    }
  }
}
