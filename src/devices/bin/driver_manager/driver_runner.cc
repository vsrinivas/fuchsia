// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "driver_runner.h"

#include <lib/fdio/directory.h>
#include <lib/fidl/llcpp/server.h>
#include <zircon/status.h>

#include <fbl/string_printf.h>
#include <fs/service.h>

#include "src/devices/bin/driver_manager/program.h"
#include "src/devices/lib/log/log.h"

namespace fdata = llcpp::fuchsia::data;
namespace fdf = llcpp::fuchsia::driver::framework;
namespace frunner = llcpp::fuchsia::component::runner;
namespace fsys = llcpp::fuchsia::sys2;

namespace {

template <typename T>
zx::status<> add_entry(async_dispatcher_t* dispatcher, DriverRunner* runner,
                       const fbl::RefPtr<fs::PseudoDir>& svc_dir) {
  const auto service = [dispatcher, runner](zx::channel request) {
    auto result = fidl::BindServer<typename T::Interface>(dispatcher, std::move(request), runner);
    if (result.is_error()) {
      LOGF(ERROR, "Failed to bind channel to '%s': %s", T::Name,
           zx_status_get_string(result.error()));
      return result.error();
    }
    return ZX_OK;
  };
  zx_status_t status = svc_dir->AddEntry(T::Name, fbl::MakeRefCounted<fs::Service>(service));
  if (status != ZX_OK) {
    LOGF(ERROR, "Failed to add directory entry '%s': %s", T::Name, zx_status_get_string(status));
  }
  return zx::make_status(status);
}

}  // namespace

DriverComponent::DriverComponent(zx::channel exposed_dir, zx::channel driver)
    : exposed_dir_(std::move(exposed_dir)), driver_(std::move(driver)) {}

void DriverComponent::Stop(DriverComponent::StopCompleter::Sync completer) {
  // TODO: Remove this warning.
  LOGF(WARNING, "\n\n==> DriverComponent::Stop\n");
  driver_.reset();
}

void DriverComponent::Kill(DriverComponent::KillCompleter::Sync completer) {
  // TODO: Remove this warning.
  LOGF(WARNING, "\n\n==> DriverComponent::Kill\n");
}

DriverHostComponent::DriverHostComponent(
    zx::channel driver_host, async_dispatcher_t* dispatcher,
    fbl::DoublyLinkedList<std::unique_ptr<DriverHostComponent>>* driver_hosts)
    : driver_host_(std::move(driver_host), dispatcher,
                   [this, driver_hosts](auto) { driver_hosts->erase(*this); }) {}

zx::status<zx::channel> DriverHostComponent::Start(
    zx::channel node, fdata::Dictionary program,
    fidl::VectorView<frunner::ComponentNamespaceEntry> ns, zx::channel outgoing_dir) {
  zx::channel client_end, server_end;
  zx_status_t status = zx::channel::create(0, &client_end, &server_end);
  if (status != ZX_OK) {
    return zx::error(status);
  }

  auto args = fdf::DriverStartArgs::UnownedBuilder()
                  .set_node(fidl::unowned_ptr(&node))
                  .set_program(fidl::unowned_ptr(&program))
                  .set_ns(fidl::unowned_ptr(&ns))
                  .set_outgoing_dir(fidl::unowned_ptr(&outgoing_dir));
  auto start = driver_host_->Start(args.build(), std::move(server_end));
  if (!start.ok()) {
    auto binary = program_value(program, "binary").value_or("");
    LOGF(ERROR, "Failed to start driver '%s' in driver host: %s", binary.data(), start.error());
    return zx::error(start.status());
  }
  return zx::ok(std::move(client_end));
}

Node::Node(Node* parent, DriverBinder* driver_binder, async_dispatcher_t* dispatcher)
    : parent_(parent), driver_binder_(driver_binder), dispatcher_(dispatcher) {}

Node::~Node() {
  if (binding_.has_value()) {
    binding_->Unbind();
  }
}

DriverHostComponent* Node::parent_driver_host() const { return parent_->driver_host_; }

void Node::set_driver_host(DriverHostComponent* driver_host) { driver_host_ = driver_host; }

void Node::set_binding(fidl::ServerBindingRef<fdf::Node> binding) {
  binding_ = std::make_optional(std::move(binding));
}

void Node::Remove() {
  if (parent_ != nullptr) {
    parent_->children_.erase(*this);
  }
}

void Node::Remove(RemoveCompleter::Sync completer) { Remove(); }

