// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_BIN_DRIVER_HOST_ZX_DRIVER_H_
#define SRC_DEVICES_BIN_DRIVER_HOST_ZX_DRIVER_H_

#include <zircon/types.h>

#include <fbl/intrusive_double_list.h>
#include <fbl/ref_counted.h>
#include <fbl/ref_ptr.h>
#include <fbl/string.h>

typedef struct fx_logger fx_logger_t;

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

// Note that this must be a struct to match the public opaque declaration.
struct zx_driver : fbl::DoublyLinkedListable<fbl::RefPtr<zx_driver>>, fbl::RefCounted<zx_driver> {
  static zx_status_t Create(std::string_view libname, fbl::RefPtr<zx_driver>* out_driver);

  ~zx_driver();

  const char* name() const { return name_; }

  zx_driver_rec_t* driver_rec() const { return driver_rec_; }

  zx_status_t status() const { return status_; }

  const fbl::String& libname() const { return libname_; }

  void set_name(const char* name) { name_ = name; }

  void set_driver_rec(zx_driver_rec_t* driver_rec) { driver_rec_ = driver_rec; }

  void set_ops(const zx_driver_ops_t* ops) { ops_ = ops; }

  void set_status(zx_status_t status) { status_ = status; }

  fx_logger_t* logger() const { return logger_; }

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
  explicit zx_driver(std::string_view libname);

  const char* name_ = nullptr;
  zx_driver_rec_t* driver_rec_ = nullptr;
  const zx_driver_ops_t* ops_ = nullptr;
  void* ctx_ = nullptr;
  fx_logger_t* logger_ = nullptr;

  fbl::String libname_;
  zx_status_t status_ = ZX_OK;
};

#endif  // SRC_DEVICES_BIN_DRIVER_HOST_ZX_DRIVER_H_
