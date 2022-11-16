// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_ADB_BIN_ADB_FILE_SYNC_ADB_FILE_SYNC_H_
#define SRC_DEVELOPER_ADB_BIN_ADB_FILE_SYNC_ADB_FILE_SYNC_H_

#include <fidl/fuchsia.hardware.adb/cpp/fidl.h>
#include <fidl/fuchsia.sys2/cpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/sys/cpp/component_context.h>
#include <lib/zx/result.h>

#include "src/developer/adb/third_party/adb-file-sync/adb-file-sync-base.h"

namespace adb_file_sync {

class AdbFileSync : public AdbFileSyncBase,
                    public fidl::WireServer<fuchsia_hardware_adb::Provider> {
 public:
  explicit AdbFileSync(std::optional<std::string> default_component)
      : context_(std::make_unique<sys::ComponentContext>(
            sys::ServiceDirectory::CreateFromNamespace(), loop_.dispatcher())),
        default_component_(std::move(default_component)) {
    loop_.StartThread("adb-file-sync-thread");
  }

  static zx_status_t StartService(std::optional<std::string> default_component);
  void OnUnbound(fidl::UnbindInfo info, fidl::ServerEnd<fuchsia_hardware_adb::Provider> server_end);

  void ConnectToService(fuchsia_hardware_adb::wire::ProviderConnectToServiceRequest* request,
                        ConnectToServiceCompleter::Sync& completer) override;

  zx::result<zx::channel> ConnectToComponent(std::string name,
                                             std::vector<std::string>* out_path) override;

  async_dispatcher_t* dispatcher() { return loop_.dispatcher(); }
  const std::optional<std::string>& default_component() { return default_component_; }

 private:
  friend class AdbFileSyncTest;

  zx_status_t ConnectToAdbDevice(zx::channel chan);

  async::Loop loop_ = async::Loop(&kAsyncLoopConfigNeverAttachToThread);
  std::unique_ptr<sys::ComponentContext> context_;
  std::optional<fidl::ServerBindingRef<fuchsia_hardware_adb::Provider>> binding_ref_;
  const std::optional<std::string> default_component_;
  fidl::SyncClient<fuchsia_sys2::RealmQuery> realm_query_;
};

}  // namespace adb_file_sync

#endif  // SRC_DEVELOPER_ADB_BIN_ADB_FILE_SYNC_ADB_FILE_SYNC_H_