void Node::AddChild(fdf::NodeAddArgs args, zx::channel controller, zx::channel node,
                    AddChildCompleter::Sync completer) {
  auto child = std::make_unique<Node>(this, driver_binder_, dispatcher_);

  auto bind_controller = fidl::BindServer<fdf::NodeController::Interface>(
      dispatcher_, std::move(controller), child.get());
  if (bind_controller.is_error()) {
    LOGF(ERROR, "Failed to bind channel to NodeController for node '%.*s': %s", args.name().size(),
         args.name().data(), zx_status_get_string(bind_controller.error()));
    completer.Close(bind_controller.error());
    return;
  }

  if (node.is_valid()) {
    auto bind_node = fidl::BindServer<fdf::Node::Interface>(
        dispatcher_, std::move(node), child.get(),
        [](fdf::Node::Interface* node, auto, auto) { static_cast<Node*>(node)->Remove(); });
    if (bind_node.is_error()) {
      LOGF(ERROR, "Failed to bind channel to Node '%.*s': %s", args.name().size(),
           args.name().data(), zx_status_get_string(bind_node.error()));
      completer.Close(bind_node.error());
      return;
    }
    child->set_binding(bind_node.take_value());
  } else {
    auto bind_result = driver_binder_->Bind(child.get(), std::move(args));
    if (bind_result.is_error()) {
      LOGF(ERROR, "Failed to bind driver to Node '%.*s': %s", args.name().size(),
           args.name().data(), bind_result.status_string());
      completer.Close(bind_result.status_value());
      return;
    }
  }

  children_.push_back(std::move(child));
}

DriverIndex::DriverIndex(MatchCallback match_callback)
    : match_callback_(std::move(match_callback)) {}

zx::status<MatchResult> DriverIndex::Match(fdf::NodeAddArgs args) {
  return match_callback_(std::move(args));
}

DriverRunner::DriverRunner(zx::channel realm, DriverIndex* driver_index,
                           async_dispatcher_t* dispatcher)
    : realm_(std::move(realm), dispatcher),
      driver_index_(driver_index),
      dispatcher_(dispatcher),
      root_node_(nullptr, this, dispatcher) {}

zx::status<> DriverRunner::PublishComponentRunner(const fbl::RefPtr<fs::PseudoDir>& svc_dir) {
  return add_entry<frunner::ComponentRunner>(dispatcher_, this, svc_dir);
}

zx::status<> DriverRunner::StartRootDriver(std::string_view name) {
  auto root_name = fidl::unowned_str(name);
  auto args = fdf::NodeAddArgs::UnownedBuilder().set_name(fidl::unowned_ptr(&root_name));
  return Bind(&root_node_, args.build());
}

void DriverRunner::Start(frunner::ComponentStartInfo start_info, zx::channel controller,
                         StartCompleter::Sync completer) {
  auto& url = start_info.resolved_url();
  auto it = driver_args_.find(std::string(url.data(), url.size()));
  if (it == driver_args_.end()) {
    LOGF(ERROR, "Failed to start driver '%.*s', unknown request for driver",
         start_info.resolved_url().size(), start_info.resolved_url().data());
    completer.Close(ZX_ERR_UNAVAILABLE);
    return;
  }
  auto driver_args = std::move(it->second);
  driver_args_.erase(it);

  // Launch a driver host, or use an existing driver host.
  DriverHostComponent* driver_host;
  if (program_value(start_info.program(), "colocate").value_or("") == "true") {
    if (driver_args.node == &root_node_) {
      LOGF(ERROR, "Failed to start driver '%.*s', root driver cannot colocate",
           start_info.resolved_url().size(), start_info.resolved_url().data());
      completer.Close(ZX_ERR_INVALID_ARGS);
      return;
    }
    driver_host = driver_args.node->parent_driver_host();
  } else {
    auto result = StartDriverHost();
    if (result.is_error()) {
      completer.Close(result.error_value());
      return;
    }
    driver_host = result.value().get();
    driver_hosts_.push_back(std::move(result.value()));
  }
  driver_args.node->set_driver_host(driver_host);

  // Start the driver within the driver host.
  zx::channel client_end, server_end;
  zx_status_t status = zx::channel::create(0, &client_end, &server_end);
  if (status != ZX_OK) {
    completer.Close(status);
    return;
  }
  auto start = driver_host->Start(std::move(client_end), std::move(start_info.program()),
                                  std::move(start_info.ns()), std::move(start_info.outgoing_dir()));
  if (start.is_error()) {
    completer.Close(start.error_value());
    return;
  }

  // Create a DriverComponent to manage the driver.
  auto driver = std::make_unique<DriverComponent>(std::move(driver_args.exposed_dir),
                                                  std::move(start.value()));
  auto bind_driver = fidl::BindServer<DriverComponent>(
      dispatcher_, std::move(controller), driver.get(),
      [this](DriverComponent* driver, auto, auto) { drivers_.erase(*driver); });
  if (bind_driver.is_error()) {
    LOGF(ERROR, "Failed to bind channel to ComponentController for driver '%.*s': %s",
         start_info.resolved_url().size(), start_info.resolved_url().data(),
         zx_status_get_string(bind_driver.error()));
    completer.Close(bind_driver.error());
    return;
  }

  // Bind the Node associated with the driver.
  auto bind_node = fidl::BindServer<fdf::Node::Interface>(
      dispatcher_, std::move(server_end), driver_args.node,
      [driver_binding = bind_driver.take_value()](fdf::Node::Interface* node, auto, auto) mutable {
        driver_binding.Unbind();
        static_cast<Node*>(node)->Remove();
      });
  if (bind_node.is_error()) {
    LOGF(ERROR, "Failed to bind channel to Node for driver '%.*s': %s",
         start_info.resolved_url().size(), start_info.resolved_url().data(),
         zx_status_get_string(bind_node.error()));
    completer.Close(bind_node.error());
    return;
  }
  driver_args.node->set_binding(bind_node.take_value());
  drivers_.push_back(std::move(driver));
}

