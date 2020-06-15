// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <algorithm>
#include <memory>
#include <utility>

#include <fbl/alloc_checker.h>
#include <fbl/intrusive_double_list.h>
#include <fbl/ref_counted.h>
#include <fbl/ref_ptr.h>
#include <fbl/slab_allocator.h>
#include <zxtest/zxtest.h>

namespace {

enum class ConstructType {
  DEFAULT,
  LVALUE_REF,
  RVALUE_REF,
  L_THEN_R_REF,
};

// Test objects.
class TestBase {
 public:
  // Various constructor forms to exercise SlabAllocator::New
  TestBase() : ctype_(ConstructType::DEFAULT) { ++allocated_obj_count_; }
  explicit TestBase(const size_t&) : ctype_(ConstructType::LVALUE_REF) { ++allocated_obj_count_; }
  explicit TestBase(size_t&&) : ctype_(ConstructType::RVALUE_REF) { ++allocated_obj_count_; }
  explicit TestBase(const size_t&, size_t&&) : ctype_(ConstructType::L_THEN_R_REF) {
    ++allocated_obj_count_;
  }

  virtual ~TestBase() { --allocated_obj_count_; }

  ConstructType ctype() const { return ctype_; }

  static void Reset() { allocated_obj_count_ = 0; }
  static size_t allocated_obj_count() { return allocated_obj_count_; }
  const uint8_t* payload() const { return payload_; }

 private:
  const ConstructType ctype_;
  uint8_t payload_[13];  // 13 bytes, just to make the size/alignment strange

  static size_t allocated_obj_count_;
};

// Static storage.
size_t TestBase::allocated_obj_count_;

template <typename SATraits, typename = void>
struct ReleaseHelper;

template <typename SATraits>
struct ReleaseHelper<SATraits,
                     typename std::enable_if<(SATraits::PtrTraits::IsManaged == false) &&
                                             (SATraits::AllocatorFlavor ==
                                              fbl::SlabAllocatorFlavor::INSTANCED)>::type> {
  static void ReleasePtr(fbl::SlabAllocator<SATraits>& allocator, typename SATraits::PtrType& ptr) {
    // Instanced slab allocators should static_assert if you attempt to
    // expand their Delete method.
#if TEST_WILL_NOT_COMPILE || 0
    allocator.Delete(ptr);
#else
    delete ptr;
#endif
  }
};

template <typename SATraits>
struct ReleaseHelper<SATraits,
                     typename std::enable_if<(SATraits::PtrTraits::IsManaged == false) &&
                                             (SATraits::AllocatorFlavor ==
                                              fbl::SlabAllocatorFlavor::MANUAL_DELETE)>::type> {
  static void ReleasePtr(fbl::SlabAllocator<SATraits>& allocator, typename SATraits::PtrType& ptr) {
    // SlabAllocated<> objects which come from MANUAL_DELETE flavors of slab
    // allocators should have their delete operator protected in order to
    // prevent someone from calling delete on the object.
#if TEST_WILL_NOT_COMPILE || 0
    delete ptr;
#else
    allocator.Delete(ptr);
#endif
  }
};

template <typename SATraits>
struct ReleaseHelper<SATraits, typename std::enable_if<(SATraits::PtrTraits::IsManaged == true) &&
                                                       (SATraits::AllocatorFlavor !=
                                                        fbl::SlabAllocatorFlavor::STATIC)>::type> {
  static void ReleasePtr(fbl::SlabAllocator<SATraits>&, typename SATraits::PtrType& ptr) {
    ptr = nullptr;
  }
};

template <typename SATraits>
struct ReleaseHelper<SATraits, typename std::enable_if<(SATraits::PtrTraits::IsManaged == false) &&
                                                       (SATraits::AllocatorFlavor ==
                                                        fbl::SlabAllocatorFlavor::STATIC)>::type> {
  static void ReleasePtr(typename SATraits::PtrType& ptr) { delete ptr; }
};

template <typename SATraits>
struct ReleaseHelper<SATraits, typename std::enable_if<(SATraits::PtrTraits::IsManaged == true) &&
                                                       (SATraits::AllocatorFlavor ==
                                                        fbl::SlabAllocatorFlavor::STATIC)>::type> {
  static void ReleasePtr(typename SATraits::PtrType& ptr) { ptr = nullptr; }
};

// Traits which define the various test flavors.
template <typename LockType,
          fbl::SlabAllocatorFlavor AllocatorFlavor = fbl::SlabAllocatorFlavor::INSTANCED,
          fbl::SlabAllocatorOptions Options = fbl::SlabAllocatorOptions::None>
struct UnmanagedTestTraits {
  class ObjType;
  using PtrType = ObjType*;
  using AllocTraits = fbl::SlabAllocatorTraits<PtrType, 1024, LockType, AllocatorFlavor, Options>;
  using AllocatorType = fbl::SlabAllocator<AllocTraits>;
  using RefList = fbl::DoublyLinkedList<PtrType>;

