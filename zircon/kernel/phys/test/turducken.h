// Copyright 2021 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef ZIRCON_KERNEL_PHYS_TEST_TURDUCKEN_H_
#define ZIRCON_KERNEL_PHYS_TEST_TURDUCKEN_H_

// A Turducken ZBI test takes a ZBI that's been marinated (compressed) until it
// quacks like a duck, shoves some turkey (a test ZBI executable) in front of
// that, and maybe some other stuffing (ZBI items) along with it; and then
// bakes the whole thing into a ZBI.  When the resulting turkey boots, it
// decompresses the embedded ZBI, does some kind of monkey business to spice
// things up (the meat of the test), and then serves the next course by loading
// the ZBI, perhaps in some strange location or with some additions.  The duck
// layer of the test (having shaken the marinade off its back) then does
// whatever it does to verify that it got loaded correctly.  Finally either it
// reports success by having a TestMain function that returns 0, or else it
// serves the next course: either another flavor (might taste like chicken), or
// a cannibalistic duck clone.  Any layer of the test can examine and modify
// the command line being passed along in place, or add new command-line items,
// to communicate to the next inner layer it hands off to.  In this way a
// single self-referential test can iterate through finite permutations of its
// behavior (ducks all the way down).

#include <lib/arch/ticks.h>
#include <lib/zbitl/image.h>
#include <zircon/boot/image.h>

#include <ktl/byte.h>
#include <ktl/optional.h>
#include <ktl/span.h>
#include <ktl/string_view.h>
#include <phys/allocation.h>

// These methods are defined in turducken.cc along with a common TestMain.
class TurduckenTestBase {
 public:
  using Zbi = zbitl::Image<ktl::span<ktl::byte>>;

  TurduckenTestBase() = delete;

  TurduckenTestBase(void* zbi, arch::EarlyTicks ticks,
                    uint32_t embedded_type = ZBI_TYPE_STORAGE_KERNEL);

  arch::EarlyTicks entry_ticks() const { return entry_ticks_; }

  // This just returns Symbolize::kProgramName_, but is shorter to type.
  [[gnu::const]] static const char* test_name();

  // The ZBI item type TestMain looks for and passes to TurduckenTest::Main.
  // Usually this is ZBI_TYPE_STORAGE_KERNEL.
  uint32_t embedded_type() const { return embedded_type_; }

  // Get the original data ZBI the test booted with.
  // This is what the options query and mutation functions below use.
  Zbi& boot_zbi() { return boot_zbi_; }

  // Get the embedded bootable ZBI with amendments added by Load() or after.
  Zbi loaded_zbi() { return Zbi(loaded_.data()); }

  // Return true if the exact word appears in the kernel command line.
  bool Option(ktl::string_view exact_word);

  // Remove (write over) any matches for Option(exact_word).
  void RemoveOption(ktl::string_view exact_word);

  // If a word appears in the kernel command line starting with the prefix,
  // return the suffix after that (possibly empty).  Returns the first match.
  ktl::optional<ktl::string_view> OptionWithPrefix(ktl::string_view prefix);

  // Return the first kernel command line word, if any that starts with the
  // prefix.  The returned span is empty if no matches are found.  Otherwise it
  // can be modified in place.
  ktl::span<char> ModifyOption(ktl::string_view prefix);

  // Unpack the embedded ZBI in the kernel_item (ZBI_TYPE_STORAGE_KERNEL).
  // Then append [first, last) to it, with extra_data_space capacity to spare.
  // Returns the new Zbi, whose storage is owned by the TurduckenTest object.
  void Load(Zbi::iterator kernel_item, Zbi::iterator first, Zbi::iterator last,
            uint32_t extra_data_space = 0);

  // Boot the ZBI set up by Load() and possibly modified thereafter.
  [[noreturn]] void Boot();

 private:
  arch::EarlyTicks entry_ticks_;
  Zbi boot_zbi_;
  Allocation loaded_;
  uint32_t embedded_type_;
};

// The TestMain in the library calls TurduckenTest::Main.
class TurduckenTest : public TurduckenTestBase {
 public:
  using TurduckenTestBase::TurduckenTestBase;

  // This method implementation actually has to be defined by each test.
  int Main(Zbi::iterator kernel_item);
};

#endif  // ZIRCON_KERNEL_PHYS_TEST_TURDUCKEN_H_
