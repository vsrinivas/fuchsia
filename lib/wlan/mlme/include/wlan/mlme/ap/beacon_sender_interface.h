// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <zircon/types.h>

namespace wlan {

class StartRequest;

class BeaconSenderInterface {
   public:
    virtual ~BeaconSenderInterface() = default;

    virtual zx_status_t Init() = 0;
    virtual bool IsStarted() = 0;
    virtual zx_status_t Start(const StartRequest& req) = 0;
    virtual zx_status_t Stop() = 0;
};

}  // namespace wlan
