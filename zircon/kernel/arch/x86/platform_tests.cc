// Copyright 2019 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <lib/cmdline.h>
#include <lib/console.h>
#include <lib/unittest/unittest.h>
#include <zircon/syscalls/system.h>

#include <arch/arch_ops.h>
#include <arch/mp.h>
#include <arch/x86.h>
#include <arch/x86/cpuid.h>
#include <arch/x86/cpuid_test_data.h>
#include <arch/x86/fake_msr_access.h>
#include <arch/x86/feature.h>
#include <arch/x86/hwp.h>
#include <arch/x86/platform_access.h>
#include <kernel/cpu.h>
#include <ktl/array.h>
#include <ktl/unique_ptr.h>

#include "../../lib/syscalls/system_priv.h"

extern void x86_amd_set_lfence_serializing(const cpu_id::CpuId*, MsrAccess*);
extern "C" void _x86_usercopy_quad(void*, void*, size_t);
extern "C" void _x86_usercopy_erms(void*, void*, size_t);
extern char __x86_indirect_thunk_r11;
extern char interrupt_maybe_mds_buff_overwrite;
extern char syscall_maybe_mds_buff_overwrite;

namespace {

static void rdtscp_aux(void* context) {
  uint32_t* const aux = reinterpret_cast<uint32_t*>(context);
  uint32_t tsc_lo, tsc_hi, aux_msr;
  asm volatile("rdtscp" : "=a"(tsc_lo), "=d"(tsc_hi), "=c"(aux_msr));
  *aux = aux_msr;
}

static bool test_x64_msrs() {
  BEGIN_TEST;

  interrupt_saved_state_t int_state = arch_interrupt_save();
  // Test read_msr for an MSR that is known to always exist on x64.
  uint64_t val = read_msr(X86_MSR_IA32_LSTAR);
  EXPECT_NE(val, 0ull);

  // Test write_msr to write that value back.
  write_msr(X86_MSR_IA32_LSTAR, val);
  arch_interrupt_restore(int_state);

  // Test read_msr_safe for an MSR that is known to not exist.
  // If read_msr_safe is busted, then this will #GP (panic).
  // TODO: Enable when the QEMU TCG issue is sorted (TCG never
  // generates a #GP on MSR access).
#ifdef DISABLED
  uint64_t bad_val;
  // AMD MSRC001_2xxx are only readable via Processor Debug.
  auto bad_status = read_msr_safe(0xC0012000, &bad_val);
  EXPECT_NE(bad_status, ZX_OK);
#endif

  // Test read_msr_on_cpu.
  uint64_t initial_fmask = read_msr(X86_MSR_IA32_FMASK);
  for (cpu_num_t i = 0; i < arch_max_num_cpus(); i++) {
    if (!mp_is_cpu_online(i)) {
      continue;
    }
    uint64_t fmask = read_msr_on_cpu(/*cpu=*/i, X86_MSR_IA32_FMASK);
    EXPECT_EQ(initial_fmask, fmask);
  }

  // Test write_msr_on_cpu
  for (cpu_num_t i = 0; i < arch_max_num_cpus(); i++) {
    if (!mp_is_cpu_online(i)) {
      continue;
    }
    write_msr_on_cpu(/*cpu=*/i, X86_MSR_IA32_FMASK, /*val=*/initial_fmask);
  }

  // If RDTSCP is supported, check that the TSC_AUX MSR is correctly programmed.
  if (x86_feature_test(X86_FEATURE_RDTSCP)) {
    for (cpu_num_t i = 0; i < arch_max_num_cpus(); i++) {
      if (!mp_is_cpu_online(i)) {
        continue;
      }
      uint64_t cpuid = read_msr_on_cpu(/*cpu=*/i, X86_MSR_IA32_TSC_AUX);
      EXPECT_EQ(cpuid, i);

      uint32_t aux;
      cpu_mask_t mask = {};
      mask |= cpu_num_to_mask(i);
      mp_sync_exec(MP_IPI_TARGET_MASK, mask, rdtscp_aux, reinterpret_cast<void*>(&aux));
      EXPECT_EQ(cpuid, aux);
    }
  }

  END_TEST;
}

static bool test_x64_msrs_k_commands() {
  BEGIN_TEST;

  console_run_script_locked("cpu rdmsr 0 0x10");

  END_TEST;
}

static bool test_x64_hwp_k_commands() {
  BEGIN_TEST;

  // Don't test at all if HWP disabled on the command line.
  if (!gCmdline.GetBool("kernel.x86.hwp", /*default_value=*/true)) {
    return true;
  }

  // If we don't support HWP, expect every command to just return "not supported".
  cpu_id::CpuId cpuid;
  if (!x86::IntelHwpSupported(&cpuid)) {
    EXPECT_EQ(ZX_ERR_NOT_SUPPORTED, console_run_script_locked("hwp"));
    return all_ok;
  }

  // Test top-level parsing.
  EXPECT_EQ(ZX_ERR_INVALID_ARGS, console_run_script_locked("hwp"));
  EXPECT_EQ(ZX_ERR_INVALID_ARGS, console_run_script_locked("hwp invalid"));
  EXPECT_EQ(ZX_ERR_INVALID_ARGS, console_run_script_locked("hwp 3"));

  // Set policy.
  EXPECT_EQ(ZX_ERR_INVALID_ARGS, console_run_script_locked("hwp set-policy"));
  EXPECT_EQ(ZX_ERR_INVALID_ARGS, console_run_script_locked("hwp set-policy invalid-policy"));
  EXPECT_EQ(ZX_ERR_INVALID_ARGS, console_run_script_locked("hwp set-policy 3"));
  EXPECT_EQ(ZX_ERR_INVALID_ARGS, console_run_script_locked("hwp set-policy performance 42"));
  EXPECT_EQ(ZX_OK, console_run_script_locked("hwp set-policy performance"));
  EXPECT_EQ(ZX_OK, console_run_script_locked("hwp set-policy power-save"));

  // Set Freq
  EXPECT_EQ(ZX_ERR_INVALID_ARGS, console_run_script_locked("hwp set-freq"));
  EXPECT_EQ(ZX_ERR_INVALID_ARGS, console_run_script_locked("hwp set-freq 0"));
  EXPECT_EQ(ZX_ERR_INVALID_ARGS, console_run_script_locked("hwp set-freq 256"));
  EXPECT_EQ(ZX_ERR_INVALID_ARGS, console_run_script_locked("hwp set-freq 10 10"));
  EXPECT_EQ(ZX_OK, console_run_script_locked("hwp set-freq 100"));
  EXPECT_EQ(ZX_OK, console_run_script_locked("hwp set-freq 255"));

  // Restore the policy to default.
  MsrAccess msr;
  x86::IntelHwpPolicy policy = x86::IntelHwpParsePolicy(gCmdline.GetString("kernel.x86.hwp_policy"))
                                   .value_or(x86::IntelHwpPolicy::kBiosSpecified);
  x86::IntelHwpInit(&cpuid, &msr, policy);

  END_TEST;
}

static bool test_x64_cpu_uarch_config_selection() {
  BEGIN_TEST;

  EXPECT_EQ(get_microarch_config(&cpu_id::kCpuIdCorei5_6260U)->x86_microarch,
            X86_MICROARCH_INTEL_SKYLAKE);

  // Intel Xeon E5-2690 V4 is Broadwell
  EXPECT_EQ(get_microarch_config(&cpu_id::kCpuIdXeon2690v4)->x86_microarch,
            X86_MICROARCH_INTEL_BROADWELL);

  // Intel Celeron J3455 is Goldmont
  EXPECT_EQ(get_microarch_config(&cpu_id::kCpuIdCeleronJ3455)->x86_microarch,
            X86_MICROARCH_INTEL_GOLDMONT);

  // AMD A4-9120C is Bulldozer
  EXPECT_EQ(get_microarch_config(&cpu_id::kCpuIdAmdA49120C)->x86_microarch,
            X86_MICROARCH_AMD_BULLDOZER);

  // AMD Ryzen Threadripper 2970WX is Zen
  EXPECT_EQ(get_microarch_config(&cpu_id::kCpuIdThreadRipper2970wx)->x86_microarch,
            X86_MICROARCH_AMD_ZEN);

  END_TEST;
}

static bool test_x64_meltdown_enumeration() {
  fbl::AllocChecker ac;
  BEGIN_TEST;

  {
    // Test an Intel Xeon E5-2690 V4 w/ older microcode (no ARCH_CAPABILITIES)
    auto data = ktl::make_unique<cpu_id::TestDataSet>(&ac, cpu_id::kTestDataXeon2690v4);
    ASSERT_TRUE(ac.check(), "");
    data->leaf7.reg[cpu_id::Features::ARCH_CAPABILITIES.reg] &=
        ~(1 << cpu_id::Features::ARCH_CAPABILITIES.bit);
    cpu_id::FakeCpuId cpu(*data.get());
    FakeMsrAccess fake_msrs = {};
    EXPECT_TRUE(x86_intel_cpu_has_meltdown(&cpu, &fake_msrs));
  }

  {
    // Test an Intel Xeon E5-2690 V4 w/ new microcode (ARCH_CAPABILITIES available)
    auto data = ktl::make_unique<cpu_id::TestDataSet>(&ac, cpu_id::kTestDataXeon2690v4);
    ASSERT_TRUE(ac.check(), "");
    data->leaf7.reg[cpu_id::Features::ARCH_CAPABILITIES.reg] |=
        (1 << cpu_id::Features::ARCH_CAPABILITIES.bit);
    cpu_id::FakeCpuId cpu(*data.get());
    FakeMsrAccess fake_msrs = {};
    fake_msrs.msrs_[0] = {X86_MSR_IA32_ARCH_CAPABILITIES, 0};
    EXPECT_TRUE(x86_intel_cpu_has_meltdown(&cpu, &fake_msrs));
  }

  {
    // Intel(R) Core(TM) i5-5257U has Meltdown
    auto data = ktl::make_unique<cpu_id::TestDataSet>(&ac);
    ASSERT_TRUE(ac.check(), "");
    data->leaf0 = {.reg = {0x14, 0x756e6547, 0x6c65746e, 0x49656e69}};
    data->leaf1 = {.reg = {0x306d4, 0x100800, 0x7ffafbbf, 0xbfebfbff}};
    data->leaf4 = {.reg = {0x1c004121, 0x1c0003f, 0x3f, 0x0}};
    data->leaf7 = {.reg = {0x0, 0x21c27ab, 0x0, 0x9c000000}};
    cpu_id::FakeCpuId cpu(*data.get());
    FakeMsrAccess fake_msrs = {};
    EXPECT_TRUE(x86_intel_cpu_has_meltdown(&cpu, &fake_msrs));
  }

  {
    // Intel(R) Xeon(R) Gold 6xxx; does not have Meltdown, reports via RDCL_NO
    auto data = ktl::make_unique<cpu_id::TestDataSet>(&ac);
    ASSERT_TRUE(ac.check(), "");
    data->leaf0 = {.reg = {0x16, 0x756e6547, 0x6c65746e, 0x49656e69}};
    data->leaf1 = {.reg = {0x50656, 0x12400800, 0x7ffefbff, 0xbfebfbff}};
    data->leaf4 = {.reg = {0x7c004121, 0x1c0003f, 0x3f, 0x0}};
    data->leaf7 = {.reg = {0x0, 0xd39ffffb, 0x808, 0xbc000400}};

    cpu_id::FakeCpuId cpu(*data.get());
    FakeMsrAccess fake_msrs = {};
    fake_msrs.msrs_[0] = {X86_MSR_IA32_ARCH_CAPABILITIES, 0x2b};
    EXPECT_FALSE(x86_intel_cpu_has_meltdown(&cpu, &fake_msrs));
  }

  {
    // Intel(R) Celeron(R) CPU J3455 (Goldmont) does not have Meltdown, _but_ old microcode
    // lacks RDCL_NO. We will misidentify this CPU as having Meltdown.
    auto data = ktl::make_unique<cpu_id::TestDataSet>(&ac);
    ASSERT_TRUE(ac.check(), "");
    data->leaf0 = {.reg = {0x15, 0x756e6547, 0x6c65746e, 0x49656e69}};
    data->leaf1 = {.reg = {0x506c9, 0x2200800, 0x4ff8ebbf, 0xbfebfbff}};
    data->leaf4 = {.reg = {0x3c000121, 0x140003f, 0x3f, 0x1}};
    data->leaf7 = {.reg = {0x0, 0x2294e283, 0x0, 0x2c000000}};
    data->leaf7.reg[cpu_id::Features::ARCH_CAPABILITIES.reg] &=
        ~(1 << cpu_id::Features::ARCH_CAPABILITIES.bit);

    FakeMsrAccess fake_msrs = {};
    {
      cpu_id::FakeCpuId cpu(*data.get());
      EXPECT_TRUE(x86_intel_cpu_has_meltdown(&cpu, &fake_msrs), "");
    }

    // Intel(R) Celeron(R) CPU J3455 (Goldmont) does not have Meltdown, reports via RDCL_NO
    // (with recent microcode updates)
    data->leaf7.reg[cpu_id::Features::ARCH_CAPABILITIES.reg] |=
        (1 << cpu_id::Features::ARCH_CAPABILITIES.bit);

    // 0x19 = RDCL_NO | SKIP_VMENTRY_L1DFLUSH | SSB_NO
    fake_msrs.msrs_[0] = {X86_MSR_IA32_ARCH_CAPABILITIES, 0x19};
    {
      cpu_id::FakeCpuId cpu(*data.get());
      EXPECT_FALSE(x86_intel_cpu_has_meltdown(&cpu, &fake_msrs), "");
    }
  }

  {
    // Intel(R) Celeron J4005 (Goldmont+ / Gemini Lake) _does_ have Meltdown,
    // IA32_ARCH_CAPABILITIES[0] = 0
    auto data = ktl::make_unique<cpu_id::TestDataSet>(&ac);
    ASSERT_TRUE(ac.check());
    data->leaf0 = {.reg = {0x16, 0x756e6547, 0x6c65746e, 0x49656e69}};
    data->leaf1 = {.reg = {0x706A1, 0x12400800, 0x7ffefbff, 0xbfebfbff}};
    data->leaf4 = {.reg = {0x7c004121, 0x1c0003f, 0x3f, 0x0}};
    data->leaf7 = {.reg = {0x0, 0xd39ffffb, 0x808, 0xbc000400}};

    cpu_id::FakeCpuId cpu(*data.get());
    FakeMsrAccess fake_msrs = {};
    fake_msrs.msrs_[0] = {X86_MSR_IA32_ARCH_CAPABILITIES, 0xA};  // microcode 2c -> Ah; 2e -> 6ah
    EXPECT_TRUE(x86_intel_cpu_has_meltdown(&cpu, &fake_msrs));
  }

  END_TEST;
}

static bool test_x64_l1tf_enumeration() {
  BEGIN_TEST;

  fbl::AllocChecker ac;
  {
    // Test an Intel Xeon E5-2690 V4 w/ older microcode (no ARCH_CAPABILITIES)
    auto data = ktl::make_unique<cpu_id::TestDataSet>(&ac, cpu_id::kTestDataXeon2690v4);
    ASSERT_TRUE(ac.check(), "");
    data->leaf7.reg[cpu_id::Features::ARCH_CAPABILITIES.reg] &=
        ~(1 << cpu_id::Features::ARCH_CAPABILITIES.bit);
    cpu_id::FakeCpuId cpu(*data.get());
    FakeMsrAccess fake_msrs = {};
    EXPECT_TRUE(x86_intel_cpu_has_l1tf(&cpu, &fake_msrs));
  }

  {
    // Test an Intel Xeon E5-2690 V4 w/ new microcode (ARCH_CAPABILITIES available)
    auto data = ktl::make_unique<cpu_id::TestDataSet>(&ac, cpu_id::kTestDataXeon2690v4);
    ASSERT_TRUE(ac.check(), "");
    data->leaf7.reg[cpu_id::Features::ARCH_CAPABILITIES.reg] |=
        (1 << cpu_id::Features::ARCH_CAPABILITIES.bit);
    cpu_id::FakeCpuId cpu(*data.get());
    FakeMsrAccess fake_msrs = {};
    fake_msrs.msrs_[0] = {X86_MSR_IA32_ARCH_CAPABILITIES, 0};
    EXPECT_TRUE(x86_intel_cpu_has_l1tf(&cpu, &fake_msrs));
  }

  {
    // Intel(R) Xeon(R) Gold 6xxx; does not have Meltdown, reports via RDCL_NO
    auto data = ktl::make_unique<cpu_id::TestDataSet>(&ac);
    ASSERT_TRUE(ac.check(), "");
    data->leaf0 = {.reg = {0x16, 0x756e6547, 0x6c65746e, 0x49656e69}};
    data->leaf1 = {.reg = {0x50656, 0x12400800, 0x7ffefbff, 0xbfebfbff}};
    data->leaf4 = {.reg = {0x7c004121, 0x1c0003f, 0x3f, 0x0}};
    data->leaf7 = {.reg = {0x0, 0xd39ffffb, 0x808, 0xbc000400}};

    cpu_id::FakeCpuId cpu(*data.get());
    FakeMsrAccess fake_msrs = {};
    fake_msrs.msrs_[0] = {X86_MSR_IA32_ARCH_CAPABILITIES, 0x2b};
    EXPECT_FALSE(x86_intel_cpu_has_l1tf(&cpu, &fake_msrs));
  }

  {
    // Intel(R) Celeron(R) CPU J3455 (Goldmont) does not have Meltdown, reports via RDCL_NO
    auto data = ktl::make_unique<cpu_id::TestDataSet>(&ac);
    ASSERT_TRUE(ac.check(), "");
    data->leaf0 = {.reg = {0x15, 0x756e6547, 0x6c65746e, 0x49656e69}};
    data->leaf1 = {.reg = {0x506c9, 0x2200800, 0x4ff8ebbf, 0xbfebfbff}};
    data->leaf4 = {.reg = {0x3c000121, 0x140003f, 0x3f, 0x1}};
    data->leaf7 = {.reg = {0x0, 0x2294e283, 0x0, 0x2c000000}};
    data->leaf7.reg[cpu_id::Features::ARCH_CAPABILITIES.reg] |=
        (1 << cpu_id::Features::ARCH_CAPABILITIES.bit);

    cpu_id::FakeCpuId cpu(*data.get());
    FakeMsrAccess fake_msrs = {};
    // 0x19 = RDCL_NO | SKIP_VMENTRY_L1DFLUSH | SSB_NO
    fake_msrs.msrs_[0] = {X86_MSR_IA32_ARCH_CAPABILITIES, 0x19};
    EXPECT_FALSE(x86_intel_cpu_has_l1tf(&cpu, &fake_msrs));
  }

  END_TEST;
}

static bool test_x64_mds_enumeration() {
  BEGIN_TEST;

  fbl::AllocChecker ac;
  {
    // Test an Intel Xeon E5-2690 V4 w/ older microcode (no ARCH_CAPABILITIES)
    FakeMsrAccess fake_msrs;
    EXPECT_TRUE(x86_intel_cpu_has_mds_taa(&cpu_id::kCpuIdXeon2690v4, &fake_msrs));
  }

  {
    // Test an Intel Xeon E5-2690 V4 w/ new microcode (ARCH_CAPABILITIES available)
    auto data = ktl::make_unique<cpu_id::TestDataSet>(&ac, cpu_id::kTestDataXeon2690v4);
    ASSERT_TRUE(ac.check(), "");
    data->leaf7.reg[cpu_id::Features::ARCH_CAPABILITIES.reg] |=
        (1 << cpu_id::Features::ARCH_CAPABILITIES.bit);
    cpu_id::FakeCpuId cpu(*data.get());
    FakeMsrAccess fake_msrs = {};
    fake_msrs.msrs_[0] = {X86_MSR_IA32_ARCH_CAPABILITIES, 0};
    EXPECT_TRUE(x86_intel_cpu_has_mds_taa(&cpu, &fake_msrs));
  }

  {
    // Intel(R) Xeon(R) Gold 6xxx; does not have MDS but it does have TAA.
    auto data = ktl::make_unique<cpu_id::TestDataSet>(&ac);
    ASSERT_TRUE(ac.check(), "");
    data->leaf0 = {.reg = {0x16, 0x756e6547, 0x6c65746e, 0x49656e69}};
    data->leaf1 = {.reg = {0x50656, 0x12400800, 0x7ffefbff, 0xbfebfbff}};
    data->leaf4 = {.reg = {0x7c004121, 0x1c0003f, 0x3f, 0x0}};
    data->leaf7 = {.reg = {0x0, 0xd39ffffb, 0x808, 0xbc000400}};

    cpu_id::FakeCpuId cpu(*data.get());
    FakeMsrAccess fake_msrs = {};
    fake_msrs.msrs_[0] = {X86_MSR_IA32_ARCH_CAPABILITIES, 0x2b};
    EXPECT_TRUE(x86_intel_cpu_has_mds_taa(&cpu, &fake_msrs));
  }

  {
    // Intel(R) Celeron(R) CPU J3455 (Goldmont) does not have MDS but does not
    // enumerate MDS_NO with microcode 32h (at least). It does not have TSX,
    // so it does not have TAA.
    auto data = ktl::make_unique<cpu_id::TestDataSet>(&ac);
    ASSERT_TRUE(ac.check(), "");
    data->leaf0 = {.reg = {0x15, 0x756e6547, 0x6c65746e, 0x49656e69}};
    data->leaf1 = {.reg = {0x506c9, 0x2200800, 0x4ff8ebbf, 0xbfebfbff}};
    data->leaf4 = {.reg = {0x3c000121, 0x140003f, 0x3f, 0x1}};
    data->leaf7 = {.reg = {0x0, 0x2294e283, 0x0, 0x2c000000}};

    cpu_id::FakeCpuId cpu(*data.get());
    FakeMsrAccess fake_msrs = {};
    // 0x19 = RDCL_NO | SKIP_VMENTRY_L1DFLUSH | SSB_NO
    fake_msrs.msrs_[0] = {X86_MSR_IA32_ARCH_CAPABILITIES, 0x19};
    EXPECT_FALSE(x86_intel_cpu_has_mds_taa(&cpu, &fake_msrs));
  }

  END_TEST;
}

static bool test_x64_swapgs_bug_enumeration() {
  BEGIN_TEST;

  fbl::AllocChecker ac;
  {
    // Test an Intel Xeon E5-2690 V4
    cpu_id::FakeCpuId cpu(cpu_id::kTestDataXeon2690v4);
    EXPECT_TRUE(x86_intel_cpu_has_swapgs_bug(&cpu), "");
  }

  {
    // Intel(R) Xeon(R) Gold 6xxx has SWAPGS bug
    auto data = ktl::make_unique<cpu_id::TestDataSet>(&ac);
    ASSERT_TRUE(ac.check(), "");
    data->leaf0 = {.reg = {0x16, 0x756e6547, 0x6c65746e, 0x49656e69}};
    data->leaf1 = {.reg = {0x50656, 0x12400800, 0x7ffefbff, 0xbfebfbff}};
    data->leaf4 = {.reg = {0x7c004121, 0x1c0003f, 0x3f, 0x0}};
    data->leaf7 = {.reg = {0x0, 0xd39ffffb, 0x808, 0xbc000400}};

    cpu_id::FakeCpuId cpu(*data.get());
    EXPECT_TRUE(x86_intel_cpu_has_swapgs_bug(&cpu), "");
  }

  {
    // Intel(R) Celeron(R) CPU J3455 (Goldmont) does not have SWAPGS bug
    auto data = ktl::make_unique<cpu_id::TestDataSet>(&ac);
    ASSERT_TRUE(ac.check(), "");
    data->leaf0 = {.reg = {0x15, 0x756e6547, 0x6c65746e, 0x49656e69}};
    data->leaf1 = {.reg = {0x506c9, 0x2200800, 0x4ff8ebbf, 0xbfebfbff}};
    data->leaf4 = {.reg = {0x3c000121, 0x140003f, 0x3f, 0x1}};
    data->leaf7 = {.reg = {0x0, 0x2294e283, 0x0, 0x2c000000}};
    cpu_id::FakeCpuId cpu(*data.get());
    EXPECT_FALSE(x86_intel_cpu_has_swapgs_bug(&cpu), "");
  }

  END_TEST;
}

static bool test_x64_ssb_enumeration() {
  BEGIN_TEST;

  fbl::AllocChecker ac;
  {
    // Test an Intel Xeon E5-2690 V4 w/ older microcode (no ARCH_CAPABILITIES)
    FakeMsrAccess fake_msrs;
    EXPECT_TRUE(x86_intel_cpu_has_ssb(&cpu_id::kCpuIdXeon2690v4, &fake_msrs));
    EXPECT_TRUE(x86_intel_cpu_has_ssbd(&cpu_id::kCpuIdXeon2690v4, &fake_msrs));
  }

  {
    // Test an Intel Xeon E5-2690 V4 w/ new microcode (ARCH_CAPABILITIES available)
    auto data = ktl::make_unique<cpu_id::TestDataSet>(&ac, cpu_id::kTestDataXeon2690v4);
    ASSERT_TRUE(ac.check(), "");
    data->leaf7.reg[cpu_id::Features::ARCH_CAPABILITIES.reg] |=
        (1 << cpu_id::Features::ARCH_CAPABILITIES.bit);
    data->leaf7.reg[cpu_id::Features::SSBD.reg] |= (1 << cpu_id::Features::SSBD.bit);
    cpu_id::FakeCpuId cpu(*data.get());
    FakeMsrAccess fake_msrs = {};
    fake_msrs.msrs_[0] = {X86_MSR_IA32_ARCH_CAPABILITIES, 0};
    EXPECT_TRUE(x86_intel_cpu_has_ssb(&cpu, &fake_msrs));
    EXPECT_TRUE(x86_intel_cpu_has_ssbd(&cpu, &fake_msrs));
  }

  {
    // Intel(R) Celeron(R) CPU J3455 (Goldmont) reports SSB_NO via IA32_ARCH_CAPABILITIES
    FakeMsrAccess fake_msrs = {};
    // 0x19 = RDCL_NO | SKIP_VMENTRY_L1DFLUSH | SSB_NO
    fake_msrs.msrs_[0] = {X86_MSR_IA32_ARCH_CAPABILITIES, 0x19};
    EXPECT_FALSE(x86_intel_cpu_has_ssb(&cpu_id::kCpuIdCeleronJ3455, &fake_msrs));
    EXPECT_FALSE(x86_intel_cpu_has_ssbd(&cpu_id::kCpuIdCeleronJ3455, &fake_msrs));
  }

  {
    // AMD Threadripper (Zen1) has SSB
    FakeMsrAccess fake_msrs;
    EXPECT_TRUE(x86_amd_cpu_has_ssb(&cpu_id::kCpuIdThreadRipper2970wx, &fake_msrs));
    EXPECT_TRUE(x86_amd_cpu_has_ssbd(&cpu_id::kCpuIdThreadRipper2970wx, &fake_msrs));
  }
  {
    // AMD A4-9120C (Stoney Ridge) has SSB
    FakeMsrAccess fake_msrs;
    EXPECT_TRUE(x86_amd_cpu_has_ssb(&cpu_id::kCpuIdAmdA49120C, &fake_msrs));
    EXPECT_TRUE(x86_amd_cpu_has_ssbd(&cpu_id::kCpuIdAmdA49120C, &fake_msrs));
  }

  END_TEST;
}

static bool test_x64_ssb_disable() {
  fbl::AllocChecker ac;
  BEGIN_TEST;

  // Test SSBD control on Intel Xeon E5-2690 V4
  {
    auto data = ktl::make_unique<cpu_id::TestDataSet>(&ac, cpu_id::kTestDataXeon2690v4);
    ASSERT_TRUE(ac.check());
    data->leaf7.reg[cpu_id::Features::SSBD.reg] |= (1 << cpu_id::Features::SSBD.bit);
    cpu_id::FakeCpuId cpu(*data.get());
    FakeMsrAccess fake_msrs = {};
    fake_msrs.msrs_[0] = {X86_MSR_IA32_SPEC_CTRL, 0};
    x86_intel_cpu_set_ssbd(&cpu, &fake_msrs);
    EXPECT_EQ(fake_msrs.msrs_[0].value, X86_SPEC_CTRL_SSBD);
  }

  // Test SSBD control on AMD Zen1; the non-architectural mechanism will be used as
  // neither AMD_SSBD nor AMD_VIRT_SSBD are set.
  {
    FakeMsrAccess fake_msrs = {};
    fake_msrs.msrs_[0] = {X86_MSR_AMD_LS_CFG, 0x0};
    x86_amd_cpu_set_ssbd(&cpu_id::kCpuIdThreadRipper2970wx, &fake_msrs);
    EXPECT_EQ(fake_msrs.msrs_[0].value, X86_AMD_LS_CFG_F17H_SSBD);
  }

  // Test SSBD Control on AMD A4-9120C (Stoney Ridge)
  {
    FakeMsrAccess fake_msrs = {};
    fake_msrs.msrs_[0] = {X86_MSR_AMD_LS_CFG, 0x0};
    x86_amd_cpu_set_ssbd(&cpu_id::kCpuIdAmdA49120C, &fake_msrs);
    EXPECT_EQ(fake_msrs.msrs_[0].value, X86_AMD_LS_CFG_F15H_SSBD);
  }

  // Test SSBD Control on AMD A4-9120C (Stoney Ridge) with VIRT_SSBD available. This is
  // what you see an an APU running a KVM hypervisor guest.
  {
    auto data = ktl::make_unique<cpu_id::TestDataSet>(&ac, cpu_id::kTestDataAmdA49120C);
    ASSERT_TRUE(ac.check());
    data->leaf8_8.reg[cpu_id::Features::AMD_VIRT_SSBD.reg] |=
        (1 << cpu_id::Features::AMD_VIRT_SSBD.bit);
    cpu_id::FakeCpuId cpu(*data.get());
    FakeMsrAccess fake_msrs = {};
    EXPECT_TRUE(x86_amd_cpu_has_ssbd(&cpu, &fake_msrs));
    fake_msrs.msrs_[0] = {X86_MSR_AMD_VIRT_SPEC_CTRL, 0x0};
    x86_amd_cpu_set_ssbd(&cpu, &fake_msrs);
    EXPECT_EQ(fake_msrs.msrs_[0].value, X86_SPEC_CTRL_SSBD);
  }

  END_TEST;
}

static bool test_x64_spectre_v2_enumeration() {
  BEGIN_TEST;

  fbl::AllocChecker ac;
  {
    // Test an Intel Xeon E5-2690 V4 w/ older microcode (no ARCH_CAPABILITIES)
    FakeMsrAccess fake_msrs;
    EXPECT_FALSE(x86_intel_cpu_has_enhanced_ibrs(&cpu_id::kCpuIdXeon2690v4, &fake_msrs));
  }
  {
    // Test an Intel Xeon E5-2690 V4 w/ new microcode (ARCH_CAPABILITIES available)
    auto data = ktl::make_unique<cpu_id::TestDataSet>(&ac, cpu_id::kTestDataXeon2690v4);
    ASSERT_TRUE(ac.check(), "");
    data->leaf7.reg[cpu_id::Features::ARCH_CAPABILITIES.reg] |=
        (1 << cpu_id::Features::ARCH_CAPABILITIES.bit);
    data->leaf7.reg[cpu_id::Features::SSBD.reg] |= (1 << cpu_id::Features::SSBD.bit);
    cpu_id::FakeCpuId cpu(*data.get());
    FakeMsrAccess fake_msrs = {};
    fake_msrs.msrs_[0] = {X86_MSR_IA32_ARCH_CAPABILITIES, 0};
    EXPECT_FALSE(x86_intel_cpu_has_enhanced_ibrs(&cpu_id::kCpuIdXeon2690v4, &fake_msrs));
  }
  {
    // Use Goldmont CPUID but set IBRS_ALL.
    FakeMsrAccess fake_msrs = {};
    // IBRS_ALL | SKIP_L1DFL_VMENTRY | (PSCHANGE)
    fake_msrs.msrs_[0] = {X86_MSR_IA32_ARCH_CAPABILITIES, 0x6a};
    EXPECT_TRUE(x86_intel_cpu_has_enhanced_ibrs(&cpu_id::kCpuIdCeleronJ3455, &fake_msrs));
  }

  END_TEST;
}

static uint32_t intel_make_microcode_checksum(uint32_t* patch, size_t bytes) {
  size_t dwords = bytes / sizeof(uint32_t);
  uint32_t sum = 0;
  for (size_t i = 0; i < dwords; i++) {
    sum += patch[i];
  }
  return -sum;
}

static bool test_x64_intel_ucode_loader() {
  BEGIN_TEST;

  // x86_intel_check_microcode_patch checks if a microcode patch is suitable for a particular
  // CPU. Test that its match logic works for various CPUs and conditions we commonly use.

  {
    uint32_t fake_patch[512] = {};
    // Intel(R) Celeron(R) CPU J3455 (Goldmont), NUC6CAYH
    cpu_id::TestDataSet data = {};
    data.leaf0 = {.reg = {0x15, 0x756e6547, 0x6c65746e, 0x49656e69}};
    data.leaf1 = {.reg = {0x506c9, 0x2200800, 0x4ff8ebbf, 0xbfebfbff}};
    data.leaf4 = {.reg = {0x3c000121, 0x140003f, 0x3f, 0x1}};
    data.leaf7 = {.reg = {0x0, 0x2294e283, 0x0, 0x2c000000}};
    cpu_id::FakeCpuId cpu(data);
    FakeMsrAccess fake_msrs = {};
    fake_msrs.msrs_[0] = {X86_MSR_IA32_PLATFORM_ID, 0x1ull << 50};  // Apollo Lake

    // Reject an all-zero patch.
    EXPECT_FALSE(
        x86_intel_check_microcode_patch(&cpu, &fake_msrs, {fake_patch, sizeof(fake_patch)}));

    // Reject patch with non-matching processor signature.
    fake_patch[0] = 0x1;
    fake_patch[4] = intel_make_microcode_checksum(fake_patch, sizeof(fake_patch));
    EXPECT_FALSE(
        x86_intel_check_microcode_patch(&cpu, &fake_msrs, {fake_patch, sizeof(fake_patch)}));

    // Expect matching patch to pass
    fake_patch[0] = 0x1;
    fake_patch[3] = data.leaf1.reg[0];  // Signature match
    fake_patch[6] = 0x3;                // Processor flags match PLATFORM_ID
    fake_patch[4] = 0;
    fake_patch[4] = intel_make_microcode_checksum(fake_patch, sizeof(fake_patch));
    EXPECT_TRUE(
        x86_intel_check_microcode_patch(&cpu, &fake_msrs, {fake_patch, sizeof(fake_patch)}));
    // Real header from 2019-01-15, rev 38
    fake_patch[0] = 0x1;
    fake_patch[1] = 0x38;
    fake_patch[2] = 0x01152019;
    fake_patch[3] = 0x506c9;
    fake_patch[6] = 0x3;  // Processor flags match PLATFORM_ID
    fake_patch[4] = 0;
    fake_patch[4] = intel_make_microcode_checksum(fake_patch, sizeof(fake_patch));
    EXPECT_TRUE(
        x86_intel_check_microcode_patch(&cpu, &fake_msrs, {fake_patch, sizeof(fake_patch)}));
  }

  END_TEST;
}

class FakeWriteMsr : public MsrAccess {
 public:
  void write_msr(uint32_t msr_index, uint64_t value) override {
    DEBUG_ASSERT(written_ == false);
    written_ = true;
    msr_index_ = msr_index;
  }

