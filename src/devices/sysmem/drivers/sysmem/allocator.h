// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_SYSMEM_DRIVERS_SYSMEM_ALLOCATOR_H_
#define SRC_DEVICES_SYSMEM_DRIVERS_SYSMEM_ALLOCATOR_H_

#include <fuchsia/sysmem/llcpp/fidl.h>
#include <lib/zx/channel.h>

#include "device.h"
#include "logging.h"
#include "logical_buffer_collection.h"

namespace sysmem_driver {

// An instance of this class serves an Allocator connection.  The lifetime of
// the instance is 1:1 with the Allocator channel.
//
// Because Allocator is essentially self-contained and handling the server end
// of a channel, most of Allocator is private.
class Allocator : public fidl::WireServer<fuchsia_sysmem::Allocator>, public LoggingMixin {
 public:
  // Public for std::unique_ptr<Allocator>:
  ~Allocator();

  static void CreateChannelOwned(zx::channel request, Device* device);

 private:
  Allocator(Device* parent_device);

  void AllocateNonSharedCollection(AllocateNonSharedCollectionRequestView request,
                                   AllocateNonSharedCollectionCompleter::Sync& completer) override;
  void AllocateSharedCollection(AllocateSharedCollectionRequestView request,
                                AllocateSharedCollectionCompleter::Sync& completer) override;
  void BindSharedCollection(BindSharedCollectionRequestView request,
                            BindSharedCollectionCompleter::Sync& completer) override;
  void ValidateBufferCollectionToken(
      ValidateBufferCollectionTokenRequestView request,
      ValidateBufferCollectionTokenCompleter::Sync& completer) override;
  void SetDebugClientInfo(SetDebugClientInfoRequestView request,
                          SetDebugClientInfoCompleter::Sync& completer) override;

  Device* parent_device_ = nullptr;

  std::optional<ClientDebugInfo> client_debug_info_;
};

}  // namespace sysmem_driver

#endif  // SRC_DEVICES_SYSMEM_DRIVERS_SYSMEM_ALLOCATOR_H_
