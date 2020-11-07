// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/arch/testing/x86/fake-cpuid.h>
#include <lib/arch/x86/lbr.h>

#include <vector>

#include <hwreg/mock.h>
#include <zxtest/zxtest.h>

// Below are tests meant for host or userland. Hardware access has been
// abstracted (i.e., via `MsrIoProvider` and`CpuidIoProvider`) and so here we
// able to test the business logic of the LbrStack in these environments which
// allows for more expressive C++ (as opposed to the kernel).

namespace {

// ALL-CAPS to conveniently match official documentation.
constexpr uint32_t IA32_DEBUGCTL = 0x0000'01d9;
constexpr uint32_t IA32_PERF_CAPABILITIES = 0x0000'0345;
constexpr uint32_t MSR_LBR_SELECT = 0x0000'01c8;
constexpr uint32_t MSR_LASTBRANCH_TOS = 0x0000'01c9;
#define MSR_LASTBRANCH_N_FROM_IP(N) (uint32_t{0x0000'0680} + uint32_t{N})
#define MSR_LASTBRANCH_N_TO_IP(N) (uint32_t{0x0000'06c0} + uint32_t{N})
#define MSR_LBR_INFO_N(N) (uint32_t{0x0000'0dc0} + uint32_t{N})

///
/// Test data.
///
/// Templated on LBR format and an index, it is convenient to define MSR and
/// LBR test values in this way. The large footprint here, largely taken up by
/// simple boilerplate, ultimately makes for more readable tests.
///

// Expected value written to MSR_LBR_SELECT.
template <bool ForUserspace, bool CallstackProfiling>
uint64_t kExpectedLbrSelectValue;
template <>
constexpr uint64_t kExpectedLbrSelectValue<true, true> = 0b1011000101;
template <>
constexpr uint64_t kExpectedLbrSelectValue<false, true> = 0b1011000110;
template <>
constexpr uint64_t kExpectedLbrSelectValue<true, false> = 0b0011000101;
template <>
constexpr uint64_t kExpectedLbrSelectValue<false, false> = 0b0011000110;

// The expected LastBranchRecord object resulting from zero-valued TO, FROM,
// and INFO (where appropriate) MSRs by LBR record type (accounting for the
// difference between std::nullopt and 0 or false).
template <arch::LbrFormat Format>
arch::LastBranchRecord kNullLbr;

// Dummy test values for the MSR_LASTBRANCH_N_FROM_IP, MSR_LASTBRANCH_0_FROM_IP
// and MSR_LBR_INFO_N MSRs for particular LBR formats. We choose a template
// parameter index of "M" to distinguish it from the commonly-used LBR index
// "N".
template <arch::LbrFormat Format, uint32_t M>
uint64_t kExampleFromIpValue;
template <arch::LbrFormat Format, uint32_t M>
uint64_t kExampleToIpValue;
template <arch::LbrFormat Format, uint32_t M>
uint64_t kExampleInfoValue;

// The expected amalgamation of the associated kExampleFromIpValue,
// kExampleToIpValue, and kExampleInfoValue values.
template <arch::LbrFormat Format, uint32_t M>
arch::LastBranchRecord kExpectedLbr = kNullLbr<Format>;

//
// k64BitEip values.
//
// FROM, TO just store addresses; metadata is not available.
//
template <>
constexpr arch::LastBranchRecord kNullLbr<arch::LbrFormat::k64BitEip> = {
    .from = zx_vaddr_t{},
    .to = zx_vaddr_t{},
    .mispredicted = std::nullopt,
    .cycle_count = std::nullopt,
    .in_tsx = std::nullopt,
    .tsx_abort = std::nullopt,
};

template <>
constexpr uint64_t kExampleFromIpValue<arch::LbrFormat::k64BitEip, 0> = 0x01234567;
template <>
constexpr uint64_t kExampleToIpValue<arch::LbrFormat::k64BitEip, 0> = 0x76543210;
template <>
constexpr arch::LastBranchRecord kExpectedLbr<arch::LbrFormat::k64BitEip, 0> = {
    .from = zx_vaddr_t{0x01234567},
    .to = zx_vaddr_t{0x76543210},
    .mispredicted = std::nullopt,
    .cycle_count = std::nullopt,
    .in_tsx = std::nullopt,
    .tsx_abort = std::nullopt,
};

template <>
constexpr uint64_t kExampleFromIpValue<arch::LbrFormat::k64BitEip, 1> = 0x0123abcd;
template <>
constexpr uint64_t kExampleToIpValue<arch::LbrFormat::k64BitEip, 1> = 0xabcd0123;
template <>
constexpr arch::LastBranchRecord kExpectedLbr<arch::LbrFormat::k64BitEip, 1> = {
    .from = zx_vaddr_t{0x0123abcd},
    .to = zx_vaddr_t{0xabcd0123},
    .mispredicted = std::nullopt,
    .cycle_count = std::nullopt,
    .in_tsx = std::nullopt,
    .tsx_abort = std::nullopt,
};

//
// k64BitLip values (same as k64BitEip).
//
// FROM, TO just store addresses; metadata is not available.
//
template <>
constexpr arch::LastBranchRecord kNullLbr<arch::LbrFormat::k64BitLip> =
    kNullLbr<arch::LbrFormat::k64BitEip>;

template <>
constexpr uint64_t kExampleFromIpValue<arch::LbrFormat::k64BitLip, 0> =
    kExampleFromIpValue<arch::LbrFormat::k64BitEip, 0>;
template <>
constexpr uint64_t kExampleToIpValue<arch::LbrFormat::k64BitLip, 0> =
    kExampleToIpValue<arch::LbrFormat::k64BitEip, 0>;
template <>
constexpr arch::LastBranchRecord kExpectedLbr<arch::LbrFormat::k64BitLip, 0> =
    kExpectedLbr<arch::LbrFormat::k64BitEip, 0>;

template <>
constexpr uint64_t kExampleFromIpValue<arch::LbrFormat::k64BitLip, 1> =
    kExampleFromIpValue<arch::LbrFormat::k64BitEip, 1>;
template <>
constexpr uint64_t kExampleToIpValue<arch::LbrFormat::k64BitLip, 1> =
    kExampleToIpValue<arch::LbrFormat::k64BitEip, 1>;
template <>
constexpr arch::LastBranchRecord kExpectedLbr<arch::LbrFormat::k64BitLip, 1> =
    kExpectedLbr<arch::LbrFormat::k64BitEip, 1>;

//
// k64BitEipWithFlags values.
//
// FROM stores an address and a misprediction bit; TO stores an address; TSX
// info and cycle counts are not available.
//
template <>
constexpr arch::LastBranchRecord kNullLbr<arch::LbrFormat::k64BitEipWithFlags> = {
    .from = zx_vaddr_t{},
    .to = zx_vaddr_t{},
    .mispredicted = false,
    .cycle_count = std::nullopt,
    .in_tsx = std::nullopt,
    .tsx_abort = std::nullopt,
};

template <>
constexpr uint64_t kExampleFromIpValue<arch::LbrFormat::k64BitEipWithFlags, 0> = 0x0000000001234567;
template <>
constexpr uint64_t kExampleToIpValue<arch::LbrFormat::k64BitEipWithFlags, 0> = 0x0000000076543210;
template <>
constexpr arch::LastBranchRecord kExpectedLbr<arch::LbrFormat::k64BitEipWithFlags, 0> = {
    .from = zx_vaddr_t{0x01234567},
    .to = zx_vaddr_t{0x76543210},
    .mispredicted = false,
    .cycle_count = std::nullopt,
    .in_tsx = std::nullopt,
    .tsx_abort = std::nullopt,
};

template <>
constexpr uint64_t kExampleFromIpValue<arch::LbrFormat::k64BitEipWithFlags, 1> = 0x800000000123abcd;
template <>
constexpr uint64_t kExampleToIpValue<arch::LbrFormat::k64BitEipWithFlags, 1> = 0x00000000abcd0123;
template <>
constexpr arch::LastBranchRecord kExpectedLbr<arch::LbrFormat::k64BitEipWithFlags, 1> = {
    .from = zx_vaddr_t{0x0123abcd},
    .to = zx_vaddr_t{0xabcd0123},
    .mispredicted = true,
    .cycle_count = std::nullopt,
    .in_tsx = std::nullopt,
    .tsx_abort = std::nullopt,
};

//
// k64BitEipWithFlagsTsx values.
//
// FROM stores an address, a misprediction bit, and TSX info; TO stores an
// address; cycle counts are not available.
//
template <>
constexpr arch::LastBranchRecord kNullLbr<arch::LbrFormat::k64BitEipWithFlagsTsx> = {
    .from = zx_vaddr_t{},
    .to = zx_vaddr_t{},
    .mispredicted = false,
    .cycle_count = std::nullopt,
    .in_tsx = false,
    .tsx_abort = false,
};

template <>
constexpr uint64_t kExampleFromIpValue<arch::LbrFormat::k64BitEipWithFlagsTsx, 0> =
    0xc000000001234567;
template <>
constexpr uint64_t kExampleToIpValue<arch::LbrFormat::k64BitEipWithFlagsTsx, 0> =
    0x0000000076543210;
template <>
constexpr arch::LastBranchRecord kExpectedLbr<arch::LbrFormat::k64BitEipWithFlagsTsx, 0> = {
    .from = zx_vaddr_t{0x01234567},
    .to = zx_vaddr_t{0x76543210},
    .mispredicted = true,
    .cycle_count = std::nullopt,
    .in_tsx = true,
    .tsx_abort = false,
};

template <>
constexpr uint64_t kExampleFromIpValue<arch::LbrFormat::k64BitEipWithFlagsTsx, 1> =
    0x600000000123abcd;
template <>
constexpr uint64_t kExampleToIpValue<arch::LbrFormat::k64BitEipWithFlagsTsx, 1> =
    0x00000000abcd0123;
template <>
constexpr arch::LastBranchRecord kExpectedLbr<arch::LbrFormat::k64BitEipWithFlagsTsx, 1> = {
    .from = zx_vaddr_t{0x0123abcd},
    .to = zx_vaddr_t{0xabcd0123},
    .mispredicted = false,
    .cycle_count = std::nullopt,
    .in_tsx = true,
    .tsx_abort = true,
};

//
// k64BitEipWithFlagsInfo values.
//
// FROM, TO just store addresses; all metadata is available in INFO.
//
template <>
constexpr arch::LastBranchRecord kNullLbr<arch::LbrFormat::k64BitEipWithFlagsInfo> = {
    .from = zx_vaddr_t{},
    .to = zx_vaddr_t{},
    .mispredicted = false,
    .cycle_count = 0,
    .in_tsx = false,
    .tsx_abort = false,
};

template <>
constexpr uint64_t kExampleFromIpValue<arch::LbrFormat::k64BitEipWithFlagsInfo, 0> = 0x01234567;
template <>
constexpr uint64_t kExampleToIpValue<arch::LbrFormat::k64BitEipWithFlagsInfo, 0> = 0x76543210;
template <>
constexpr uint64_t kExampleInfoValue<arch::LbrFormat::k64BitEipWithFlagsInfo, 0> =
    0xc000000000000007;
template <>
constexpr arch::LastBranchRecord kExpectedLbr<arch::LbrFormat::k64BitEipWithFlagsInfo, 0> = {
    .from = zx_vaddr_t{0x01234567},
    .to = zx_vaddr_t{0x76543210},
    .mispredicted = true,
    .cycle_count = 7,
    .in_tsx = true,
    .tsx_abort = false,
};

template <>
constexpr uint64_t kExampleFromIpValue<arch::LbrFormat::k64BitEipWithFlagsInfo, 1> = 0x0123abcd;
template <>
constexpr uint64_t kExampleToIpValue<arch::LbrFormat::k64BitEipWithFlagsInfo, 1> = 0xabcd0123;
template <>
constexpr uint64_t kExampleInfoValue<arch::LbrFormat::k64BitEipWithFlagsInfo, 1> =
    0x6000000000000019;
template <>
constexpr arch::LastBranchRecord kExpectedLbr<arch::LbrFormat::k64BitEipWithFlagsInfo, 1> = {
    .from = zx_vaddr_t{0x0123abcd},
    .to = zx_vaddr_t{0xabcd0123},
    .mispredicted = false,
    .cycle_count = 25,
    .in_tsx = true,
    .tsx_abort = true,
};

//
// k64BitLipWithFlagsCycles values.
//
// FROM stores an address and a misprediction bit; TO stores an address and a
// cycle count; TSX info is not available.
//
template <>
constexpr arch::LastBranchRecord kNullLbr<arch::LbrFormat::k64BitLipWithFlagsCycles> = {
    .from = zx_vaddr_t{},
    .to = zx_vaddr_t{},
    .mispredicted = false,
    .cycle_count = 0,
    .in_tsx = std::nullopt,
    .tsx_abort = std::nullopt,
};

template <>
constexpr uint64_t kExampleFromIpValue<arch::LbrFormat::k64BitLipWithFlagsCycles, 0> =
    0x8000000001234567;
template <>
constexpr uint64_t kExampleToIpValue<arch::LbrFormat::k64BitLipWithFlagsCycles, 0> =
    0x0007000076543210;
template <>
constexpr arch::LastBranchRecord kExpectedLbr<arch::LbrFormat::k64BitLipWithFlagsCycles, 0> = {
    .from = zx_vaddr_t{0x01234567},
    .to = zx_vaddr_t{0x76543210},
    .mispredicted = true,
    .cycle_count = 7,
    .in_tsx = std::nullopt,
    .tsx_abort = std::nullopt,
};

template <>
constexpr uint64_t kExampleFromIpValue<arch::LbrFormat::k64BitLipWithFlagsCycles, 1> =
    0x000000000123abcd;
template <>
constexpr uint64_t kExampleToIpValue<arch::LbrFormat::k64BitLipWithFlagsCycles, 1> =
    0x00190000abcd0123;
template <>
constexpr arch::LastBranchRecord kExpectedLbr<arch::LbrFormat::k64BitLipWithFlagsCycles, 1> = {
    .from = zx_vaddr_t{0x0123abcd},
    .to = zx_vaddr_t{0xabcd0123},
    .mispredicted = false,
    .cycle_count = 25,
    .in_tsx = std::nullopt,
    .tsx_abort = std::nullopt,
};

//
// k64BitLipWithFlagsInfo values (same as k64BitEipWithFlagsInfo).
//
// FROM, TO just store addresses; all metadata is available in INFO.
//
template <>
constexpr arch::LastBranchRecord kNullLbr<arch::LbrFormat::k64BitLipWithFlagsInfo> =
    kNullLbr<arch::LbrFormat::k64BitEipWithFlagsInfo>;

template <>
constexpr uint64_t kExampleFromIpValue<arch::LbrFormat::k64BitLipWithFlagsInfo, 0> =
    kExampleFromIpValue<arch::LbrFormat::k64BitEipWithFlagsInfo, 0>;
template <>
constexpr uint64_t kExampleToIpValue<arch::LbrFormat::k64BitLipWithFlagsInfo, 0> =
    kExampleToIpValue<arch::LbrFormat::k64BitEipWithFlagsInfo, 0>;
template <>
constexpr uint64_t kExampleInfoValue<arch::LbrFormat::k64BitLipWithFlagsInfo, 0> =
    kExampleInfoValue<arch::LbrFormat::k64BitEipWithFlagsInfo, 0>;
template <>
constexpr arch::LastBranchRecord kExpectedLbr<arch::LbrFormat::k64BitLipWithFlagsInfo, 0> =
    kExpectedLbr<arch::LbrFormat::k64BitEipWithFlagsInfo, 0>;

template <>
constexpr uint64_t kExampleFromIpValue<arch::LbrFormat::k64BitLipWithFlagsInfo, 1> =
    kExampleFromIpValue<arch::LbrFormat::k64BitEipWithFlagsInfo, 1>;
template <>
constexpr uint64_t kExampleToIpValue<arch::LbrFormat::k64BitLipWithFlagsInfo, 1> =
    kExampleToIpValue<arch::LbrFormat::k64BitEipWithFlagsInfo, 1>;
template <>
constexpr uint64_t kExampleInfoValue<arch::LbrFormat::k64BitLipWithFlagsInfo, 1> =
    kExampleInfoValue<arch::LbrFormat::k64BitEipWithFlagsInfo, 1>;
template <>
constexpr arch::LastBranchRecord kExpectedLbr<arch::LbrFormat::k64BitLipWithFlagsInfo, 1> =
    kExpectedLbr<arch::LbrFormat::k64BitEipWithFlagsInfo, 1>;

///
/// Test cases.
///
/// The tests are parametrized by the following "test case" classes, which are
/// expected to have the following static constexpr members:
///
///   * `uint32_t kCpuidVersionValue`: the CPUID leaf 1 EAX value identifying a
///      particular Intel SoC in this context.
///
///   * `arch::LbrFormat kLbrFormat`: the SoC's LBR format.
///
///   * `bool kEnableForUser`: whether to filter the LBRs so that only branches
///      ending in userland are included.
///
///   * `size_t kExpectedSize`: the SoC's expected LBR stack size.
///
///   * `bool kExpectCallstackProfiling`: whether the SoC is expected to
///      support callstack profiling.
///

// Merom.
template <bool ForUser>
struct IntelCore2Case {
  // (family, extended model, model) = (0x6, 0x0, 0xf).
  static constexpr uint32_t kCpuidVersionValue = 0x000006f0;
  // It is unknown whether the format is actually k64BitLip or k64BitEip (the
  // documentation is characteristically both vague and ambiguous), but both
  // address formats coincide on Fuchsia anyway.
  static constexpr arch::LbrFormat kLbrFormat = arch::LbrFormat::k64BitEip;
  static constexpr bool kEnableForUser = ForUser;

