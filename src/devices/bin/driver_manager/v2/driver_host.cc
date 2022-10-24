// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/devices/bin/driver_manager/v2/driver_host.h"

#include <lib/driver2/start_args.h>

#include "src/devices/lib/log/log.h"

namespace fdh = fuchsia_driver_host;
namespace frunner = fuchsia_component_runner;
namespace fdf = fuchsia_driver_framework;

namespace dfv2 {

zx::result<> SetEncodedConfig(fdf::wire::DriverStartArgs& args,
                              frunner::wire::ComponentStartInfo& start_info) {
  if (!start_info.has_encoded_config()) {
    return zx::ok();
  }

  if (!start_info.encoded_config().is_buffer() && !start_info.encoded_config().is_bytes()) {
    LOGF(ERROR, "Failed to parse encoded config in start info. Encoding is not buffer or bytes.");
    return zx::error(ZX_ERR_INVALID_ARGS);
  }

  if (start_info.encoded_config().is_buffer()) {
    args.set_config(std::move(start_info.encoded_config().buffer().vmo));
    return zx::ok();
  }

  auto vmo_size = start_info.encoded_config().bytes().count();
  zx::vmo vmo;

  auto status = zx::vmo::create(vmo_size, ZX_RIGHT_TRANSFER | ZX_RIGHT_READ, &vmo);
  if (status != ZX_OK) {
    return zx::error(status);
  }

  status = vmo.write(start_info.encoded_config().bytes().data(), 0, vmo_size);
  if (status != ZX_OK) {
    return zx::error(status);
  }

  args.set_config(std::move(vmo));
  return zx::ok();
}

DriverHostComponent::DriverHostComponent(
    fidl::ClientEnd<fdh::DriverHost> driver_host, async_dispatcher_t* dispatcher,
    fbl::DoublyLinkedList<std::unique_ptr<DriverHostComponent>>* driver_hosts)
    : driver_host_(std::move(driver_host), dispatcher,
                   fidl::ObserveTeardown([this, driver_hosts] { driver_hosts->erase(*this); })) {}

zx::result<fidl::ClientEnd<fdh::Driver>> DriverHostComponent::Start(
    fidl::ClientEnd<fdf::Node> client_end, std::string node_name,
    fidl::VectorView<fuchsia_driver_framework::wire::NodeSymbol> symbols,
    frunner::wire::ComponentStartInfo start_info) {
  auto endpoints = fidl::CreateEndpoints<fdh::Driver>();
  if (endpoints.is_error()) {
    return endpoints.take_error();
  }

  auto binary = driver::ProgramValue(start_info.program(), "binary").value_or("");
  fidl::Arena arena;
  fdf::wire::DriverStartArgs args(arena);
  args.set_node(std::move(client_end))
      .set_node_name(arena, fidl::StringView::FromExternal(node_name))
      .set_url(arena, start_info.resolved_url())
      .set_program(arena, start_info.program())
      .set_ns(arena, start_info.ns())
      .set_outgoing_dir(std::move(start_info.outgoing_dir()));

  auto status = SetEncodedConfig(args, start_info);
  if (status.is_error()) {
    return status.take_error();
  }

  if (!symbols.empty()) {
    args.set_symbols(arena, symbols);
  }

  auto start = driver_host_->Start(args, std::move(endpoints->server));
  if (!start.ok()) {
    LOGF(ERROR, "Failed to start driver '%s' in driver host: %s", binary.data(),
         start.FormatDescription().data());
    return zx::error(start.status());
  }

  return zx::ok(std::move(endpoints->client));
}

zx::result<uint64_t> DriverHostComponent::GetProcessKoid() const {
  auto result = driver_host_.sync()->GetProcessKoid();
  if (!result.ok()) {
    return zx::error(result.status());
  }
  if (result->is_error()) {
    return zx::error(result->error_value());
  }
  return zx::ok(result->value()->koid);
}

zx::result<> DriverHostComponent::InstallLoader(
    fidl::ClientEnd<fuchsia_ldsvc::Loader> loader_client) const {
  auto result = driver_host_->InstallLoader(std::move(loader_client));
  if (!result.ok()) {
    return zx::error(result.status());
  }
  return zx::ok();
}

}  // namespace dfv2
