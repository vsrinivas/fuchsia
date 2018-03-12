// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/protocol/i2c.h>
#include <fbl/algorithm.h>
#include <fbl/alloc_checker.h>


#include "tas57xx.h"

namespace audio {
namespace gauss {

constexpr float Tas57xx::kMaxGain;
constexpr float Tas57xx::kMinGain;

// static
fbl::unique_ptr<Tas57xx> Tas57xx::Create(i2c_protocol_t *i2c, uint32_t index) {
    fbl::AllocChecker ac;

    auto ptr = fbl::unique_ptr<Tas57xx>(new (&ac) Tas57xx());
    if (!ac.check()) {
        return nullptr;
    }

    memcpy(&ptr->i2c_, i2c, sizeof(*i2c));

    return ptr;
}
Tas57xx::~Tas57xx() {}

Tas57xx::Tas57xx() {}

zx_status_t Tas57xx::Reset(){
    return WriteReg(0x01, 0x01);
}

zx_status_t Tas57xx::SetGain(float gain) {
    gain = fbl::clamp(gain, kMinGain, kMaxGain);

    uint8_t gain_reg = static_cast<uint8_t>( 48 - gain*2);

    zx_status_t status;
    status = WriteReg(61,gain_reg);
    if (status != ZX_OK) return status;
    status = WriteReg(62,gain_reg);
    if (status != ZX_OK) return status;
    current_gain_ = gain;
    return status;
}

zx_status_t Tas57xx::GetGain(float *gain) {
    *gain = current_gain_;
    return ZX_OK;
}

bool Tas57xx::ValidGain(float gain) {
    return (gain <= kMaxGain) && (gain >= kMinGain);
}

zx_status_t Tas57xx::Init(uint8_t slot) {
    if (slot > 7)
        return ZX_ERR_INVALID_ARGS;
    zx_status_t status;
    status = WriteReg(40, 0x13);
    if (status != ZX_OK) return status;
    status = WriteReg(41, static_cast<uint8_t>(1 + 32*slot));
    if (status != ZX_OK) return status;
    return WriteReg(42, 0x22);
}

zx_status_t Tas57xx::Standby() {
    return WriteReg(0x02, 0x10);
}

zx_status_t Tas57xx::ExitStandby() {
    return WriteReg(0x02, 0x00);
}

zx_status_t Tas57xx::WriteReg(uint8_t reg, uint8_t value) {
    uint8_t write_buf[2];
    write_buf[0] = reg;
    write_buf[1] = value;
    return i2c_transact(&i2c_, 0, write_buf, 2, 0, NULL, NULL);
}
} //namespace gauss
} //namespace audio