  static constexpr size_t kExpectedStackSize = 4;
  static constexpr bool kExpectCallstackProfiling = false;
};

// Cherry Trail.
template <bool ForUser>
struct AirmontCase {
  // (family, extended model, model) = (0x6, 0x4, 0xc).
  static constexpr uint32_t kCpuidVersionValue = 0x000406c0;
  // It is unknown whether the format is actually k64BitLip or k64BitEip (the
  // documentation is characteristically both vague and ambiguous), but both
  // address formats coincide on Fuchsia anyway.
  static constexpr arch::LbrFormat kLbrFormat = arch::LbrFormat::k64BitLip;
  static constexpr bool kEnableForUser = ForUser;

  static constexpr size_t kExpectedStackSize = 8;
  static constexpr bool kExpectCallstackProfiling = false;
};

// Bloomfield.
template <bool ForUser>
struct NehalemCase {
  // (family, extended model, model) = (0x6, 0x1, 0xa).
  static constexpr uint32_t kCpuidVersionValue = 0x000106a0;
  static constexpr arch::LbrFormat kLbrFormat = arch::LbrFormat::k64BitEipWithFlags;
  static constexpr bool kEnableForUser = ForUser;

  static constexpr size_t kExpectedStackSize = 16;
  static constexpr bool kExpectCallstackProfiling = false;
};

// Haswell S.
template <bool ForUser>
struct HaswellCase {
  // (family, extended model, model) = (0x6, 0x3, 0xd).
  static constexpr uint32_t kCpuidVersionValue = 0x000306d0;
  static constexpr arch::LbrFormat kLbrFormat = arch::LbrFormat::k64BitEipWithFlagsTsx;
  static constexpr bool kEnableForUser = ForUser;

