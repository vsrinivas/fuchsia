// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_DRIVER_HOST_COMPOSITE_DEVICE_H_
#define SRC_DEVICES_DRIVER_HOST_COMPOSITE_DEVICE_H_

#include <ddk/driver.h>
#include <fbl/array.h>
#include <fbl/ref_counted.h>
#include <fbl/ref_ptr.h>

#include "zx-device.h"

typedef fbl::Array<fbl::RefPtr<zx_device>> CompositeComponents;

// Modifies |device| to have the appropriate protocol_id, ctx, and ops tables
// for a composite device
zx_status_t InitializeCompositeDevice(const fbl::RefPtr<zx_device>& device,
                                      CompositeComponents&& components);

// These objects are state shared by all components of the composite device that
// enables one of them (the first to try) to begin teardown of the composite
// device.  This is used for implementing unbind.
class CompositeDevice : public fbl::RefCounted<CompositeDevice> {
 public:
  explicit CompositeDevice(fbl::RefPtr<zx_device> device) : device_(std::move(device)) {}
  ~CompositeDevice();
  fbl::RefPtr<zx_device> Detach() { return std::move(device_); }

 private:
  fbl::RefPtr<zx_device> device_;
};

#endif  // SRC_DEVICES_DRIVER_HOST_COMPOSITE_DEVICE_H_
