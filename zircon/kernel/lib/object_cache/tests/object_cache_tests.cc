// Copyright 2020 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <lib/fit/defer.h>
#include <lib/object_cache.h>
#include <lib/unittest/unittest.h>
#include <zircon/errors.h>
#include <zircon/time.h>

#include <fbl/alloc_checker.h>
#include <fbl/ref_counted.h>
#include <fbl/ref_ptr.h>
#include <fbl/vector.h>
#include <kernel/auto_preempt_disabler.h>
#include <kernel/event.h>
#include <kernel/thread.h>
#include <ktl/algorithm.h>
#include <ktl/array.h>
#include <ktl/atomic.h>
#include <ktl/move.h>
#include <ktl/optional.h>

#include <ktl/enforce.h>

namespace {

using object_cache::DefaultAllocator;
using object_cache::Deletable;
using object_cache::ObjectCache;
using object_cache::Option;
using object_cache::UniquePtr;

struct TestAllocator {
  static constexpr size_t kSlabSize = DefaultAllocator::kSlabSize;

  static zx::result<void*> Allocate() {
    allocated_slabs.fetch_add(1, ktl::memory_order_relaxed);
    return DefaultAllocator::Allocate();
  }

  static void Release(void* slab) {
    freed_slabs.fetch_add(1, ktl::memory_order_relaxed);
    DefaultAllocator::Release(slab);
  }

  static void CountObjectAllocation() { DefaultAllocator::CountObjectAllocation(); }
  static void CountObjectFree() { DefaultAllocator::CountObjectFree(); }
  static void CountSlabAllocation() { DefaultAllocator::CountSlabAllocation(); }
  static void CountSlabFree() { DefaultAllocator::CountSlabFree(); }

  static void ResetCounts() {
    allocated_slabs = 0;
    freed_slabs = 0;
  }

  inline static ktl::atomic<int> allocated_slabs{0};
  inline static ktl::atomic<int> freed_slabs{0};
};

struct TestObject {
  TestObject() { constructor_count++; }
  explicit TestObject(int32_t data) : data{data} { constructor_count++; }
  ~TestObject() { destructor_count++; }

  TestObject(const TestObject&) = default;
  TestObject& operator=(const TestObject&) = default;

  static constexpr int32_t kDefaultDataValue = 0xdeadbeef;
  int32_t data{kDefaultDataValue};
  uint8_t extra[256 - sizeof(data)];

  static void ResetCounts() {
    constructor_count = 0;
    destructor_count = 0;
  }

  inline static ktl::atomic<int> constructor_count{0};
  inline static ktl::atomic<int> destructor_count{0};
};

struct TestParent : fbl::RefCounted<TestParent> {
  TestParent() = default;
  ~TestParent() { destructor_count++; }

  struct Child : fbl::RefCounted<Child>, Deletable<Child, TestAllocator> {
    explicit Child(fbl::RefPtr<TestParent> parent) : parent{ktl::move(parent)} {}
    fbl::RefPtr<TestParent> parent;
  };

  zx::result<fbl::RefPtr<Child>> Allocate() {
    auto result = allocator.Allocate(fbl::RefPtr{this});
    if (result.is_error()) {
      return result.take_error();
    }
    return zx::ok(fbl::AdoptRef(result.value().release()));
  }

  using Allocator = ObjectCache<Child, Option::Single, TestAllocator>;
  static constexpr size_t kObjectsPerSlab = Allocator::objects_per_slab();
  static constexpr size_t kReserveSlabs = 1;
  Allocator allocator{kReserveSlabs};

