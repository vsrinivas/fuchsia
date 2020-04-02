// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/hardware/mediacodec/llcpp/fidl.h>
#include <lib/fdio/directory.h>
#include <lib/fdio/namespace.h>

#include <memory>

#include <ddktl/device.h>
#include <ddktl/fidl.h>
#include <ddktl/protocol/platform/device.h>

#include "macros.h"
#include "tests/test_support.h"

class AmlogicTestDevice;
using DdkDeviceType = ddk::Device<AmlogicTestDevice, ddk::Messageable>;

class AmlogicTestDevice : public llcpp::fuchsia::hardware::mediacodec::Tester::Interface,
                          public DdkDeviceType {
 public:
  AmlogicTestDevice(zx_device_t* parent) : DdkDeviceType(parent) {}
  zx_status_t Bind() { return DdkAdd("test_amlogic_video"); }

  void DdkRelease() { delete this; }
  zx_status_t DdkMessage(fidl_msg_t* msg, fidl_txn_t* txn) {
    DdkTransaction transaction(txn);
    llcpp::fuchsia::hardware::mediacodec::Tester::Dispatch(this, msg, &transaction);
    return transaction.Status();
  }

  void SetOutputDirectoryHandle(zx::channel handle,
                                SetOutputDirectoryHandleCompleter::Sync completer) override {
    fdio_ns_t* ns;
    zx_status_t status = fdio_ns_get_installed(&ns);
    status = fdio_ns_bind(ns, "/tmp", handle.release());
    fprintf(stderr, "NS bind: %d\n", status);
  }
  void RunTests(RunTestsCompleter::Sync completer) override {
    TestSupport::set_parent_device(parent());
    if (!TestSupport::RunAllTests()) {
      DECODE_ERROR("Tests failed, failing to initialize");
      completer.Reply(ZX_ERR_INTERNAL);
    } else {
      completer.Reply(ZX_OK);
    }
  }
};

extern "C" zx_status_t test_amlogic_video_bind(void* ctx, zx_device_t* parent) {
  auto test_device = std::make_unique<AmlogicTestDevice>(parent);

  if (test_device->Bind() != ZX_OK) {
    return ZX_ERR_INTERNAL;
  }
  test_device.release();
  return ZX_OK;
}
