// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LIB_SYMBOLIZER_MARKUP_INCLUDE_LIB_SYMBOLIZER_MARKUP_WRITER_H_
#define SRC_LIB_SYMBOLIZER_MARKUP_INCLUDE_LIB_SYMBOLIZER_MARKUP_WRITER_H_

#include <lib/fit/defer.h>
#include <lib/stdcompat/span.h>
#include <zircon/assert.h>

#include <climits>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <string_view>
#include <type_traits>
#include <utility>

namespace symbolizer_markup {

// A supported output color, whose value derives from the corresponding SGR
// control sequence.
enum class Color {
  kDefault = 0,
  kBlack = 30,
  kRed = 31,
  kGreen = 32,
  kYellow = 33,
  kBlue = 34,
  kMagenta = 35,
  kCyan = 36,
  kWhite = 37,
};

// Permissions attached to a region of memory.
struct MemoryPermissions {
  bool read = false;
  bool write = false;
  bool execute = false;
};

// Writer emits symbolizer markup. Writing is abstracted by way of Sink, which
// is any type that is callable with a std::string_view argument.
//
// Each call represents a single markup element.
//
// Example usage:
// ```
//  symbolizer_markup::Writer writer(std::ref(sink));
//
//  // Color will persist until `red` goes out of scope, at which point it will
//  // return to the default.
//  auto red = writer.ChangeColor(symbolizer_markup::Color::kRed);
//
//  // Apart from ChangeColor(), calls can be chained.
//  writer.Reset()
//      .Newline()
//      .Symbol("foobar")
//      .Newline()
//      .Data("baz")
//      .Newline();
//
// ```
template <typename Sink>
class Writer {
 public:
  static_assert(std::is_invocable_v<Sink, std::string_view>);

  explicit Writer(Sink&& sink) : sink_(std::forward<Sink>(sink)) {}

  //
  // Colorization.
  //
  // https://fuchsia.dev/fuchsia-src/reference/kernel/symbolizer_markup?hl=en#colorization
  //

  // Changes the output color, optionally boldened. Once the return value goes
  // out of scope, the color is changed back to the default. Among subsequent
  // calls, 'last wins'.
  auto ChangeColor(Color color, bool bold = false) {
    using namespace std::string_view_literals;

    Literal("\033["sv).template DecimalDigits(static_cast<unsigned int>(color)).Literal('m');
    if (bold) {
      Literal("\033[1m"sv);
    }

    return fit::defer([this]() { Literal("\033[0m"sv); });
  }

  //
  // Presentation elements.
  //
  // https://fuchsia.dev/fuchsia-src/reference/kernel/symbolizer_markup?hl=en#presentation_elements
  //

  // Emits the markup for a symbol or type, given its linkage name.
  //
  // {{{symbol:$name}}}
  Writer& Symbol(std::string_view name) { return BeginElement(kSymbol).Field(name).EndElement(); }

  // Emits the markup for the memory address of a code location.
  //
  // {{{pc:$addr}}}
  Writer& Code(uintptr_t pc) { return BeginElement(kPc).HexField(pc).EndElement(); }

  // Emits the markup for the memory address of a data location.
  //
  // {{{data:$addr}}}
  Writer& Data(uintptr_t addr) { return BeginElement(kData).HexField(addr).EndElement(); }

  // Emits the markup for a backtrace frame off of the callstack.
  //
  // {{{bt:$frame:$pc:ra}}}
  Writer& ReturnAddressFrame(unsigned int frame, uintptr_t pc) {
    return BeginElement(kBt).DecimalField(frame).HexField(pc).Field(kRa).EndElement();
  }

  // Emits the markup for a backtrace frame leading into an interrupt.
  //
  // {{{bt:$frame:$pc:pc}}}
  Writer& ExactPcFrame(unsigned int frame, uintptr_t pc) {
    return BeginElement(kBt).DecimalField(frame).HexField(pc).Field(kPc).EndElement();
  }

  // TODO(fxbug.dev/91214): Support the "hexdict" field.

  //
  // Trigger elements.
  //
  // https://fuchsia.dev/fuchsia-src/reference/kernel/symbolizer_markup?hl=en#trigger_elements
  //

  // Emits the markup for a dumpfile, given its type and name.
  //
  // {{{dumpfile:$type:$name}}}
  Writer& Dumpfile(std::string_view type, std::string_view name) {
    return BeginElement(kDumpfile).Field(type).Field(name).EndElement();
  }

  //
  // Contextual elements.
  //
  // https://fuchsia.dev/fuchsia-src/reference/kernel/symbolizer_markup?hl=en#contextual_elements
  //

  // Emits the markup to reset the context.
  //
  // {{{reset}}}
  Writer& Reset() { return BeginElement(kReset).EndElement(); }

  // Emits the markup for a given ELF module.
  //
  // {{{module:$id:$name:elf:$build_id}}}
  Writer& ElfModule(unsigned int id, std::string_view name, cpp20::span<const std::byte> build_id) {
    return BeginElement(kModule)
        .DecimalField(id)
        .Field(name)
        .Field(kElf)
        .HexField(build_id)
        .EndElement();
  }

