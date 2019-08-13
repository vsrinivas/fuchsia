// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_FS_PTY_SERVICE_H_
#define LIB_FS_PTY_SERVICE_H_

#include <fuchsia/io/llcpp/fidl.h>
#include <lib/fs-pty/tty-connection-internal.h>
#include <lib/zx/eventpair.h>

#include <utility>

#include <fs/vfs.h>
#include <fs/vnode.h>

namespace fs_pty {

// This is roughly the same as fs::Service, but GetNodeInfo returns a TTY type
// ConsoleOps should be a type that implements methods
// static zx_status_t Read(const Console&, void* data, size_t len, size_t* out_actual);
// static zx_status_t Write(const Console&, const void* data, size_t len, size_t* out_actual);
// static zx_status_t GetEvent(const Console&, zx::eventpair* event);
template <typename Connection, typename ConsoleOps, typename Console>
class Service final : public fs::Vnode {
 public:
  using SelfType = Service<Connection, ConsoleOps, Console>;

  explicit Service(Console console) : console_(std::move(console)) {}

  ~Service() override = default;

  Service(const SelfType&) = delete;
  Service(SelfType&&) = delete;
  SelfType& operator=(const SelfType&) = delete;
  SelfType& operator=(SelfType&&) = delete;

  // |Vnode| implementation:
  zx_status_t ValidateFlags([[maybe_unused]] uint32_t flags) override { return ZX_OK; }

  zx_status_t Getattr(vnattr_t* attr) override {
    memset(attr, 0, sizeof(vnattr_t));
    attr->mode = V_TYPE_CDEV | V_IRUSR | V_IWUSR;
    attr->nlink = 1;
    return ZX_OK;
  }

  zx_status_t Serve(fs::Vfs* vfs, zx::channel svc_request, uint32_t flags) override {
    return vfs->ServeConnection(std::make_unique<Connection>(console_, vfs, fbl::WrapRefPtr(this),
                                                             std::move(svc_request), flags));
  }

  [[nodiscard]] bool IsDirectory() const override { return false; }

  zx_status_t GetNodeInfo([[maybe_unused]] uint32_t flags, fuchsia_io_NodeInfo* info) override {
    zx::eventpair event;
    zx_status_t status = ConsoleOps::GetEvent(console_, &event);
    if (status != ZX_OK) {
      return status;
    }
    info->tag = static_cast<fidl_union_tag_t>(::llcpp::fuchsia::io::NodeInfo::Tag::kTty);
    info->tty.event = event.release();
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

 private:
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
using TtyService = Service<::fs_pty::internal::TtyConnection<Console>, ConsoleOps, Console>;

}  // namespace fs_pty

#endif  // LIB_FS_PTY_SERVICE_H_
