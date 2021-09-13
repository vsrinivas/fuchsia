// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LIB_ELFLDLTL_INCLUDE_LIB_ELFLDLTL_FUZZER_H_
#define SRC_LIB_ELFLDLTL_INCLUDE_LIB_ELFLDLTL_FUZZER_H_

// This provides some helpers for writing fuzzers for ELF data using
// -fsanitize=fuzzer (see https://llvm.org/docs/LibFuzzer.html and
// https://github.com/google/fuzzing/blob/HEAD/docs/split-inputs.md).

#include <lib/elfldltl/layout.h>
#include <lib/stdcompat/span.h>

#include <array>
#include <memory>
#include <tuple>
#include <utility>

#include <fuzzer/FuzzedDataProvider.h>

namespace elfldltl {

// Randomly delegate to either of the two instantiations of Fuzzer, which are
// default-constructible classes of objects callable with FuzzedDataProvider&.
template <template <elfldltl::ElfData Data> class Fuzzer>
struct ElfDataFuzzer {
  int operator()(FuzzedDataProvider& provider) const {
    bool little = provider.ConsumeBool();
    return little ? Fuzzer<elfldltl::ElfData::k2Lsb>{}(provider)
                  : Fuzzer<elfldltl::ElfData::k2Msb>{}(provider);
  }
};

// Randomly delegate to any of the four instantiations of Fuzzer, which are
// default-constructible classes of objects callable with FuzzedDataProvider&.
template <template <class Elf> class Fuzzer>
struct ElfFuzzer {
  template <elfldltl::ElfClass ByClass>
  struct FuzzerByClass {
    template <elfldltl::ElfData ByData>
    using FuzzerByData = Fuzzer<elfldltl::Elf<ByClass, ByData>>;

    using Select = ElfDataFuzzer<FuzzerByData>;
  };

  int operator()(FuzzedDataProvider& provider) const {
    using Fuzzer64 = typename FuzzerByClass<elfldltl::ElfClass::k64>::Select;
    using Fuzzer32 = typename FuzzerByClass<elfldltl::ElfClass::k32>::Select;
    bool is64 = provider.ConsumeBool();
    return is64 ? Fuzzer64{}(provider) : Fuzzer32{}(provider);
  }
};

// This generates a tuple of span<const T>... fuzzer input blobs.
// Each blob is guaranteed to be aligned to Align bytes.
template <size_t Align, typename... T>
class FuzzerInput {
 public:
  using Inputs = std::tuple<cpp20::span<const T>...>;
  using InputBytes = std::array<cpp20::span<const std::byte>, sizeof...(T)>;

  explicit FuzzerInput(FuzzedDataProvider& provider) : FuzzerInput(provider, kSequence) {}

  // Return the tuple [span_T1, span_T2, ...] for each T.
  Inputs inputs() const { return inputs(kSequence); }

  // Return the array of span<byte> [span_1, span_2, ...].
  InputBytes as_bytes() const {
    constexpr auto get_bytes = [](auto&&... input) {
      return InputBytes{cpp20::as_bytes(input)...};
    };
    return std::apply(get_bytes, inputs());
  }

 private:
  using InputStorage = std::array<std::vector<std::byte>, sizeof...(T)>;

  static constexpr auto kSequence = std::make_index_sequence<sizeof...(T)>();

  template <size_t... I>
  explicit FuzzerInput(FuzzedDataProvider& provider, std::index_sequence<I...> seq)
      : bytes_{MakeBytes<I>(provider)...}, inputs_(std::make_tuple(MakeInput<I>(bytes_[I])...)) {}

  template <size_t I>
  std::vector<std::byte> MakeBytes(FuzzedDataProvider& provider) {
    if constexpr (I < sizeof...(T) - 1) {
      // Consume a random number of bytes for this input blob.
      size_t size_bytes = provider.ConsumeIntegralInRange<size_t>(0, provider.remaining_bytes());
      return provider.ConsumeBytes<std::byte>(size_bytes);
    } else {
      // The last input blob consumes the remaining bytes.
      return provider.ConsumeRemainingBytes<std::byte>();
    }
  }

  // Take an arbitrary byte vector and produce a span<T> for T[I].
  template <size_t I>
  auto MakeInput(std::vector<std::byte>& bytes) {
    using Input = std::tuple_element_t<I, Inputs>;
    using element_type = typename Input::element_type;
    static_assert(Align >= alignof(element_type));

    // Adjust the data pointer to skip any unaligned prefix.
    void* ptr = const_cast<void*>(static_cast<const void*>(bytes.data()));
    size_t space = bytes.size();
    for (size_t size = space; size > 0; --size) {
      void* aligned = std::align(Align, size, ptr, space);
      if (aligned) {
        return Input{static_cast<typename Input::const_pointer>(aligned),
                     size / sizeof(element_type)};
      }
    }

    return Input{};
  }

  template <size_t... I>
  Inputs inputs(std::index_sequence<I...> seq) const {
    return {{std::get<I>(inputs_).data(), std::get<I>(inputs_).size()}...};
  }

  InputStorage bytes_;
  Inputs inputs_;
};

}  // namespace elfldltl

#endif  // SRC_LIB_ELFLDLTL_INCLUDE_LIB_ELFLDLTL_FUZZER_H_
