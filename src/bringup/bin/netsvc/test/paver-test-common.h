// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#ifndef SRC_BRINGUP_BIN_NETSVC_TEST_PAVER_TEST_COMMON_H_
#define SRC_BRINGUP_BIN_NETSVC_TEST_PAVER_TEST_COMMON_H_

#include <fidl/fuchsia.device/cpp/wire.h>
#include <fidl/fuchsia.fshost/cpp/wire.h>
#include <fidl/fuchsia.fshost/cpp/wire_test_base.h>
#include <fidl/fuchsia.paver/cpp/wire.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/async/dispatcher.h>
#include <lib/devmgr-integration-test/fixture.h>
#include <lib/fidl-async/cpp/bind.h>
#include <lib/netboot/netboot.h>
#include <lib/sync/completion.h>
#include <lib/zircon-internal/thread_annotations.h>
#include <lib/zx/channel.h>
#include <lib/zx/time.h>
#include <threads.h>
#include <zircon/errors.h>
#include <zircon/time.h>

#include <algorithm>
#include <memory>
#include <vector>

#include <fbl/auto_lock.h>
#include <fbl/mutex.h>
#include <ramdevice-client/ramdisk.h>
#include <zxtest/zxtest.h>

#include "src/bringup/bin/netsvc/paver.h"
#include "src/lib/storage/vfs/cpp/pseudo_dir.h"
#include "src/lib/storage/vfs/cpp/service.h"
#include "src/lib/storage/vfs/cpp/synchronous_vfs.h"
#include "src/storage/testing/fake-paver.h"

class FakeFshost : public fidl::testing::WireTestBase<fuchsia_fshost::Admin> {
 public:
  zx_status_t Connect(async_dispatcher_t* dispatcher,
                      fidl::ServerEnd<fuchsia_fshost::Admin> request) {
    dispatcher_ = dispatcher;
    return fidl::BindSingleInFlightOnly<fidl::WireServer<fuchsia_fshost::Admin>>(
        dispatcher, std::move(request), this);
  }

  void WriteDataFile(WriteDataFileRequestView request,
                     WriteDataFileCompleter::Sync& completer) override {
    data_file_path_ = request->filename.get();
    completer.ReplySuccess();
  }

  void NotImplemented_(const std::string& name, fidl::CompleterBase& completer) override {
    FAIL("Unexpected call to fuchsia.fshost.Admin: %s", name.c_str());
  }

  const std::string& data_file_path() const { return data_file_path_; }

 private:
  async_dispatcher_t* dispatcher_ = nullptr;
  std::string data_file_path_;
};

class FakeSvc {
 public:
  explicit FakeSvc(async_dispatcher_t* dispatcher) : dispatcher_(dispatcher), vfs_(dispatcher) {
    auto root_dir = fbl::MakeRefCounted<fs::PseudoDir>();
    root_dir->AddEntry(
        fidl::DiscoverableProtocolName<fuchsia_paver::Paver>,
        fbl::MakeRefCounted<fs::Service>([this](fidl::ServerEnd<fuchsia_paver::Paver> request) {
          return fake_paver_.Connect(dispatcher_, std::move(request));
        }));
    root_dir->AddEntry(
        fidl::DiscoverableProtocolName<fuchsia_fshost::Admin>,
        fbl::MakeRefCounted<fs::Service>([this](fidl::ServerEnd<fuchsia_fshost::Admin> request) {
          return fake_fshost_.Connect(dispatcher_, std::move(request));
        }));

    zx::result server_end = fidl::CreateEndpoints(&svc_local_);
    ASSERT_OK(server_end.status_value());
    vfs_.ServeDirectory(root_dir, std::move(server_end.value()));
  }

  paver_test::FakePaver& fake_paver() { return fake_paver_; }
  FakeFshost& fake_fshost() { return fake_fshost_; }
  fidl::ClientEnd<fuchsia_io::Directory>& svc_chan() { return svc_local_; }

 private:
  async_dispatcher_t* dispatcher_;
  fs::SynchronousVfs vfs_;
  paver_test::FakePaver fake_paver_;
  FakeFshost fake_fshost_;
  fidl::ClientEnd<fuchsia_io::Directory> svc_local_;
};

class FakeDev {
 public:
  FakeDev() {
    auto args = devmgr_integration_test::IsolatedDevmgr::DefaultArgs();
    args.sys_device_driver = "/boot/driver/platform-bus.so";

    ASSERT_OK(devmgr_integration_test::IsolatedDevmgr::Create(std::move(args), &devmgr_));
    // TODO(https://fxbug.dev/80815): Stop requiring this recursive wait.
    fbl::unique_fd fd;
    ASSERT_OK(device_watcher::RecursiveWaitForFile(devmgr_.devfs_root(),
                                                   "sys/platform/00:00:2d/ramctl", &fd));
  }

  devmgr_integration_test::IsolatedDevmgr devmgr_;
};

class PaverTest : public zxtest::Test {
 protected:
  PaverTest()
      : loop_(&kAsyncLoopConfigNoAttachToCurrentThread),
        fake_svc_(loop_.dispatcher()),
        paver_(std::move(fake_svc_.svc_chan()), fake_dev_.devmgr_.devfs_root().duplicate()) {
    loop_.StartThread("paver-test-loop");
  }

  ~PaverTest() {
    // Need to make sure paver thread exits.
    Wait();
    if (ramdisk_ != nullptr) {
      ramdisk_destroy(ramdisk_);
      ramdisk_ = nullptr;
    }
    loop_.Shutdown();
  }

  void Wait() {
    while (paver_.InProgress())
      continue;
  }

  void SpawnBlockDevice() {
    fbl::unique_fd fd;
    ASSERT_OK(device_watcher::RecursiveWaitForFile(fake_dev_.devmgr_.devfs_root(),
                                                   "sys/platform/00:00:2d/ramctl", &fd));
    ASSERT_OK(ramdisk_create_at(fake_dev_.devmgr_.devfs_root().get(), zx_system_get_page_size(),
                                100, &ramdisk_));
    std::string expected = std::string("/dev/") + ramdisk_get_path(ramdisk_);
    fake_svc_.fake_paver().set_expected_device(expected);
  }

  async::Loop loop_;
  ramdisk_client_t* ramdisk_ = nullptr;
  FakeSvc fake_svc_;
  FakeDev fake_dev_;
  netsvc::Paver paver_;
};

#endif  // SRC_BRINGUP_BIN_NETSVC_TEST_PAVER_TEST_COMMON_H_
