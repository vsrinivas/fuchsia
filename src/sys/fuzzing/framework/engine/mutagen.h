// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_SYS_FUZZING_FRAMEWORK_ENGINE_MUTAGEN_H_
#define SRC_SYS_FUZZING_FRAMEWORK_ENGINE_MUTAGEN_H_

#include <stddef.h>

#include <random>

#include "src/lib/fxl/macros.h"
#include "src/sys/fuzzing/common/input.h"
#include "src/sys/fuzzing/common/options.h"
#include "src/sys/fuzzing/framework/engine/dictionary.h"

namespace fuzzing {

// Describes the types of mutation corresponding to the |Mutagen| methods below, and is used to
// record the sequence of mutations.
//
// The order here matters, as |Mutagen::Mutate| violates the abstraction a bit to get better
// performance: it uses the ordering to constrain which mutations to pick from based on the input
// size and output capacity.
//
// TODO(fxbug.dev/84365): This is currently missing a strategy to pull from the dictionary.
// TODO(fxbug.dev/85308): Add support for data-flow-guided fuzzing.
enum Mutation : uint8_t {
  // 1 < size
  kSkipSome,

  // 1 < size <= capacity
  kShuffle,
  kReplaceSome,

  // 0 < size <= capacity
  kFlip,
  kReplaceOne,
  kReplaceUnsigned,
  kReplaceNum,
  kMergeReplace,

  // 0 < size < capacity
  kInsertSome,
  kMergeInsert,

  // 0 <= size < capacity
  kInsertOne,
  kInsertRepeated,
};

// This class represents the source of mutations when fuzzing. It is heavily inspired by libFuzzer's
// MutationDispatcher, here:
//   https://github.com/llvm/llvm-project/blob/main/compiler-rt/lib/fuzzer/FuzzerMutate.cpp
//
// During fuzzing, the framework will pick an input from the corpus, and pass it to this object. It
// will then use this object to generate a sequence of mutated inputs that it can send to the
// target adapter.
class Mutagen final {
 public:
  Mutagen() = default;
  ~Mutagen() = default;

  const Dictionary& dictionary() const { return dictionary_; }

  // The sequence of mutations since the input was last set.
  const std::vector<Mutation>& mutations() const { return mutations_; }

  void set_input(const Input* input);
  void set_crossover(const Input* crossover) { crossover_ = crossover; }
  void set_dictionary(Dictionary dictionary) { dictionary_ = std::move(dictionary); }

  // Sets options.
  void Configure(const std::shared_ptr<Options>& options);

  // Makes a copy of the previously configured input, mutates it using a pseudoradomly selected
  // mutation strategy, and stores the result in |out|.
  void Mutate(Input* out);

  // All other the mutators below return true when they have successfully mutated and written |data|
  // to |out|, and false otherwise. Each makes assumptions about the given |size|, which are
  // enforced by |Mutate|, above. Callers should not call these directly except for testing. Use
  // |Mutate| instead.

  // Remove some bytes from |data| when writing to |out|. Assumes |size > 1|; |size > max_size| is
  // allowed.
  bool SkipSome(const uint8_t* data, size_t size, size_t max_size, Input* out);

  // Shuffle some subsequence of |data| when writing it to |out|. Assumes |size > 1|.
  bool Shuffle(const uint8_t* data, size_t size, Input* out);

  // Flip a bit at some location in |data| when writing it to |out|. Assumes |size != 0|.
  bool Flip(const uint8_t* data, size_t size, Input* out);

  // Replace one byte in |data| when writing it to |out|. Assumes |size != 0|.
  bool ReplaceOne(const uint8_t* data, size_t size, Input* out);

  // Find and replace an unsigned integer value in |data| when writing it to |out|. Assumes
  // |size != 0|.
  bool ReplaceUnsigned(const uint8_t* data, size_t size, Input* out);

  // Find and replace an ASCII representation of a number in |data| when writing it to |out|.
  // Assumes |size != 0|.
  bool ReplaceNum(const uint8_t* data, size_t size, Input* out);

  // Replace some subsequence of |data| with another, possibly overlapping subsequence when writing
  // it to |out|. Assumes |size != 0|.
  bool ReplaceSome(const uint8_t* data, size_t size, Input* out);

  // For each of |size1| bytes, choose from |data1| or |data2|, and write the result to |out|.
  bool MergeReplace(const uint8_t* data1, size_t size1, const uint8_t* data2, size_t size2,
                    Input* out);

  // Copy some section of |data| and insert it when writing |data| to |out|. Assumes
  // |size < max_size|.
  bool InsertSome(const uint8_t* data, size_t size, size_t max_size, Input* out);

  // Interleave segments of |data1| and |data2| and write the result to |out|, up to
  // |max_size|.
  bool MergeInsert(const uint8_t* data1, size_t size1, const uint8_t* data2, size_t size2,
                   size_t max_size, Input* out);

  // Insert one byte somewhere into |data| when writing it to |out|. Implies a "max_size" of
  // |size + 1|.
  bool InsertOne(const uint8_t* data, size_t size, Input* out);

  // Insert a sequence created by repeating a byte somewhere into |data| when writing it to |out|.
  // Assumes |size < max_size|.
  bool InsertRepeated(const uint8_t* data, size_t size, size_t max_size, Input* out);

 private:
  // Returns a random value.
  template <typename T>
  T Pick() {
    return static_cast<T>(prng_());
  }

  // Returns a random value in [min, max].
  template <typename T>
  T Pick(T min, T max) {
    return min + (Pick<T>() % (max - min + 1));
  }

  // Pick a random byte, with preference given to 0 and 255.
  uint8_t PickPreferred();

  // Pick a random byte, with preference given to special ASCII characters.
  char PickSpecial();

 private:
  std::shared_ptr<Options> options_;
  std::minstd_rand prng_;
  const Input* input_;
  const Input* crossover_;
  Dictionary dictionary_;
  std::vector<Mutation> mutations_;

  FXL_DISALLOW_COPY_ASSIGN_AND_MOVE(Mutagen);
};

template <>
bool Mutagen::Pick<bool>();
template <>
uint64_t Mutagen::Pick<uint64_t>();

}  // namespace fuzzing

#endif  // SRC_SYS_FUZZING_FRAMEWORK_ENGINE_MUTAGEN_H_
