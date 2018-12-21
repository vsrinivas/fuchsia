// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <iostream>
#include <random>
#include <vector>

#include <fbl/intrusive_single_list.h>
#include <fbl/ref_counted.h>
#include <fbl/ref_ptr.h>
#include <fbl/slab_allocator.h>
#include <fbl/string_printf.h>
#include <perftest/perftest.h>

namespace {

template <size_t BufSize, size_t SlabSize>
class DataBuf;

template <size_t ObjSize,
          size_t SlabSize = fbl::DEFAULT_SLAB_ALLOCATOR_SLAB_SIZE>
struct AllocatorTraits
    : public fbl::StaticSlabAllocatorTraits<
          fbl::RefPtr<DataBuf<ObjSize, SlabSize>>, SlabSize> {
  static constexpr size_t UserBufSize = ObjSize;
};

// Note: DataBuf doesn't really care about the SlabSize. But DataBuf does care
// about the allocator's type. And the allocator's type depends on the SlabSize.
template <size_t BufSize, size_t SlabSize>
class DataBuf : public fbl::SlabAllocated<AllocatorTraits<BufSize, SlabSize>>,
                public fbl::RefCounted<DataBuf<BufSize, SlabSize>>,
                public fbl::SinglyLinkedListable<
                    fbl::RefPtr<DataBuf<BufSize, SlabSize>>> {
 public:
  DataBuf() {
    // We provide a user-defined default constructor that does nothing, to
    // avoid the cost of zeroing out |data|.
    //
    // Rationale: in the absence of a user-defined constructor, expressions
    // such as |DataBuf()| or |new DataBuf()| trigger value-initialization,
    // which zeros out |data|. For details, see
    // https://stackoverflow.com/a/2418195
  }

 private:
  char data[BufSize];
};

template <typename AllocatorTraits>
bool RetainAndFree(const std::vector<size_t>& replacement_sequence,
                   perftest::RepeatState* state) {
  using AllocatorType = fbl::SlabAllocator<AllocatorTraits>;
  const size_t num_bufs_to_retain = replacement_sequence.size();

  if (num_bufs_to_retain < 1) {
    std::cerr << "Must retain at least 1 buffer" << std::endl;
    return false;
  }

  // Populate an initial collection of buffers.
  std::vector<typename AllocatorType::PtrType> retained_bufs(
      num_bufs_to_retain);
  std::generate(retained_bufs.begin(), retained_bufs.end(),
                [] { return AllocatorType::New(); });
  for (size_t i = 0; i < retained_bufs.size(); ++i) {
    if (!retained_bufs[i]) {
      std::cerr << "Failed to allocate buf " << i << " before benchmark loop"
                << std::endl;
      return false;
    }
  }

  // The benchmark task: replace a random existing buffer with a new one.
  for (size_t i = 0; state->KeepRunning(); ++i) {
    const size_t old_buf_index = replacement_sequence[i % num_bufs_to_retain];
    const auto& buf = (retained_bufs[old_buf_index] = AllocatorType::New());
    if (!buf) {
      std::cerr << "Failed to allocate " << AllocatorTraits::UserBufSize
                << " bytes at benchmark iteration " << i << std::endl;
      return false;
    }
  }

  return true;
}

// Measure the time taken to allocate and immediately free a block
// from a slab allocator. The block is allocated from the pool
// initialized by one of the DECLARE_STATIC_SLAB_ALLOCATOR_STORAGE
// statements at the end of this file. This benchmark represents (presumed)
// best-case behavior, as the memory pool should be unfragmented.
template <typename AllocatorTraits>
bool AllocAndFree(perftest::RepeatState* state) {
  while (state->KeepRunning()) {
    if (!fbl::SlabAllocator<AllocatorTraits>::New()) {
      return false;
    }
  }
  return true;
}

// Measure the time taken to free the oldest allocated block, and allocate a
// new one, using the slab allocator. The block is allocated from the pool
// initialized by one of the DECLARE_STATIC_SLAB_ALLOCATOR_STORAGE statements
// at the end of this file. This benchmark abstracts a network copy workload,
// when copying from a fast source, to a slow sink.
template <typename AllocatorTraits>
bool RetainAndFreeOldest(perftest::RepeatState* state,
                         size_t num_bufs_to_retain) {
  // Generate the sequence of indexes of buffers to replace.
  std::vector<size_t> buf_to_free(num_bufs_to_retain);
  std::iota(buf_to_free.begin(), buf_to_free.end(), 0);
  return RetainAndFree<AllocatorTraits>(buf_to_free, state);
}

// Measure the time taken to free a random allocated block, and allocate a
// new one, using the slab allocator. The block is allocated from the pool
// initialized by one of the DECLARE_STATIC_SLAB_ALLOCATOR_STORAGE statements
// at the end of this file. This benchmark attempts to quantify the effects of
// memory fragmentation.
template <typename AllocatorTraits>
bool RetainAndFreeRandom(perftest::RepeatState* state,
                         size_t num_bufs_to_retain) {
  // Generate the sequence of indexes of buffers to replace.
  std::vector<size_t> buf_to_free(num_bufs_to_retain);
  std::random_device rand_dev;
  std::iota(buf_to_free.begin(), buf_to_free.end(), 0);
  std::shuffle(buf_to_free.begin(), buf_to_free.end(), rand_dev);
  return RetainAndFree<AllocatorTraits>(buf_to_free, state);
}

template <typename AllocatorTraits, auto Callable>
void RegisterRetainedMemTest(const char* name) {
  constexpr size_t kBlockSizeBytes = AllocatorTraits::UserBufSize;
  // The maximum value of 32768KB below was chosen empirically, as the point at
  // which allocators started showing scaling behaviors on Eve.
  for (size_t total_size_kbytes : {8, 32, 128, 512, 2048, 8192, 32768}) {
    const size_t total_size_bytes = total_size_kbytes * 1024;
    perftest::RegisterTest(
        fbl::StringPrintf("SlabAlloc/Static/%s/%zubytes/%zuKbytes/%zuKbytes",
                          name, kBlockSizeBytes,
                          AllocatorTraits::SLAB_SIZE / 1024, total_size_kbytes)
            .c_str(),
        Callable, total_size_bytes / kBlockSizeBytes);
  }
}

template <typename AllocatorTraits, auto Callable>
void RegisterNoRetainedMemTest(const char* name) {
  constexpr size_t kBlockSizeBytes = AllocatorTraits::UserBufSize;
  perftest::RegisterTest(
      fbl::StringPrintf("SlabAlloc/Static/%s/%zubytes/%zuKbytes", name,
                        kBlockSizeBytes, AllocatorTraits::SLAB_SIZE / 1024)
          .c_str(),
      Callable);
}

#define REGISTER_TEST(REGISTRATION_TEMPLATE, NAME, ALLOCATOR_TRAITS) \
  REGISTRATION_TEMPLATE<ALLOCATOR_TRAITS, NAME<ALLOCATOR_TRAITS>>(#NAME);

// The motivation for multiple sizes is to quantify any scaling behavior with
// the size of the allocation.
constexpr size_t kSmallBlockSizeBytes = 64;
constexpr size_t kLargeBlockSizeBytes = 8192;

// This value must accommodate the maximal value of |total_size_kbytes| in
// RegisterRetainedMemTest().
constexpr size_t kLiveAllocLimitBytes = 32u * 1024 * 1024;

using SmallBlockAllocatorTraits = AllocatorTraits<kSmallBlockSizeBytes>;
using LargeBlockAllocatorTraits =
    AllocatorTraits<kLargeBlockSizeBytes, kLargeBlockSizeBytes * 205>;

static_assert(
    fbl::SlabAllocator<LargeBlockAllocatorTraits>::AllocsPerSlab ==
        fbl::SlabAllocator<SmallBlockAllocatorTraits>::AllocsPerSlab,
    "Please adjust the SLAB_SIZE parameter for LargeBlockAllocatorTraits, so "
    "that the LargeBlockAllocator amortizes malloc() calls over as many slab "
    "objects as the SmallBlockAllocator.");

void RegisterTests() {
  REGISTER_TEST(RegisterNoRetainedMemTest, AllocAndFree,
                SmallBlockAllocatorTraits);
  REGISTER_TEST(RegisterNoRetainedMemTest, AllocAndFree,
                LargeBlockAllocatorTraits);

  REGISTER_TEST(RegisterRetainedMemTest, RetainAndFreeOldest,
                SmallBlockAllocatorTraits);
  REGISTER_TEST(RegisterRetainedMemTest, RetainAndFreeOldest,
                LargeBlockAllocatorTraits);

  REGISTER_TEST(RegisterRetainedMemTest, RetainAndFreeRandom,
                SmallBlockAllocatorTraits);
  REGISTER_TEST(RegisterRetainedMemTest, RetainAndFreeRandom,
                LargeBlockAllocatorTraits);
}

PERFTEST_CTOR(RegisterTests);

}  // namespace

#define DECLARE_STATIC_STORAGE(SLAB_TRAITS)                           \
  DECLARE_STATIC_SLAB_ALLOCATOR_STORAGE(                              \
      SLAB_TRAITS, (kLiveAllocLimitBytes /                            \
                    (fbl::SlabAllocator<SLAB_TRAITS>::AllocsPerSlab * \
                     SLAB_TRAITS::UserBufSize)) +                     \
                       1 /* Round up */);

DECLARE_STATIC_STORAGE(SmallBlockAllocatorTraits);
DECLARE_STATIC_STORAGE(LargeBlockAllocatorTraits);