  class ObjType : public TestBase,
                  public fbl::SlabAllocated<AllocTraits>,
                  public fbl::DoublyLinkedListable<PtrType> {
   public:
    ObjType() : TestBase() {}
    explicit ObjType(const size_t& val) : TestBase(val) {}
    explicit ObjType(size_t&& val) : TestBase(std::move(val)) {}
    explicit ObjType(const size_t& a, size_t&& b) : TestBase(a, std::move(b)) {}
  };

  static constexpr size_t MaxSlabs = 4;
  static constexpr bool IsManaged = false;
  static constexpr size_t MaxAllocs(size_t slabs) { return AllocatorType::AllocsPerSlab * slabs; }
};

template <typename LockType, fbl::SlabAllocatorOptions Options = fbl::SlabAllocatorOptions::None>
struct UniquePtrTestTraits {
  class ObjType;
  using PtrType = std::unique_ptr<ObjType>;
  using AllocTraits = fbl::SlabAllocatorTraits<PtrType, 1024, LockType,
                                               fbl::SlabAllocatorFlavor::INSTANCED, Options>;
  using AllocatorType = fbl::SlabAllocator<AllocTraits>;
  using RefList = fbl::DoublyLinkedList<PtrType>;

  class ObjType : public TestBase,
                  public fbl::SlabAllocated<AllocTraits>,
                  public fbl::DoublyLinkedListable<PtrType> {
   public:
    ObjType() : TestBase() {}
    explicit ObjType(const size_t& val) : TestBase(val) {}
    explicit ObjType(size_t&& val) : TestBase(std::move(val)) {}
    explicit ObjType(const size_t& a, size_t&& b) : TestBase(a, std::move(b)) {}
  };

  static constexpr size_t MaxSlabs = 4;
  static constexpr bool IsManaged = true;
  static constexpr size_t MaxAllocs(size_t slabs) { return AllocatorType::AllocsPerSlab * slabs; }
};

template <typename LockType, fbl::SlabAllocatorOptions Options = fbl::SlabAllocatorOptions::None>
struct RefPtrTestTraits {
  class ObjType;
  using PtrType = fbl::RefPtr<ObjType>;
  using AllocTraits = fbl::SlabAllocatorTraits<PtrType, 1024, LockType,
                                               fbl::SlabAllocatorFlavor::INSTANCED, Options>;
  using AllocatorType = fbl::SlabAllocator<AllocTraits>;
  using RefList = fbl::DoublyLinkedList<PtrType>;

  class ObjType : public TestBase,
                  public fbl::RefCounted<ObjType>,
                  public fbl::SlabAllocated<AllocTraits>,
                  public fbl::DoublyLinkedListable<PtrType> {
   public:
    ObjType() : TestBase() {}
    explicit ObjType(const size_t& val) : TestBase(val) {}
    explicit ObjType(size_t&& val) : TestBase(std::move(val)) {}
    explicit ObjType(const size_t& a, size_t&& b) : TestBase(a, std::move(b)) {}
  };

