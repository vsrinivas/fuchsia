// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/elfldltl/machine.h>

#include <type_traits>

#include <zxtest/zxtest.h>

namespace {

template <elfldltl::ElfMachine Machine, typename SizeType>
constexpr void CheckMachine() {
  using Traits = elfldltl::AbiTraits<Machine>;

  constexpr decltype(auto) align = Traits::template kStackAlignment<SizeType>;
  EXPECT_TRUE((std::is_same_v<decltype(align), const SizeType>));
  EXPECT_EQ(align, 16u);

  constexpr SizeType base = 1025, size = 2000;
  constexpr decltype(auto) sp = Traits::InitialStackPointer(base, size);
  EXPECT_TRUE((std::is_same_v<decltype(sp), const SizeType>));
  switch (Machine) {
    default:
      EXPECT_EQ(sp, 3024u);
      break;
    case elfldltl::ElfMachine::kX86_64:
      EXPECT_EQ(sp, 3016u);
      break;
    case elfldltl::ElfMachine::k386:
      EXPECT_EQ(sp, 3020u);
      break;
  }
}

template <elfldltl::ElfMachine... Machines>
struct CheckMachines {
  CheckMachines() {
    (CheckMachine<Machines, uint32_t>(), ...);
    (CheckMachine<Machines, uint64_t>(), ...);
  }
};

TEST(ElfldltlAbiTests, Machines) { elfldltl::AllSupportedMachines<CheckMachines>(); }

}  // namespace
