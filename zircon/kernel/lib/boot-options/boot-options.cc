// Copyright 2020 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <ctype.h>
#include <inttypes.h>
#include <lib/boot-options/boot-options.h>
#include <lib/boot-options/word-view.h>
#include <zircon/compiler.h>

namespace {
#include "enum.h"
}

BootOptions* gBootOptions = nullptr;

using namespace std::string_view_literals;

// This avoids libc++ functions the kernel can't use, and avoids strtoul so as
// not to require NUL termination.
//
// TODO(fxbug.dev/62052): Reconsider the overflow policy below.
std::optional<int64_t> BootOptions::ParseInt(std::string_view value) {
  int64_t neg = 1;
  if (value.substr(0, 1) == "-") {
    neg = -1;
    value.remove_prefix(1);
  } else if (value.substr(0, 1) == "+") {
    value.remove_prefix(1);
  }

  std::optional<int64_t> result;
  auto from_chars = [&](std::string_view prefix, int base, bool trim = false) {
    if (value.substr(0, prefix.size()) == prefix) {
      if (trim) {
        value.remove_prefix(prefix.size());
      }
      if (value.empty()) {
        return false;
      }
      int64_t result_value = 0;
      for (char c : value) {
        mul_overflow(result_value, base, &result_value);
        switch (c) {
          case '0' ... '9':
            if (c - '0' >= base) {
              return false;
            }
            add_overflow(result_value, c - '0', &result_value);
            break;
          case 'a' ... 'f':
            if (base != 16) {
              return false;
            }
            add_overflow(result_value, c - 'a' + 10, &result_value);
            break;
          default:
            return false;
        }
      }
      result = result_value * neg;
      return true;
    }
    return false;
  };

  from_chars("0x", 16, true) || from_chars("0", 8) || from_chars("", 10);
  return result;
}

namespace {

constexpr char kComplainPrefix[] = "kernel";

// This names an arbitrary index for each member.  This explicit indirection
// avoids having a constexpr table of string_view constants, which doesn't fly
// for pure PIC.
enum class Index {
#define DEFINE_OPTION(name, type, member, init, doc) member,
#include <lib/boot-options/options.inc>
#undef DEFINE_OPTION
};

// Map Index::member to the name for BootOptions::member.  The compiler may
// optimize this back into a table lookup with an array of string_view
// constants, but that optimization is disabled when pure PIC is required.
constexpr std::string_view OptionName(Index idx) {
  switch (idx) {
#define DEFINE_OPTION(name, type, member, init, doc) \
  case Index::member:                                \
    return name##sv;
#include <lib/boot-options/options.inc>
#undef DEFINE_OPTION
  }
  return {};
}

// This overload lets the generic lambda below work for both cases.
constexpr std::string_view OptionName(std::string_view name) { return name; }

// Compare option names, using either string_view or Index.
constexpr auto OptionLessThan = [](auto&& a, auto&& b) { return OptionName(a) < OptionName(b); };

constexpr auto CheckSortedNames = [](const auto& names) {
  return std::is_sorted(names.begin(), names.end(), OptionLessThan);
};

#if _LIBCPP_STD_VER > 17
#define CONSTEXPR_STD_SORT constexpr
#else
#define CONSTEXPR_STD_SORT const
#endif

// kSortedNames lists Index values in ascending lexicographic order of name.
CONSTEXPR_STD_SORT auto kSortedNames = []() {
  std::array names{
#define DEFINE_OPTION(name, type, member, init, doc) Index::member,
#include <lib/boot-options/options.inc>
#undef DEFINE_OPTION
  };
  // TODO(mcgrathr): C++20 has constexpr std::sort but libc++ doesn't implement
  // it yet.  Should be:
  // std::sort(names.begin(), names.end(), OptionLessThan);
  for ([[maybe_unused]] auto& i : names) {
    for (auto& j : names) {
      if (&j < &names[names.size() - 1] && OptionLessThan((&j)[1], j)) {
        std::swap(j, (&j)[1]);
      }
    }
  }
#if _LIBCPP_STD_VER == 17
  ZX_ASSERT(CheckSortedNames(names));
#endif
  return names;
}();
#if _LIBCPP_STD_VER > 17
static_assert(CheckSortedNames(kSortedNames));
#endif

// Map option name to Index using binary search.
std::optional<Index> FindOption(std::string_view name) {
  if (auto it = std::lower_bound(kSortedNames.begin(), kSortedNames.end(), name, OptionLessThan);
      it != kSortedNames.end() && name == OptionName(*it)) {
    return *it;
  }
  return std::nullopt;
}

// The length of the longest option name.
CONSTEXPR_STD_SORT size_t kMaxNameLen =
    OptionName(*std::max_element(kSortedNames.begin(), kSortedNames.end(), [](Index a, Index b) {
      return OptionName(a).size() < OptionName(b).size();
    })).size();

template <Index member, typename T>
void ShowOption(const T& value, bool defaults, FILE* out);

#define DEFINE_OPTION(name, type, member, init, doc)                                  \
  template <>                                                                         \
  void ShowOption<Index::member, type>(const type& value, bool defaults, FILE* out) { \
    const type default_value init;                                                    \
    BootOptions::Print(OptionName(Index::member), value, out);                        \
    if (defaults) {                                                                   \
      fprintf(out, " (default ");                                                     \
      BootOptions::Print(OptionName(Index::member), default_value, out);              \
      fprintf(out, ")\n");                                                            \
    } else {                                                                          \
      fprintf(out, "\n");                                                             \
    }                                                                                 \
  }
#include <lib/boot-options/options.inc>
#undef DEFINE_OPTION

}  // namespace