  static constexpr size_t MaxSlabs = 4;
  static constexpr bool IsManaged = true;
  static constexpr size_t MaxAllocs(size_t slabs) { return AllocatorType::AllocsPerSlab * slabs; }
};

template <typename, bool>
struct ObjCounterHelper;

template <typename SA>
struct ObjCounterHelper<SA, true> {
  static bool CheckObjCount(const SA& allocator, size_t expected) {
    return (allocator.obj_count() == expected);
  }
  static bool CheckMaxObjCount(const SA& allocator, size_t expected) {
    return (allocator.max_obj_count() == expected);
  }
  static void ResetMaxObjCount(SA* allocator) { allocator->ResetMaxObjCount(); }
  static bool StaticCheckObjCount(size_t expected) { return (SA::obj_count() == expected); }
  static bool StaticCheckMaxObjCount(size_t expected) { return (SA::max_obj_count() == expected); }
  static void StaticResetMaxObjCount() { SA::ResetMaxObjCount(); }
};

template <typename SA>
struct ObjCounterHelper<SA, false> {
  static bool CheckObjCount(const SA&, size_t) { return true; }
  static bool CheckMaxObjCount(const SA&, size_t) { return true; }
  static void ResetMaxObjCount(SA*) {}
  static bool StaticCheckObjCount(size_t) { return true; }
  static bool StaticCheckMaxObjCount(size_t) { return true; }
  static void StaticResetMaxObjCount() {}
};

template <typename Traits>
void do_slab_test(typename Traits::AllocatorType& allocator, size_t test_allocs) {
  const size_t MAX_ALLOCS = Traits::MaxAllocs(allocator.max_slabs());
  typename Traits::RefList ref_list;
  const bool ENB_OBJ_COUNT =
      Traits::AllocTraits::Options & fbl::SlabAllocatorOptions::EnableObjectCount;
  using AllocatorType = typename Traits::AllocatorType;

  ObjCounterHelper<AllocatorType, ENB_OBJ_COUNT>::ResetMaxObjCount(&allocator);
  bool res = ObjCounterHelper<AllocatorType, ENB_OBJ_COUNT>::CheckObjCount(allocator, 0);
  EXPECT_TRUE(res);
  res = ObjCounterHelper<AllocatorType, ENB_OBJ_COUNT>::CheckMaxObjCount(allocator, 0);
  EXPECT_TRUE(res);

  // Allocate up to the test limit.
  for (size_t i = 0; i < test_allocs; ++i) {
    typename Traits::PtrType ptr;

    EXPECT_EQ(std::min(i, MAX_ALLOCS), TestBase::allocated_obj_count());
    res = ObjCounterHelper<AllocatorType, ENB_OBJ_COUNT>::CheckObjCount(
        allocator, TestBase::allocated_obj_count());
    EXPECT_TRUE(res);
    res = ObjCounterHelper<AllocatorType, ENB_OBJ_COUNT>::CheckMaxObjCount(
        allocator, TestBase::allocated_obj_count());
    EXPECT_TRUE(res);

    // Allocate the object; exercise the various constructors
    switch (i % 4) {
      case 0:
        ptr = allocator.New();
        break;
      case 1:
        ptr = allocator.New(i);
        break;
      case 2:
        ptr = allocator.New(std::move(i));
        break;
      case 3:
        ptr = allocator.New(i, std::move(i));
        break;
    }

    if (i < MAX_ALLOCS) {
      ASSERT_NOT_NULL(ptr, "Allocation failed when it should not have!");
      ref_list.push_front(std::move(ptr));
    } else {
      ASSERT_NULL(ptr, "Allocation succeeded when it should not have!");
    }

    EXPECT_EQ(std::min(i + 1, MAX_ALLOCS), TestBase::allocated_obj_count());
    res = ObjCounterHelper<AllocatorType, ENB_OBJ_COUNT>::CheckObjCount(
        allocator, TestBase::allocated_obj_count());
    EXPECT_TRUE(res);
    res = ObjCounterHelper<AllocatorType, ENB_OBJ_COUNT>::CheckMaxObjCount(
        allocator, TestBase::allocated_obj_count());
    EXPECT_TRUE(res);
  }

  // Now remove and de-allocate.
  size_t max_obj_count = TestBase::allocated_obj_count();
  size_t i;
  for (i = 0; !ref_list.is_empty(); ++i) {
    auto ptr = ref_list.pop_back();

    ASSERT_NOT_NULL(ptr, "nullptr in ref list!  This should be impossible.");
    EXPECT_EQ(std::min(test_allocs, MAX_ALLOCS) - i, TestBase::allocated_obj_count());
    res = ObjCounterHelper<AllocatorType, ENB_OBJ_COUNT>::CheckObjCount(
        allocator, TestBase::allocated_obj_count());
    EXPECT_TRUE(res);
    res =
        ObjCounterHelper<AllocatorType, ENB_OBJ_COUNT>::CheckMaxObjCount(allocator, max_obj_count);
    EXPECT_TRUE(res);

    switch (i % 4) {
      case 0:
        EXPECT_EQ(ConstructType::DEFAULT, ptr->ctype());
        break;
      case 1:
        EXPECT_EQ(ConstructType::LVALUE_REF, ptr->ctype());
        break;
      case 2:
        EXPECT_EQ(ConstructType::RVALUE_REF, ptr->ctype());
        break;
      case 3:
        EXPECT_EQ(ConstructType::L_THEN_R_REF, ptr->ctype());
        break;
    }

    // Release the reference (how this gets done depends on allocator flavor and pointer type)
    ReleaseHelper<typename Traits::AllocTraits>::ReleasePtr(allocator, ptr);

    if (i % 2 == 1) {
      ObjCounterHelper<AllocatorType, ENB_OBJ_COUNT>::ResetMaxObjCount(&allocator);
      max_obj_count = TestBase::allocated_obj_count();
    }
    res =
        ObjCounterHelper<AllocatorType, ENB_OBJ_COUNT>::CheckMaxObjCount(allocator, max_obj_count);
    EXPECT_TRUE(res);
  }

  EXPECT_EQ(std::min(test_allocs, MAX_ALLOCS), i);
  res = ObjCounterHelper<AllocatorType, ENB_OBJ_COUNT>::CheckObjCount(allocator, 0);
  EXPECT_TRUE(res);
  res = ObjCounterHelper<AllocatorType, ENB_OBJ_COUNT>::CheckMaxObjCount(allocator, i % 2);
  EXPECT_TRUE(res);
  ObjCounterHelper<AllocatorType, ENB_OBJ_COUNT>::ResetMaxObjCount(&allocator);
  res = ObjCounterHelper<AllocatorType, ENB_OBJ_COUNT>::CheckMaxObjCount(allocator, 0);
  EXPECT_TRUE(res);

#if TEST_WILL_NOT_COMPILE || 0
  allocator.obj_count();
#endif
#if TEST_WILL_NOT_COMPILE || 0
  allocator.max_obj_count();
#endif
#if TEST_WILL_NOT_COMPILE || 0
  allocator.ResetMaxObjCount();
#endif
}

template <typename Traits, size_t SlabCount = Traits::MaxSlabs>
void slab_test() {
  typename Traits::AllocatorType allocator(SlabCount);

  TestBase::Reset();

  {
    auto fn = [&allocator]() { do_slab_test<Traits>(allocator, 1); };
    ASSERT_NO_FAILURES(fn(), "Single allocator test failed");
  }

  {
    auto fn = [&allocator]() { do_slab_test<Traits>(allocator, Traits::MaxAllocs(SlabCount) / 2); };
    ASSERT_NO_FAILURES(fn(), "1/2 capacity allocator test failed");
  }

  {
    auto fn = [&allocator]() { do_slab_test<Traits>(allocator, Traits::MaxAllocs(SlabCount) + 4); };
    ASSERT_NO_FAILURES(fn(), "Over-capacity allocator test failed");
  }
}

template <typename LockType, fbl::SlabAllocatorOptions Options = fbl::SlabAllocatorOptions::None>
struct StaticUnmanagedTestTraits {
  class ObjType;
  using PtrType = ObjType*;
  using AllocTraits = fbl::StaticSlabAllocatorTraits<PtrType, 1024, LockType, Options>;
  using AllocatorType = fbl::SlabAllocator<AllocTraits>;
  using RefList = fbl::DoublyLinkedList<PtrType>;

