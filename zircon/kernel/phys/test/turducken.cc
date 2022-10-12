// Copyright 2021 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include "turducken.h"

#include <ctype.h>
#include <lib/boot-options/word-view.h>
#include <lib/memalloc/range.h>
#include <lib/zbitl/error-stdio.h>
#include <stdio.h>
#include <zircon/assert.h>
#include <zircon/boot/image.h>

#include <cstdint>

#include <ktl/algorithm.h>
#include <ktl/optional.h>
#include <ktl/span.h>
#include <ktl/string_view.h>
#include <phys/symbolize.h>
#include <phys/trampoline-boot.h>
#include <phys/zbitl-allocation.h>

#include "ktl/iterator.h"
#include "test-main.h"
#include "zircon/compiler.h"

#include <ktl/enforce.h>

namespace {

// Whether after the test routine returns, should try and boot the
// next kernel item.
// Optional, default: false.
constexpr ktl::string_view kBootNextOpt = "turducken.boot-next=";

ktl::span<ktl::byte> GetZbi(void* zbi) {
  auto zbi_header = static_cast<const zbi_header_t*>(zbi);
  auto zbi_bytes = zbitl::StorageFromRawHeader(zbi_header);
  return {static_cast<ktl::byte*>(zbi), zbi_bytes.size()};
}

constexpr auto ForEachWord = [](TurduckenTestBase::Zbi& zbi, auto&& callback) {
  for (auto [header, payload] : zbi) {
    if (header->type == ZBI_TYPE_CMDLINE) {
      ktl::string_view line{
          reinterpret_cast<const char*>(payload.data()),
          payload.size(),
      };
      for (ktl::string_view word : WordView(line)) {
        ktl::span mutable_word{const_cast<char*>(word.data()), word.size()};
        if (!callback(word, mutable_word)) {
          break;
        }
      }
    }
  }
  auto result = zbi.take_error();
  ZX_ASSERT_MSG(result.is_ok(), "%.*s", static_cast<int>(result.error_value().zbi_error.size()),
                result.error_value().zbi_error.data());
};

}  // namespace

TurduckenTestBase::TurduckenTestBase(void* zbi, arch::EarlyTicks ticks, uint32_t embedded_type)
    : entry_ticks_(ticks), boot_zbi_(GetZbi(zbi)), embedded_type_(embedded_type) {}

const char* TurduckenTestBase::test_name() { return ProgramName(); }

bool TurduckenTestBase::Option(ktl::string_view exact_word) {
  bool result = false;
  auto match_word = [exact_word, &result](ktl::string_view word,
                                          ktl::span<char> mutable_word) -> bool {
    if (word == exact_word) {
      result = true;
      return false;
    }
    return true;
  };
  ForEachWord(boot_zbi(), match_word);
  return result;
}

void TurduckenTestBase::RemoveOption(ktl::string_view exact_word) {
  auto match_word = [exact_word](ktl::string_view word, ktl::span<char> mutable_word) -> bool {
    if (word == exact_word) {
      ktl::fill(mutable_word.begin(), mutable_word.end(), ' ');
    }
    return true;
  };
  ForEachWord(boot_zbi(), match_word);
}

ktl::optional<ktl::string_view> TurduckenTestBase::OptionWithPrefix(ktl::string_view prefix) {
  ktl::optional<ktl::string_view> result;
  auto match_word = [prefix, &result](ktl::string_view word, ktl::span<char> mutable_word) -> bool {
    if (ktl::starts_with(word, prefix)) {
      result = word.substr(prefix.size());
      return false;
    }
    return true;
  };
  ForEachWord(boot_zbi(), match_word);
  return result;
}

ktl::span<char> TurduckenTestBase::ModifyOption(ktl::string_view prefix) {
  ktl::span<char> result;
  auto match_word = [prefix, &result](ktl::string_view word, ktl::span<char> mutable_word) -> bool {
    if (ktl::starts_with(word, prefix)) {
      result = mutable_word;
      return false;
    }
    return true;
  };
  ForEachWord(boot_zbi(), match_word);
  return result;
}

