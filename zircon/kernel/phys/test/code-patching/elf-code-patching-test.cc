// Copyright 2022 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <inttypes.h>
#include <lib/zbitl/error-stdio.h>
#include <lib/zbitl/view.h>
#include <stdio.h>

#include <ktl/byte.h>
#include <ktl/span.h>
#include <ktl/string_view.h>
#include <phys/elf-image.h>
#include <phys/kernel-package.h>
#include <phys/symbolize.h>

#include "../test-main.h"
#include "test.h"

#include <ktl/enforce.h>

namespace {

// The package prefix under which the test images to be loaded live.
constexpr ktl::string_view kPackage = "elf-code-patching-test-data";

constexpr ktl::string_view kAddOne = "add-one";
constexpr ktl::string_view kMultiply = "multiply_by_factor";

}  // namespace

int TestMain(void* zbi_ptr, arch::EarlyTicks) {
  MainSymbolize symbolize("elf-code-patching-test");
  InitMemory(zbi_ptr);

  KernelStorage kernelfs;
  kernelfs.Init(static_cast<zbi_header_t*>(zbi_ptr));

  KernelStorage::Bootfs bootfs;
  if (auto result = kernelfs.root().subdir(kPackage); result.is_error()) {
    zbitl::PrintBootfsError(result.error_value());
    return 1;
  } else {
    bootfs = ktl::move(result).value();
  }

  constexpr uint64_t kValue = 42;

  // Test that unpatched add-one loads and behaves as expected.
  // Note this keeps the Allocation alive after the test so its
  // pages won't be reused for the second test, since that could
  // require cache flushing operations.
  Allocation unpatched;
  printf("%s: Testing unpatched add-one...", symbolize.name());
  {
    ElfImage add_one;
    if (auto result = add_one.Init(bootfs, kAddOne, true); result.is_error()) {
      zbitl::PrintBootfsError(result.error_value());
      return 1;
    }

    add_one.AssertInterp(kAddOne, symbolize.BuildIdString());
    unpatched = add_one.Load(false);
    add_one.Relocate();

    printf("Calling %#" PRIx64 "...", add_one.entry());
    uint64_t value = add_one.Call<TestFn>(kValue);
    ZX_ASSERT_MSG(value == kValue + 1, "unpatched add-one: got %" PRIu64 " != expected %" PRIu64,
                  value, kValue + 1);
  }
  printf("OK\n");

  // Now test it with nop patching: AddOne becomes the identity function.
  Allocation patched;
  printf("%s: Testing patched add-one...", symbolize.name());
  {
    ElfImage add_one;
    if (auto result = add_one.Init(bootfs, kAddOne, true); result.is_error()) {
      zbitl::PrintBootfsError(result.error_value());
      return 1;
    }

    add_one.AssertInterp(kAddOne, symbolize.BuildIdString());
    patched = add_one.Load(false);
    add_one.Relocate();

    enum class ExpectedCase : uint32_t { kAddOne = kAddOneCaseId };

    auto patch = [](code_patching::Patcher& patcher, ExpectedCase case_id,
                    ktl::span<ktl::byte> code) -> fit::result<ElfImage::Error> {
      ZX_ASSERT_MSG(case_id == ExpectedCase::kAddOne,
                    "code-patching case ID %" PRIu32 " != expected %" PRIu32,
                    static_cast<uint32_t>(case_id), static_cast<uint32_t>(ExpectedCase::kAddOne));
      ZX_ASSERT_MSG(code.size_bytes() == PATCH_SIZE_ADD_ONE, "code patch %zu bytes != expected %d",
                    code.size_bytes(), PATCH_SIZE_ADD_ONE);
      printf("Patching [%p, %p)...", code.data(), code.data() + code.size());
      patcher.NopFill(code);
      return fit::ok();
    };
    auto result = add_one.ForEachPatch<ExpectedCase>(patch, patched);
    ZX_ASSERT(result.is_ok());

    printf("Calling %#" PRIx64 "...", add_one.entry());
    uint64_t value = add_one.Call<TestFn>(kValue);
    ZX_ASSERT_MSG(value == kValue, "nop-patched add-one: got %" PRIu64 " != expected %" PRIu64,
                  value, kValue);
  }
  printf("OK\n");

  // Now test the hermetic blob stub case.
  Allocation patched_stub2;
  printf("%s: Testing hermetic blob (alternative 1)...", symbolize.name());
  {
    ElfImage multiply;
    if (auto result = multiply.Init(bootfs, kMultiply, true); result.is_error()) {
      zbitl::PrintBootfsError(result.error_value());
      return 1;
    }

    multiply.AssertInterp(kMultiply, symbolize.BuildIdString());
    patched_stub2 = multiply.Load(false);
    multiply.Relocate();

    enum class ExpectedCase : uint32_t { kMultiply = kMultiplyByFactorCaseId };

    auto patch = [](code_patching::Patcher& patcher, ExpectedCase case_id,
                    ktl::span<ktl::byte> code) -> fit::result<ElfImage::Error> {
      ZX_ASSERT_MSG(case_id == ExpectedCase::kMultiply,
                    "code-patching case ID %" PRIu32 " != expected %" PRIu32,
                    static_cast<uint32_t>(case_id), static_cast<uint32_t>(ExpectedCase::kMultiply));
      ZX_ASSERT_MSG(code.size_bytes() == PATCH_SIZE_MULTIPLY_BY_FACTOR,
                    "code patch %zu bytes != expected %d", code.size_bytes(),
                    PATCH_SIZE_MULTIPLY_BY_FACTOR);
      printf("Patching [%p, %p)...", code.data(), code.data() + code.size());
      return patcher.PatchWithAlternative(code, "multiply_by_two");
    };
    auto result = multiply.ForEachPatch<ExpectedCase>(patch, patched_stub2);
    ZX_ASSERT_MSG(result.is_ok(), "%.*s", static_cast<int>(result.error_value().reason.size()),
                  result.error_value().reason.data());

    printf("Calling %#" PRIx64 "...", multiply.entry());
    uint64_t value = multiply.Call<TestFn>(kValue);
    ZX_ASSERT_MSG(value == kValue * 2, "multiply_by_two got %" PRIu64 " != expected %" PRIu64,
                  value, kValue * 2);
  }
  printf("OK\n");

  // Now test the hermetic blob stub case.
  Allocation patched_stub10;
  printf("%s: Testing hermetic blob (alternative 2)...", symbolize.name());
  {
    ElfImage multiply;
    if (auto result = multiply.Init(bootfs, kMultiply, true); result.is_error()) {
      zbitl::PrintBootfsError(result.error_value());
      return 1;
    }

    multiply.AssertInterp(kMultiply, symbolize.BuildIdString());
    patched_stub10 = multiply.Load(false);
    multiply.Relocate();

    enum class ExpectedCase : uint32_t { kMultiply = kMultiplyByFactorCaseId };

    auto patch = [](code_patching::Patcher& patcher, ExpectedCase case_id,
                    ktl::span<ktl::byte> code) -> fit::result<ElfImage::Error> {
      ZX_ASSERT_MSG(case_id == ExpectedCase::kMultiply,
                    "code-patching case ID %" PRIu32 " != expected %" PRIu32,
                    static_cast<uint32_t>(case_id), static_cast<uint32_t>(ExpectedCase::kMultiply));
      ZX_ASSERT_MSG(code.size_bytes() == PATCH_SIZE_MULTIPLY_BY_FACTOR,
                    "code patch %zu bytes != expected %d", code.size_bytes(),
                    PATCH_SIZE_MULTIPLY_BY_FACTOR);
      printf("Patching [%p, %p)...", code.data(), code.data() + code.size());
      return patcher.PatchWithAlternative(code, "multiply_by_ten");
    };
    auto result = multiply.ForEachPatch<ExpectedCase>(patch, patched_stub10);
    ZX_ASSERT_MSG(result.is_ok(), "%.*s", static_cast<int>(result.error_value().reason.size()),
                  result.error_value().reason.data());

    printf("Calling %#" PRIx64 "...", multiply.entry());
    uint64_t value = multiply.Call<TestFn>(kValue);
    ZX_ASSERT_MSG(value == kValue * 10, "multiply_by_ten got %" PRIu64 " != expected %" PRIu64,
                  value, kValue * 10);
  }
  printf("OK\n");

  return 0;
}