  class ObjType : public TestBase,
                  public fbl::SlabAllocated<AllocTraits>,
                  public fbl::DoublyLinkedListable<PtrType> {
   public:
    ObjType() : TestBase() {}
    explicit ObjType(const size_t& val) : TestBase(val) {}
    explicit ObjType(size_t&& val) : TestBase(std::move(val)) {}
    explicit ObjType(const size_t& a, size_t&& b) : TestBase(a, std::move(b)) {}
  };

  static size_t MaxAllocs() { return AllocatorType::AllocsPerSlab * AllocatorType::max_slabs(); }

  static constexpr size_t MaxSlabs = 4;
  static constexpr bool IsManaged = false;
};
template <typename LockType>
using StaticCountedUnmanagedTestTraits =
    StaticUnmanagedTestTraits<LockType, fbl::SlabAllocatorOptions::EnableObjectCount>;

template <typename LockType, fbl::SlabAllocatorOptions Options = fbl::SlabAllocatorOptions::None>
struct StaticUniquePtrTestTraits {
  class ObjType;
  using PtrType = std::unique_ptr<ObjType>;
  using AllocTraits = fbl::StaticSlabAllocatorTraits<PtrType, 1024, LockType, Options>;
  using AllocatorType = fbl::SlabAllocator<AllocTraits>;
  using RefList = fbl::DoublyLinkedList<PtrType>;

  class ObjType : public TestBase,
                  public fbl::SlabAllocated<AllocTraits>,
                  public fbl::DoublyLinkedListable<PtrType> {
   public:
    ObjType() : TestBase() {}
    explicit ObjType(const size_t& val) : TestBase(val) {}
    explicit ObjType(size_t&& val) : TestBase(std::move(val)) {}
    explicit ObjType(const size_t& a, size_t&& b) : TestBase(a, std::move(b)) {}
  };

  static size_t MaxAllocs() { return AllocatorType::AllocsPerSlab * AllocatorType::max_slabs(); }

  static constexpr size_t MaxSlabs = 4;
  static constexpr bool IsManaged = false;
};

template <typename LockType>
using StaticCountedUniquePtrTestTraits =
    StaticUniquePtrTestTraits<LockType, fbl::SlabAllocatorOptions::EnableObjectCount>;

template <typename LockType, fbl::SlabAllocatorOptions Options = fbl::SlabAllocatorOptions::None>
struct StaticRefPtrTestTraits {
  class ObjType;
  using PtrType = fbl::RefPtr<ObjType>;
  using AllocTraits = fbl::StaticSlabAllocatorTraits<PtrType, 1024, LockType, Options>;
  using AllocatorType = fbl::SlabAllocator<AllocTraits>;
  using RefList = fbl::DoublyLinkedList<PtrType>;

