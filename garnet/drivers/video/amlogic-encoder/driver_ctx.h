// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_DRIVERS_VIDEO_AMLOGIC_ENCODER_DRIVER_CTX_H_
#define GARNET_DRIVERS_VIDEO_AMLOGIC_ENCODER_DRIVER_CTX_H_

// Manages context for the driver's lifetime.
class DriverCtx {
 public:
  DriverCtx();

  zx_status_t Init();

 private:
};

#endif  // GARNET_DRIVERS_VIDEO_AMLOGIC_ENCODER_DRIVER_CTX_H_
