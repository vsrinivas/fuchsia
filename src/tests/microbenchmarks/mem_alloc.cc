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

/*** Common definitions ***/
// The motivation for multiple sizes is to quantify any scaling behavior with
// the size of the allocation.
constexpr size_t kSmallBlockSizeBytes = 64;
constexpr size_t kLargeBlockSizeBytes = 8192;

// This value must accommodate the maximal value of |total_size_kbytes| in
// RegisterRetainedMemTest().
constexpr size_t kLiveAllocLimitBytes = 32u * 1024 * 1024;

template <size_t Size>
class DataBuf {
 public:
  DataBuf(){
      // We provide a user-defined default constructor that does nothing, to
      // avoid the cost of zeroing out |data|.
      //
      // Rationale: in the absence of a user-defined constructor, expressions
      // such as |DataBuf()| or |new DataBuf()| trigger value-initialization,
      // which zeros out |data|. For details, see
      // https://stackoverflow.com/a/2418195
  };

 private:
  char data[Size];
};

template <typename AllocatorTraits>
class SlabDataBuf;

template <typename AllocatorTraits>
class SlabDataBuf : public fbl::SlabAllocated<AllocatorTraits>,
                    public fbl::RefCounted<SlabDataBuf<AllocatorTraits>>,
                    public fbl::SinglyLinkedListable<fbl::RefPtr<SlabDataBuf<AllocatorTraits>>> {
 public:
  SlabDataBuf(){
      // As with DataBuf, we provide a user-defined default constructor that
      // does nothing, to avoid the cost of zeroing out |data|.
      //
      // CAUTION: For reasons that are unclear, inheriting DataBuf (rather than
      // declaring our own |data|) does not avoid the cost of zeroing out
      // DataBuf::data, even though DataBuf has a user-defined default
      // constructor.
  };

 private:
  char data[AllocatorTraits::kUserBufSize];
};

/*** Static slab allocator definitions ***/
template <typename AllocatorTraits>
class StaticSlabAllocator;

template <size_t ObjSize, size_t SlabSize>
struct StaticSlabAllocatorTraits
    : public fbl::StaticSlabAllocatorTraits<
          fbl::RefPtr<SlabDataBuf<StaticSlabAllocatorTraits<ObjSize, SlabSize>>>, SlabSize> {
  using AllocT = StaticSlabAllocator<StaticSlabAllocatorTraits<ObjSize, SlabSize>>;
  static constexpr char kName[] = "SlabStatic";
  static constexpr size_t kUserBufSize = ObjSize;
  static constexpr size_t kSlabSizeKbytes = SlabSize / 1024;

  static auto GetConfigAsString() {
    return fbl::StringPrintf("%zubytes/%zuKbytes", kUserBufSize, kSlabSizeKbytes);
  }
};

template <typename AllocatorTraits>
class StaticSlabAllocator {
 public:
  static constexpr size_t kUserBufSize = AllocatorTraits::kUserBufSize;
  auto New() { return fbl::SlabAllocator<AllocatorTraits>::New(); }
};

using StaticSmallBlockAllocatorTraits =
    StaticSlabAllocatorTraits<kSmallBlockSizeBytes, fbl::DEFAULT_SLAB_ALLOCATOR_SLAB_SIZE>;
using StaticLargeBlockAllocatorTraits =
    StaticSlabAllocatorTraits<kLargeBlockSizeBytes, kLargeBlockSizeBytes * 205>;

static_assert(fbl::SlabAllocator<StaticLargeBlockAllocatorTraits>::AllocsPerSlab ==
                  fbl::SlabAllocator<StaticSmallBlockAllocatorTraits>::AllocsPerSlab,
              "Please adjust the SLAB_SIZE parameter for "
              "StaticLargeBlockAllocatorTraits, so that the StaticLargeBlockAllocator "
              "amortizes malloc() calls over as many slab objects as the Static "
              "SmallBlockAllocator.");

/*** Instanced slab allocator definitions ***/
template <typename AllocatorTraits>
class InstancedSlabAllocator;

template <size_t ObjSize, size_t SlabSize>
struct InstancedSlabAllocatorTraits
    : public fbl::SlabAllocatorTraits<
          fbl::RefPtr<SlabDataBuf<InstancedSlabAllocatorTraits<ObjSize, SlabSize>>>, SlabSize> {
  using AllocT = InstancedSlabAllocator<InstancedSlabAllocatorTraits<ObjSize, SlabSize>>;
  static constexpr char kName[] = "SlabInstanced";
  static constexpr size_t kUserBufSize = ObjSize;
  static constexpr size_t kSlabSizeKbytes = SlabSize / 1024;

  static auto GetConfigAsString() {
    return fbl::StringPrintf("%zubytes/%zuKbytes", kUserBufSize, kSlabSizeKbytes);
  }
};

