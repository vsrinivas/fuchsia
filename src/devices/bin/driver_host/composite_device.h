// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_BIN_DRIVER_HOST_COMPOSITE_DEVICE_H_
#define SRC_DEVICES_BIN_DRIVER_HOST_COMPOSITE_DEVICE_H_

#include <lib/ddk/driver.h>

#include <fbl/array.h>
#include <fbl/ref_counted.h>
#include <fbl/ref_ptr.h>

#include "src/devices/bin/driver_host/zx_device.h"

struct CompositeFragment {
  std::string name;
  fbl::RefPtr<zx_device> device;
};

typedef fbl::Array<CompositeFragment> CompositeFragments;

// Modifies |device| to have the appropriate protocol_id, ctx, and ops tables
// for a composite device
zx_status_t InitializeCompositeDevice(const fbl::RefPtr<zx_device>& device,
                                      CompositeFragments&& fragments);

// Returns a zx_driver instance for composite devices
fbl::RefPtr<zx_driver> GetCompositeDriver(DriverHostContext* ctx);

// These objects are state shared by all fragments of the composite device that
// enables one of them (the first to try) to begin teardown of the composite
// device.  This is used for implementing unbind.
class CompositeDevice : public fbl::RefCounted<CompositeDevice> {
 public:
  explicit CompositeDevice(fbl::RefPtr<zx_device> device) : device_(std::move(device)) {}
  ~CompositeDevice();

  uint32_t GetFragmentCount();

  void GetFragments(composite_device_fragment_t* comp_list, size_t comp_count, size_t* comp_actual);

  bool GetFragment(const char* name, zx_device_t** out);

 private:
  fbl::RefPtr<zx_device> device_;
};

#endif  // SRC_DEVICES_BIN_DRIVER_HOST_COMPOSITE_DEVICE_H_
