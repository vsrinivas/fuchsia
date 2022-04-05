// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_BIN_DRIVER_HOST_ZX_DRIVER_H_
#define SRC_DEVICES_BIN_DRIVER_HOST_ZX_DRIVER_H_

#include <fidl/fuchsia.device.manager/cpp/wire.h>
#include <lib/fdf/internal.h>
#include <lib/fidl/llcpp/client.h>
#include <lib/syslog/logger.h>
#include <lib/trace/event.h>
#include <zircon/types.h>

#include <fbl/intrusive_double_list.h>
#include <fbl/ref_counted.h>
#include <fbl/ref_ptr.h>
#include <fbl/string.h>
#include <fbl/string_buffer.h>

#include "driver_stack_manager.h"
#include "src/devices/bin/driver_host/inspect.h"

class DriverInspect;
struct InspectNodeCollection;

class Driver;

namespace internal {

struct BindContext {
  fbl::RefPtr<zx_device_t> parent;
  fbl::RefPtr<zx_device_t> child;
};

struct CreationContext {
  fbl::RefPtr<zx_device_t> parent;
  fbl::RefPtr<zx_device_t> child;
  fidl::WireSharedClient<fuchsia_device_manager::Coordinator> coordinator_client;
};

void set_bind_context(internal::BindContext* ctx);
void set_creation_context(internal::CreationContext* ctx);

}  // namespace internal

// Note that this must be a struct to match the public opaque declaration.
struct zx_driver : fbl::DoublyLinkedListable<fbl::RefPtr<zx_driver>>, fbl::RefCounted<zx_driver> {
  // |drivers| should outlive zx_driver struct
  static zx_status_t Create(std::string_view libname, InspectNodeCollection& drivers,
                            fbl::RefPtr<zx_driver>* out_driver);

  ~zx_driver();

  const char* name() const { return name_; }

  zx_driver_rec_t* driver_rec() const { return driver_rec_; }

  zx_status_t status() const { return status_; }

  const fbl::String& libname() const { return libname_; }

  void set_name(const char* name) {
    name_ = name;
    inspect_.set_name(name);
    ReconfigureLogger({});
  }

  void set_driver_rec(zx_driver_rec_t* driver_rec) { driver_rec_ = driver_rec; }

  void set_ops(const zx_driver_ops_t* ops) {
    ops_ = ops;
    inspect_.set_ops(ops);
  }

  void set_status(zx_status_t status) {
    status_ = status;
    inspect_.set_status(status);
  }

  zx_status_t set_driver_min_log_severity(uint32_t severity) {
    inspect_.set_driver_min_log_severity(severity);
    return fx_logger_set_min_severity(logger(), severity);
  }

  fx_logger_t* logger() const { return logger_; }

  DriverInspect& inspect() { return inspect_; }

  // Interface to |ops|. These names contain Op in order to not
  // collide with e.g. RefPtr names.

  bool has_init_op() const { return ops_->init != nullptr; }

  bool has_bind_op() const { return ops_->bind != nullptr; }

  bool has_create_op() const { return ops_->create != nullptr; }

  bool has_run_unit_tests_op() const { return ops_->run_unit_tests != nullptr; }

  zx_status_t InitOp(const fbl::RefPtr<Driver>& driver);

  zx_status_t BindOp(internal::BindContext* bind_context, const fbl::RefPtr<Driver>& driver,
                     const fbl::RefPtr<zx_device_t>& device) const;

  zx_status_t CreateOp(internal::CreationContext* creation_context,
                       const fbl::RefPtr<Driver>& driver, const fbl::RefPtr<zx_device_t>& parent,
                       const char* name, const char* args, zx_handle_t rpc_channel) const;

  void ReleaseOp(const fbl::RefPtr<Driver>& driver) const;

  bool RunUnitTestsOp(const fbl::RefPtr<zx_device_t>& parent, const fbl::RefPtr<Driver>& driver,
                      zx::channel test_output) const;

  zx_status_t ReconfigureLogger(cpp20::span<const char* const> tags) const;

 private:
  friend std::unique_ptr<zx_driver> std::make_unique<zx_driver>();
  zx_driver(fx_logger_t* logger, std::string_view libname, InspectNodeCollection& drivers);

  const char* name_ = nullptr;
  zx_driver_rec_t* driver_rec_ = nullptr;
  const zx_driver_ops_t* ops_ = nullptr;
  void* ctx_ = nullptr;

  fx_logger_t* logger_;
  fbl::String libname_;
  zx_status_t status_ = ZX_OK;

  DriverInspect inspect_;
};

#endif  // SRC_DEVICES_BIN_DRIVER_HOST_ZX_DRIVER_H_
