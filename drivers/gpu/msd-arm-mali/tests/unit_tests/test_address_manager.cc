// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "address_manager.h"
#include "mock/mock_mmio.h"
#include "platform_mmio.h"
#include "registers.h"
#include "gtest/gtest.h"

namespace {
TEST(AddressManager, MockAssignFinish)
{
    std::unique_ptr<RegisterIo> reg_io(new RegisterIo(MockMmio::Create(1024 * 1024)));
    std::shared_ptr<MsdArmConnection> connection = MsdArmConnection::Create(0, nullptr);
    AddressManager address_manager;
    auto atom = std::make_unique<MsdArmAtom>(connection, 0, 0, 0);

    EXPECT_TRUE(address_manager.AssignAddressSpace(reg_io.get(), atom.get()));
    registers::AsRegisters as_regs(0);
    EXPECT_EQ(0x4du, as_regs.MemoryAttributes().ReadFrom(reg_io.get()).reg_value());
    EXPECT_EQ((1u << 2) | 3, as_regs.TranslationTable().ReadFrom(reg_io.get()).reg_value() & 0xff);

    address_manager.AtomFinished(reg_io.get(), atom.get());
    EXPECT_EQ(0x4du, as_regs.MemoryAttributes().ReadFrom(reg_io.get()).reg_value());
    EXPECT_EQ(0u, as_regs.TranslationTable().ReadFrom(reg_io.get()).reg_value());

    connection = std::shared_ptr<MsdArmConnection>();

    EXPECT_FALSE(address_manager.AssignAddressSpace(reg_io.get(), atom.get()));
}
}