  // Emits the markup for the load image of a module. The given permissions
  // must admit at least one of reading, writing, or execution.
  //
  // {{mmap:$start:$size:load:$module_id:$perms:$static_start}}
  Writer& LoadImageMmap(uintptr_t start, size_t size, unsigned int module_id,
                        const MemoryPermissions& perms, uint64_t static_start) {
    ZX_ASSERT(perms.read || perms.write || perms.execute);
    char perm_str[3];
    size_t perm_size = 0;
    if (perms.read) {
      perm_str[perm_size++] = 'r';
    }
    if (perms.write) {
      perm_str[perm_size++] = 'w';
    }
    if (perms.execute) {
      perm_str[perm_size++] = 'x';
    }

    return BeginElement(kMmap)
        .HexField(start)
        .HexField(size)
        .Field(kLoad)
        .DecimalField(module_id)
        .Field({perm_str, perm_size})
        .HexField(static_start)
        .EndElement();
  }

  //
  // Helpers for writing markup fragments.
  //

  constexpr Writer& Literal(std::string_view str) {
    if (!str.empty()) {
      sink_(str);
    }
    return *this;
  }

  constexpr Writer& Literal(char c) { return Literal({&c, 1}); }

  constexpr Writer& Newline() { return Literal('\n'); }

  // Emits "$prefix: ", a conventional way of establishing the context of a
  // line of emitted markup.
  constexpr Writer& Prefix(std::string_view prefix) { return Literal(prefix).Literal(": "); }

  // Emits the decimal digits for a given unsigned integer. Leading zeroes are
  // not emitted.
  template <typename Uint, typename = std::enable_if_t<std::is_unsigned_v<Uint>>>
  constexpr Writer& DecimalDigits(Uint n) {
    return Digits<10>(n);
  }

  // Emits the hexadecimal digits for a given unsigned integer. Leading zeroes
  // are not emitted, but a leading "0x is.
  template <typename Uint, typename = std::enable_if_t<std::is_unsigned_v<Uint>>>
  constexpr Writer& HexDigits(Uint n) {
    return Literal(kHexPrefix).template Digits<16>(n);
  }

 private:
  static constexpr std::string_view kDecimalDigits = "0123456789";
  static constexpr std::string_view kHexDigits = "0123456789abcdef";

  // Defined here to limit `using namespace std::string_view_literals`
  //  everywhere.
  static constexpr std::string_view kBt = "bt";
  static constexpr std::string_view kData = "data";
  static constexpr std::string_view kDumpfile = "dumpfile";
  static constexpr std::string_view kElf = "elf";
  static constexpr std::string_view kLoad = "load";
  static constexpr std::string_view kMmap = "mmap";
  static constexpr std::string_view kModule = "module";
  static constexpr std::string_view kPc = "pc";
  static constexpr std::string_view kRa = "ra";
  static constexpr std::string_view kReset = "reset";
  static constexpr std::string_view kSymbol = "symbol";

  static constexpr std::string_view kBeginElement = "{{{";
  static constexpr std::string_view kEndElement = "}}}";
  static constexpr std::string_view kHexPrefix = "0x";

  // Emits the digits for a given unsigned integer, for a base of either 10 or
  // 16. Leading zeroes are not emitted.
  template <size_t Base, typename Uint, typename = std::enable_if_t<std::is_unsigned_v<Uint>>>
  [[gnu::always_inline]] constexpr Writer& Digits(Uint n) {
    static_assert(Base == 10 || Base == 16);

    if (n == 0) {
      return Literal('0');
    }

    constexpr size_t kMaxDigits = []() {
      if constexpr (Base == 16) {
        return sizeof(Uint) * CHAR_BIT / 4;
      } else {
        return std::numeric_limits<Uint>::digits10;
      }
    }();
    char digits[kMaxDigits] = {};

    char* p = &digits[sizeof(digits)];
    do {
      *--p = kHexDigits[n % Base];
      n /= Base;
    } while (n > 0);

    std::string_view str{p, static_cast<size_t>(&digits[sizeof(digits)] - p)};
    str.remove_prefix(str.find_first_not_of('0'));
    return Literal(str);
  }

  Writer& Separator() { return Literal(':'); }

  Writer& BeginElement(std::string_view name) { return Literal(kBeginElement).Literal(name); }

  Writer& EndElement() { return Literal(kEndElement); }

  //
  // Helpers for writing markup fields.
  //

  Writer& Field(std::string_view str) { return Separator().Literal(str); }

  Writer& DecimalField(unsigned int n) { return Separator().DecimalDigits(n); }

  template <typename Uint, typename = std::enable_if_t<std::is_unsigned_v<Uint>>>
  Writer& HexField(Uint n) {
    return Separator().HexDigits(n);
  }

  Writer& HexField(cpp20::span<const std::byte> bytes) {
    Separator();
    for (auto byte : bytes) {
      Digits<16>(*reinterpret_cast<const uint8_t*>(&byte));
    }
    return *this;
  }

  Sink sink_;
};

}  // namespace symbolizer_markup

#endif  // SRC_LIB_SYMBOLIZER_MARKUP_INCLUDE_LIB_SYMBOLIZER_MARKUP_WRITER_H_