  static constexpr size_t kExpectedStackSize = 16;
  static constexpr bool kExpectCallstackProfiling = true;
};

// Coffee Lake S.
template <bool ForUser>
struct SkylakeCase {
  // (family, extended model, model) = (0x6, 0x9, 0xe).
  static constexpr uint32_t kCpuidVersionValue = 0x000906e0;
  static constexpr arch::LbrFormat kLbrFormat = arch::LbrFormat::k64BitEipWithFlagsInfo;
  static constexpr bool kEnableForUser = ForUser;

  static constexpr size_t kExpectedStackSize = 32;
  static constexpr bool kExpectCallstackProfiling = true;
};

// Apollo Lake.
template <bool ForUser>
struct GoldmontCase {
  // (family, extended model, model) = (0x6, 0x5, 0xc).
  static constexpr uint32_t kCpuidVersionValue = 0x000506c0;
  static constexpr arch::LbrFormat kLbrFormat = arch::LbrFormat::k64BitLipWithFlagsCycles;
  static constexpr bool kEnableForUser = ForUser;

  static constexpr size_t kExpectedStackSize = 32;
  static constexpr bool kExpectCallstackProfiling = true;
};

// Gemini Lake.
template <bool ForUser>
struct GoldmontPlusCase {
  // (family, extended model, model) = (0x6, 0x5, 0xc).
  static constexpr uint32_t kCpuidVersionValue = 0x000506c0;
  static constexpr arch::LbrFormat kLbrFormat = arch::LbrFormat::k64BitLipWithFlagsInfo;
  static constexpr bool kEnableForUser = ForUser;

