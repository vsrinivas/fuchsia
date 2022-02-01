// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/svc/dir.h>
#include <lib/vfs/cpp/pseudo_dir.h>
#include <lib/vfs/cpp/service.h>
#include <zircon/assert.h>
#include <zircon/errors.h>

#include <algorithm>
#include <memory>

namespace {

constexpr char kPathDelimiter = '/';
constexpr size_t kPathDelimiterSize = 1;

// Adds a new empty directory |name| to |dir| and sets |out| to new directory.
zx_status_t AddNewEmptyDirectory(vfs::PseudoDir* dir, std::string name, vfs::PseudoDir** out) {
  auto subdir = std::make_unique<vfs::PseudoDir>();
  auto ptr = subdir.get();
  zx_status_t status = dir->AddEntry(std::move(name), std::move(subdir));
  if (status == ZX_OK) {
    *out = ptr;
  }
  return status;
}

// Disallow empty paths and paths like `.`, `..`, and so on.
bool IsPathValid(const std::string& path) {
  return !path.empty() && !std::all_of(path.cbegin(), path.cend(), [](char c) { return c == '.'; });
}

zx_status_t GetDirectory(vfs::PseudoDir* dir, std::string name, bool create_if_empty,
                         vfs::PseudoDir** out) {
  if (!IsPathValid(name)) {
    return ZX_ERR_INVALID_ARGS;
  }

  vfs::internal::Node* node = nullptr;
  zx_status_t status = dir->Lookup(name, &node);
  if (status != ZX_OK) {
    if (!create_if_empty) {
      return status;
    }

    return AddNewEmptyDirectory(dir, std::move(name), out);
  }

  *out = static_cast<vfs::PseudoDir*>(node);
  return ZX_OK;
}

zx_status_t GetDirectoryByPath(vfs::PseudoDir* root, std::string path, bool create_if_empty,
                               vfs::PseudoDir** out) {
  *out = root;

  // If empty, return root directory.
  if (path.empty()) {
    return ZX_OK;
  }

  // Don't allow leading nor trailing slashes.
  if (path[0] == kPathDelimiter || path[path.size() - 1] == kPathDelimiter) {
    return ZX_ERR_INVALID_ARGS;
  }

  size_t start_pos = 0;
  size_t end_pos = path.find(kPathDelimiter);

  while (end_pos != std::string::npos) {
    std::string current_path = path.substr(start_pos, end_pos - start_pos);
    zx_status_t status = GetDirectory(*out, current_path, create_if_empty, out);
    if (status != ZX_OK) {
      return status;
    }

    start_pos = end_pos + kPathDelimiterSize;
    end_pos = path.find(kPathDelimiter, start_pos);
  }

  return GetDirectory(*out, path.substr(start_pos), create_if_empty, out);
}

zx_status_t AddServiceEntry(vfs::PseudoDir* node, const std::string& service_name, void* context,
                            svc_connector_t handler) {
  return node->AddEntry(
      service_name,
      std::make_unique<vfs::Service>([service_name = std::string(service_name), context, handler](
                                         zx::channel channel, async_dispatcher_t* dispatcher) {
        // We drop |dispatcher| on the floor because the libsvc.so
        // declaration of |svc_connector_t| doesn't receive a
        // |dispatcher|. Callees are likely to use the default
        // |async_dispatcher_t| for the current thread.
        handler(context, service_name.c_str(), channel.release());
      }));
}

}  // namespace

struct svc_dir {
  std::unique_ptr<vfs::PseudoDir> impl = std::make_unique<vfs::PseudoDir>();
};

zx_status_t svc_dir_create(async_dispatcher_t* dispatcher, zx_handle_t dir_request,
                           svc_dir_t** result) {
  svc_dir_t* dir = new svc_dir_t;
  zx_status_t status =
      dir->impl->Serve(fuchsia::io::OPEN_RIGHT_READABLE | fuchsia::io::OPEN_RIGHT_WRITABLE,
                       zx::channel(dir_request), dispatcher);
  if (status != ZX_OK) {
    delete dir;
    return status;
  }
  *result = dir;
  return ZX_OK;
}

zx_status_t svc_dir_add_service(svc_dir_t* dir, const char* type, const char* service_name,
                                void* context, svc_connector_t handler) {
  const char* path = type == nullptr ? "" : type;
  return svc_dir_add_service_by_path(dir, path, service_name, context, handler);
}

zx_status_t svc_dir_add_service_by_path(svc_dir_t* dir, const char* path, const char* service_name,
                                        void* context, svc_connector_t* handler) {
  vfs::PseudoDir* node = nullptr;
  zx_status_t status = GetDirectoryByPath(dir->impl.get(), path, /*create_if_empty=*/true, &node);
  if (status != ZX_OK) {
    return status;
  }

  return AddServiceEntry(node, service_name, context, handler);
}

zx_status_t svc_dir_remove_service(svc_dir_t* dir, const char* type, const char* service_name) {
  const char* path = type == nullptr ? "" : type;
  return svc_dir_remove_service_by_path(dir, path, service_name);
}

zx_status_t svc_dir_remove_service_by_path(svc_dir_t* dir, const char* path,
                                           const char* service_name) {
  vfs::PseudoDir* node = nullptr;
  zx_status_t status = GetDirectoryByPath(dir->impl.get(), path, /*create_if_empty=*/false, &node);
  if (status != ZX_OK) {
    return status;
  }

  return node->RemoveEntry(service_name);
}

zx_status_t svc_dir_destroy(svc_dir_t* dir) {
  delete dir;
  return ZX_OK;
}
