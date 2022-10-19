// Copyright 2021 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <lib/boot-options/boot-options.h>
#include <zircon/assert.h>

#include <dev/init.h>
#include <phys/handoff.h>

void DriverHandoffEarly(const PhysHandoff& handoff) {
  PlatformUartDriverHandoffEarly(gBootOptions->serial);
  PlatformDriverHandoffEarly(handoff.arch_handoff);
}

void DriverHandoffLate(const PhysHandoff& handoff) {
  PlatformUartDriverHandoffLate(gBootOptions->serial);
  PlatformDriverHandoffLate(handoff.arch_handoff);
}
