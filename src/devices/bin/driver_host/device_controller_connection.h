// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_BIN_DRIVER_HOST_DEVICE_CONTROLLER_CONNECTION_H_
#define SRC_DEVICES_BIN_DRIVER_HOST_DEVICE_CONTROLLER_CONNECTION_H_

#include <fuchsia/device/manager/llcpp/fidl.h>
#include <fuchsia/io/llcpp/fidl.h>
#include <fuchsia/io2/llcpp/fidl.h>
#include <lib/zx/channel.h>

#include <fbl/ref_ptr.h>

#include "async_loop_owned_rpc_handler.h"

class DriverHostContext;
struct zx_device;

class DeviceControllerConnection
    : public AsyncLoopOwnedRpcHandler<DeviceControllerConnection>,
      public fidl::WireInterface<fuchsia_device_manager::DeviceController>,
      public fidl::WireRawChannelInterface<fuchsia_io::Directory> {
 public:
  // |ctx| must outlive this connection
  DeviceControllerConnection(DriverHostContext* ctx, fbl::RefPtr<zx_device> dev,
                             fidl::ServerEnd<fuchsia_device_manager::DeviceController> rpc,
                             fidl::Client<fuchsia_device_manager::Coordinator> coordinator_client);

  // |ctx| must outlive this connection
  static zx_status_t Create(DriverHostContext* ctx, fbl::RefPtr<zx_device> dev, zx::channel rpc,
                            fidl::Client<fuchsia_device_manager::Coordinator> coordinator_client,
                            std::unique_ptr<DeviceControllerConnection>* conn);

  ~DeviceControllerConnection();

  static void HandleRpc(std::unique_ptr<DeviceControllerConnection> conn,
                        async_dispatcher_t* dispatcher, async::WaitBase* wait, zx_status_t status,
                        const zx_packet_signal_t* signal);
  zx_status_t HandleRead();

  const fbl::RefPtr<zx_device>& dev() const { return dev_; }

 protected:
  DriverHostContext* const driver_host_context_;
  const fbl::RefPtr<zx_device> dev_;

 private:
  fidl::Client<fuchsia_device_manager::Coordinator> coordinator_client_;
  // Fidl methods
  void BindDriver(::fidl::StringView driver_path, ::zx::vmo driver,
                  BindDriverCompleter::Sync& _completer) override;
  void ConnectProxy(::zx::channel shadow, ConnectProxyCompleter::Sync& _completer) override;
  void Init(InitCompleter::Sync& _completer) override;
  void Suspend(uint32_t flags, SuspendCompleter::Sync& _completer) override;
  void Resume(uint32_t target_system_state, ResumeCompleter::Sync& _completer) override;
  void Unbind(UnbindCompleter::Sync& _completer) override;
  void CompleteRemoval(CompleteRemovalCompleter::Sync& _completer) override;
  void CompleteCompatibilityTests(fuchsia_device_manager::wire::CompatibilityTestStatus status,
                                  CompleteCompatibilityTestsCompleter::Sync& _completer) override;

  // Io.fidl methods
  void Open(uint32_t flags, uint32_t mode, ::fidl::StringView path, ::zx::channel object,
            OpenCompleter::Sync& _completer) override;

  // All methods below are intentionally unimplemented.
  void AddInotifyFilter(::fidl::StringView path, fuchsia_io2::wire::InotifyWatchMask filters,
                        uint32_t watch_descriptor, ::zx::socket socket,
                        AddInotifyFilterCompleter::Sync& _completer) override {}

  void Clone(uint32_t flags, ::zx::channel object, CloneCompleter::Sync& _completer) override {}
  void Close(CloseCompleter::Sync& _completer) override {}
  void Describe(DescribeCompleter::Sync& _completer) override {}
  void GetToken(GetTokenCompleter::Sync& _completer) override {}
  void Rewind(RewindCompleter::Sync& _completer) override {}
  void ReadDirents(uint64_t max_bytes, ReadDirentsCompleter::Sync& _completer) override {}
  void Unlink(::fidl::StringView path, UnlinkCompleter::Sync& _completer) override {}
  void Unlink2(::fidl::StringView name, fuchsia_io2::wire::UnlinkOptions options,
               Unlink2Completer::Sync& _completer) override {}
  void SetAttr(uint32_t flags, fuchsia_io::wire::NodeAttributes attributes,
               SetAttrCompleter::Sync& _completer) override {}
  void Sync(SyncCompleter::Sync& _completer) override {}
  void GetAttr(GetAttrCompleter::Sync& _completer) override {}
  void Rename(::fidl::StringView src, ::zx::handle dst_parent_token, ::fidl::StringView dst,
              RenameCompleter::Sync& _completer) override {}
  void Link(::fidl::StringView src, ::zx::handle dst_parent_token, ::fidl::StringView dst,
            LinkCompleter::Sync& _completer) override {}
  void Watch(uint32_t mask, uint32_t options, ::zx::channel watcher,
             WatchCompleter::Sync& _completer) override {}
};

struct DevhostRpcReadContext {
  const char* path;
  DeviceControllerConnection* conn;
};

#endif  // SRC_DEVICES_BIN_DRIVER_HOST_DEVICE_CONTROLLER_CONNECTION_H_