  class ObjType : public TestBase,
                  public fbl::SlabAllocated<AllocTraits>,
                  public fbl::RefCounted<ObjType>,
                  public fbl::DoublyLinkedListable<PtrType> {
   public:
    ObjType() : TestBase() {}
    explicit ObjType(const size_t& val) : TestBase(val) {}
    explicit ObjType(size_t&& val) : TestBase(std::move(val)) {}
    explicit ObjType(const size_t& a, size_t&& b) : TestBase(a, std::move(b)) {}
  };

  static constexpr size_t MaxSlabs = 4;
  static constexpr bool IsManaged = false;

  static size_t MaxAllocs() { return AllocatorType::AllocsPerSlab * AllocatorType::max_slabs(); }
};

template <typename LockType>
using StaticCountedRefPtrTestTraits =
    StaticRefPtrTestTraits<LockType, fbl::SlabAllocatorOptions::EnableObjectCount>;

template <typename Traits>
void do_static_slab_test(size_t test_allocs) {
  const bool ENB_OBJ_COUNT =
      Traits::AllocTraits::Options & fbl::SlabAllocatorOptions::EnableObjectCount;
  using AllocatorType = typename Traits::AllocatorType;

  const size_t MAX_ALLOCS = Traits::MaxAllocs();
  typename Traits::RefList ref_list;
  ObjCounterHelper<AllocatorType, ENB_OBJ_COUNT>::StaticResetMaxObjCount();
  bool res = ObjCounterHelper<AllocatorType, ENB_OBJ_COUNT>::StaticCheckObjCount(0);
  EXPECT_TRUE(res);
  res = ObjCounterHelper<AllocatorType, ENB_OBJ_COUNT>::StaticCheckMaxObjCount(0);
  EXPECT_TRUE(res);

  // Allocate up to the test limit.
  for (size_t i = 0; i < test_allocs; ++i) {
    typename Traits::PtrType ptr;

    EXPECT_EQ(std::min(i, MAX_ALLOCS), TestBase::allocated_obj_count());
    res = ObjCounterHelper<AllocatorType, ENB_OBJ_COUNT>::StaticCheckObjCount(
        TestBase::allocated_obj_count());
    EXPECT_TRUE(res);
    res = ObjCounterHelper<AllocatorType, ENB_OBJ_COUNT>::StaticCheckMaxObjCount(
        TestBase::allocated_obj_count());
    EXPECT_TRUE(res);

    // Allocate the object; exercise the various constructors
    switch (i % 4) {
      case 0:
        ptr = AllocatorType::New();
        break;
      case 1:
        ptr = AllocatorType::New(i);
        break;
      case 2:
        ptr = AllocatorType::New(std::move(i));
        break;
      case 3:
        ptr = AllocatorType::New(i, std::move(i));
        break;
    }

    if (i < MAX_ALLOCS) {
      ASSERT_NOT_NULL(ptr, "Allocation failed when it should not have!");
      ref_list.push_front(std::move(ptr));
    } else {
      ASSERT_NULL(ptr, "Allocation succeeded when it should not have!");
    }

    EXPECT_EQ(std::min(i + 1, MAX_ALLOCS), TestBase::allocated_obj_count());
    res = ObjCounterHelper<AllocatorType, ENB_OBJ_COUNT>::StaticCheckObjCount(
        TestBase::allocated_obj_count());
    EXPECT_TRUE(res);
    res = ObjCounterHelper<AllocatorType, ENB_OBJ_COUNT>::StaticCheckMaxObjCount(
        TestBase::allocated_obj_count());
    EXPECT_TRUE(res);
  }

  // Now remove and de-allocate.
  size_t max_obj_count = TestBase::allocated_obj_count();
  size_t i;
  for (i = 0; !ref_list.is_empty(); ++i) {
    auto ptr = ref_list.pop_back();

    ASSERT_NOT_NULL(ptr, "nullptr in ref list!  This should be impossible.");
    EXPECT_EQ(std::min(test_allocs, MAX_ALLOCS) - i, TestBase::allocated_obj_count());
    res = ObjCounterHelper<AllocatorType, ENB_OBJ_COUNT>::StaticCheckObjCount(
        TestBase::allocated_obj_count());
    EXPECT_TRUE(res);
    res = ObjCounterHelper<AllocatorType, ENB_OBJ_COUNT>::StaticCheckMaxObjCount(max_obj_count);
    EXPECT_TRUE(res);

    switch (i % 4) {
      case 0:
        EXPECT_EQ(ConstructType::DEFAULT, ptr->ctype());
        break;
      case 1:
        EXPECT_EQ(ConstructType::LVALUE_REF, ptr->ctype());
        break;
      case 2:
        EXPECT_EQ(ConstructType::RVALUE_REF, ptr->ctype());
        break;
      case 3:
        EXPECT_EQ(ConstructType::L_THEN_R_REF, ptr->ctype());
        break;
    }

    // Release the reference (how this gets done depends on allocator flavor and pointer type)
    ReleaseHelper<typename Traits::AllocTraits>::ReleasePtr(ptr);
    if (i % 2 == 1) {
      ObjCounterHelper<AllocatorType, ENB_OBJ_COUNT>::StaticResetMaxObjCount();
      max_obj_count = TestBase::allocated_obj_count();
    }
    res = ObjCounterHelper<AllocatorType, ENB_OBJ_COUNT>::StaticCheckMaxObjCount(max_obj_count);
    EXPECT_TRUE(res);
  }

  EXPECT_EQ(std::min(test_allocs, MAX_ALLOCS), i);
  res = ObjCounterHelper<AllocatorType, ENB_OBJ_COUNT>::StaticCheckObjCount(0);
  EXPECT_TRUE(res);
  res = ObjCounterHelper<AllocatorType, ENB_OBJ_COUNT>::StaticCheckMaxObjCount(i % 2);
  EXPECT_TRUE(res);
  ObjCounterHelper<AllocatorType, ENB_OBJ_COUNT>::StaticResetMaxObjCount();
  res = ObjCounterHelper<AllocatorType, ENB_OBJ_COUNT>::StaticCheckMaxObjCount(0);
  EXPECT_TRUE(res);
#if TEST_WILL_NOT_COMPILE || 0
  AllocatorType::obj_count();
#endif
#if TEST_WILL_NOT_COMPILE || 0
  AllocatorType::max_obj_count();
#endif
#if TEST_WILL_NOT_COMPILE || 0
  AllocatorType::ResetMaxObjCount();
#endif
}

template <typename Traits>
void static_slab_test() {
  TestBase::Reset();

  ASSERT_NO_FAILURES(do_static_slab_test<Traits>(1), "Single allocator test failed");

  ASSERT_NO_FAILURES(do_static_slab_test<Traits>(Traits::MaxAllocs() / 2),
                     "1/2 capacity allocator test failed");

  ASSERT_NO_FAILURES(do_static_slab_test<Traits>(Traits::MaxAllocs() + 4),
                     "Over-capacity allocator test failed");
}
}  // namespace

