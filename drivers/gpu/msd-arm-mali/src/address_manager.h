// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ADDRESS_MANAGER_H_
#define ADDRESS_MANAGER_H_

#include "msd_arm_atom.h"
#include "msd_arm_connection.h"
#include "registers.h"

class AddressManager {
public:
    bool AssignAddressSpace(RegisterIo* io, MsdArmAtom* atom);

    void AtomFinished(RegisterIo* io, MsdArmAtom* atom);

private:
    // Wait for the MMU to finish processing any existing commands.
    static void WaitForMmuIdle(RegisterIo* io, registers::AsRegisters as_regs);

    std::shared_ptr<MsdArmConnection> connection_;
};

#endif // ADDRESS_MANAGER_H_
