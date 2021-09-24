// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/sys/fuzzing/framework/engine/mutagen.h"

#include <lib/syslog/cpp/macros.h>

namespace fuzzing {
namespace {

// The minimum and maximum length of a repeated byte sequence to be inserted. See
// |Mutagen::InsertRepeated|.
constexpr size_t kMinRepeat = 3;
constexpr size_t kMaxRepeat = 128;

// Swap bytes.
template <typename T>
T Bswap(T value);

template <>
uint8_t Bswap(uint8_t value) {
  return value;
}

template <>
uint16_t Bswap(uint16_t value) {
  return __builtin_bswap16(value);
}

template <>
uint32_t Bswap(uint32_t value) {
  return __builtin_bswap32(value);
}

template <>
uint64_t Bswap(uint64_t value) {
  return __builtin_bswap64(value);
}

// Write the remainder of |data| after a given |offset|.
void WriteAfter(size_t offset, const uint8_t* data, size_t size, Input* out) {
  if (offset < size) {
    out->Write(&data[offset], size - offset);
  }
}

}  // namespace

void Mutagen::set_input(const Input* input) {
  input_ = input;
  mutations_.clear();
}

void Mutagen::Configure(const std::shared_ptr<Options>& options) {
  options_ = options;
  prng_.seed(options_->seed());
  dictionary_.Configure(options_);
}

void Mutagen::Mutate(Input* out) {
  out->Clear();
  FX_DCHECK(options_);
  auto max_size = std::min(out->capacity(), options_->max_input_size());
  if (!max_size) {
    return;  // Empty input is the only valid possibility.
  }
  auto* data = input_->data();
  auto size = input_->size();
  // See the note on |Mutation|. This relies on the ordering of the enum to constrain which
  // mutations can be selected for the current input size and output capacity.
  uint8_t mutation, min, max;
  if (1 < size) {
    min = kSkipSome;
  } else if (0 < size) {
    min = kFlip;
  } else {
    min = kInsertOne;
  }
  if (size > max_size) {
    max = kSkipSome;
  } else if (size == max_size) {
    max = kMergeReplace;
  } else if (size + kMinRepeat > max_size) {
    max = kInsertOne;
  } else {
    max = kInsertRepeated;
  }
  // Mutation may fail in some cases, e.g. |ReplaceNum| with no ASCII digits. Try several times
  // before returning a default input.
  bool mutated = false;
  constexpr size_t kNumAttempts = 100;
  for (size_t i = 0; !mutated && i < kNumAttempts; ++i) {
    mutation = Pick<uint8_t>(min, max);
    switch (mutation) {
      // 1 < size
      case kSkipSome:
        mutated = SkipSome(data, size, max_size, out);
        break;
      // 1 < size <= capacity
      case kShuffle:
        mutated = Shuffle(data, size, out);
        break;
      case kReplaceSome:
        mutated = ReplaceSome(data, size, out);
        break;
      // 0 < size <= capacity
      case kFlip:
        mutated = Flip(data, size, out);
        break;
      case kReplaceOne:
        mutated = ReplaceOne(data, size, out);
        break;
      case kReplaceUnsigned:
        mutated = ReplaceUnsigned(data, size, out);
        break;
      case kReplaceNum:
        mutated = ReplaceNum(data, size, out);
        break;
      case kMergeReplace:
        mutated = MergeReplace(data, size, crossover_->data(), crossover_->size(), out);
        break;
      // 0 < size < capacity
      case kInsertSome:
        mutated = InsertSome(data, size, max_size, out);
        break;
      case kMergeInsert:
        mutated = MergeInsert(data, size, crossover_->data(), crossover_->size(), max_size, out);
        break;
      // 0 <= size < capacity
      case kInsertOne:
        mutated = InsertOne(data, size, out);
        break;
      case kInsertRepeated:
        mutated = InsertRepeated(data, size, max_size, out);
        break;
      default:
        FX_NOTREACHED();
    }
  }
  if (mutated) {
    mutations_.push_back(static_cast<Mutation>(mutation));
  } else {
    out->Write(0xff);
  }
}

// Skip (erase) some number of bytes.
bool Mutagen::SkipSome(const uint8_t* data, size_t size, size_t max_size, Input* out) {
  auto min_skip = max_size < size ? size - max_size : 1;
  auto skip_len = Pick<size_t>(min_skip, size - 1);
  auto skip_off = Pick<size_t>(0, size - skip_len);
  out->Write(&data[0], skip_off);
  WriteAfter(skip_off + skip_len, data, size, out);
  return true;
}

bool Mutagen::Shuffle(const uint8_t* data, size_t size, Input* out) {
  constexpr size_t kMinShuffle = 2;
  constexpr size_t kMaxShuffle = 8;
  auto shuffle_len = Pick<size_t>(kMinShuffle, std::min(size, kMaxShuffle));
  auto shuffle_off = Pick<size_t>(0, size - shuffle_len);
  out->Write(data, size);
  auto* out_data = out->data();
  std::shuffle(&out_data[shuffle_off], &out_data[shuffle_off + shuffle_len], prng_);
  return true;
}

bool Mutagen::Flip(const uint8_t* data, size_t size, Input* out) {
  out->Write(data, size);
  auto flip_off = Pick<size_t>(0, size - 1);
  auto flip_bit = 1 << Pick<uint8_t>(0, 7);
  out->data()[flip_off] ^= flip_bit;
  return true;
}

bool Mutagen::ReplaceOne(const uint8_t* data, size_t size, Input* out) {
  out->Write(data, size);
  auto replace_off = Pick<size_t>(0, size - 1);
  out->data()[replace_off] = PickSpecial();
  return true;
}

// Generates a new unsigned value of type |T|, using one or more transformations experimentally
// determined to be useful by libFuzzer. See |ChangeBinaryInteger| in libFuzzer's FuzzerMutate.cpp.
template <typename T>
static T MutateUnsigned(const uint8_t* data, size_t size, T randval) {
  bool use_size = randval & 1;
  bool do_bswap = randval & 2;
  T val;
  if (use_size) {
    // Replace value with size.
    if (do_bswap) {
      val = Bswap<T>(static_cast<T>(size));
    } else {
      val = static_cast<T>(size);
    }
  } else {
    // +/- 16, but using unsigned so overflow is well-defined.
    T adjustment = ((randval >> 2) & 0x1f) - 16;
    memcpy(&val, data, sizeof(val));
    if (!adjustment) {
      val = -val;
    } else if (do_bswap) {
      val = Bswap<T>(Bswap<T>(val) + adjustment);
    } else {
      val += adjustment;
    }
  }
  return val;
}

bool Mutagen::ReplaceUnsigned(const uint8_t* data, size_t size, Input* out) {
  // Pick 1, 2, 4, or 8 bytes.
  // NOLINTNEXTLINE(google-runtime-int)
  static_assert(sizeof(unsigned long long) * 8 == 64);
  auto replace_max = std::min(3, (63 - __builtin_clzll(size)));
  auto replace_len = 1ULL << Pick<size_t>(0, replace_max);
  auto replace_off = Pick<size_t>(0, size - replace_len);
  out->Write(&data[0], replace_off);
  switch (replace_len) {
    case sizeof(uint8_t): {
      auto val = MutateUnsigned<uint8_t>(&data[replace_off], size, Pick<uint8_t>());
      out->Write(&val, sizeof(val));
      break;
    }
    case sizeof(uint16_t): {
      auto val = MutateUnsigned<uint16_t>(&data[replace_off], size, Pick<uint16_t>());
      out->Write(&val, sizeof(val));
      break;
    }
    case sizeof(uint32_t): {
      auto val = MutateUnsigned<uint32_t>(&data[replace_off], size, Pick<uint32_t>());
      out->Write(&val, sizeof(val));
      break;
    }
    case sizeof(uint64_t): {
      auto val = MutateUnsigned<uint64_t>(&data[replace_off], size, Pick<uint64_t>());
      out->Write(&val, sizeof(val));
      break;
    }
    default:
      FX_NOTREACHED();
  }
  WriteAfter(replace_off + replace_len, data, size, out);
  return true;
}

bool Mutagen::ReplaceNum(const uint8_t* data, size_t size, Input* out) {
  size_t i = Pick<size_t>(0, size - 1);
  while (i < size && !isdigit(data[i])) {
    ++i;
  }
  auto num_off = i;
  uint64_t val = 0;
  // log10(2^64) + 1 = 20, so stop after 20 digits.
  size_t num_len = 0;
  while (i < size && isdigit(data[i]) && num_len < 20) {
    val = (val * 10) + (data[i++] - '0');
    ++num_len;
  }
  if (!num_len) {
    return false;
  }
  out->Write(&data[0], num_off);
  switch (Pick<uint8_t>(0, 4)) {
    case 0:
      val++;
      break;
    case 1:
      val--;
      break;
    case 2:
      val <<= 1;
      break;
    case 3:
      val >>= 1;
      break;
    default:
      val = Pick<uint64_t>(0, val * val);
      break;
  }
  // This writes out the value "backwards", but as a mutated value it doesn't make much difference.
  for (i = 0; i < num_len; ++i) {
    out->Write(static_cast<uint8_t>((val) % 10 + '0'));
    val /= 10;
  }
  WriteAfter(num_off + num_len, data, size, out);
  return true;
}

bool Mutagen::ReplaceSome(const uint8_t* data, size_t size, Input* out) {
  FX_DCHECK(size > 1);
  auto replace_len = Pick<size_t>(1, size - 1);
  auto replace_src = Pick<size_t>(0, size - replace_len);
  auto replace_dst = Pick<size_t>(0, size - replace_len);
  if (replace_src == replace_dst) {
    return false;
  }
  out->Write(&data[0], replace_dst);
  out->Write(&data[replace_src], replace_len);
  WriteAfter(replace_dst + replace_len, data, size, out);
  return true;
}

bool Mutagen::MergeReplace(const uint8_t* data1, size_t size1, const uint8_t* data2, size_t size2,
                           Input* out) {
  auto swap = Pick<bool>();
  auto* data = swap ? data1 : data2;
  auto size = swap ? size1 : size2;
  auto* next_data = swap ? data2 : data1;
  auto next_size = swap ? size2 : size1;
  size_t merge_off = 0;
  while (merge_off < size1 && merge_off < size2) {
    auto merge_len = std::min(Pick<size_t>(1, size), size - merge_off);
    out->Write(&data[merge_off], merge_len);
    merge_off += merge_len;
    std::swap(data, next_data);
    std::swap(size, next_size);
  }
  if (merge_off < size1) {
    out->Write(&data1[merge_off], size1 - merge_off);
  }
  return true;
}

bool Mutagen::InsertSome(const uint8_t* data, size_t size, size_t max_size, Input* out) {
  auto insert_len = Pick<size_t>(1, std::min(max_size - size, size));
  auto insert_src = Pick<size_t>(0, size - insert_len);
  auto insert_dst = Pick<size_t>(0, size);
  out->Write(&data[0], insert_dst);
  out->Write(&data[insert_src], insert_len);
  WriteAfter(insert_dst, data, size, out);
  return true;
}

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
bool Mutagen::MergeInsert(const uint8_t* data1, size_t size1, const uint8_t* data2, size_t size2,
                          size_t max_size, Input* out) {
  auto swap = Pick<bool>();
  auto* data = swap ? data1 : data2;
  auto* next_data = swap ? data2 : data1;
  auto size = swap ? size1 : size2;
  auto next_size = swap ? size2 : size1;
  size_t off = 0;
  size_t next_off = 0;
  size_t out_off = 0;
  while (off < size && out_off < max_size) {
    auto len = std::min(Pick<size_t>(1, size), size - off);
    FX_DCHECK(len);
    auto out_len = std::min(len, max_size - out_off);
    out->Write(&data[off], out_len);
    off += out_len;
    out_off += out_len;
    std::swap(data, next_data);
    std::swap(size, next_size);
    std::swap(off, next_off);
  }
  if (next_off < next_size && out_off < max_size) {
    auto out_len = std::min(next_size - next_off, max_size - out_off);
    out->Write(&next_data[next_off], out_len);
  }
  return true;
}

// Insert a single byte.
bool Mutagen::InsertOne(const uint8_t* data, size_t size, Input* out) {
  auto insert_off = Pick<size_t>(0, size);
  out->Write(&data[0], insert_off);
  out->Write(PickSpecial());
  WriteAfter(insert_off, data, size, out);
  return true;
}

// Insert a byte repeated several times.
bool Mutagen::InsertRepeated(const uint8_t* data, size_t size, size_t max_size, Input* out) {
  if (max_size < size + kMinRepeat) {
    return false;
  }
  size_t max_repeat = std::min(max_size - size, kMaxRepeat);
  auto insert_len = Pick<size_t>(kMinRepeat, max_repeat);
  auto insert_off = Pick<size_t>(0, size);
  auto insert_val = PickPreferred();
  out->Write(&data[0], insert_off);
  for (size_t i = 0; i < insert_len; ++i) {
    out->Write(insert_val);
  }
  WriteAfter(insert_off, data, size, out);
  return true;
}

template <>
bool Mutagen::Pick() {
  return prng_() % 2;
}

template <>
uint64_t Mutagen::Pick() {
  return (uint64_t(prng_()) << 32) | prng_();
}

// Pick a random byte, with preference given to 0 and 255.
uint8_t Mutagen::PickPreferred() {
  auto val = Pick<uint16_t>(0, 512);
  return static_cast<uint8_t>(val < 256 ? val : (val < 384 ? 0x00 : 0xff));
}

// Pick a random byte, with preference given to special ASCII characters.
char Mutagen::PickSpecial() {
  constexpr const char kSpecialChars[] = " !\"#$%&'()*+,-./012:;<=>?@[]`{|}~Az\xff\x00";
  auto val = Pick<uint16_t>(0, 512);
  return val < 256 ? static_cast<char>(val) : kSpecialChars[val % sizeof(kSpecialChars)];
}

}  // namespace fuzzing