  bool written_ = false;
  uint32_t msr_index_;
};

static bool test_x64_intel_ucode_patch_loader() {
  BEGIN_TEST;

  cpu_id::TestDataSet data = {};
  cpu_id::FakeCpuId cpu(data);
  FakeWriteMsr msrs;
  uint32_t fake_patch[512] = {};

  // This test can only run on physical Intel x86-64 hosts; x86_intel_get_patch_level
  // does not use an interface to access patch_level registers and those registers are
  // only present/writable on h/w.
  if (x86_vendor == X86_VENDOR_INTEL && !x86_feature_test(X86_FEATURE_HYPERVISOR)) {
    // Expect that a patch == current patch is not loaded.
    uint32_t current_patch_level = x86_intel_get_patch_level();
    fake_patch[1] = current_patch_level;
    x86_intel_load_microcode_patch(&cpu, &msrs, {fake_patch, sizeof(fake_patch)});
    EXPECT_FALSE(msrs.written_);

    // Expect that a newer patch is loaded.
    fake_patch[1] = current_patch_level + 1;
    x86_intel_load_microcode_patch(&cpu, &msrs, {fake_patch, sizeof(fake_patch)});
    EXPECT_TRUE(msrs.written_);
    EXPECT_EQ(msrs.msr_index_, X86_MSR_IA32_BIOS_UPDT_TRIG);
  }

  END_TEST;
}

static bool test_x64_power_limits() {
  BEGIN_TEST;
  FakeMsrAccess fake_msrs = {};
  // defaults on Ava/Eve. They both use the same Intel chipset
  // only diff is the WiFi. Ava uses Broadcom vs Eve uses Intel
  fake_msrs.msrs_[0] = {X86_MSR_PKG_POWER_LIMIT, 0x1807800dd8038};
  fake_msrs.msrs_[1] = {X86_MSR_RAPL_POWER_UNIT, 0xA0E03};
  // This default value does not look right, but this is a RO MSR
  fake_msrs.msrs_[2] = {X86_MSR_PKG_POWER_INFO, 0x24};
  // Read the defaults from pkg power msr.
  uint64_t default_val = fake_msrs.read_msr(X86_MSR_PKG_POWER_LIMIT);
  uint32_t new_power_limit = 4500;
  uint32_t new_time_window = 24000000;
  // expected value in MSR with the new power limit and time window
  uint64_t new_msr = 0x18078009d8024;
  zx_system_powerctl_arg_t arg;
  arg.x86_power_limit.clamp = static_cast<uint8_t>(default_val >> 16 & 0x01);
  arg.x86_power_limit.enable = static_cast<uint8_t>(default_val >> 15 & 0x01);
  // changing the value to 4.5W from 7W = 0x24 in the MSR
  // X86_MSR_PKG_POWER_LIMIT & 0x7FFF = 0x24 * power_units should give 4.5W
  arg.x86_power_limit.power_limit = new_power_limit;
  // changing the value to 24s from 28s = 0x4E in the MSR
  arg.x86_power_limit.time_window = new_time_window;
  // write it back again to see if the new function does it right
  auto status = arch_system_powerctl(ZX_SYSTEM_POWERCTL_X86_SET_PKG_PL1, &arg, &fake_msrs);
  if (status != ZX_ERR_NOT_SUPPORTED) {
    uint64_t new_val = fake_msrs.read_msr(X86_MSR_PKG_POWER_LIMIT);
    EXPECT_EQ(new_val, new_msr, "Set power limit failed");
  }
  END_TEST;
}

static bool test_amd_platform_init() {
  BEGIN_TEST;
  FakeMsrAccess fake_msrs = {};

  // Test that set_lfence_serializing sets the LFENCE bit when its not already set.
  fake_msrs.msrs_[0] = {X86_MSR_AMD_F10_DE_CFG, 0};
  x86_amd_set_lfence_serializing(&cpu_id::kCpuIdThreadRipper2970wx, &fake_msrs);
  EXPECT_EQ(fake_msrs.msrs_[0].value, 0x2ull, "");

  // Test that set_lfence_serializing doesn't change the LFENCE bit when its set.
  fake_msrs.msrs_[0] = {X86_MSR_AMD_F10_DE_CFG, 0x2ull};
  fake_msrs.no_writes_ = true;
  x86_amd_set_lfence_serializing(&cpu_id::kCpuIdThreadRipper2970wx, &fake_msrs);
  EXPECT_EQ(fake_msrs.msrs_[0].value, 0x2ull, "");

  END_TEST;
}

static bool test_spectre_v2_mitigations() {
  BEGIN_TEST;
  bool sp_match;

  // Execute x86_ras_fill and make sure %rsp is unchanged.
  __asm__ __volatile__(
      "mov %%rsp, %%r11\n"
      "call x86_ras_fill\n"
      "cmp %%rsp, %%r11\n"
      "setz %0"
      : "=r"(sp_match)::"memory", "%r11");
  EXPECT_EQ(sp_match, true);

  // Test that retpoline thunks are correctly patched.
  unsigned char check_buffer[16] = {};
  memcpy(check_buffer, &__x86_indirect_thunk_r11, sizeof(check_buffer));
  if (x86_get_disable_spec_mitigations() == true || x86_cpu_has_enhanced_ibrs()) {
    // If speculative execution mitigations are disabled or Enhanced IBRS is enabled, we expect the
    // retpoline thunk to be:
    // __x86_indirect_thunk:
    //   41 ff e3        jmp *%r11
    EXPECT_EQ(check_buffer[0], 0x41);
    EXPECT_EQ(check_buffer[1], 0xff);
    EXPECT_EQ(check_buffer[2], 0xe3);
  } else if (x86_vendor == X86_VENDOR_INTEL) {
    // We expect the generic thunk to be:
    // __x86_indirect_thunk:
    //  e8 ?? ?? ?? ?? call ...
    //
    // We cannot test the exact contents of the thunk as the call target depends on the internal
    // alignment. Instead check that the first byte is the call instruction we expect.
    EXPECT_EQ(check_buffer[0], 0xe8);
  } else if (x86_vendor == X86_VENDOR_AMD) {
    // We expect the AMD thunk to be:
    // __x86_indirect_thunk:
    //   0f ae e8      lfence
    //   41 ff e3      jmp *%r11
    EXPECT_EQ(check_buffer[0], 0x0f);
    EXPECT_EQ(check_buffer[1], 0xae);
    EXPECT_EQ(check_buffer[2], 0xe8);
    EXPECT_EQ(check_buffer[3], 0x41);
    EXPECT_EQ(check_buffer[4], 0xff);
    EXPECT_EQ(check_buffer[5], 0xe3);
  } else {
    ASSERT_TRUE(false, "Unknown vendor.");
  }

  END_TEST;
}

static bool test_mds_mitigations() {
  BEGIN_TEST;

  for (char *src : { &interrupt_maybe_mds_buff_overwrite, &syscall_maybe_mds_buff_overwrite }) {
    unsigned char check_buffer[5];
    memcpy(check_buffer, src, sizeof(check_buffer));
    if (x86_cpu_should_md_clear_on_user_return()) {
      EXPECT_EQ(check_buffer[0], 0xe8);  // Expect a call to mds_buff_overwrite
    } else {
      // If speculative execution mitigations are disabled or we're not affected by MDS or don't
      // have MD_CLEAR, expect NOPs.
      for (size_t i = 0; i < sizeof(check_buffer); i++) {
        EXPECT_EQ(check_buffer[i], 0x90);
      }
    }
  }

  END_TEST;
}

static bool test_usercopy_variants() {
  BEGIN_TEST;

  // kSize must be greater than 8 and not a multiple of 8 to fully test _x86_usercopy_quad.
  constexpr size_t kSize = 37;
  unsigned char src[kSize];
  unsigned char dst[kSize];  // Copy buffer a -> b and check the copy worked.

  // Prefill test pattern
  for (uint i = 0; i < kSize; i++) {
    src[i] = static_cast<unsigned char>(i);
  }

  // Vary size; test both quad and erms versions.
  for (uint i = 1; i < kSize; i++) {
    memset(dst, 0, kSize);
    _x86_usercopy_quad(dst, src, i);
    for (uint j = 0; j < i; j++) {
      EXPECT_EQ(src[j], dst[j]);
    }
    memset(dst, 0, kSize);
    _x86_usercopy_erms(dst, src, i);
    for (uint j = 0; j < i; j++) {
      EXPECT_EQ(src[j], dst[j]);
    }
  }
  for (uint i = 0; i < kSize; i++) {
    EXPECT_EQ(src[i], i);
  }

  END_TEST;
}

static bool test_turbo_enable_disable() {
  BEGIN_TEST;

  // Test turbo enable/disable on an Intel Skylake-U processor w/ Turbo.
  {
    FakeMsrAccess fake_msrs = {};
    fake_msrs.msrs_[0] = {X86_MSR_IA32_MISC_ENABLE, 0x850089};
    x86_intel_cpu_set_turbo(&cpu_id::kCpuIdCorei5_6260U, &fake_msrs, Turbostate::DISABLED);
    EXPECT_TRUE(fake_msrs.read_msr(X86_MSR_IA32_MISC_ENABLE) &
                X86_MSR_IA32_MISC_ENABLE_TURBO_DISABLE);
    x86_intel_cpu_set_turbo(&cpu_id::kCpuIdCorei5_6260U, &fake_msrs, Turbostate::ENABLED);
    EXPECT_FALSE(fake_msrs.read_msr(X86_MSR_IA32_MISC_ENABLE) &
                 X86_MSR_IA32_MISC_ENABLE_TURBO_DISABLE);
  }

  // Test turbo enable/disable on a processor without turbo
  {
    FakeMsrAccess fake_msrs = {};  // Access to unimplemented MSRs will crash.
    x86_intel_cpu_set_turbo(&cpu_id::kCpuIdCeleronJ3455, &fake_msrs, Turbostate::ENABLED);
  }

  // Test an AMD Threadripper
  {
    FakeMsrAccess fake_msrs = {};
    fake_msrs.msrs_[0] = {X86_MSR_K7_HWCR, 0xb000010};
    x86_amd_cpu_set_turbo(&cpu_id::kCpuIdThreadRipper2970wx, &fake_msrs, Turbostate::DISABLED);
    EXPECT_TRUE(fake_msrs.read_msr(X86_MSR_K7_HWCR) & X86_MSR_K7_HWCR_CPB_DISABLE);
    x86_amd_cpu_set_turbo(&cpu_id::kCpuIdThreadRipper2970wx, &fake_msrs, Turbostate::ENABLED);
    EXPECT_FALSE(fake_msrs.read_msr(X86_MSR_K7_HWCR) & X86_MSR_K7_HWCR_CPB_DISABLE);
  }

  END_TEST;
}

}  // anonymous namespace

