// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/sys/appmgr/namespace_builder.h"

#include <fcntl.h>
#include <lib/fdio/cpp/caller.h>
#include <lib/fdio/directory.h>
#include <lib/fdio/fd.h>
#include <lib/fdio/fdio.h>
#include <lib/fdio/limits.h>
#include <lib/syslog/cpp/macros.h>
#include <stdlib.h>
#include <unistd.h>
#include <zircon/processargs.h>

#include <fbl/unique_fd.h>
#include <src/lib/fxl/strings/concatenate.h>

#include "src/lib/files/directory.h"
#include "src/lib/files/path.h"
#include "src/sys/appmgr/allow_list.h"

namespace component {

namespace fio = fuchsia_io;

constexpr char kDeprecatedDataName[] = "deprecated-data";
constexpr char kBlockedDataName[] = "data";

NamespaceBuilder::~NamespaceBuilder() = default;

void NamespaceBuilder::AddFlatNamespace(fuchsia::sys::FlatNamespacePtr ns) {
  if (ns && ns->paths.size() == ns->directories.size()) {
    for (size_t i = 0; i < ns->paths.size(); ++i) {
      AddDirectoryIfNotPresent(ns->paths.at(i), std::move(ns->directories.at(i)));
    }
  }
}

void NamespaceBuilder::AddPackage(fidl::InterfaceHandle<fuchsia::io::Directory> package) {
  PushDirectoryFromChannel("/pkg", std::move(package));
}

void NamespaceBuilder::AddConfigData(const SandboxMetadata& sandbox, const std::string& pkg_name) {
  for (const auto& feature : sandbox.features()) {
    if (feature == "config-data") {
      PushDirectoryFromPathAs("/pkgfs/packages/config-data/0/meta/data/" + pkg_name,
                              "/config/data");
    }
  }
}

void NamespaceBuilder::AddDirectoryIfNotPresent(
    const std::string& path, fidl::InterfaceHandle<fuchsia::io::Directory> directory) {
  if (std::find(paths_.begin(), paths_.end(), path) != paths_.end()) {
    FX_LOGS(INFO) << "Namespace conflict for " << ns_id << ": " << path;
    return;
  }
  PushDirectoryFromChannel(path, std::move(directory));
}

void NamespaceBuilder::AddServices(fidl::InterfaceHandle<fuchsia::io::Directory> services) {
  PushDirectoryFromChannel("/svc", std::move(services));
}

void NamespaceBuilder::AddHub(const HubDirectoryFactory& hub_directory_factory) {
  if (std::find(paths_.begin(), paths_.end(), "/hub") != paths_.end())
    return;
  PushDirectoryFromChannel("/hub", hub_directory_factory());
}

zx_status_t NamespaceBuilder::AddSandbox(const SandboxMetadata& sandbox,
                                         const HubDirectoryFactory& hub_directory_factory) {
  return AddSandbox(
      sandbox, hub_directory_factory,
      [] {
        FX_NOTREACHED() << "IsolatedDataPathFactory unexpectedly used";
        return fpromise::ok("");
      },
      [] {
        FX_NOTREACHED() << "IsolatedCachePathFactory unexpectedly used";
        return fpromise::ok("");
      },
      [] { return fpromise::ok("/tmp"); });
}

zx_status_t NamespaceBuilder::AddSandbox(
    const SandboxMetadata& sandbox, const HubDirectoryFactory& hub_directory_factory,
    const IsolatedDataPathFactory& isolated_data_path_factory,
    const IsolatedCachePathFactory& isolated_cache_path_factory,
    const IsolatedTempPathFactory& isolated_temp_path_factory) {
  for (const auto& path : sandbox.dev()) {
    if (path == "class") {
      FX_LOGS(WARNING) << "Ignoring request for all device classes";
      continue;
    }
    PushDirectoryFromPath("/dev/" + path);
  }

  for (const auto& path : sandbox.system()) {
    // 'deprecated-data' is the value used to access /system/data
    // to request a directory inside /system/data 'deprecated-data/some/path' is supplied
    if (path == kDeprecatedDataName ||
        path.find(fxl::Concatenate({kDeprecatedDataName, "/"})) == 0) {
      FX_LOGS(ERROR) << "Request for 'deprecated-data' by " << ns_id
                     << " ignored, this feature is no longer available";
    } else if (path == kBlockedDataName ||
               path.find(fxl::Concatenate({kBlockedDataName, "/"})) == 0) {
      FX_LOGS(ERROR) << "Request for 'data' in namespace '" << ns_id
                     << "' ignored, this feature is no longer available";
    } else {
      PushDirectoryFromPath("/system/" + path);
    }
  }

  for (const auto& path : sandbox.pkgfs())
    PushDirectoryFromPath("/pkgfs/" + path);

  // Prioritize isolated persistent storage over shell feature, if both are
  // present.
  if (sandbox.HasFeature("isolated-persistent-storage")) {
    auto data_path = isolated_data_path_factory();
    if (data_path.is_ok()) {
      PushDirectoryFromPathAs(data_path.value(), "/data");
    } else {
      return data_path.error();
    }
  }

  for (const auto& feature : sandbox.features()) {
    if (feature == "root-ssl-certificates") {
      PushDirectoryFromPathAs("/pkgfs/packages/root_ssl_certificates/0/data", "/config/ssl");
    } else if (feature == "deprecated-global-data") {
      FX_LOGS(WARNING) << "Component " << ns_id
                       << " is not allowlisted to use deprecated-global-data."
                       << " See: https://fxbug.dev/36227#c43";
    } else if (feature == "deprecated-global-tmp") {
      FX_LOGS(WARNING) << "Component " << ns_id
                       << " is not allowlisted to use deprecated-global-tmp."
                       << " See: https://fxbug.dev/36227#c43";
    } else if (feature == "deprecated-global-hub") {
      FX_LOGS(WARNING) << "Component " << ns_id
                       << " is not allowlisted to use deprecated-global-hub."
                       << " See: https://fxbug.dev/36227#c43";
    } else if (feature == "shell-commands") {
      PushDirectoryFromPathAs("/pkgfs/packages/shell-commands/0/bin", "/bin");
    } else if (feature == "vulkan") {
      PushDirectoryFromPath("/dev/class/goldfish-address-space");
      PushDirectoryFromPath("/dev/class/goldfish-control");
      PushDirectoryFromPath("/dev/class/goldfish-pipe");
      PushDirectoryFromPath("/dev/class/goldfish-sync");
      PushDirectoryFromPath("/dev/class/gpu");
      PushDirectoryFromPathAs("/pkgfs/packages/config-data/0/meta/data/vulkan-icd/icd.d",
                              "/config/vulkan/icd.d");
    } else if (feature == "isolated-cache-storage") {
      PushDirectoryFromPathAs(isolated_cache_path_factory().value(), "/cache");
    } else if (feature == "isolated-temp") {
      PushDirectoryFromPathAs(isolated_temp_path_factory().value(), "/tmp");
    } else if (feature == "hub") {
      AddHub(hub_directory_factory);
    }
  }

  for (const auto& path : sandbox.boot())
    PushDirectoryFromPath("/boot/" + path);

  return ZX_OK;
}

void NamespaceBuilder::PushDirectoryFromPath(std::string path) {
  PushDirectoryFromPathAs(path, path);
}

void NamespaceBuilder::PushDirectoryFromPathAs(std::string src_path, std::string dst_path) {
  // The POSIX flags below specify that the resulting directory will inherit the maximum set of
  // rights from the root connection serving the namespace (write and/or execute).
  fio::wire::OpenFlags flags =
      fio::wire::OpenFlags::kDirectory | fio::wire::OpenFlags::kRightReadable |
      fio::wire::OpenFlags::kPosixWritable | fio::wire::OpenFlags::kPosixExecutable;
  PushDirectoryFromPathAsWithPermissions(std::move(src_path), std::move(dst_path), flags);
}

void NamespaceBuilder::PushDirectoryFromPathAsWithPermissions(std::string src_path,
                                                              std::string dst_path,
                                                              fio::wire::OpenFlags flags) {
  if (std::find(paths_.begin(), paths_.end(), dst_path) != paths_.end()) {
    return;
  }
  fbl::unique_fd dir;
  zx_status_t status =
      fdio_open_fd(src_path.c_str(), static_cast<uint32_t>(flags), dir.reset_and_get_address());
  if (status != ZX_OK) {
    return;
  }
  fdio_cpp::FdioCaller caller(std::move(dir));
  zx::result channel = caller.take_directory();
  if (channel.is_error()) {
    FX_DLOGS(WARNING) << "Failed to clone channel for " << src_path << ": "
                      << channel.status_string();
    return;
  }
  PushDirectoryFromChannel(std::move(dst_path), fidl::InterfaceHandle<fuchsia::io::Directory>(
                                                    channel.value().TakeChannel()));
}

void NamespaceBuilder::PushDirectoryFromChannel(
    std::string path, fidl::InterfaceHandle<fuchsia::io::Directory> channel) {
  FX_DCHECK(std::find(paths_.begin(), paths_.end(), path) == paths_.end());
  types_.push_back(PA_HND(PA_NS_DIR, types_.size()));
  handles_.push_back(channel.channel().get());
  paths_.push_back(std::move(path));

  handle_pool_.push_back(std::move(channel));
}

fdio_flat_namespace_t* NamespaceBuilder::Build() {
  path_data_.resize(paths_.size());
  for (size_t i = 0; i < paths_.size(); ++i)
    path_data_[i] = paths_[i].c_str();

  flat_ns_.count = types_.size();
  flat_ns_.handle = handles_.data();
  flat_ns_.type = types_.data();
  flat_ns_.path = path_data_.data();
  Release();
  return &flat_ns_;
}

fuchsia::sys::FlatNamespace NamespaceBuilder::BuildForRunner() {
  fuchsia::sys::FlatNamespace flat_namespace;
  flat_namespace.paths.clear();
  flat_namespace.directories.clear();

  for (auto& path : paths_) {
    flat_namespace.paths.push_back(std::move(path));
  }

  for (auto& handle : handle_pool_) {
    flat_namespace.directories.push_back(std::move(handle));
  }
  return flat_namespace;
}

void NamespaceBuilder::Release() {
  for (auto& handle : handle_pool_) {
    __UNUSED const zx_handle_t unused = handle.TakeChannel().release();
  }
  handle_pool_.clear();
}

}  // namespace component
