// Copyright 2020 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef ZIRCON_KERNEL_LIB_BOOT_OPTIONS_ENUM_H_
#define ZIRCON_KERNEL_LIB_BOOT_OPTIONS_ENUM_H_

#include <lib/boot-options/types.h>
#include <stdio.h>
#include <zircon/assert.h>

#include <optional>
#include <string_view>

// This is specialized for each Enum type with a generic lambda that calls
// either EnumParser::Case or EnumPrinter::Case once for each enum value.
template <typename T>
constexpr bool Enum = false;  // Will cause errors since it's not callable.

#if BOOT_OPTIONS_TESTONLY_OPTIONS
template <>
constexpr auto Enum<TestEnum> = [](auto&& Switch) {
  return Switch  //
      .Case("default", TestEnum::kDefault)
      .Case("value1", TestEnum::kValue1)
      .Case("value2", TestEnum::kValue2);
};
#endif

template <>
constexpr auto Enum<OomBehavior> = [](auto&& Switch) {
  return Switch  //
      .Case("reboot", OomBehavior::kReboot)
      .Case("jobkill", OomBehavior::kJobKill);
};

template <>
constexpr auto Enum<PageTableEvictionPolicy> = [](auto&& Switch) {
  return Switch  //
      .Case("always", PageTableEvictionPolicy::kAlways)
      .Case("never", PageTableEvictionPolicy::kNever)
      .Case("on_request", PageTableEvictionPolicy::kOnRequest);
};

template <>
constexpr auto Enum<EntropyTestSource> = [](auto&& Switch) {
  return Switch  //
      .Case("hw_rng", EntropyTestSource::kHwRng)
      .Case("jitterentropy", EntropyTestSource::kJitterEntropy);
};

template <>
constexpr auto Enum<GfxConsoleFont> = [](auto&& Switch) {
  return Switch  //
      .Case("9x16", GfxConsoleFont::k9x16)
      .Case("18x32", GfxConsoleFont::k18x32);
};

#if BOOT_OPTIONS_GENERATOR || defined(__x86_64__)

template <>
constexpr auto Enum<IntelHwpPolicy> = [](auto&& Switch) {
  return Switch  //
      .Case("bios-specified", IntelHwpPolicy::kBiosSpecified)
      .Case("performance", IntelHwpPolicy::kPerformance)
      .Case("balanced", IntelHwpPolicy::kBalanced)
      .Case("power-save", IntelHwpPolicy::kPowerSave)
      .Case("stable-performance", IntelHwpPolicy::kStablePerformance);
};

#endif  // BOOT_OPTIONS_GENERATOR || defined(__x86_64__)

// Helpers for enum overrides.

template <typename F, typename T>
class EnumEnumerator {
 public:
  EnumEnumerator(F call, T value) : call_(std::move(call)), value_(std::move(value)) {}

  constexpr EnumEnumerator& Case(std::string_view name, T value) {
    call_(name);
    return *this;
  }

 private:
  F call_;
  T value_;
};

template <typename T>
class EnumParser {
 public:
  EnumParser(std::string_view name, T* result) : name_(name), result_(result) {}
  constexpr EnumParser(const EnumParser&) noexcept = delete;
  constexpr EnumParser(EnumParser&& other) noexcept {
    result_ = other.result_;
    name_ = other.name_;
    other.result_ = nullptr;
  }
  ~EnumParser() {
    ZX_DEBUG_ASSERT_MSG(result_ == nullptr, "Failed to call EnumParser::Check() value: %.*s",
                        static_cast<int>(name_.size()), name_.data());
  }

  bool Check() {
    T* actual = result_;
    result_ = nullptr;
    if (actual) {
      printf("WARN: Ignored unknown value '%.*s' for multiple-choice option %p\n",
             static_cast<int>(name_.size()), name_.data(), this);
      printf("WARN: Valid choices are:");
      Enum<T>(EnumEnumerator{[](std::string_view name) {
                               printf(" %.*s", static_cast<int>(name.size()), name.data());
                             },
                             *actual});
      printf("\n");
    }
    return actual == nullptr;
  }

  constexpr EnumParser&& Case(std::string_view name, T value) {
    if (result_ && name == name_) {
      *result_ = value;
      result_ = nullptr;
    }
    return std::move(*this);
  }

 private:
  std::string_view name_;
  T* result_ = nullptr;
};

template <typename T>
class EnumPrinter {
 public:
  EnumPrinter(T value, FILE* out) : value_(std::move(value)), out_(out) {}

  EnumPrinter& Case(std::string_view name, T value) {
    if (value_ && *value_ == value) {
      value_.reset();
      fprintf(out_, "%.*s", static_cast<int>(name.size()), name.data());
    }
    return *this;
  }

  ~EnumPrinter() {
    if (value_) {
      fprintf(out_, "<unknown.enum.value.%#lx>", static_cast<unsigned long int>(*value_));
    }
  }

 private:
  std::optional<T> value_;
  FILE* out_ = nullptr;
};

// Deduction guides.

template <typename F, typename T>
EnumEnumerator(F, T) -> EnumEnumerator<F, T>;

template <typename T>
EnumParser(std::string_view, T*) -> EnumParser<T>;

template <typename T>
EnumPrinter(const T&, FILE*) -> EnumPrinter<T>;

#endif  // ZIRCON_KERNEL_LIB_BOOT_OPTIONS_ENUM_H_
