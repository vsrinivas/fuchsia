// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_BIN_DRIVER_HOST_DEVHOST_H_
#define SRC_DEVICES_BIN_DRIVER_HOST_DEVHOST_H_

#include <fuchsia/device/manager/llcpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/async/cpp/wait.h>
#include <lib/zircon-internal/thread_annotations.h>
#include <lib/zx/channel.h>
#include <stdint.h>
#include <threads.h>
#include <zircon/compiler.h>
#include <zircon/fidl.h>
#include <zircon/types.h>

#include <memory>

#include <ddk/binding.h>
#include <ddk/device.h>
#include <ddk/driver.h>
#include <ddktl/fidl.h>
#include <fbl/intrusive_double_list.h>
#include <fbl/ref_counted.h>
#include <fbl/ref_ptr.h>
#include <fbl/string.h>

#include "async_loop_owned_rpc_handler.h"
#include "devfs_connection.h"
#include "driver_host_context.h"
#include "lock.h"
#include "zx_device.h"

namespace fuchsia = ::llcpp::fuchsia;

namespace internal {

struct BindContext {
  fbl::RefPtr<zx_device_t> parent;
  fbl::RefPtr<zx_device_t> child;
};

struct CreationContext {
  fbl::RefPtr<zx_device_t> parent;
  fbl::RefPtr<zx_device_t> child;
  zx::unowned_channel device_controller_rpc;
  zx::unowned_channel coordinator_rpc;
};

void set_bind_context(internal::BindContext* ctx);
void set_creation_context(internal::CreationContext* ctx);

}  // namespace internal

// Nothing outside of devmgr/{devmgr,driver_host,rpc-device}.c
// should be calling internal::*() APIs, as this could
// violate the internal locking design.

// Safe external APIs are in device.h and device_internal.h

// Note that this must be a struct to match the public opaque declaration.
struct zx_driver : fbl::DoublyLinkedListable<fbl::RefPtr<zx_driver>>, fbl::RefCounted<zx_driver> {
  static zx_status_t Create(fbl::RefPtr<zx_driver>* out_driver);

  const char* name() const { return name_; }

  zx_driver_rec_t* driver_rec() const { return driver_rec_; }

  zx_status_t status() const { return status_; }

  const fbl::String& libname() const { return libname_; }

  void set_name(const char* name) { name_ = name; }

  void set_driver_rec(zx_driver_rec_t* driver_rec) { driver_rec_ = driver_rec; }

  void set_ops(const zx_driver_ops_t* ops) { ops_ = ops; }

  void set_status(zx_status_t status) { status_ = status; }

  void set_libname(fbl::StringPiece libname) { libname_ = libname; }

  // Interface to |ops|. These names contain Op in order to not
  // collide with e.g. RefPtr names.

  bool has_init_op() const { return ops_->init != nullptr; }

  bool has_bind_op() const { return ops_->bind != nullptr; }

  bool has_create_op() const { return ops_->create != nullptr; }

  bool has_run_unit_tests_op() const { return ops_->run_unit_tests != nullptr; }

  zx_status_t InitOp() { return ops_->init(&ctx_); }

  zx_status_t BindOp(internal::BindContext* bind_context,
                     const fbl::RefPtr<zx_device_t>& device) const {
    fbl::StringBuffer<32> trace_label;
    trace_label.AppendPrintf("%s:bind", name_);
    TRACE_DURATION("driver_host:driver-hooks", trace_label.data());

    internal::set_bind_context(bind_context);
    auto status = ops_->bind(ctx_, device.get());
    internal::set_bind_context(nullptr);
    return status;
  }

  zx_status_t CreateOp(internal::CreationContext* creation_context,
                       const fbl::RefPtr<zx_device_t>& parent, const char* name, const char* args,
                       zx_handle_t rpc_channel) const {
    internal::set_creation_context(creation_context);
    auto status = ops_->create(ctx_, parent.get(), name, args, rpc_channel);
    internal::set_creation_context(nullptr);
    return status;
  }

  void ReleaseOp() const {
    // TODO(kulakowski/teisenbe) Consider poisoning the ops_ table on release.
    ops_->release(ctx_);
  }

  bool RunUnitTestsOp(const fbl::RefPtr<zx_device_t>& parent, zx::channel test_output) const {
    return ops_->run_unit_tests(ctx_, parent.get(), test_output.release());
  }

 private:
  friend std::unique_ptr<zx_driver> std::make_unique<zx_driver>();
  zx_driver() = default;

  const char* name_ = nullptr;
  zx_driver_rec_t* driver_rec_ = nullptr;
  const zx_driver_ops_t* ops_ = nullptr;
  void* ctx_ = nullptr;
  fbl::String libname_;
  zx_status_t status_ = ZX_OK;
};

extern zx_protocol_device_t device_default_ops;

namespace internal {

// Get the DriverHostContext that should be used by all external API methods
DriverHostContext* ContextForApi();

class DevhostControllerConnection : public AsyncLoopOwnedRpcHandler<DevhostControllerConnection>,
                                    public fuchsia::device::manager::DevhostController::Interface {
 public:
  // |ctx| must outlive this connection
  explicit DevhostControllerConnection(DriverHostContext* ctx) : driver_host_context_(ctx) {}

  static void HandleRpc(std::unique_ptr<DevhostControllerConnection> conn,
                        async_dispatcher_t* dispatcher, async::WaitBase* wait, zx_status_t status,
                        const zx_packet_signal_t* signal);
  zx_status_t HandleRead();

 private:
  void CreateDevice(zx::channel coordinator_rpc, zx::channel device_controller_rpc,
                    ::fidl::StringView driver_path, ::zx::vmo driver, ::zx::handle parent_proxy,
                    ::fidl::StringView proxy_args, uint64_t local_device_id,
                    CreateDeviceCompleter::Sync completer) override;
  void CreateCompositeDevice(zx::channel coordinator_rpc, zx::channel device_controller_rpc,
                             ::fidl::VectorView<uint64_t> fragments, ::fidl::StringView name,
                             uint64_t local_device_id,
                             CreateCompositeDeviceCompleter::Sync completer) override;
  void CreateDeviceStub(zx::channel coordinator_rpc, zx::channel device_controller_rpc,
                        uint32_t protocol_id, uint64_t local_device_id,
                        CreateDeviceStubCompleter::Sync completer) override;

  DriverHostContext* const driver_host_context_;
};

zx_status_t fidl_handler(fidl_msg_t* msg, fidl_txn_t* txn, void* cookie);

// State that is shared between the zx_device implementation and driver_host-core.cpp
void finalize() REQ_DM_LOCK;
extern fbl::DoublyLinkedList<zx_device*, zx_device::DeferNode> defer_device_list USE_DM_LOCK;
extern int enumerators USE_DM_LOCK;

// Lookup the a driver by name, and if it's not found, install the given vmo as
// that driver.
zx_status_t find_driver(fbl::StringPiece libname, zx::vmo vmo, fbl::RefPtr<zx_driver_t>* out);

}  // namespace internal

// Construct a string describing the path of |dev| relative to its most
// distant ancestor in this driver_host.
const char* mkdevpath(const fbl::RefPtr<zx_device_t>& dev, char* path, size_t max);

#endif  // SRC_DEVICES_BIN_DRIVER_HOST_DEVHOST_H_