using MutexLock = ::fbl::Mutex;
using NullLock = ::fbl::NullLock;

DECLARE_STATIC_SLAB_ALLOCATOR_STORAGE(StaticUnmanagedTestTraits<MutexLock>::AllocTraits, 1);
DECLARE_STATIC_SLAB_ALLOCATOR_STORAGE(StaticUniquePtrTestTraits<MutexLock>::AllocTraits, 1);
DECLARE_STATIC_SLAB_ALLOCATOR_STORAGE(StaticRefPtrTestTraits<MutexLock>::AllocTraits, 1);

DECLARE_STATIC_SLAB_ALLOCATOR_STORAGE(StaticUnmanagedTestTraits<NullLock>::AllocTraits, 1);
DECLARE_STATIC_SLAB_ALLOCATOR_STORAGE(StaticUniquePtrTestTraits<NullLock>::AllocTraits, 1);
DECLARE_STATIC_SLAB_ALLOCATOR_STORAGE(StaticRefPtrTestTraits<NullLock>::AllocTraits, 1);

DECLARE_STATIC_SLAB_ALLOCATOR_STORAGE(StaticCountedUnmanagedTestTraits<MutexLock>::AllocTraits, 1);
DECLARE_STATIC_SLAB_ALLOCATOR_STORAGE(StaticCountedUniquePtrTestTraits<MutexLock>::AllocTraits, 1);
DECLARE_STATIC_SLAB_ALLOCATOR_STORAGE(StaticCountedRefPtrTestTraits<MutexLock>::AllocTraits, 1);

DECLARE_STATIC_SLAB_ALLOCATOR_STORAGE(StaticCountedUnmanagedTestTraits<NullLock>::AllocTraits, 1);
DECLARE_STATIC_SLAB_ALLOCATOR_STORAGE(StaticCountedUniquePtrTestTraits<NullLock>::AllocTraits, 1);
DECLARE_STATIC_SLAB_ALLOCATOR_STORAGE(StaticCountedRefPtrTestTraits<NullLock>::AllocTraits, 1);

#define MAKE_TEST(_name, ...)      \
  TEST(SlabAllocatorTest, _name) { \
    auto fn = __VA_ARGS__;         \
    ASSERT_NO_FAILURES(fn());      \
  }

