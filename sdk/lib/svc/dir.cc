// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/fuchsia.io/cpp/wire.h>
#include <lib/svc/dir.h>
#include <lib/zx/channel.h>
#include <zircon/assert.h>
#include <zircon/errors.h>
#include <zircon/types.h>

#include <algorithm>
#include <memory>
#include <utility>

#include <fbl/ref_ptr.h>
#include <src/lib/storage/vfs/cpp/pseudo_dir.h>
#include <src/lib/storage/vfs/cpp/remote_dir.h>
#include <src/lib/storage/vfs/cpp/service.h>
#include <src/lib/storage/vfs/cpp/synchronous_vfs.h>
#include <src/lib/storage/vfs/cpp/vnode.h>

namespace {

constexpr char kPathDelimiter = '/';
constexpr size_t kPathDelimiterSize = 1;

// Adds a new empty directory |name| to |dir| and sets |out| to new directory.
zx_status_t AddNewEmptyDirectory(fbl::RefPtr<fs::PseudoDir> dir, const std::string& name,
                                 fbl::RefPtr<fs::PseudoDir>* out) {
  auto subdir = fbl::MakeRefCounted<fs::PseudoDir>();
  zx_status_t status = dir->AddEntry(name, subdir);
  if (status == ZX_OK) {
    *out = subdir;
  }
  return status;
}

// Disallow empty paths and paths like `.`, `..`, and so on.
bool IsPathValid(const std::string& path) {
  return !path.empty() && !std::all_of(path.cbegin(), path.cend(), [](char c) { return c == '.'; });
}

zx_status_t GetDirectory(fbl::RefPtr<fs::PseudoDir> dir, const std::string& name,
                         bool create_if_empty, fbl::RefPtr<fs::PseudoDir>* out) {
  if (!IsPathValid(name)) {
    return ZX_ERR_INVALID_ARGS;
  }

  fbl::RefPtr<fs::Vnode> node = nullptr;
  zx_status_t status = dir->Lookup(name, &node);
  if (status != ZX_OK) {
    if (!create_if_empty) {
      return status;
    }

    return AddNewEmptyDirectory(dir, name, out);
  }

  *out = fbl::RefPtr<fs::PseudoDir>::Downcast(node);

  return ZX_OK;
}

zx_status_t GetDirectoryByPath(fbl::RefPtr<fs::PseudoDir> root, const std::string& path,
                               bool create_if_empty, fbl::RefPtr<fs::PseudoDir>* out) {
  *out = std::move(root);

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

zx_status_t AddServiceEntry(fbl::RefPtr<fs::PseudoDir> node, const std::string& service_name,
                            void* context, svc_connector_t handler) {
  return node->AddEntry(service_name,
                        fbl::MakeRefCounted<fs::Service>([service_name = std::string(service_name),
                                                          context, handler](zx::channel channel) {
                          handler(context, service_name.c_str(), channel.release());

                          return ZX_OK;
                        }));
}

}  // namespace

struct svc_dir {
  std::unique_ptr<fs::SynchronousVfs> vfs = nullptr;
  fbl::RefPtr<fs::PseudoDir> root = fbl::MakeRefCounted<fs::PseudoDir>();
};

zx_status_t svc_dir_create(async_dispatcher_t* dispatcher, zx_handle_t dir_request,
                           svc_dir_t** result) {
  svc_dir_create_without_serve(result);
  zx_status_t status = svc_dir_serve(*result, dispatcher, dir_request);
  if (status != ZX_OK) {
    svc_dir_destroy(*result);
    return status;
  }

  return ZX_OK;
}

zx_status_t svc_dir_create_without_serve(svc_dir_t** result) {
  *result = new svc_dir_t;
  return ZX_OK;
}

zx_status_t svc_dir_serve(svc_dir_t* dir, async_dispatcher_t* dispatcher, zx_handle_t request) {
  if (dir == nullptr) {
    return ZX_ERR_INVALID_ARGS;
  }

  if (dir->vfs == nullptr) {
    dir->vfs = std::make_unique<fs::SynchronousVfs>(dispatcher);
  }

  return dir->vfs->Serve(dir->root, zx::channel(request), fs::VnodeConnectionOptions::ReadWrite());
}

zx_status_t svc_dir_add_service(svc_dir_t* dir, const char* type, const char* service_name,
                                void* context, svc_connector_t handler) {
  const char* path = type == nullptr ? "" : type;
  return svc_dir_add_service_by_path(dir, path, service_name, context, handler);
}

zx_status_t svc_dir_add_service_by_path(svc_dir_t* dir, const char* path, const char* service_name,
                                        void* context, svc_connector_t* handler) {
  fbl::RefPtr<fs::PseudoDir> node;
  zx_status_t status = GetDirectoryByPath(dir->root, path, /*create_if_empty=*/true, &node);
  if (status != ZX_OK) {
    return status;
  }

  return AddServiceEntry(node, service_name, context, handler);
}

zx_status_t svc_dir_add_directory(svc_dir_t* dir, const char* name, zx_handle_t subdir) {
  return svc_dir_add_directory_by_path(dir, /*path=*/nullptr, name, subdir);
}

zx_status_t svc_dir_add_directory_by_path(svc_dir_t* dir, const char* path, const char* name,
                                          zx_handle_t subdir) {
  if (dir == nullptr || name == nullptr || subdir == ZX_HANDLE_INVALID) {
    return ZX_ERR_INVALID_ARGS;
  }

  fbl::RefPtr<fs::PseudoDir> node;
  // Create an empty string manually if `path` is nullptr. Compiler crashes when
  // constructing a string from a nullptr.
  std::string safe_path = path != nullptr ? path : "";
  zx_status_t status = GetDirectoryByPath(dir->root, safe_path, /*create_if_empty=*/true, &node);
  if (status != ZX_OK) {
    return status;
  }

  fidl::ClientEnd<fuchsia_io::Directory> client_end((zx::channel(subdir)));
  auto remote_dir = fbl::MakeRefCounted<fs::RemoteDir>(std::move(client_end));
  return node->AddEntry(name, std::move(remote_dir));
}

zx_status_t svc_dir_remove_service(svc_dir_t* dir, const char* type, const char* service_name) {
  const char* path = type == nullptr ? "" : type;
  return svc_dir_remove_service_by_path(dir, path, service_name);
}

zx_status_t svc_dir_remove_service_by_path(svc_dir_t* dir, const char* path,
                                           const char* service_name) {
  return svc_dir_remove_entry_by_path(dir, path, service_name);
}

zx_status_t svc_dir_remove_directory(svc_dir_t* dir, const char* name) {
  if (dir == nullptr || name == nullptr) {
    return ZX_ERR_INVALID_ARGS;
  }

  return dir->root->RemoveEntry(name);
}

zx_status_t svc_dir_remove_entry_by_path(svc_dir_t* dir, const char* path, const char* name) {
  fbl::RefPtr<fs::PseudoDir> parent_directory;
  zx_status_t status =
      GetDirectoryByPath(dir->root, path, /*create_if_empty=*/false, &parent_directory);
  if (status != ZX_OK) {
    return status;
  }

  fbl::RefPtr<fs::Vnode> node;
  status = parent_directory->Lookup(name, &node);
  if (status != ZX_OK) {
    return status;
  }

  status = parent_directory->RemoveEntry(name, node.get());
  if (status == ZX_OK) {
    dir->vfs->CloseAllConnectionsForVnode(*node, nullptr);
  }

  return status;
}

zx_status_t svc_dir_destroy(svc_dir_t* dir) {
  delete dir;
  return ZX_OK;
}