zx::status<> DriverRunner::Bind(Node* node, fdf::NodeAddArgs args) {
  auto match_result = driver_index_->Match(std::move(args));
  if (match_result.is_error()) {
    return match_result.take_error();
  }
  auto match = std::move(match_result.value());
  auto name = "driver-" + std::to_string(NextId());
  auto create_result = CreateComponent(name, match.url, "drivers");
  if (create_result.is_error()) {
    return create_result.take_error();
  }
  driver_args_.emplace(match.url, DriverArgs{std::move(create_result.value()), node});
  return zx::ok();
}

zx::status<std::unique_ptr<DriverHostComponent>> DriverRunner::StartDriverHost() {
  auto name = "driver_host-" + std::to_string(NextId());
  auto create = CreateComponent(name, "fuchsia-boot:///#meta/driver_host2.cm", "driver_hosts");
  if (create.is_error()) {
    return zx::error(create.error_value());
  }

  zx::channel client_end, server_end;
  zx_status_t status = zx::channel::create(0, &client_end, &server_end);
  if (status != ZX_OK) {
    return zx::error(status);
  }
  auto path = fbl::StringPrintf("svc/%s", fdf::DriverHost::Name);
  status = fdio_service_connect_at(create.value().get(), path.data(), server_end.release());
  if (status != ZX_OK) {
    LOGF(ERROR, "Failed to connect to service '%s': %s", path.data(), zx_status_get_string(status));
    return zx::error(status);
  }

  auto driver_host =
      std::make_unique<DriverHostComponent>(std::move(client_end), dispatcher_, &driver_hosts_);
  return zx::ok(std::move(driver_host));
}

zx::status<zx::channel> DriverRunner::CreateComponent(std::string name, std::string url,
                                                      std::string collection) {
  zx::channel client_end, server_end;
  zx_status_t status = zx::channel::create(0, &client_end, &server_end);
  if (status != ZX_OK) {
    return zx::error(status);
  }
  auto bind_cb = [name](auto result) {
    if (result.is_err()) {
      LOGF(ERROR, "Failed to bind component '%s': %u", name.data(), result.err());
    }
  };
  auto create_cb = [this, name, collection, server_end = std::move(server_end),
                    bind_cb = std::move(bind_cb)](auto result) mutable {
    if (result.is_err()) {
      LOGF(ERROR, "Failed to create component '%s': %u", name.data(), result.err());
      return;
    }
    auto bind = realm_->BindChild(fsys::ChildRef{.name = fidl::unowned_str(name),
                                                 .collection = fidl::unowned_str(collection)},
                                  std::move(server_end), std::move(bind_cb));
    if (!bind.ok()) {
      LOGF(ERROR, "Failed to bind component '%s': %s", name.data(), bind.error());
    }
  };
  auto unowned_name = fidl::unowned_str(name);
  auto unowned_url = fidl::unowned_str(url);
  auto startup = fsys::StartupMode::LAZY;
  auto child_decl = fsys::ChildDecl::UnownedBuilder()
                        .set_name(fidl::unowned_ptr(&unowned_name))
                        .set_url(fidl::unowned_ptr(&unowned_url))
                        .set_startup(fidl::unowned_ptr(&startup));
  auto create = realm_->CreateChild(fsys::CollectionRef{.name = fidl::unowned_str(collection)},
                                    child_decl.build(), std::move(create_cb));
  if (!create.ok()) {
    LOGF(ERROR, "Failed to create component '%s': %s", name.data(), create.error());
    return zx::error(ZX_ERR_INTERNAL);
  }
  return zx::ok(std::move(client_end));
}

uint64_t DriverRunner::NextId() { return next_id_++; }
