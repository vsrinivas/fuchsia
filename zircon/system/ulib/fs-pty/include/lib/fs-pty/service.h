// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_FS_PTY_SERVICE_H_
#define LIB_FS_PTY_SERVICE_H_

#include <fuchsia/hardware/pty/llcpp/fidl.h>
#include <fuchsia/io/llcpp/fidl.h>
#include <lib/fs-pty/tty-connection-internal.h>
#include <lib/zx/eventpair.h>

#include <type_traits>
#include <utility>

#include <fs/vfs.h>
#include <fs/vfs_types.h>
#include <fs/vnode.h>

namespace fs_pty {

namespace internal {

void DispatchPtyDeviceMessage(::llcpp::fuchsia::hardware::pty::Device::Interface* interface,
                              fidl_incoming_msg_t* msg, fidl::Transaction* txn);

}

// This is roughly the same as fs::Service, but GetNodeInfo returns a TTY type
// ConsoleOps should be a type that implements methods
// static zx_status_t Read(const Console&, void* data, size_t len, size_t* out_actual);
// static zx_status_t Write(const Console&, const void* data, size_t len, size_t* out_actual);
// static zx_status_t GetEvent(const Console&, zx::eventpair* event);
//
// |PtyDeviceImpl| allows users to inject an implementation of |fuchsia.hardware.pty/Device|.
template <typename PtyDeviceImpl, typename ConsoleOps, typename Console>
class Service : public fs::Vnode {
 public:
  using SelfType = Service<PtyDeviceImpl, ConsoleOps, Console>;

  static_assert(
      std::is_base_of<::llcpp::fuchsia::hardware::pty::Device::Interface, PtyDeviceImpl>::value);

  explicit Service(Console console) : pty_device_impl_(console), console_(console) {}

  ~Service() override = default;

  Service(const SelfType&) = delete;
  Service(SelfType&&) = delete;
  SelfType& operator=(const SelfType&) = delete;
  SelfType& operator=(SelfType&&) = delete;

  // |Vnode| implementation:
  fs::VnodeProtocolSet GetProtocols() const final { return fs::VnodeProtocol::kTty; }

  zx_status_t GetAttributes(fs::VnodeAttributes* attr) override {
    *attr = fs::VnodeAttributes();
    attr->mode = V_TYPE_CDEV | V_IRUSR | V_IWUSR;
    attr->link_count = 1;
    return ZX_OK;
  }

  // From fs::Vnode
  void HandleFsSpecificMessage(fidl_incoming_msg_t* msg, fidl::Transaction* txn) override {
    auto pty_device_interface =
        static_cast<llcpp::fuchsia::hardware::pty::Device::Interface*>(&pty_device_impl_);
    internal::DispatchPtyDeviceMessage(pty_device_interface, msg, txn);
  }

  zx_status_t GetNodeInfoForProtocol([[maybe_unused]] fs::VnodeProtocol protocol,
                                     [[maybe_unused]] fs::Rights rights,
                                     fs::VnodeRepresentation* info) override {
    zx::eventpair event;
    zx_status_t status = ConsoleOps::GetEvent(console_, &event);
    if (status != ZX_OK) {
      return status;
    }
    *info = fs::VnodeRepresentation::Tty{.event = std::move(event)};
    return ZX_OK;
  }

  zx_status_t Read(void* data, size_t len, [[maybe_unused]] zx_off_t offset,
                   size_t* out_actual) override {
    return ConsoleOps::Read(console_, data, len, out_actual);
  }

  zx_status_t Write(const void* data, size_t len, [[maybe_unused]] zx_off_t offset,
                    size_t* out_actual) override {
    return ConsoleOps::Write(console_, data, len, out_actual);
  }

 protected:
  PtyDeviceImpl pty_device_impl_;
  Console console_;
};

// Simple ConsoleOps implementation for the special-case where the Console type
// is a pointer to an object that implements the functions.
template <typename Console>
struct SimpleConsoleOps {
 public:
  static zx_status_t Read(const Console& console, void* data, size_t len, size_t* out_actual) {
    return console->Read(data, len, out_actual);
  }
  static zx_status_t Write(const Console& console, const void* data, size_t len,
                           size_t* out_actual) {
    return console->Write(data, len, out_actual);
  }
  static zx_status_t GetEvent(const Console& console, zx::eventpair* event) {
    return console->GetEvent(event);
  }
};

// An alias for a service that just wants to return ZX_ERR_NOT_SUPPORTED for all
// of the PTY requests.
template <typename ConsoleOps, typename Console>
using TtyService = Service<::fs_pty::internal::NullPtyDevice<Console>, ConsoleOps, Console>;

}  // namespace fs_pty

#endif  // LIB_FS_PTY_SERVICE_H_
