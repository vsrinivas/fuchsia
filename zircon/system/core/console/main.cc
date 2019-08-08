// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/boot/llcpp/fidl.h>
#include <lib/fdio/directory.h>
#include <lib/fdio/vfs.h>
#include <lib/svc/outgoing.h>
#include <lib/zx/eventpair.h>
#include <lib/zx/resource.h>
#include <zircon/status.h>
#include <zircon/syscalls.h>

#include "connection.h"
#include "console.h"

namespace {

// This is roughly the same as fs::Service, but GetNodeInfo returns a TTY type
class VfsTty : public fs::Vnode {
 public:
  explicit VfsTty(fbl::RefPtr<Console> console) : console_(console) {}

  ~VfsTty() override = default;

  VfsTty(const VfsTty&) = delete;
  VfsTty(VfsTty&&) = delete;
  VfsTty& operator=(const VfsTty&) = delete;
  VfsTty& operator=(VfsTty&&) = delete;

  // |Vnode| implementation:
  zx_status_t ValidateFlags(uint32_t flags) final { return ZX_OK; }

  zx_status_t Getattr(vnattr_t* attr) final {
    memset(attr, 0, sizeof(vnattr_t));
    attr->mode = V_TYPE_CDEV | V_IRUSR | V_IWUSR;
    attr->nlink = 1;
    return ZX_OK;
  }

  zx_status_t Serve(fs::Vfs* vfs, zx::channel svc_request, uint32_t flags) final {
    return vfs->ServeConnection(std::make_unique<Connection>(console_, vfs, fbl::WrapRefPtr(this),
                                                             std::move(svc_request), flags));
  }

  bool IsDirectory() const final { return false; }

  zx_status_t GetNodeInfo(uint32_t flags, fuchsia_io_NodeInfo* info) final {
    ::llcpp::fuchsia::io::NodeInfo llcpp_info;
    zx_status_t status = console_->GetNodeInfo(&llcpp_info);
    if (status != ZX_OK) {
      return status;
    }
    info->tag = static_cast<fidl_union_tag_t>(::llcpp::fuchsia::io::NodeInfo::Tag::kTty);
    info->tty.event = llcpp_info.mutable_tty().event.release();
    return ZX_OK;
  }

  zx_status_t Read(void* data, size_t len, size_t offset, size_t* out_actual) final {
    return console_->Read(data, len, offset, out_actual);
  }

  zx_status_t Write(const void* data, size_t len, size_t offset, size_t* out_actual) final {
    return console_->Write(data, len, offset, out_actual);
  }

 private:
  fbl::RefPtr<Console> console_;
};

zx::resource GetRootResource() {
  zx::channel local, remote;
  zx_status_t status = zx::channel::create(0, &local, &remote);
  if (status != ZX_OK) {
    return {};
  }
  status = fdio_service_connect("/svc/fuchsia.boot.RootResource", remote.release());
  if (status != ZX_OK) {
    printf("console: Could not connect to RootResource service: %s\n",
           zx_status_get_string(status));
    return {};
  }

  ::llcpp::fuchsia::boot::RootResource::SyncClient client(std::move(local));
  auto result = client.Get();
  if (result.status() != ZX_OK) {
    printf("console: Could not retrieve RootResource: %s\n", zx_status_get_string(result.status()));
    return {};
  }
  zx::resource root_resource(std::move(result.Unwrap()->resource));
  return root_resource;
}

}  // namespace

int main(int argc, const char** argv) {
  zx::resource root_resource(GetRootResource());
  // Provide a RxSource that grabs the data from the kernel serial connection
  Console::RxSource rx_source = [root_resource = std::move(root_resource)](uint8_t* byte) {
    size_t length = 1;
    zx_status_t status = zx_debug_read(root_resource.get(), reinterpret_cast<char*>(byte), &length);
    if (status == ZX_ERR_NOT_SUPPORTED) {
      // Suppress the error print in this case.  No console on this machine.
      return status;
    } else if (status != ZX_OK) {
      printf("console: error %s, length %zu from zx_debug_read syscall, exiting.\n",
             zx_status_get_string(status), length);
      return status;
    }
    if (length != 1) {
      return ZX_ERR_SHOULD_WAIT;
    }
    return ZX_OK;
  };

  Console::TxSink tx_sink = [](const uint8_t* buffer, size_t length) {
    return zx_debug_write(reinterpret_cast<const char*>(buffer), length);
  };

  async::Loop loop(&kAsyncLoopConfigNoAttachToThread);
  async_dispatcher_t* dispatcher = loop.dispatcher();

  fbl::RefPtr<Console> console;
  zx_status_t status =
      Console::Create(dispatcher, std::move(rx_source), std::move(tx_sink), &console);
  if (status != ZX_OK) {
    printf("console: Console::Create() = %s\n", zx_status_get_string(status));
    return -1;
  }

  svc::Outgoing outgoing(dispatcher);
  status = outgoing.ServeFromStartupInfo();
  if (status != ZX_OK) {
    printf("console: outgoing.ServeFromStartupInfo() = %s\n", zx_status_get_string(status));
    return -1;
  }

  outgoing.svc_dir()->AddEntry("fuchsia.hardware.pty.Device",
                               fbl::AdoptRef(new VfsTty(std::move(console))));

  status = loop.Run();
  ZX_ASSERT(status == ZX_OK);
  return 0;
}