MAKE_TEST(UnmanagedSingleSlabMutex, slab_test<UnmanagedTestTraits<MutexLock>, 1>)
MAKE_TEST(UnmanagedMultiSlabMutex, slab_test<UnmanagedTestTraits<MutexLock>>)
MAKE_TEST(UniquePtrSingleSlabMutex, slab_test<UniquePtrTestTraits<MutexLock>, 1>)
MAKE_TEST(UniquePtrMultiSlabMutex, slab_test<UniquePtrTestTraits<MutexLock>>)
MAKE_TEST(RefPtrSingleSlabMutex, slab_test<RefPtrTestTraits<MutexLock>, 1>)
MAKE_TEST(RefPtrMultiSlabMutex, slab_test<RefPtrTestTraits<MutexLock>>)
MAKE_TEST(UnmanagedSingleSlabUnlock, slab_test<UnmanagedTestTraits<NullLock>, 1>)
MAKE_TEST(UnmanagedMultiSlabUnlock, slab_test<UnmanagedTestTraits<NullLock>>)
MAKE_TEST(UniquePtrSingleSlabUnlock, slab_test<UniquePtrTestTraits<NullLock>, 1>)
MAKE_TEST(UniquePtrMultiSlabUnlock, slab_test<UniquePtrTestTraits<NullLock>>)
MAKE_TEST(RefPtrSingleSlabUnlock, slab_test<RefPtrTestTraits<NullLock>, 1>)
MAKE_TEST(RefPtrMultiSlabUnlock, slab_test<RefPtrTestTraits<NullLock>>)
MAKE_TEST(ManualDeleteUnmanagedMutex,
          slab_test<UnmanagedTestTraits<MutexLock, fbl::SlabAllocatorFlavor::MANUAL_DELETE>>)
MAKE_TEST(ManualDeleteUnmanagedUnlock,
          slab_test<UnmanagedTestTraits<NullLock, fbl::SlabAllocatorFlavor::MANUAL_DELETE>>)
MAKE_TEST(StaticUnmanagedUnlock, static_slab_test<StaticUnmanagedTestTraits<NullLock>>)
MAKE_TEST(StaticUniquePtrUnlock, static_slab_test<StaticUniquePtrTestTraits<NullLock>>)
MAKE_TEST(StaticRefPtrUnlock, static_slab_test<StaticRefPtrTestTraits<NullLock>>)
MAKE_TEST(StaticUnmanagedMutex, static_slab_test<StaticUnmanagedTestTraits<MutexLock>>)
MAKE_TEST(StaticUniquePtrMutex, static_slab_test<StaticUniquePtrTestTraits<MutexLock>>)
MAKE_TEST(StaticRefPtrMutex, static_slab_test<StaticRefPtrTestTraits<MutexLock>>)
MAKE_TEST(CountedUnmanagedSingleSlabMutex,
          slab_test<UnmanagedTestTraits<MutexLock, fbl::SlabAllocatorFlavor::INSTANCED,
                                        fbl::SlabAllocatorOptions::EnableObjectCount>,
                    1>)
MAKE_TEST(CountedUnmanagedMultiSlabMutex,
          slab_test<UnmanagedTestTraits<MutexLock, fbl::SlabAllocatorFlavor::INSTANCED,
                                        fbl::SlabAllocatorOptions::EnableObjectCount>>)
MAKE_TEST(
    CountedUniquePtrSingleSlabMutex,
    slab_test<UniquePtrTestTraits<MutexLock, fbl::SlabAllocatorOptions::EnableObjectCount>, 1>)
MAKE_TEST(CountedUniquePtrMultiSlabMutex,
          slab_test<UniquePtrTestTraits<MutexLock, fbl::SlabAllocatorOptions::EnableObjectCount>>)
MAKE_TEST(CountedRefPtrSingleSlabMutex,
          slab_test<RefPtrTestTraits<MutexLock, fbl::SlabAllocatorOptions::EnableObjectCount>, 1>)
MAKE_TEST(CountedRefPtrMultiSlabMutex,
          slab_test<RefPtrTestTraits<MutexLock, fbl::SlabAllocatorOptions::EnableObjectCount>>)
MAKE_TEST(CountedUnmanagedSingleSlabUnlock,
          slab_test<UnmanagedTestTraits<NullLock, fbl::SlabAllocatorFlavor::INSTANCED,
                                        fbl::SlabAllocatorOptions::EnableObjectCount>,
                    1>)
MAKE_TEST(CountedUnmanagedMultiSlabUnlock,
          slab_test<UnmanagedTestTraits<NullLock, fbl::SlabAllocatorFlavor::INSTANCED,
                                        fbl::SlabAllocatorOptions::EnableObjectCount>>)
MAKE_TEST(CountedUniquePtrSingleSlabUnlock,
          slab_test<UniquePtrTestTraits<NullLock, fbl::SlabAllocatorOptions::EnableObjectCount>, 1>)
MAKE_TEST(CountedUniquePtrMultiSlabUnlock,
          slab_test<UniquePtrTestTraits<NullLock, fbl::SlabAllocatorOptions::EnableObjectCount>>)
MAKE_TEST(CountedRefPtrSingleSlabUnlock,
          slab_test<RefPtrTestTraits<NullLock, fbl::SlabAllocatorOptions::EnableObjectCount>, 1>)