  static void ResetCounts() { destructor_count = 0; }
  inline static ktl::atomic<int> destructor_count{0};
};

template <int retain_slabs, int slab_count, Option option>
bool ObjectCacheTests() {
  BEGIN_TEST;

  TestObject::ResetCounts();
  ASSERT_EQ(0, TestObject::constructor_count);
  ASSERT_EQ(0, TestObject::destructor_count);

  TestAllocator::ResetCounts();
  ASSERT_EQ(0, TestAllocator::allocated_slabs);
  ASSERT_EQ(0, TestAllocator::freed_slabs);

  const int objects_per_slab = ObjectCache<TestObject>::objects_per_slab();
  const int object_count = objects_per_slab * slab_count;

  fbl::Vector<UniquePtr<TestObject, TestAllocator>> objects;

  fbl::AllocChecker checker;
  objects.reserve(object_count, &checker);
  ASSERT_TRUE(checker.check());

  {
    ktl::optional<ObjectCache<TestObject, option, TestAllocator>> object_cache;
    if constexpr (option == Option::Single) {
      object_cache.emplace(retain_slabs);
    } else {
      auto result = ObjectCache<TestObject, Option::PerCpu, TestAllocator>::Create(retain_slabs);
      ASSERT_TRUE(result.is_ok());
      object_cache.emplace(ktl::move(result.value()));
    }

    // Stay on one CPU during the following tests to verify numeric properties of a single per-CPU
    // cache. Accounting for CPU migration during the tests would make them overly complicated for
    // little value.
    Thread* const current_thread = Thread::Current::Get();
    const cpu_mask_t original_affinity_mask = current_thread->GetCpuAffinity();

    const auto restore_affinity = fit::defer([original_affinity_mask, current_thread]() {
      current_thread->SetCpuAffinity(original_affinity_mask);
    });

    {
      AutoPreemptDisabler preempt_disable;
      const cpu_num_t current_cpu = arch_curr_cpu_num();
      current_thread->SetCpuAffinity(cpu_num_to_mask(current_cpu));
    }

    EXPECT_EQ(0u, object_cache->slab_count());
    EXPECT_EQ(0, TestObject::constructor_count);
    EXPECT_EQ(0, TestObject::destructor_count);

    for (int i = 0; i < object_count; i++) {
      auto result = object_cache->Allocate(i);
      ASSERT_TRUE(result.is_ok());
      EXPECT_EQ(i + 1, TestObject::constructor_count);
      EXPECT_EQ(0, TestObject::destructor_count);
      EXPECT_EQ(i, result->data);

      objects.push_back(ktl::move(result.value()), &checker);
      ASSERT_TRUE(checker.check());
    }
    EXPECT_EQ(slab_count, static_cast<int>(object_cache->slab_count()));
    EXPECT_EQ(slab_count, TestAllocator::allocated_slabs);
    EXPECT_EQ(0, TestAllocator::freed_slabs);

    // Release the first slab worth of objects.
    for (int i = 0; i < objects_per_slab; i++) {
      objects[i] = nullptr;
      EXPECT_EQ(object_count, TestObject::constructor_count);
      EXPECT_EQ(i + 1, TestObject::destructor_count);
    }
    EXPECT_EQ(slab_count, TestAllocator::allocated_slabs);
    EXPECT_EQ(slab_count <= retain_slabs ? 0 : 1, TestAllocator::freed_slabs);
    EXPECT_EQ(TestAllocator::allocated_slabs - TestAllocator::freed_slabs,
              static_cast<int>(object_cache->slab_count()));

    objects.reset();

    EXPECT_EQ(TestObject::constructor_count, TestObject::destructor_count);
    EXPECT_EQ(object_count, TestObject::destructor_count);
  }
  EXPECT_EQ(TestObject::constructor_count, TestObject::destructor_count);

  EXPECT_EQ(TestAllocator::allocated_slabs, TestAllocator::freed_slabs);

  END_TEST;
}

bool BackreferenceLifetimeTests() {
  BEGIN_TEST;

  TestParent::ResetCounts();
  EXPECT_EQ(0, TestParent::destructor_count);

  TestAllocator::ResetCounts();
  ASSERT_EQ(0, TestAllocator::allocated_slabs);
  ASSERT_EQ(0, TestAllocator::freed_slabs);

  fbl::AllocChecker checker;
  fbl::RefPtr parent = fbl::AdoptRef(new (&checker) TestParent{});
  ASSERT_TRUE(checker.check());

  auto result1 = parent->Allocate();
  auto result2 = parent->Allocate();
  auto result3 = parent->Allocate();

  parent.reset();
  EXPECT_EQ(0, TestParent::destructor_count);

  result1.value().reset();
  EXPECT_EQ(0, TestParent::destructor_count);

  result2.value().reset();
  EXPECT_EQ(0, TestParent::destructor_count);

  result3.value().reset();
  EXPECT_EQ(1, TestParent::destructor_count);

  EXPECT_EQ(TestAllocator::allocated_slabs, TestAllocator::freed_slabs);

  END_TEST;
}

bool BackreferenceLifetimeStressTests() {
  BEGIN_TEST;

  const size_t kStressTestIterations = 1000;
  for (size_t iteration = 0; iteration < kStressTestIterations; iteration++) {
    TestParent::ResetCounts();
    EXPECT_EQ(0, TestParent::destructor_count);

    TestAllocator::ResetCounts();
    ASSERT_EQ(0, TestAllocator::allocated_slabs);
    ASSERT_EQ(0, TestAllocator::freed_slabs);

    fbl::AllocChecker checker;
    fbl::RefPtr parent = fbl::AdoptRef(new (&checker) TestParent{});
    ASSERT_TRUE(checker.check());

    struct Control {
      TestParent* parent;
      AutounsignalEvent allocation_event{};
      Event deallocaton_event{};
      AutounsignalEvent finished_event{};
      ktl::atomic<bool> failed{false};
      ktl::atomic<int> count{0};
    };

    const size_t object_count = TestParent::kObjectsPerSlab;
    Control control{.parent = parent.get()};

    const auto thread_body = +[](void* arg) -> int {
      Control& control = *static_cast<Control*>(arg);

      fbl::AllocChecker checker;
      fbl::Vector<fbl::RefPtr<TestParent::Child>> objects;
      objects.reserve(object_count, &checker);
      if (!checker.check()) {
        control.failed = true;
        return ZX_ERR_NO_MEMORY;
      }

      for (size_t i = 0; i < object_count; i++) {
        auto result = control.parent->Allocate();
        if (result.is_error()) {
          control.failed = true;
          return result.error_value();
        }

        objects.push_back(ktl::move(result.value()), &checker);
        if (!checker.check()) {
          control.failed = true;
          return ZX_ERR_NO_MEMORY;
        }

        control.count++;
        Thread::Current::Yield();
      }

      control.allocation_event.Signal();
      control.deallocaton_event.Wait();

      for (size_t i = 0; i < object_count; i++) {
        objects[i] = nullptr;
        control.count--;
        Thread::Current::Yield();
      }

      control.finished_event.Signal();
      return ZX_OK;
    };

    const int thread_count = 8;
    ktl::array<Thread*, thread_count> threads;
    for (Thread*& thread : threads) {
      thread = Thread::Create("ObjectCacheRace", thread_body, &control, DEFAULT_PRIORITY);
    }
    // Resume threads in quick succession to get maximum overlap in the allocation
    // phase.
    for (Thread* thread : threads) {
      thread->Resume();
    }

    // Wait for each worker to finish allocating children.
    while (control.count != thread_count * object_count && !control.failed) {
      control.allocation_event.Wait();
    }
    EXPECT_FALSE(control.failed);
    EXPECT_EQ(thread_count, TestAllocator::allocated_slabs);
    EXPECT_EQ(0, TestAllocator::freed_slabs);

    // Workers should not touch the parent object after they finish allocating
    // children.
    control.parent = nullptr;
    EXPECT_EQ(0, TestParent::destructor_count);

    // Children should maintain the lifetime of the parent.
    parent = nullptr;
    EXPECT_EQ(0, TestParent::destructor_count);

    control.deallocaton_event.Signal();

    // Wait for each worker to finish freeing children.
    while (control.count != 0 && !control.failed) {
      control.finished_event.Wait();
    }
    EXPECT_FALSE(control.failed);
    EXPECT_EQ(1, TestParent::destructor_count);
    EXPECT_EQ(thread_count, TestAllocator::allocated_slabs);
    EXPECT_EQ(thread_count, TestAllocator::freed_slabs);

    for (Thread* thread : threads) {
      int retcode;
      thread->Join(&retcode, ZX_TIME_INFINITE);
      EXPECT_EQ(ZX_OK, retcode);
    }
  }

  END_TEST;
}

}  // namespace

UNITTEST_START_TESTCASE(object_cache_tests)
UNITTEST("object_cache_tests<0, 2, Single>", (ObjectCacheTests<0, 2, Option::Single>))
UNITTEST("object_cache_tests<1, 2, Single>", (ObjectCacheTests<1, 2, Option::Single>))
UNITTEST("object_cache_tests<2, 2, Single>", (ObjectCacheTests<2, 2, Option::Single>))
UNITTEST("object_cache_tests<0, 2, PerCpu>", (ObjectCacheTests<0, 2, Option::PerCpu>))
UNITTEST("object_cache_tests<1, 2, PerCpu>", (ObjectCacheTests<1, 2, Option::PerCpu>))
UNITTEST("object_cache_tests<2, 2, PerCpu>", (ObjectCacheTests<2, 2, Option::PerCpu>))
UNITTEST("backreference_lifetime_tests", BackreferenceLifetimeTests)
UNITTEST("backreference_lifetime_stress_tests", BackreferenceLifetimeStressTests)
UNITTEST_END_TESTCASE(object_cache_tests, "object_cache", "object_cache tests")
