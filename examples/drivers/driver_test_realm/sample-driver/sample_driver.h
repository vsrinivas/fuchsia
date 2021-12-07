// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef EXAMPLES_DRIVERS_DRIVER_TEST_REALM_SAMPLE_DRIVER_SAMPLE_DRIVER_H_
#define EXAMPLES_DRIVERS_DRIVER_TEST_REALM_SAMPLE_DRIVER_SAMPLE_DRIVER_H_

#include <fidl/fuchsia.hardware.sample/cpp/wire.h>
#include <lib/inspect/cpp/inspect.h>

#include <ddktl/device.h>

namespace sample_driver {

class SampleDriver;
using DeviceType = ddk::Device<SampleDriver, ddk::Initializable,
                               ddk::Messageable<fuchsia_hardware_sample::Echo>::Mixin>;
class SampleDriver : public DeviceType {
 public:
  explicit SampleDriver(zx_device_t* parent) : DeviceType(parent) {}
  virtual ~SampleDriver() = default;

  static zx_status_t Bind(void* ctx, zx_device_t* dev);
  zx_status_t Bind();
  void DdkInit(ddk::InitTxn txn);
  void DdkRelease();

  // For inspect test.
  zx::vmo inspect_vmo() { return inspect_.DuplicateVmo(); }

  void EchoString(EchoStringRequestView request, EchoStringCompleter::Sync& completer) override {
    completer.Reply(request->value);
  }

 private:
  inspect::Inspector inspect_;
  inspect::BoolProperty is_bound = inspect_.GetRoot().CreateBool("is_bound", false);
};

}  // namespace sample_driver

#endif  // EXAMPLES_DRIVERS_DRIVER_TEST_REALM_SAMPLE_DRIVER_SAMPLE_DRIVER_H_