MAKE_TEST(CountedRefPtrMultiSlabUnlock,
          slab_test<RefPtrTestTraits<NullLock, fbl::SlabAllocatorOptions::EnableObjectCount>>)
MAKE_TEST(CountedManualDeleteUnmanagedMutex,
          slab_test<UnmanagedTestTraits<MutexLock, fbl::SlabAllocatorFlavor::MANUAL_DELETE,
                                        fbl::SlabAllocatorOptions::EnableObjectCount>>)
MAKE_TEST(CountedManualDeleteUnmanagedUnlock,
          slab_test<UnmanagedTestTraits<NullLock, fbl::SlabAllocatorFlavor::MANUAL_DELETE,
                                        fbl::SlabAllocatorOptions::EnableObjectCount>>)
MAKE_TEST(CountedStaticUnmanagedMutex,
          static_slab_test<
              StaticUnmanagedTestTraits<MutexLock, fbl::SlabAllocatorOptions::EnableObjectCount>>)
MAKE_TEST(CountedStaticUniquePtrMutex,
          static_slab_test<
              StaticUniquePtrTestTraits<MutexLock, fbl::SlabAllocatorOptions::EnableObjectCount>>)
MAKE_TEST(CountedStaticRefPtrMutex,
          static_slab_test<
              StaticRefPtrTestTraits<MutexLock, fbl::SlabAllocatorOptions::EnableObjectCount>>)
MAKE_TEST(CountedStaticUnmanagedUnlock,
          static_slab_test<
              StaticUnmanagedTestTraits<NullLock, fbl::SlabAllocatorOptions::EnableObjectCount>>)
MAKE_TEST(CountedStaticUniquePtrUnlock,
          static_slab_test<
              StaticUniquePtrTestTraits<NullLock, fbl::SlabAllocatorOptions::EnableObjectCount>>)
MAKE_TEST(CountedStaticRefPtrUnlock,
          static_slab_test<
              StaticRefPtrTestTraits<NullLock, fbl::SlabAllocatorOptions::EnableObjectCount>>)

#undef MAKE_TEST

TEST(SlabAllocatorTest, AllowManualDeleteOperator) {
#if TEST_WILL_NOT_COMPILE || 0
  {
    struct Obj;
    using Traits =
        fbl::StaticSlabAllocatorTraits<std::unique_ptr<Obj>, 4096, fbl::Mutex,
                                       fbl::SlabAllocatorOptions::AllowManualDeleteOperator>;
    struct Obj : public fbl::SlabAllocated<Traits> {};
    [[maybe_unused]] fbl::SlabAllocator<Traits> allocator(1);
  }
#endif
#if TEST_WILL_NOT_COMPILE || 0
  {
    struct Obj;
    using Traits = fbl::UnlockedStaticSlabAllocatorTraits<
        std::unique_ptr<Obj>, 4096, fbl::SlabAllocatorOptions::AllowManualDeleteOperator>;
    struct Obj : public fbl::SlabAllocated<Traits> {};
    [[maybe_unused]] fbl::SlabAllocator<Traits> allocator(1);
  }
#endif
#if TEST_WILL_NOT_COMPILE || 0
  {
    struct Obj;
    using Traits =
        fbl::InstancedSlabAllocatorTraits<std::unique_ptr<Obj>, 4096, fbl::Mutex,
                                          fbl::SlabAllocatorOptions::AllowManualDeleteOperator>;
    struct Obj : public fbl::SlabAllocated<Traits> {};
    [[maybe_unused]] fbl::SlabAllocator<Traits> allocator(1);
  }
#endif
#if TEST_WILL_NOT_COMPILE || 0
  {
    struct Obj;
    using Traits = fbl::UnlockedInstancedSlabAllocatorTraits<
        std::unique_ptr<Obj>, 4096, fbl::SlabAllocatorOptions::AllowManualDeleteOperator>;
    struct Obj : public fbl::SlabAllocated<Traits> {};
    [[maybe_unused]] fbl::SlabAllocator<Traits> allocator(1);
  }
#endif
  {
    struct Obj;
    struct DeleteFriend;
    using Traits = fbl::UnlockedManualDeleteSlabAllocatorTraits<
        Obj*, 4096, fbl::SlabAllocatorOptions::AllowManualDeleteOperator>;
    struct Obj : public fbl::SlabAllocated<Traits> {
      friend struct DeleteFriend;
    };

    struct DeleteFriend {
      static void Delete(Obj* obj) { delete obj; }
    };

    [[maybe_unused]] fbl::SlabAllocator<Traits> allocator(1);

    // Heap allocate and then delete an object.  This should never ASSERT
    // because of the option flag that we passed.  Note that we still need to be
    // a friend of the object in order to even attempt to do this.
    Obj* the_obj = new Obj();
    ASSERT_NOT_NULL(the_obj);
    DeleteFriend::Delete(the_obj);
  }
}
