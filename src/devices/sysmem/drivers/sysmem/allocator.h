// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_SYSMEM_DRIVERS_SYSMEM_ALLOCATOR_H_
#define SRC_DEVICES_SYSMEM_DRIVERS_SYSMEM_ALLOCATOR_H_

#include "device.h"
#include "fuchsia/sysmem/c/fidl.h"
#include "lib/fidl-async-2/fidl_server.h"
#include "lib/fidl-async-2/simple_binding.h"
#include "logging.h"

namespace sysmem_driver {

// An instance of this class serves an Allocator connection.  The lifetime of
// the instance is 1:1 with the Allocator channel.
//
// Because Allocator is essentially self-contained and handling the server end
// of a channel, most of Allocator is private.
class Allocator : public FidlServer<Allocator,
                                    SimpleBinding<Allocator, fuchsia_sysmem_Allocator_ops_t,
                                                  fuchsia_sysmem_Allocator_dispatch>,
                                    vLog> {
 public:
  // Public for std::unique_ptr<Allocator>:
  ~Allocator();

 private:
  friend class FidlServer;
  Allocator(Device* parent_device);

  static const fuchsia_sysmem_Allocator_ops_t kOps;

  zx_status_t AllocateNonSharedCollection(zx_handle_t buffer_collection_request_param);
  zx_status_t AllocateSharedCollection(zx_handle_t token_request);
  zx_status_t BindSharedCollection(zx_handle_t token, zx_handle_t buffer_collection);

  Device* parent_device_ = nullptr;
};

}  // namespace sysmem_driver

#endif  // SRC_DEVICES_SYSMEM_DRIVERS_SYSMEM_ALLOCATOR_H_