  static constexpr size_t kExpectedStackSize = 32;
  static constexpr bool kExpectCallstackProfiling = true;
};

///
/// Tests.
///

template <typename TestCase>
void TestEnabling() {
  hwreg::Mock msrMock;
  auto& msrIo = *msrMock.io();

  arch::testing::FakeCpuidIo cpuid;
  // Intel as the vendor.
  cpuid.Populate(0x0, 0x0, arch::CpuidIo::kEbx, 0x756e'6547)
      .Populate(0x0, 0x0, arch::CpuidIo::kEdx, 0x4965'6e69)
      .Populate(0x0, 0x0, arch::CpuidIo::kEcx, 0x6c65'746e)
      .Populate(0x1, 0x0, arch::CpuidIo::kEax, TestCase::kCpuidVersionValue)
      // Has the PDCM feature flag.
      .Populate(0x1, 0x0, arch::CpuidIo::kEcx, uint32_t{1} << 15);

  arch::LbrStack stack(cpuid);

  ASSERT_TRUE(stack.is_supported());

  // Random state with bits 0 and 11 unset (corresponding to enabling LBRs and
  // freezing their recording on Performance Monitor interrupt). We expect bits
  // outside of 0 and 11 to remain untouched across our access. In particular,
  // when disabling, only bit 0 should be updated.
  const uint64_t initial_debugctl = 0b01111000010;

  // Not yet enabled.
  msrMock.ExpectRead(initial_debugctl, IA32_DEBUGCTL);
  EXPECT_FALSE(stack.is_enabled(msrIo));

  // Enable.
  const uint64_t enabled_debugctl = initial_debugctl | 0b100000000001;
  const uint64_t expected_select_value =
      kExpectedLbrSelectValue<TestCase::kEnableForUser, TestCase::kExpectCallstackProfiling>;
  msrMock.ExpectRead(initial_debugctl, IA32_DEBUGCTL)
      .ExpectWrite(enabled_debugctl, IA32_DEBUGCTL)
      .ExpectWrite(expected_select_value, MSR_LBR_SELECT);
  stack.Enable(msrIo, TestCase::kEnableForUser);

  // Now enabled.
  msrMock.ExpectRead(enabled_debugctl, IA32_DEBUGCTL);
  EXPECT_TRUE(stack.is_enabled(msrIo));

  // Disable.
  const uint64_t disabled_debugctl = enabled_debugctl ^ 0b1;
  msrMock.ExpectRead(enabled_debugctl, IA32_DEBUGCTL);
  msrMock.ExpectWrite(disabled_debugctl, IA32_DEBUGCTL);
  stack.Disable(msrIo);

  // Now disabled.
  msrMock.ExpectRead(disabled_debugctl, IA32_DEBUGCTL);
  EXPECT_FALSE(stack.is_enabled(msrIo));

  ASSERT_NO_FATAL_FAILURES(msrMock.VerifyAndClear());
}

// Callers must provide where the top of the stack should start on iteration
// (interpreted modulo the stack size).
template <typename TestCase>
void TestIteration(uint32_t top_of_stack) {
  hwreg::Mock msrMock;
  auto& msrIo = *msrMock.io();

  arch::testing::FakeCpuidIo cpuid;
  // Intel as the vendor.
  cpuid.Populate(0x0, 0x0, arch::CpuidIo::kEbx, 0x756e'6547)
      .Populate(0x0, 0x0, arch::CpuidIo::kEdx, 0x4965'6e69)
      .Populate(0x0, 0x0, arch::CpuidIo::kEcx, 0x6c65'746e)
      .Populate(0x1, 0x0, arch::CpuidIo::kEax, TestCase::kCpuidVersionValue)
      // Has the PDCM feature flag.
      .Populate(0x1, 0x0, arch::CpuidIo::kEcx, uint32_t{1} << 15);

  arch::LbrStack stack(cpuid);

  ASSERT_TRUE(stack.is_supported());
  EXPECT_EQ(TestCase::kExpectedStackSize, stack.size());

  //
  // Assume we've now enabled the stack.
  //

  // The LBR format is given by the bottom 6 bits of of the
  // IA32_PERF_CAPABILITIES register; since the enum value is backed by the
  // corresponding uint8_t, we can cast an LbrFormat to a uint64_t to arrive at
  // a suitablly corresponding register value.
  msrMock.ExpectRead(static_cast<uint64_t>(TestCase::kLbrFormat), IA32_PERF_CAPABILITIES);
  constexpr bool is_info_format = TestCase::kLbrFormat == arch::LbrFormat::k64BitEipWithFlagsInfo ||
                                  TestCase::kLbrFormat == arch::LbrFormat::k64BitLipWithFlagsInfo;

  top_of_stack %= stack.size();
  msrMock.ExpectRead(uint64_t{top_of_stack}, MSR_LASTBRANCH_TOS);

  // Iterating from the top of the stack, we stub in the test data for the
  // first two branches, and then provide null data for the rest.
  for (uint32_t i = 0; i < stack.size(); ++i) {
    uint32_t idx = (top_of_stack + i) % stack.size();
    const uint64_t from = (i == 0)   ? kExampleFromIpValue<TestCase::kLbrFormat, 0>
                          : (i == 1) ? kExampleFromIpValue<TestCase::kLbrFormat, 1>
                                     : 0;
    const uint64_t to = (i == 0)   ? kExampleToIpValue<TestCase::kLbrFormat, 0>
                        : (i == 1) ? kExampleToIpValue<TestCase::kLbrFormat, 1>
                                   : 0;
    msrMock.ExpectRead(from, MSR_LASTBRANCH_N_FROM_IP(idx))
        .ExpectRead(to, MSR_LASTBRANCH_N_TO_IP(idx));
    if constexpr (is_info_format) {
      const uint64_t info = (i == 0)   ? kExampleInfoValue<TestCase::kLbrFormat, 0>
                            : (i == 1) ? kExampleInfoValue<TestCase::kLbrFormat, 1>
                                       : 0;
      msrMock.ExpectRead(info, MSR_LBR_INFO_N(idx));
    }
  }

  std::vector<arch::LastBranchRecord> lbrs;
  stack.ForEachRecord(msrIo, [&lbrs](const arch::LastBranchRecord& lbr) { lbrs.push_back(lbr); });
  ASSERT_EQ(stack.size(), lbrs.size());

  for (uint32_t i = 0; i < stack.size(); ++i) {
    auto& expected = (i == 0)   ? kExpectedLbr<TestCase::kLbrFormat, 0>
                     : (i == 1) ? kExpectedLbr<TestCase::kLbrFormat, 1>
                                : kNullLbr<TestCase::kLbrFormat>;
    auto& actual = lbrs[i];
    EXPECT_EQ(expected.from, actual.from);
    EXPECT_EQ(expected.to, actual.to);
    EXPECT_EQ(expected.mispredicted, actual.mispredicted);
    EXPECT_EQ(expected.cycle_count, actual.cycle_count);
    EXPECT_EQ(expected.in_tsx, actual.in_tsx);
    EXPECT_EQ(expected.tsx_abort, actual.tsx_abort);
  }
  ASSERT_NO_FATAL_FAILURES(msrMock.VerifyAndClear());
}

#define TEST_ENABLING(name)                                  \
  TEST(LbrStackTests, name##Enabling) {                      \
    using UserspaceCase = name##Case<true>;                  \
    ASSERT_NO_FATAL_FAILURES(TestEnabling<UserspaceCase>()); \
                                                             \
    using KernelCase = name##Case<false>;                    \
    ASSERT_NO_FATAL_FAILURES(TestEnabling<KernelCase>());    \
  }

// We want to choose four values for the "top of the stack" that in general
// would give uniform-like sampling across the range of [0:stack size). A
// straightforward means to choosing these values would be to look at multiples
// of a sufficiently large prime.
//
// For a prime p != 2, there are no repeats among
// 0, p % 2^k, 2p % 2^k, 3p % 2^k, ...  (2^k - 1)*p % 2^k
//
// Accordingly, as nonzero LBR stack sizes are multiples of 2 greater than or
// equal to 4, in taking p = 17 we can pseudorandomly generate four distinct
// top-of-stack indices with 0, 17, 34, and 51.
//
// Moreover, the userspace/kernel distinction doesn't matter for iteration
// (though the public API may be ignorant of that); for simplicity we test
// iteration 'for userspace' alone.
//
#define TEST_ITERATION(name)                               \
  TEST(LbrStackTests, name##Iteration) {                   \
    using TestCase = name##Case<true>;                     \
    ASSERT_NO_FATAL_FAILURES(TestIteration<TestCase>(0));  \
    ASSERT_NO_FATAL_FAILURES(TestIteration<TestCase>(17)); \
    ASSERT_NO_FATAL_FAILURES(TestIteration<TestCase>(34)); \
    ASSERT_NO_FATAL_FAILURES(TestIteration<TestCase>(51)); \
  }

TEST_ENABLING(IntelCore2)
TEST_ENABLING(Airmont)
TEST_ENABLING(Nehalem)
TEST_ENABLING(Haswell)
TEST_ENABLING(Skylake)
TEST_ENABLING(Goldmont)
TEST_ENABLING(GoldmontPlus)

TEST_ITERATION(IntelCore2)
TEST_ITERATION(Airmont)
TEST_ITERATION(Nehalem)
TEST_ITERATION(Haswell)
TEST_ITERATION(Skylake)
TEST_ITERATION(Goldmont)
TEST_ITERATION(GoldmontPlus)

TEST(LbrStackTests, UnknownSystem) {
  hwreg::Mock msrMock;
  auto& msrIo = *msrMock.io();

  arch::testing::FakeCpuidIo cpuid;
  cpuid.Populate(0x0, 0x0, arch::CpuidIo::kEbx, 0x1234'4321)
      .Populate(0x0, 0x0, arch::CpuidIo::kEdx, 0x5678'8765)
      .Populate(0x0, 0x0, arch::CpuidIo::kEcx, 0xabcd'dcba)
      .Populate(0x1, 0x0, arch::CpuidIo::kEax, 0x11111111)
      // Has the PDCM feature flag.
      .Populate(0x1, 0x0, arch::CpuidIo::kEcx, uint32_t{1} << 15);

  arch::LbrStack stack(cpuid);

  ASSERT_FALSE(stack.is_supported());
  EXPECT_EQ(0, stack.size());

  // If unsupported, checking whether the stack is enabled should not result
  // in MSR access.
  msrMock.ExpectNoIo();
  EXPECT_FALSE(stack.is_enabled(msrIo));

  ASSERT_NO_FATAL_FAILURES(msrMock.VerifyAndClear());
}

TEST(LbrStackTests, PdcmUnsupported) {
  hwreg::Mock msrMock;
  auto& msrIo = *msrMock.io();

  // A Haswell S, but with PDCM no longer a feature... from, say, a microcode
  // update?
  arch::testing::FakeCpuidIo cpuid;
  cpuid.Populate(0x0, 0x0, arch::CpuidIo::kEbx, 0x756e'6547)
      .Populate(0x0, 0x0, arch::CpuidIo::kEdx, 0x4965'6e69)
      .Populate(0x0, 0x0, arch::CpuidIo::kEcx, 0x6c65'746e)
      .Populate(0x1, 0x0, arch::CpuidIo::kEax, 0x000306d0)
      .Populate(0x1, 0x0, arch::CpuidIo::kEcx, 0);

  arch::LbrStack stack(cpuid);

  ASSERT_FALSE(stack.is_supported());
  EXPECT_EQ(16, stack.size());

  // If unsupported, checking whether the stack is enabled should not result
  // in MSR access.
  msrMock.ExpectNoIo();
  EXPECT_FALSE(stack.is_enabled(msrIo));

  ASSERT_NO_FATAL_FAILURES(msrMock.VerifyAndClear());
}

}  // namespace
