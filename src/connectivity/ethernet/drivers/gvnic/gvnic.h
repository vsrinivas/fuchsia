// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CONNECTIVITY_ETHERNET_DRIVERS_GVNIC_GVNIC_H_
#define SRC_CONNECTIVITY_ETHERNET_DRIVERS_GVNIC_GVNIC_H_

#include <lib/inspect/cpp/inspect.h>

#include <ddktl/device.h>

namespace gvnic {

class Gvnic;
using DeviceType = ddk::Device<Gvnic, ddk::Initializable>;
class Gvnic : public DeviceType {
 public:
  explicit Gvnic(zx_device_t* parent) : DeviceType(parent) {}
  virtual ~Gvnic() = default;

  static zx_status_t Bind(void* ctx, zx_device_t* dev);
  zx_status_t Bind();
  void DdkInit(ddk::InitTxn txn);
  void DdkRelease();

  // For inspect test.
  zx::vmo inspect_vmo() { return inspect_.DuplicateVmo(); }

 private:
  inspect::Inspector inspect_;
  // TODO(charlieross): `is_bound` is an example property. Replace this with useful properties of
  // the device.
  inspect::BoolProperty is_bound = inspect_.GetRoot().CreateBool("is_bound", false);
};

}  // namespace gvnic

#endif  // SRC_CONNECTIVITY_ETHERNET_DRIVERS_GVNIC_GVNIC_H_
