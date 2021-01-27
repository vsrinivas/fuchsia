// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_SYSMEM_DRIVERS_SYSMEM_BUFFER_COLLECTION_TOKEN_H_
#define SRC_DEVICES_SYSMEM_DRIVERS_SYSMEM_BUFFER_COLLECTION_TOKEN_H_

#include <fuchsia/sysmem/c/fidl.h>
#include <fuchsia/sysmem/llcpp/fidl.h>
#include <lib/fidl-async-2/fidl_server.h>
#include <lib/fidl-async-2/simple_binding.h>

#include "binding_handle.h"
#include "logging.h"
#include "logical_buffer_collection.h"

namespace sysmem_driver {

class BufferCollectionToken;

class BufferCollectionToken : public llcpp::fuchsia::sysmem::BufferCollectionToken::Interface,
                              public LoggingMixin,
                              public fbl::RefCounted<BufferCollectionToken> {
 public:
  ~BufferCollectionToken();

  static BindingHandle<BufferCollectionToken> Create(Device* parent_device,
                                                     fbl::RefPtr<LogicalBufferCollection> parent,
                                                     uint32_t rights_attenuation_mask);

  void SetErrorHandler(fit::function<void(zx_status_t)> error_handler) {
    error_handler_ = std::move(error_handler);
  }
  void Bind(zx::channel channel);

  void Duplicate(uint32_t rights_attenuation_mask, zx::channel buffer_collection_token_request,
                 DuplicateCompleter::Sync& completer) override;
  void Sync(SyncCompleter::Sync&) override;
  void Close(CloseCompleter::Sync&) override;
  void SetName(uint32_t priority, fidl::StringView name, SetNameCompleter::Sync&) override;
  void SetDebugClientInfo(fidl::StringView, uint64_t id,
                          SetDebugClientInfoCompleter::Sync&) override;
  void SetDebugTimeoutLogDeadline(int64_t deadline,
                                  SetDebugTimeoutLogDeadlineCompleter::Sync&) override;

  void SetDebugClientInfo(std::string name, uint64_t id);

  LogicalBufferCollection* parent();
  fbl::RefPtr<LogicalBufferCollection> parent_shared();

  void SetServerKoid(zx_koid_t server_koid);

  zx_koid_t server_koid();

  bool is_done();

  bool was_unfound_token() const { return was_unfound_token_; }

  void SetBufferCollectionRequest(zx::channel buffer_collection_request);

  zx::channel TakeBufferCollectionRequest();

  const std::string& debug_name() const { return debug_info_.name; }
  uint64_t debug_id() const { return debug_info_.id; }

  void CloseChannel();

 private:
  friend class FidlServer;
  BufferCollectionToken(Device* parent_device, fbl::RefPtr<LogicalBufferCollection> parent,
                        uint32_t rights_attenuation_mask);

  void FailAsync(Location location, zx_status_t status, const char* format, ...);

  Device* parent_device_ = nullptr;
  fbl::RefPtr<LogicalBufferCollection> parent_;
  // 1 bit means the right is allowed.  0 bit means the right is attenuated.
  //
  // TODO(fxbug.dev/50578): Finish plumbing this.
  uint32_t rights_attenuation_mask_ = std::numeric_limits<uint32_t>::max();
  fit::function<void(zx_status_t)> error_handler_;

  zx_koid_t server_koid_ = ZX_KOID_INVALID;

  // Becomes true on the first Close() or BindSharedCollection().  This being
  // true means a channel close is not fatal to the LogicalBufferCollection.
  // However, if the client sends a redundant Close(), that is fatal to the
  // BufferCollectionToken and fatal to the LogicalBufferCollection.
  bool is_done_ = false;

  bool was_unfound_token_ = false;

  std::optional<fidl::ServerBindingRef<llcpp::fuchsia::sysmem::BufferCollectionToken>>
      server_binding_;

  // This is set up to once during
  // LogicalBufferCollection::BindSharedCollection(), and essentially curries
  // the buffer_collection_request past the processing of any remaining
  // inbound messages on the BufferCollectionToken before starting to serve
  // the BufferCollection that the token was exchanged for.  This way, inbound
  // Duplicate() messages in the BufferCollectionToken are seen before any
  // BufferCollection::SetConstraints() (which might otherwise try to allocate
  // buffers too soon before all tokens are gone).
  zx::channel buffer_collection_request_;

  LogicalBufferCollection::ClientInfo debug_info_;
  inspect::Node node_;
  inspect::UintProperty debug_id_property_;
  inspect::StringProperty debug_name_property_;
  inspect::ValueList properties_;
};

}  // namespace sysmem_driver

#endif  // SRC_DEVICES_SYSMEM_DRIVERS_SYSMEM_BUFFER_COLLECTION_TOKEN_H_