UNITTEST_START_TESTCASE(x64_platform_tests)
UNITTEST("basic test of read/write MSR variants", test_x64_msrs)
UNITTEST("test k cpu rdmsr commands", test_x64_msrs_k_commands)
UNITTEST("test k hwp commands", test_x64_hwp_k_commands)
UNITTEST("test uarch_config is correctly selected", test_x64_cpu_uarch_config_selection)
UNITTEST("test enumeration of x64 Meltdown vulnerability", test_x64_meltdown_enumeration)
UNITTEST("test enumeration of x64 L1TF vulnerability", test_x64_l1tf_enumeration)
UNITTEST("test enumeration of x64 MDS vulnerability", test_x64_mds_enumeration)
UNITTEST("test enumeration of x64 SWAPGS vulnerability", test_x64_swapgs_bug_enumeration)
UNITTEST("test enumeration of x64 SSB vulnerability", test_x64_ssb_enumeration)
UNITTEST("test mitigation of x64 SSB vulnerability", test_x64_ssb_disable)
UNITTEST("test enumeration of x64 Spectre V2 flags", test_x64_spectre_v2_enumeration)
UNITTEST("test Intel x86 microcode patch loader match and load logic", test_x64_intel_ucode_loader)
UNITTEST("test Intel x86 microcode patch loader mechanism", test_x64_intel_ucode_patch_loader)
UNITTEST("test pkg power limit change", test_x64_power_limits)
UNITTEST("test amd_platform_init", test_amd_platform_init)
UNITTEST("test spectre v2 mitigation building blocks", test_spectre_v2_mitigations)
UNITTEST("test mds mitigation building blocks", test_mds_mitigations)
UNITTEST("test usercopy variants", test_usercopy_variants)
UNITTEST("test enable/disable turbo/core performance boost", test_turbo_enable_disable)
UNITTEST_END_TESTCASE(x64_platform_tests, "x64_platform_tests", "")