BootOptions::WordResult BootOptions::ParseWord(std::string_view word) {
  std::string_view key, value;
  if (auto eq = word.find('='); eq == std::string_view::npos) {
    // No '=' means the whole word is the key, with an empty value.
    key = word;
  } else {
    key = word.substr(0, eq);
    value = word.substr(eq + 1);
  }

  // Match the key against the known option names.
  // Note this leaves the member with its current/default value but still
  // returns true when the key was known but the value was unparsable.
  if (auto option = FindOption(key)) {
    switch (*option) {
#define DEFINE_OPTION(name, type, member, init, doc) \
  case Index::member:                                \
    Parse(value, &BootOptions::member);              \
    break;
#include <lib/boot-options/options.inc>
#undef DEFINE_OPTION
    }
    return {key, true};
  }

  return {key, false};
}

void BootOptions::SetMany(std::string_view cmdline, FILE* complain) {
  bool verbose = complain != nullptr;
  if (!complain) {
    complain = stdout;
  }
  for (auto word : WordView(cmdline)) {
    if (auto result = ParseWord(word);
        !result.known &&
        (verbose ||
         result.key.substr(0, std::string_view(kComplainPrefix).size()) == kComplainPrefix)) {
      if (result.key.size() > kMaxNameLen) {
        fprintf(complain, "NOTE: Unrecognized kernel option %zu characters long (max %zu)\n",
                result.key.size(), kMaxNameLen);
      } else {
        char name[kMaxNameLen + 1];
        name[SanitizeString(name, kMaxNameLen, result.key)] = '\0';
        fprintf(complain, "WARN: Kernel ignored unrecognized option '%s'\n", name);
      }
    }
  }
}

int BootOptions::Show(std::string_view key, bool defaults, FILE* out) {
  if (auto option = FindOption(key)) {
    switch (*option) {
#define DEFINE_OPTION(name, type, member, init, doc)        \
  case Index::member:                                       \
    ShowOption<Index::member, type>(member, defaults, out); \
    break;
#include <lib/boot-options/options.inc>
#undef DEFINE_OPTION
    }
    return 0;
  }
  return -1;
}

void BootOptions::Show(bool defaults, FILE* out) {
#define DEFINE_OPTION(name, type, member, init, doc) \
  ShowOption<Index::member, type>(member, defaults, out);
#include <lib/boot-options/options.inc>
#undef DEFINE_OPTION
}

// Helpers for BootOptions::Parse overloads below.

namespace {

template <typename T>
void ParseIntValue(std::string_view value, T& result) {
  if (auto parsed = BootOptions::ParseInt(value)) {
    result = static_cast<T>(*parsed);
  }
}

}  // namespace

// Overloads for various types.

void BootOptions::Parse(std::string_view value, bool BootOptions::*member) {
  // Any other value, even an empty value, means true.
  this->*member = value != "false" && value != "0" && value != "off";
}

void BootOptions::PrintValue(const bool& value, FILE* out) {
  fprintf(out, "%s", value ? "true" : "false");
}

void BootOptions::Parse(std::string_view value, uint64_t BootOptions::*member) {
  ParseIntValue(value, this->*member);
}

void BootOptions::PrintValue(const uint64_t& value, FILE* out) { fprintf(out, "%#" PRIx64, value); }

void BootOptions::Parse(std::string_view value, uint32_t BootOptions::*member) {
  ParseIntValue(value, this->*member);
}

void BootOptions::PrintValue(const uint32_t& value, FILE* out) { fprintf(out, "%#" PRIx32, value); }

void BootOptions::Parse(std::string_view value, SmallString BootOptions::*member) {
  SmallString& result = this->*member;
  size_t wrote = value.copy(&result[0], result.size());
  // In the event of a value of size greater or equal to SmallString's capacity,
  // truncate to keep invariant that the string is NUL-terminated.
  result[std::min(wrote, result.size() - 1)] = '\0';
}

void BootOptions::PrintValue(const SmallString& value, FILE* out) {
  ZX_ASSERT(value.back() == '\0');
  fprintf(out, "%s", &value[0]);
}

void BootOptions::Parse(std::string_view value, RedactedHex BootOptions::*member) {
  RedactedHex& result = this->*member;
  if (std::all_of(value.begin(), value.end(), isxdigit)) {
    result.len = value.copy(&result.hex[0], result.hex.size());
    Redact(value);
  }
}

void BootOptions::PrintValue(const RedactedHex& value, FILE* out) {
  if (value.len > 0) {
    fprintf(out, "<redacted.%zu.hex.chars>", value.len);
  }
}

#if BOOT_OPTIONS_TESTONLY_OPTIONS

void BootOptions::Parse(std::string_view value, TestEnum BootOptions::*member) {
  Enum<TestEnum>(EnumParser{value, &(this->*member)});
}

void BootOptions::PrintValue(const TestEnum& value, FILE* out) {
  Enum<TestEnum>(EnumPrinter{value, out});
}

void BootOptions::Parse(std::string_view value, TestStruct BootOptions::*member) {
  if (value == "test") {
    (this->*member).present = true;
  } else {
    printf("WARN: Ignored unknown value '%.*s' for test option\n", static_cast<int>(value.size()),
           value.data());
  }
}

void BootOptions::PrintValue(const TestStruct& value, FILE* out) { fprintf(out, "test"); }

#endif  // BOOT_OPTIONS_TESTONLY_OPTIONS
