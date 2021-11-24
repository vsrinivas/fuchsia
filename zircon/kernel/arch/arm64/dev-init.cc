// Copyright 2021 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <dev/hdcp/amlogic_s912/init.h>
#include <dev/psci.h>
#include <phys/arch/arch-handoff.h>

void ArchDriverHandoffEarly(const ArchPhysHandoff& arch_handoff) {
  if (arch_handoff.psci_driver) {
    PsciInit(arch_handoff.psci_driver.value());
  }
}

void ArchDriverHandoffLate(const ArchPhysHandoff& arch_handoff) {
  if (arch_handoff.amlogic_hdcp_driver) {
    AmlogicS912HdcpInit(arch_handoff.amlogic_hdcp_driver.value());
  }
}
