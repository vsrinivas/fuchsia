// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <float.h>
#include <limits.h>
#include <sched.h>
#include <stdint.h>
#include <threads.h>

#include <vector>
#include <limits>

#include <zxtest/zxtest.h>

namespace {

constexpr char kMainThreadError[] = "MainThread: Unexpected initialized value";
constexpr char kBackgroundThreadError[] = "BackgroundThread: Unexpected initialized value";

struct Bits {
  uint64_t bits0 : 9;
  uint64_t bits1 : 9;
  uint64_t bits2 : 9;
  uint64_t bits3 : 9;
  uint64_t bits4 : 9;
  uint64_t bits5 : 9;
  uint64_t bits6 : 9;
  double f64;
  uint64_t bits7 : 9;
  uint64_t bits8 : 9;
  uint64_t bits9 : 9;
  uint64_t bits10 : 9;
  uint64_t bits11 : 9;
  uint64_t bits12 : 9;
  uint64_t bits13 : 9;
};

thread_local bool u1 = true;
thread_local uint8_t u8 = std::numeric_limits<uint8_t>::max();
thread_local uint16_t u16 = std::numeric_limits<uint16_t>::max();
thread_local uint32_t u32 = std::numeric_limits<uint32_t>::max();
thread_local uint64_t u64 = std::numeric_limits<uint64_t>::max();
thread_local uintptr_t uptr = std::numeric_limits<uintptr_t>::max();
thread_local int8_t i8 = std::numeric_limits<int8_t>::max();
thread_local int16_t i16 = std::numeric_limits<int16_t>::max();
thread_local int32_t i32 = std::numeric_limits<int32_t>::max();
thread_local int64_t i64 = std::numeric_limits<int64_t>::max();
thread_local intptr_t iptr = std::numeric_limits<intptr_t>::max();
thread_local float f32 = std::numeric_limits<float>::max();
thread_local double f64 = std::numeric_limits<double>::max();
thread_local void* ptr = &ptr;
thread_local Bits bits = {
    0x1ffu, 0x1ffu, 0x1ffu, 0x1ffu, 0x1ffu, 0x1ffu, 0x1ffu, std::numeric_limits<double>::max(),
    0x1ffu, 0x1ffu, 0x1ffu, 0x1ffu, 0x1ffu, 0x1ffu, 0x1ffu,
};

struct BasicInitializerInfo {
  bool u1;
  uint8_t u8;
  uint16_t u16;
  uint32_t u32;
  uint64_t u64;
  uintptr_t uptr;
  int8_t i8;
  int16_t i16;
  int32_t i32;
  int64_t i64;
  intptr_t iptr;
  float f32;
  double f64;
  void* ptr_addr;
  void* ptr_val;
  Bits bits;
};

int GetBasicInitializers(void* arg) {
  auto info = reinterpret_cast<BasicInitializerInfo*>(arg);
  info->u1 = u1;
  info->u8 = u8;
  info->u16 = u16;
  info->u32 = u32;
  info->u64 = u64;
  info->uptr = uptr;
  info->i8 = i8;
  info->i16 = i16;
  info->i32 = i32;
  info->i64 = i64;
  info->iptr = iptr;
  info->f32 = f32;
  info->f64 = f64;
  info->ptr_addr = &ptr;
  info->ptr_val = ptr;
  info->bits.bits0 = bits.bits0;
  info->bits.bits1 = bits.bits1;
  info->bits.bits2 = bits.bits2;
  info->bits.bits3 = bits.bits3;
  info->bits.bits4 = bits.bits4;
  info->bits.bits5 = bits.bits5;
  info->bits.bits6 = bits.bits7;
  info->bits.bits7 = bits.bits7;
  info->bits.bits8 = bits.bits8;
  info->bits.bits9 = bits.bits9;
  info->bits.bits10 = bits.bits10;
  info->bits.bits11 = bits.bits11;
  info->bits.bits12 = bits.bits12;
  info->bits.bits13 = bits.bits13;
  return 0;
}

void VerifyBasicInitializers(const BasicInitializerInfo* info, const char* error_message) {
  ASSERT_EQ(info->u1, true, "%s", error_message);
  ASSERT_EQ(info->u8, std::numeric_limits<uint8_t>::max(), "%s", error_message);
  ASSERT_EQ(info->u16, std::numeric_limits<uint16_t>::max(), "%s", error_message);
  ASSERT_EQ(info->u32, std::numeric_limits<uint32_t>::max(), "%s", error_message);
  ASSERT_EQ(info->u64, std::numeric_limits<uint64_t>::max(), "%s", error_message);
  ASSERT_EQ(info->uptr, std::numeric_limits<uintptr_t>::max(), "%s", error_message);
  ASSERT_EQ(info->i8, std::numeric_limits<int8_t>::max(), "%s", error_message);
  ASSERT_EQ(info->i16, std::numeric_limits<int16_t>::max(), "%s", error_message);
  ASSERT_EQ(info->i32, std::numeric_limits<int32_t>::max(), "%s", error_message);
  ASSERT_EQ(info->i64, std::numeric_limits<int64_t>::max(), "%s", error_message);
  ASSERT_EQ(info->iptr, std::numeric_limits<intptr_t>::max(), "%s", error_message);
  ASSERT_EQ(info->f32, std::numeric_limits<float>::max(), "%s", error_message);
  ASSERT_EQ(info->f64, std::numeric_limits<double>::max(), "%s", error_message);
  ASSERT_EQ(info->ptr_addr, info->ptr_val, "%s", error_message);
  ASSERT_EQ(info->bits.bits0, 0x1ffu, "%s", error_message);
  ASSERT_EQ(info->bits.bits1, 0x1ffu, "%s", error_message);
  ASSERT_EQ(info->bits.bits2, 0x1ffu, "%s", error_message);
  ASSERT_EQ(info->bits.bits3, 0x1ffu, "%s", error_message);
  ASSERT_EQ(info->bits.bits4, 0x1ffu, "%s", error_message);
  ASSERT_EQ(info->bits.bits5, 0x1ffu, "%s", error_message);
  ASSERT_EQ(info->bits.bits6, 0x1ffu, "%s", error_message);
  ASSERT_EQ(info->bits.bits7, 0x1ffu, "%s", error_message);
  ASSERT_EQ(info->bits.bits8, 0x1ffu, "%s", error_message);
  ASSERT_EQ(info->bits.bits9, 0x1ffu, "%s", error_message);
  ASSERT_EQ(info->bits.bits10, 0x1ffu, "%s", error_message);
  ASSERT_EQ(info->bits.bits11, 0x1ffu, "%s", error_message);
  ASSERT_EQ(info->bits.bits12, 0x1ffu, "%s", error_message);
  ASSERT_EQ(info->bits.bits13, 0x1ffu, "%s", error_message);
}

constexpr char kThreadNameBasicInitializer[] = "GetInitializers";

TEST(ExecutableTlsTest, BasicInitalizersInThread) {
  thrd_t thread;
  auto info = std::make_unique<BasicInitializerInfo>();

  int ret = thrd_create_with_name(&thread, &GetBasicInitializers, info.get(),
                                  kThreadNameBasicInitializer);
  ASSERT_EQ(ret, thrd_success, "unable to create GetInitializers thread");

  ret = thrd_join(thread, nullptr);
  ASSERT_EQ(ret, thrd_success, "unable to join GetInitializers thread");
  VerifyBasicInitializers(info.get(), kBackgroundThreadError);
}

TEST(ExecutableTlsTest, BasicInitalizersInMain) {
  auto info = std::make_unique<BasicInitializerInfo>();

  ASSERT_EQ(GetBasicInitializers(info.get()), 0);
  VerifyBasicInitializers(info.get(), kMainThreadError);
}

constexpr int kArraySize = 1024;

#define BYTES_4 0xffu, 0xffu, 0xffu, 0xffu,
#define BYTES_16 BYTES_4 BYTES_4 BYTES_4 BYTES_4
#define BYTES_64 BYTES_16 BYTES_16 BYTES_16 BYTES_16
#define BYTES_256 BYTES_64 BYTES_64 BYTES_64 BYTES_64
#define BYTES_1024 BYTES_256 BYTES_256 BYTES_256 BYTES_256

thread_local uint8_t array[kArraySize] = {BYTES_1024};

struct ArrayInfo {
  uint8_t array[kArraySize];
};

int GetArray(void* arg) {
  auto info = reinterpret_cast<ArrayInfo*>(arg);
  memcpy(info->array, array, kArraySize);
  return 0;
}

constexpr char kThreadNameArrayInitializer[] = "ArrayInitializers";

TEST(ExecutableTlsTest, ArrayInitializerInThread) {
  thrd_t thread;
  auto info = std::make_unique<ArrayInfo>();
  int ret = thrd_create_with_name(&thread, &GetArray, info.get(), kThreadNameArrayInitializer);
  ASSERT_EQ(ret, thrd_success, "unable to create GetArray thread");
  ret = thrd_join(thread, nullptr);
  ASSERT_EQ(ret, thrd_success, "unable to join GetArray thread");
  for (const auto byte : info->array) {
    ASSERT_EQ(byte, std::numeric_limits<uint8_t>::max(), "%s", kBackgroundThreadError);
  }
}

TEST(ExecutableTlsTest, ArrayInitializerInMain) {
  auto info = std::make_unique<ArrayInfo>();
  ASSERT_EQ(GetArray(info.get()), 0);
  for (const auto byte : info->array) {
    ASSERT_EQ(byte, std::numeric_limits<uint8_t>::max(), "%s", kMainThreadError);
  }
}

constexpr int kBitArraySize = 1 << 20;
thread_local uint8_t big_array[kBitArraySize];

struct BigArrayInfo {
  uint8_t big_array[kBitArraySize];
};

int GetBigArray(void* arg) {
  auto info = reinterpret_cast<BigArrayInfo*>(arg);
  memcpy(info->big_array, big_array, kBitArraySize);
  return 0;
}

void VerifyBigArray(BigArrayInfo const* const info, const char* error_message) {
  uint8_t sum = 0u;
  for (const auto byte : info->big_array) {
    sum |= byte;
  }
  ASSERT_EQ(sum, 0u, "%s", error_message);
}

constexpr char kThreadNameBigArray[] = "GetBigArray";

TEST(ExecutableTlsTest, BigArrayInitializerInThread) {
  thrd_t thread;
  auto info = std::make_unique<BigArrayInfo>();

  int ret = thrd_create_with_name(&thread, &GetBigArray, info.get(), kThreadNameBigArray);
  ASSERT_EQ(ret, thrd_success, "unable to create GetBigArray thread");

  ret = thrd_join(thread, nullptr);
  ASSERT_EQ(ret, thrd_success, "unable to join GetBigArray thread");
  VerifyBigArray(info.get(), kBackgroundThreadError);
}

TEST(ExecutableTlsTest, BigArrayInitializerInMain) {
  auto info = std::make_unique<BigArrayInfo>();

  ASSERT_EQ(GetBigArray(info.get()), 0);
  VerifyBigArray(info.get(), kMainThreadError);
}

struct Ctor {
  Ctor() : x(std::numeric_limits<uint64_t>::max()) {}
  uint64_t x;
};

thread_local Ctor ctor;

struct CtorInfo {
  Ctor ctor;
};

int GetCtor(void* arg) {
  auto info = reinterpret_cast<CtorInfo*>(arg);
  info->ctor = ctor;
  return 0;
}

constexpr char kThreadNameStructureInitializer[] = "StructureInitializer";

TEST(ExecutableTlsTest, StructureInitializerInThread) {
  thrd_t thread;
  auto info = std::make_unique<CtorInfo>();
  int ret = thrd_create_with_name(&thread, &GetCtor, info.get(), kThreadNameStructureInitializer);
  ASSERT_EQ(ret, thrd_success, "unable to create GetCtor thread");
  ret = thrd_join(thread, nullptr);
  ASSERT_EQ(ret, thrd_success, "unable to join GetCtor thread");
  ASSERT_EQ(info->ctor.x, std::numeric_limits<uint64_t>::max(), "%s", kBackgroundThreadError);
}

TEST(ExecutableTlsTest, StructureInitalizierInMain) {
  auto info = std::make_unique<CtorInfo>();
  ASSERT_EQ(GetCtor(info.get()), 0);
  ASSERT_EQ(info->ctor.x, std::numeric_limits<uint64_t>::max(), "%s", kMainThreadError);
}

__attribute__((aligned(0x1000))) thread_local int aligned_var = 123;

struct AlignmentInfo {
  int* aligned_var_addr;
  int aligned_var_value;
};

int GetAlignment(void* arg) {
  auto info = reinterpret_cast<AlignmentInfo*>(arg);
  // TODO(fxbug.dev/31523): Make this work on ARM64.
  info->aligned_var_value = aligned_var;
  info->aligned_var_addr = &aligned_var;
  return 0;
}

void VerifyAlignment(AlignmentInfo const* const info, const char* error_message) {
  // TODO(fxbug.dev/31523): Make this work on ARM64.
  EXPECT_EQ(reinterpret_cast<uintptr_t>(info->aligned_var_addr) % 0x1000, 0, "%s", error_message);
  EXPECT_EQ(info->aligned_var_value, 123, "%s", error_message);
}

constexpr char kThreadNameGetAlignment[] = "GetInitializers";

TEST(ExecutableTlsTest, AlignmentInitializerInThread) {
  thrd_t thread;
  auto info = std::make_unique<AlignmentInfo>();

  int ret = thrd_create_with_name(&thread, &GetAlignment, info.get(), kThreadNameGetAlignment);
  ASSERT_EQ(ret, thrd_success, "unable to create GetAlignment thread");

  ret = thrd_join(thread, nullptr);
  ASSERT_EQ(ret, thrd_success, "unable to join GetAlignment thread");
  VerifyAlignment(info.get(), kBackgroundThreadError);
}

TEST(ExecutableTlsTest, AlignmentInitializierInMain) {
  auto info = std::make_unique<AlignmentInfo>();

  ASSERT_EQ(GetAlignment(info.get()), 0);
  VerifyAlignment(info.get(), kMainThreadError);
}

struct ArraySpamInfo {
  uint8_t index;
  bool failure;
  uint8_t failure_offset;
  uint8_t actual_value;
  uint8_t expected_value;
};

int TestArraySpam(void* arg) {
  auto info = reinterpret_cast<ArraySpamInfo*>(arg);
  for (uint8_t iteration = 0; iteration < 100; ++iteration) {
    uint8_t starting_value = static_cast<uint8_t>(info->index + iteration);
    uint8_t value = starting_value;
    for (auto& byte : array) {
      byte = value;
      ++value;
    }
    sched_yield();
    value = starting_value;
    uint8_t failure_offset = 0;
    for (const auto byte : array) {
      if (byte != value) {
        info->failure = true;
        info->actual_value = byte;
        info->expected_value = value;
        info->failure_offset = failure_offset;
        return 0;
      }
      ++value;
      ++failure_offset;
    }
  }
  info->failure = false;

  return 0;
}

constexpr char kThreadNameArraySpam[] = "TestArraySpam";

TEST(ExecutableTlsTest, ArrayInitializerSpamThread) {
  constexpr int kThreadCount = 64;
  std::vector<thrd_t> threads(kThreadCount);
  std::vector<ArraySpamInfo> info(kThreadCount);

  for (uint8_t index = 0; index < kThreadCount; ++index) {
    info[index].index = index;
    int ret =
        thrd_create_with_name(&threads[index], &TestArraySpam, &info[index], kThreadNameArraySpam);
    ASSERT_EQ(ret, thrd_success, "unable to create TestArraySpamInfo thread");
  }
  for (uint8_t index = 0; index < kThreadCount; ++index) {
    int ret = thrd_join(threads[index], nullptr);
    ASSERT_EQ(ret, thrd_success, "unable to join TestArraySpamInfo thread");
    ASSERT_FALSE(info[index].failure,
                 "%s: thread=%d ExpectedValue=%d ActualValue=%d FailureOffset=%d",
                 kBackgroundThreadError, index, info[index].expected_value,
                 info[index].actual_value, info[index].failure_offset);
  }
}

TEST(ExecutableTlsTest, ArrayInitializierSpamMain) {
  auto info = std::make_unique<ArraySpamInfo>();
  ASSERT_EQ(TestArraySpam(info.get()), 0);
  ASSERT_FALSE(info->failure, "%s: ExpectedValue=%d ActualValue=%d FailureOffset=%d",
               kMainThreadError, info->expected_value, info->actual_value, info->failure_offset);
}

}  // namespace
