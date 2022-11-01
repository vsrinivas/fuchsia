// Copyright 2021 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include "turducken.h"

#include <lib/zbitl/error-stdio.h>
#include <stdlib.h>
#include <zircon/assert.h>

#include <ktl/string_view.h>
#include <phys/symbolize.h>

#include <ktl/enforce.h>

// Declared in turducken.h.
const char* kTestName = "turducken-test";

// These are embedded in the original image (with "flavor=turkey").
constexpr ktl::string_view kSmokeSignal = "turducken-test.smoke";
constexpr ktl::string_view kApertif = "turducken-test.flavor=";

// These are added on the fly.
constexpr ktl::string_view kZeroQuacks = "turducken-test.quacks=0";
constexpr ktl::string_view kQuacks = kZeroQuacks.substr(0, kZeroQuacks.size() - 1);

constexpr unsigned int kQuackCount = 5;
static_assert(kQuackCount < 10);  // Fits in one digit.

int TurduckenTest::Main(Zbi::iterator kernel_item) {
  // This is embedded in the original ZBI command line and always passed on.
  ZX_ASSERT(Option(kSmokeSignal));

  auto flavor = OptionWithPrefix(kApertif);
  ZX_ASSERT(flavor.has_value());

  auto change_flavor = [this, flavor = *flavor](ktl::string_view tasty) {
    ktl::span<char> change = ModifyOption(kApertif);
    ZX_ASSERT(change.size() > kApertif.size());
    change = change.subspan(kApertif.size());
    ZX_ASSERT(change.size() == flavor.size());
    ZX_ASSERT(change.size() >= tasty.size());
    size_t n = tasty.copy(change.data(), change.size());
    ZX_ASSERT(n == tasty.size());
    change = change.subspan(n);
    memset(change.data(), ' ', change.size());
  };

  ktl::string_view extra_option;

  if (flavor == "turkey") {
    printf("%s: Slicing through the turkey into the duck!\n", test_name());
    change_flavor("ducky");
    extra_option = kZeroQuacks;
  } else if (flavor == "ducky") {
    ktl::span<char> quacks = ModifyOption(kQuacks);
    ZX_ASSERT(quacks.size() > kQuacks.size());
    quacks = quacks.subspan(kQuacks.size());
    ZX_ASSERT(!quacks.empty());
    ZX_ASSERT(quacks.front() >= '0');
    ZX_ASSERT(quacks.front() <= '9');
    unsigned int count = quacks.front() - '0';
    printf("%s: Ducky quacks %u of %u times\n", test_name(), count, kQuackCount);
    if (count == kQuackCount) {
      change_flavor("goose");
    } else {
      ZX_ASSERT(count < kQuackCount);
      ++quacks.front();
    }
  } else if (flavor == "goose") {
    printf("%s: It wasn't ducks all the way down after all!\n", test_name());
    return 0;
  } else {
    ZX_PANIC("Don't like the taste of %.*s!", static_cast<int>(flavor->size()), flavor->data());
  }

  const uint32_t extra_space = static_cast<uint32_t>(
      sizeof(zbi_header_t) + ZBI_ALIGN(static_cast<uint32_t>(extra_option.size())));
  printf("%s: %u extra space for %zu chars of new option text\n", test_name(), extra_space,
         extra_option.size());

  Load(kernel_item, kernel_item, boot_zbi().end(), extra_space);

  if (!extra_option.empty()) {
    ktl::span chars(extra_option.data(), extra_option.size());
    auto payload = ktl::as_bytes(chars);
    auto result = loaded_zbi().Append({.type = ZBI_TYPE_CMDLINE}, payload);
    if (result.is_error()) {
      printf("%s: cannot add new ZBI_TYPE_CMDLINE payload of %zu bytes: ", test_name(),
             payload.size_bytes());
      zbitl::PrintViewError(result.error_value());
      printf("\n");
      abort();
    }
  }

  Boot();
}
