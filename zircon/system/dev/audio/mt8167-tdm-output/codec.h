// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <memory>

#include <ddk/debug.h>
#include <ddk/protocol/i2c.h>
#include <ddktl/pdev.h>

namespace audio {
namespace mt8167 {

// TODO(andresoportus): Move this to channel communication as we separate codecs from controllers.
class Codec {
public:
    virtual ~Codec() = default;

    virtual bool ValidGain(float gain) const = 0;
    virtual zx_status_t SetGain(float gain) = 0;
    virtual zx_status_t Init() = 0;
    virtual zx_status_t Reset() = 0;
    virtual zx_status_t Standby() = 0;
    virtual zx_status_t ExitStandby() = 0;
    virtual float GetGain() const = 0;
    virtual float GetMinGain() const = 0;
    virtual float GetMaxGain() const = 0;
    virtual float GetGainStep() const = 0;
};
} // namespace mt8167
} // namespace audio