template <typename AllocatorTraits>
class InstancedSlabAllocator {
 public:
  static constexpr size_t kUserBufSize = AllocatorTraits::kUserBufSize;

  InstancedSlabAllocator() : allocator_(kMaxSlabs) {}
  auto New() { return allocator_.New(); }

 private:
  static constexpr size_t kMaxSizeBytes = kLiveAllocLimitBytes;
  static constexpr size_t kMaxSlabs =
      (kMaxSizeBytes / (fbl::SlabAllocator<AllocatorTraits>::AllocsPerSlab * kUserBufSize)) +
      1 /* Round up */;

  fbl::SlabAllocator<AllocatorTraits> allocator_;
};

using InstancedSmallBlockAllocatorTraits =
    InstancedSlabAllocatorTraits<kSmallBlockSizeBytes, fbl::DEFAULT_SLAB_ALLOCATOR_SLAB_SIZE>;
using InstancedLargeBlockAllocatorTraits =
    InstancedSlabAllocatorTraits<kLargeBlockSizeBytes, kLargeBlockSizeBytes * 187>;

static_assert(fbl::SlabAllocator<InstancedLargeBlockAllocatorTraits>::AllocsPerSlab ==
                  fbl::SlabAllocator<InstancedSmallBlockAllocatorTraits>::AllocsPerSlab,
              "Please adjust the SLAB_SIZE parameter for "
              "InstancedLargeBlockAllocatorTraits, so that the "
              "InstancedLargeBlockAllocator amortizes malloc() calls over as many slab "
              "objects as the InstancedSmallBlockAllocator.");

/*** Heap allocator definitions ***/
template <typename AllocatorTraits>
class HeapAllocator;

template <size_t ObjSize>
struct HeapAllocatorTraits {
  using AllocT = HeapAllocator<HeapAllocatorTraits<ObjSize>>;
  static constexpr char kName[] = "Malloc";
  static constexpr size_t kUserBufSize = ObjSize;
  static auto GetConfigAsString() { return fbl::StringPrintf("%zubytes", kUserBufSize); }
};

template <typename AllocatorTraits>
class HeapAllocator {
 public:
  static constexpr size_t kUserBufSize = AllocatorTraits::kUserBufSize;
  auto New() { return std::make_unique<DataBuf<kUserBufSize>>(); }
};

using HeapSmallBlockAllocatorTraits = HeapAllocatorTraits<kSmallBlockSizeBytes>;
using HeapLargeBlockAllocatorTraits = HeapAllocatorTraits<kLargeBlockSizeBytes>;

