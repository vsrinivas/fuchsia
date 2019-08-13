// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/svc/dir.h>
#include <lib/sys/cpp/outgoing_directory.h>
#include <lib/vfs/cpp/pseudo_dir.h>
#include <lib/vfs/cpp/service.h>

struct svc_dir {
  sys::OutgoingDirectory impl;
};

zx_status_t svc_dir_create(async_dispatcher_t* dispatcher, zx_handle_t dir_request,
                           svc_dir_t** result) {
  svc_dir_t* dir = new svc_dir_t;
  zx_status_t status = dir->impl.Serve(zx::channel(dir_request), dispatcher);
  if (status != ZX_OK) {
    delete dir;
    return status;
  }
  *result = dir;
  return ZX_OK;
}

zx_status_t svc_dir_add_service(svc_dir_t* dir, const char* type, const char* service_name,
                                void* context, svc_connector_t handler) {
  // |node| is owned by |dir|, which means we can hold a raw pointer to |node| because
  // our client isn't allowed to delete |dir| out from under this function.
  vfs::PseudoDir* node = dir->impl.root_dir();
  if (type != nullptr) {
    node = dir->impl.GetOrCreateDirectory(type);
  }
  return node->AddEntry(
      service_name,
      std::make_unique<vfs::Service>([service_name = std::string(service_name), context, handler](
                                         zx::channel channel, async_dispatcher_t* dispatcher) {
        // We drop |dispatcher| on the floor because the libsvc.so declaration of |svc_connector_t|
        // doesn't receive a |dispatcher|. Callees are likely to use the default
        // |async_dispatcher_t| for the current thread.
        handler(context, service_name.c_str(), channel.release());
      }));
}

zx_status_t svc_dir_remove_service(svc_dir_t* dir, const char* type, const char* service_name) {
  // |node| is owned by |dir|, which means we can hold a raw pointer to |node| because
  // our client isn't allowed to delete |dir| out from under this function.
  vfs::PseudoDir* node = dir->impl.root_dir();
  if (type != nullptr) {
    node = dir->impl.GetOrCreateDirectory(type);
  }
  return node->RemoveEntry(service_name);
}

zx_status_t svc_dir_destroy(svc_dir_t* dir) {
  delete dir;
  return ZX_OK;
}