void TurduckenTestBase::Load(TurduckenTestBase::Zbi::iterator kernel_item,
                             TurduckenTestBase::Zbi::iterator first,
                             TurduckenTestBase::Zbi::iterator last, uint32_t extra_data_space) {
  const size_t last_offset =
      last == boot_zbi().end() ? boot_zbi().size_bytes() : last.item_offset();
  const size_t rest_size_bytes = last_offset - first.item_offset();
  printf("%s: tail of ZBI items %zu bytes to copy\n", test_name(), rest_size_bytes);

  const auto length = zbitl::UncompressedLength(*kernel_item->header);
  auto load_buffer_size = BootZbi::SuggestedAllocation(length);
  load_buffer_size.size += rest_size_bytes + extra_data_space;

  fbl::AllocChecker ac;
  loaded_ = Allocation::New(ac, memalloc::Type::kZbiTestPayload, load_buffer_size.size,
                            load_buffer_size.alignment);
  if (!ac.check()) {
    ZX_PANIC("cannot allocate %#zx bytes aligned to %#zx\n", load_buffer_size.size,
             load_buffer_size.alignment);
  }

  auto copy_result = boot_zbi_.CopyStorageItem(loaded_.data(), kernel_item, ZbitlScratchAllocator);
  if (copy_result.is_error()) {
    printf("%s: failed to decompress embedded ZBI: ", test_name());
    zbitl::PrintViewCopyError(copy_result.error_value());
    printf("\n");
    abort();
  }

  Zbi new_zbi = loaded_zbi();
  printf("%s: ZBI payload item of %u bytes decompressed into %zu of %zu bytes\n", test_name(),
         kernel_item->header->length, new_zbi.size_bytes(), loaded_.size_bytes());
  ZX_ASSERT(new_zbi.size_bytes() > 0);

  if (first == last) {
    printf("%s: no items to extend embedded ZBI\n", test_name());
    return;
  }

  auto extend_result = new_zbi.Extend(first, last);
  if (extend_result.is_error()) {
    printf("%s: failed to extend embedded ZBI: ", test_name());
    zbitl::PrintViewCopyError(extend_result.error_value());
    printf("\n");
    abort();
  } else {
    printf("%s: extended embedded ZBI with %zu bytes of incoming ZBI items\n", test_name(),
           rest_size_bytes);
  }
}

void TurduckenTestBase::Boot() {
  TrampolineBoot boot;

  auto result = boot.Init(BootZbi::InputZbi(ktl::as_bytes(loaded_.data())));
  if (result.is_error()) {
    printf("%s: cannot handle embedded ZBI: ", test_name());
    zbitl::PrintViewCopyError(result.error_value());
    printf("\n");
    abort();
  } else {
    printf("%s: BootZbi::Init OK\n", test_name());
  }

  result = boot.Load(0, kernel_load_address_, data_load_address_);

  if (result.is_error()) {
    printf("%s: cannot load embedded ZBI: ", test_name());
    zbitl::PrintViewCopyError(result.error_value());
    printf("\n");
    abort();
  } else {
    printf("%s: BootZbi::Load OK\n", test_name());
  }

  printf("%s: Loaded kernel and data; data ZBI occupies %#zx of %#zx bytes.\n", test_name(),
         boot.DataZbi().size_bytes(), boot.DataZbi().storage().size());

  boot.Log();
  boot.Boot();
}

int TestMain(void* zbi, arch::EarlyTicks entry_ticks) {
  MainSymbolize symbolize(kTestName);
  symbolize.ContextAlways();

  InitMemory(zbi);

  TurduckenTest test(zbi, entry_ticks);
  test.LogCmdLineArguments();

  // Check if we should boot the next kernel item, after
  // booting test.Main.
  auto maybe_opt = test.OptionWithPrefix(kBootNextOpt);
  bool boot_next = false;
  if (!!maybe_opt) {
    boot_next = (maybe_opt == "true");
  }

  auto kernel_item = test.boot_zbi().begin();
  while (kernel_item != test.boot_zbi().end() &&
         kernel_item->header->type != test.embedded_type()) {
    ++kernel_item;
  }

  if (const int test_result = test.Main(kernel_item); !boot_next || test_result != 0) {
    return test_result;
  }

  test.Load(ktl::next(kernel_item), ktl::next(kernel_item, 2), test.boot_zbi().end());
  test.Boot();
  __UNREACHABLE;
}

void TurduckenTestBase::LogCmdLineArguments() {
  for (auto [header, payload] : boot_zbi()) {
    if (header->type == ZBI_TYPE_CMDLINE) {
      ktl::string_view cmdline{
          reinterpret_cast<const char*>(payload.data()),
          payload.size(),
      };
      printf("%s: CMDLINE @ %p |%.*s|\n", test_name(), payload.data(),
             static_cast<int>(cmdline.size()), cmdline.data());
    }
  }
  ZX_ASSERT(boot_zbi().take_error().is_ok());
}

void TurduckenTestBase::LogBootZbiItems(Zbi::iterator match) {
  for (Zbi::iterator it = boot_zbi_.begin(); it != boot_zbi_.end(); ++it) {
    ktl::string_view type = zbitl::TypeName(it->header->type);
    printf("%s: ZBI item: %2s %.*s (%#x) length=%#x flags=%#x\n", gSymbolize->name(),
           it == match ? "=>" : "", static_cast<int>(type.size()), type.data(), it->header->type,
           it->header->length, it->header->flags);
  }
  auto result = boot_zbi_.take_error();
  if (result.is_error()) {
    printf("%s: ZBI iteration error: ", gSymbolize->name());
    zbitl::PrintViewError(result.error_value());
  }
}

ktl::optional<uint64_t> TurduckenTestBase::ParseUint(ktl::string_view value_str) {
  if (value_str.empty()) {
    return ktl::nullopt;
  }

  uint64_t dec_base = 1;
  uint64_t value = 0;
  for (auto it = value_str.rbegin(); it != value_str.rend(); ++it) {
    auto ch = *it;
    if (isdigit(ch) == 0) {
      return ktl::nullopt;
    }
    unsigned val = ch - '0';
    value += dec_base * val;
    dec_base *= 10;
  }

  return value;
}