/*** Benchmark code ***/
// Utility function called from RetainAndFreeOldest() and RetainAndFreeRandom().
template <typename AllocatorT>
bool RetainAndFree(const std::vector<size_t>& replacement_sequence, perftest::RepeatState* state) {
  const size_t num_bufs_to_retain = replacement_sequence.size();

  if (num_bufs_to_retain < 1) {
    std::cerr << "Must retain at least 1 buffer" << std::endl;
    return false;
  }

  // Populate an initial collection of buffers.
  AllocatorT allocator;
  std::vector<decltype(allocator.New())> retained_bufs(num_bufs_to_retain);
  std::generate(retained_bufs.begin(), retained_bufs.end(), [&] { return allocator.New(); });
  for (size_t i = 0; i < retained_bufs.size(); ++i) {
    if (!retained_bufs[i]) {
      std::cerr << "Failed to allocate buf " << i << " before benchmark loop" << std::endl;
      return false;
    }
  }

  // The benchmark task: replace a random existing buffer with a new one.
  for (size_t i = 0; state->KeepRunning(); ++i) {
    const size_t old_buf_index = replacement_sequence[i % num_bufs_to_retain];
    const auto& buf = (retained_bufs[old_buf_index] = allocator.New());
    if (!buf) {
      std::cerr << "Failed to allocate " << allocator.kUserBufSize
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
template <typename AllocatorT>
bool AllocAndFree(perftest::RepeatState* state) {
  AllocatorT allocator;
  while (state->KeepRunning()) {
    auto buf = allocator.New();
    perftest::DoNotOptimize(buf);
    if (!buf) {
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
template <typename AllocatorT>
bool RetainAndFreeOldest(perftest::RepeatState* state, size_t num_bufs_to_retain) {
  // Generate the sequence of indexes of buffers to replace.
  std::vector<size_t> buf_to_free(num_bufs_to_retain);
  std::iota(buf_to_free.begin(), buf_to_free.end(), 0);
  return RetainAndFree<AllocatorT>(buf_to_free, state);
}

// Measure the time taken to free a random allocated block, and allocate a
// new one, using the slab allocator. The block is allocated from the pool
// initialized by one of the DECLARE_STATIC_SLAB_ALLOCATOR_STORAGE statements
// at the end of this file. This benchmark attempts to quantify the effects of
// memory fragmentation.
template <typename AllocatorT>
bool RetainAndFreeRandom(perftest::RepeatState* state, size_t num_bufs_to_retain) {
  // Generate the sequence of indexes of buffers to replace.
  std::vector<size_t> buf_to_free(num_bufs_to_retain);
  std::random_device rand_dev;
  std::iota(buf_to_free.begin(), buf_to_free.end(), 0);
  std::shuffle(buf_to_free.begin(), buf_to_free.end(), rand_dev);
  return RetainAndFree<AllocatorT>(buf_to_free, state);
}

/*** Linkage and instantiation ***/
using RetainedMemPerfTest = bool (*)(perftest::RepeatState* state, size_t num_bufs_to_retain);
template <typename AllocatorTraits, RetainedMemPerfTest PerfTest>
void RegisterTest(const char* bench_name) {
  // The maximum value of 32768KB below was chosen empirically, as the point at
  // which allocators started showing scaling behaviors on Eve.
  for (size_t total_size_kbytes : {8, 32, 128, 512, 2048, 8192, 32768}) {
    const size_t total_size_bytes = total_size_kbytes * 1024;
    perftest::RegisterTest(
        fbl::StringPrintf("MemAlloc/%s/%s/%s/%zuKbytes", AllocatorTraits::kName, bench_name,
                          AllocatorTraits::GetConfigAsString().c_str(), total_size_kbytes)
            .c_str(),
        PerfTest, total_size_bytes / AllocatorTraits::kUserBufSize);
  }
}

using NoRetainedMemPerfTest = bool (*)(perftest::RepeatState* state);
template <typename AllocatorTraits, NoRetainedMemPerfTest PerfTest>
void RegisterTest(const char* bench_name) {
  perftest::RegisterTest(fbl::StringPrintf("MemAlloc/%s/%s/%s", AllocatorTraits::kName, bench_name,
                                           AllocatorTraits::GetConfigAsString().c_str())
                             .c_str(),
                         PerfTest);
}

#define REGISTER_PERF_TEST_INSTANCE(ALLOCATION_PATTERN, ALLOCATOR_TRAITS) \
  RegisterTest<ALLOCATOR_TRAITS, ALLOCATION_PATTERN<ALLOCATOR_TRAITS::AllocT>>(#ALLOCATION_PATTERN);

#define REGISTER_PERF_TEST(ALLOCATION_PATTERN)                                           \
  do {                                                                                   \
    REGISTER_PERF_TEST_INSTANCE(ALLOCATION_PATTERN, StaticSmallBlockAllocatorTraits);    \
    REGISTER_PERF_TEST_INSTANCE(ALLOCATION_PATTERN, StaticLargeBlockAllocatorTraits);    \
    REGISTER_PERF_TEST_INSTANCE(ALLOCATION_PATTERN, InstancedSmallBlockAllocatorTraits); \
    REGISTER_PERF_TEST_INSTANCE(ALLOCATION_PATTERN, InstancedLargeBlockAllocatorTraits); \
    REGISTER_PERF_TEST_INSTANCE(ALLOCATION_PATTERN, HeapSmallBlockAllocatorTraits);      \
    REGISTER_PERF_TEST_INSTANCE(ALLOCATION_PATTERN, HeapLargeBlockAllocatorTraits);      \
  } while (0)

void RegisterTests() {
  REGISTER_PERF_TEST(AllocAndFree);
  REGISTER_PERF_TEST(RetainAndFreeOldest);
  REGISTER_PERF_TEST(RetainAndFreeRandom);
}

PERFTEST_CTOR(RegisterTests);

}  // namespace

#define DECLARE_STATIC_STORAGE(SLAB_TRAITS)                                                  \
  DECLARE_STATIC_SLAB_ALLOCATOR_STORAGE(                                                     \
      SLAB_TRAITS, (kLiveAllocLimitBytes / (fbl::SlabAllocator<SLAB_TRAITS>::AllocsPerSlab * \
                                            SLAB_TRAITS::kUserBufSize)) +                    \
                       1 /* Round up */);

DECLARE_STATIC_STORAGE(StaticSmallBlockAllocatorTraits);
DECLARE_STATIC_STORAGE(StaticLargeBlockAllocatorTraits);
