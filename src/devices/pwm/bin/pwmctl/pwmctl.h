// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#ifndef SRC_DEVICES_PWM_BIN_PWMCTL_PWMCTL_H_
#define SRC_DEVICES_PWM_BIN_PWMCTL_PWMCTL_H_

#include <fidl/fuchsia.hardware.pwm/cpp/wire.h>
#include <zircon/types.h>

namespace pwmctl {

zx_status_t run(int argc, char const* argv[], fidl::ClientEnd<fuchsia_hardware_pwm::Pwm> device);

}  // namespace pwmctl

#endif  // SRC_DEVICES_PWM_BIN_PWMCTL_PWMCTL_H_
