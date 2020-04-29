// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef FBL_TESTS_INTRUSIVE_CONTAINERS_BASE_TEST_ENVIRONMENTS_H_
#define FBL_TESTS_INTRUSIVE_CONTAINERS_BASE_TEST_ENVIRONMENTS_H_

#include <utility>

#include <fbl/auto_call.h>
#include <fbl/ref_counted.h>
#include <fbl/tests/intrusive_containers/objects.h>
#include <fbl/tests/intrusive_containers/test_environment_utils.h>
#include <zxtest/zxtest.h>

namespace fbl {
namespace tests {
namespace intrusive_containers {

// TestEnvironmentBase<>
//
// The base class for all tests environments.  TestEnvironmentBase handles
// creating and tracking raw pointers to test objects so they can be cleaned up
// without leaking, even while testing unmanaged pointer types.
// TestEnvironmentBase is also where the default container storage lives.
template <typename TestEnvTraits>
class TestEnvironmentBase {
 public:
  using ObjType = typename TestEnvTraits::ObjType;
  using PtrType = typename TestEnvTraits::PtrType;
  using ContainerType = typename TestEnvTraits::ContainerType;
  using PtrTraits = typename ContainerType::PtrTraits;

 protected:
  PtrType CreateTrackedObject(size_t ndx, size_t value, bool ref_held) {
    if ((ndx >= OBJ_COUNT) || objects_[ndx])
      return PtrType(nullptr);

    PtrType ret = TestEnvTraits::CreateObject(value);
    if (ret == nullptr)
      return PtrType(nullptr);

    objects_[ndx] = PtrTraits::GetRaw(ret);

    if (ref_held)
      refs_held_++;

    return std::move(ret);
  }

  static constexpr size_t OBJ_COUNT = 17;

  ContainerType container_;
  ObjType* objects_[OBJ_COUNT] = {nullptr};
  size_t refs_held_ = 0;
};

// TestEnvironmentSpecialized<>
//
// Specializations of the base test environment which handle the specific
// details of testing the various pointer types.
template <typename TestEnvTraits>
class TestEnvironmentSpecialized;

template <typename T>
class TestEnvironmentSpecialized<UnmanagedTestTraits<T>>
    : public TestEnvironmentBase<UnmanagedTestTraits<T>> {
 protected:
  using Base = TestEnvironmentBase<UnmanagedTestTraits<T>>;
  using PtrType = typename Base::PtrType;
  static constexpr auto OBJ_COUNT = Base::OBJ_COUNT;

  void ReleaseObject(size_t ndx) {
    if (HoldingObject(ndx)) {
      delete this->objects_[ndx];
      this->objects_[ndx] = nullptr;
      this->refs_held_--;
    }
  }

  bool HoldingObject(size_t ndx) const { return ((ndx < OBJ_COUNT) && this->objects_[ndx]); }

  PtrType CreateTrackedObject(size_t ndx, size_t value, bool hold_ref = false) {
    return Base::CreateTrackedObject(ndx, value, true);
  }
};

template <typename T>
class TestEnvironmentSpecialized<UniquePtrDefaultDeleterTestTraits<T>>
    : public TestEnvironmentBase<UniquePtrDefaultDeleterTestTraits<T>> {
 protected:
  using Base = TestEnvironmentBase<UniquePtrDefaultDeleterTestTraits<T>>;
  using PtrType = typename Base::PtrType;
  static constexpr auto OBJ_COUNT = Base::OBJ_COUNT;

  void ReleaseObject(size_t ndx) {
    if (ndx < OBJ_COUNT)
      this->objects_[ndx] = nullptr;
  }

  bool HoldingObject(size_t ndx) const { return false; }

  PtrType CreateTrackedObject(size_t ndx, size_t value, bool hold_ref = false) {
    return Base::CreateTrackedObject(ndx, value, false);
  }
};

template <typename T>
class TestEnvironmentSpecialized<UniquePtrCustomDeleterTestTraits<T>>
    : public TestEnvironmentBase<UniquePtrCustomDeleterTestTraits<T>> {
 protected:
  using Base = TestEnvironmentBase<UniquePtrCustomDeleterTestTraits<T>>;
  using PtrType = typename Base::PtrType;
  static constexpr auto OBJ_COUNT = Base::OBJ_COUNT;

  void ReleaseObject(size_t ndx) {
    if (ndx < OBJ_COUNT)
      this->objects_[ndx] = nullptr;
  }

  bool HoldingObject(size_t ndx) const { return false; }

  PtrType CreateTrackedObject(size_t ndx, size_t value, bool hold_ref = false) {
    return Base::CreateTrackedObject(ndx, value, false);
  }
};

template <typename T>
class TestEnvironmentSpecialized<RefPtrTestTraits<T>>
    : public TestEnvironmentBase<RefPtrTestTraits<T>> {
 protected:
  using Base = TestEnvironmentBase<RefPtrTestTraits<T>>;
  using PtrType = typename Base::PtrType;
  static constexpr auto OBJ_COUNT = Base::OBJ_COUNT;

  PtrType CreateTrackedObject(size_t ndx, size_t value, bool hold_ref = false) {
    PtrType ret = Base::CreateTrackedObject(ndx, value, hold_ref);

    if (hold_ref)
      refed_objects_[ndx] = ret;

    return std::move(ret);
  }

  void ReleaseObject(size_t ndx) {
    if (ndx < OBJ_COUNT) {
      this->objects_[ndx] = nullptr;
      if (refed_objects_[ndx]) {
        refed_objects_[ndx] = nullptr;
        this->refs_held_--;
      }
    }
  }

  bool HoldingObject(size_t ndx) const {
    return ((ndx < OBJ_COUNT) && (refed_objects_[ndx] != nullptr));
  }

 private:
  PtrType refed_objects_[OBJ_COUNT];
};

// TestEnvironment<>
//
// Test environment which defines and implements tests and test utilities which
// are applicable to all containers.
template <typename TestEnvTraits>
class TestEnvironment : public TestEnvironmentSpecialized<TestEnvTraits> {
 public:
  using ObjType = typename TestEnvTraits::ObjType;
  using PtrType = typename TestEnvTraits::PtrType;
  using ContainerTraits = typename ObjType::ContainerTraits;
  using ContainerType = typename ContainerTraits::ContainerType;
  using ContainerChecker = typename ContainerType::CheckerType;
  using OtherContainerType = typename ContainerTraits::OtherContainerType;
  using PtrTraits = typename ContainerType::PtrTraits;
  using NodeTraits = typename ContainerType::NodeTraits;

  enum class RefAction {
    HoldNone,
    HoldSome,
    HoldAll,
  };

  TestEnvironment() { TestEnvTraits::ResetCustomDeleter(); }
  ~TestEnvironment() { Reset(); }

  // Utility methods used to check if the target of an Erase operation is
  // valid, whether the target of the operation is expressed as an iterator, a
  // key or as an object pointer.
  bool ValidEraseTarget(size_t key) { return container().find(key).IsValid(); }
  bool ValidEraseTarget(ObjType& target) { return NodeTraits::node_state(target).InContainer(); }
  bool ValidEraseTarget(typename ContainerType::iterator& target) {
    return target.IsValid() && NodeTraits::node_state(*target).InContainer();
  }

  // Utility method for checking the size of the container via either size()
  // or size_slow(), depending on whether or not the container supports a
  // constant order size operation.
  template <typename CType>
  static size_t Size(const CType& container) {
    return SizeUtils<CType>::size(container);
  }

  // The default method for populating a container will depend on whether this
  // is a sequence container or an associative container.  Sequenced
  // containers will use an implementation of push_front while associative
  // containers will assign a key to the objects which get created and then
  // use an implementation of insert.
  virtual void Populate(ContainerType& container, RefAction ref_action = RefAction::HoldSome) = 0;

  void Reset() {
    ContainerChecker::SanityCheck(container());
    container().clear();
    ASSERT_NO_FATAL_FAILURES(ContainerChecker::SanityCheck(container()));

    for (size_t i = 0; i < OBJ_COUNT; ++i)
      ReleaseObject(i);

    EXPECT_EQ(0u, refs_held());
    refs_held() = 0;

    EXPECT_EQ(0u, ObjType::live_obj_count());
    ObjType::ResetLiveObjCount();
  }

  void Clear() {
    // Start by making some objects.
    ASSERT_NO_FAILURES(Populate(container()));

    // Clear the container.  Afterwards, the number of live objects we have
    // should be equal to the number of references being held by the test
    // environment.
    container().clear();
    EXPECT_EQ(0u, Size(container()));
    EXPECT_EQ(refs_held(), ObjType::live_obj_count());

    for (size_t i = 0; i < OBJ_COUNT; ++i) {
      EXPECT_NOT_NULL(objects()[i]);

      // If our underlying object it still being kept alive by the test
      // environment, make sure that its internal pointer state has been
      // properly cleared out.
      if (HoldingObject(i)) {
        auto& ns = ContainerType::NodeTraits::node_state(*objects()[i]);
        EXPECT_FALSE(ns.InContainer());
      }
    }

    TestEnvTraits::CheckCustomDeleteInvocations(OBJ_COUNT);
  }

  void ClearUnsafe() {
    // Start by making some objects.
    ASSERT_NO_FAILURES(Populate(container()));

    // Perform an unsafe clear of the container.  Afterwards, the number of
    // live objects we have should be equal to the number of elements
    // initially added to the container, since the unsafe operation should
    // not have released any references to any objects during the unsafe
    // clear operation.
    //
    // Note: This is currently a moot point.  clear_unsafe() operations are
    // only currently allowed on unmanaged pointers, and the test framework
    // (by necessity) always hold references to all internally allocated
    // unmanaged objects.
    container().clear_unsafe();
    EXPECT_EQ(0u, Size(container()));
    EXPECT_EQ(OBJ_COUNT, ObjType::live_obj_count());

    for (size_t i = 0; i < OBJ_COUNT; ++i) {
      EXPECT_NOT_NULL(objects()[i]);

      // Make sure that the internal pointer states of all of our objects
      // do not know yet that they have been removed from the container.
      // The clear_unsafe operation should not have updated any of the
      // internal object states.
      auto& ns = ContainerType::NodeTraits::node_state(*objects()[i]);
      EXPECT_TRUE(ns.InContainer());
    }
  }

  void IsEmpty() {
    EXPECT_TRUE(container().is_empty());
    ASSERT_NO_FAILURES(Populate(container()));
    EXPECT_FALSE(container().is_empty());
    Reset();
    EXPECT_TRUE(container().is_empty());
    TestEnvTraits::CheckCustomDeleteInvocations(OBJ_COUNT);
  }

  template <typename TargetType>
  void DoErase(TargetType&& target, size_t ndx, size_t remaining, bool check_ndx = true) {
    ASSERT_TRUE(ndx < OBJ_COUNT);
    ASSERT_TRUE(remaining <= OBJ_COUNT);
    ASSERT_TRUE(!container().is_empty());
    ASSERT_TRUE(ValidEraseTarget(target));
    EXPECT_EQ(remaining, ObjType::live_obj_count());
    EXPECT_EQ(remaining, Size(container()));
    size_t erased_ndx;

    {
      // Erase the item and sanity check it against our tracking.
      PtrType tmp = container().erase(target);
      ASSERT_NOT_NULL(tmp);
      if (check_ndx) {
        EXPECT_EQ(tmp->value(), ndx);
        EXPECT_EQ(objects()[ndx], tmp->raw_ptr());
      }
      erased_ndx = tmp->value();

      // Make sure that the intrusive bookkeeping is up-to-date.
      auto& ns = ContainerType::NodeTraits::node_state(*tmp);
      EXPECT_TRUE(ns.IsValid());
      EXPECT_FALSE(ns.InContainer());

      // The container has shrunk, but the object should still be around.
      EXPECT_EQ(remaining, ObjType::live_obj_count());
      EXPECT_EQ(remaining - 1, Size(container()));
    }

    // If we were not holding onto the object using the test
    // environment's tracking, the live object count should have
    // dropped.  Otherwise, it should remain the same.
    if (!HoldingObject(erased_ndx))
      EXPECT_EQ(remaining - 1, ObjType::live_obj_count());
    else
      EXPECT_EQ(remaining, ObjType::live_obj_count());

    // Let go of the object and verify that it has now gone away.
    ReleaseObject(erased_ndx);
    EXPECT_EQ(remaining - 1, ObjType::live_obj_count());
  }

  void IterErase() {
    // Don't perform index sanity checks for the objects we erase unless
    // this is a sequence container type.
    bool check_ndx = ContainerType::IsSequenced;
    size_t erased = 0;

    // Remove all of the elements from the container by erasing from the front.
    ASSERT_NO_FAILURES(Populate(container()));
    for (size_t i = 0; i < OBJ_COUNT; ++i) {
      TestEnvTraits::CheckCustomDeleteInvocations(erased);
      DoErase(container().begin(), i, OBJ_COUNT - i, check_ndx);
      TestEnvTraits::CheckCustomDeleteInvocations(++erased);
    }

    EXPECT_EQ(0u, ObjType::live_obj_count());
    EXPECT_EQ(0u, Size(container()));

    // Remove all but 2 of the elements from the container by erasing from the middle.
    static_assert(2 < OBJ_COUNT, "OBJ_COUNT too small to run Erase test!");
    ASSERT_NO_FAILURES(Populate(container()));
    auto iter = container().begin();
    iter++;
    for (size_t i = 1; i < OBJ_COUNT - 1; ++i) {
      TestEnvTraits::CheckCustomDeleteInvocations(erased);
      DoErase(iter++, i, OBJ_COUNT - i + 1, check_ndx);
      TestEnvTraits::CheckCustomDeleteInvocations(++erased);
    }

    // Attempting to erase end() from a container with more than one element in
    // it should return nullptr.
    EXPECT_NULL(container().erase(container().end()));
    TestEnvTraits::CheckCustomDeleteInvocations(erased);
    DoErase(container().begin(), 0, 2, check_ndx);
    TestEnvTraits::CheckCustomDeleteInvocations(++erased);

    // Attempting to erase end() from a container with just one element in
    // it should return nullptr.
    EXPECT_NULL(container().erase(container().end()));
    TestEnvTraits::CheckCustomDeleteInvocations(erased);
    DoErase(container().begin(), OBJ_COUNT - 1, 1, check_ndx);
    TestEnvTraits::CheckCustomDeleteInvocations(++erased);

    // Attempting to erase end() from an empty container should return nullptr.
    EXPECT_EQ(0u, ObjType::live_obj_count());
    EXPECT_EQ(0u, Size(container()));
    EXPECT_NULL(container().erase(container().end()));
    EXPECT_EQ(erased, OBJ_COUNT * 2);
    TestEnvTraits::CheckCustomDeleteInvocations(OBJ_COUNT * 2);
  }

  void ReverseIterErase() {
    // Don't perform index sanity checks for the objects we erase unless
    // this is a sequence container type.
    bool check_ndx = ContainerType::IsSequenced;

    // Remove all of the elements from the container by erasing from the back.
    ASSERT_NO_FAILURES(Populate(container()));
    auto iter = container().end();
    iter--;
    for (size_t i = 0; i < OBJ_COUNT; ++i) {
      DoErase(iter--, OBJ_COUNT - i - 1, OBJ_COUNT - i, check_ndx);
    }

    EXPECT_EQ(0u, ObjType::live_obj_count());
    EXPECT_EQ(0u, Size(container()));
  }

  void DirectErase() {
    // Remove all of the elements from the container by erasing using direct
    // node pointers which should end up always being at the front of the
    // container.
    ASSERT_NO_FAILURES(Populate(container(), RefAction::HoldAll));
    for (size_t i = 0; i < OBJ_COUNT; ++i) {
      ASSERT_NOT_NULL(objects()[i]);
      DoErase(*objects()[i], i, OBJ_COUNT - i);
    }

    EXPECT_EQ(0u, ObjType::live_obj_count());
    EXPECT_EQ(0u, Size(container()));

    // Remove all of the elements from the container by erasing using direct
    // node pointers which should end up always being at the back of the
    // container.
    ASSERT_NO_FAILURES(Populate(container(), RefAction::HoldAll));
    for (size_t i = 0; i < OBJ_COUNT; ++i) {
      size_t ndx = OBJ_COUNT - i - 1;
      ASSERT_NOT_NULL(objects()[ndx]);
      DoErase(*objects()[ndx], ndx, ndx + 1);
    }

    EXPECT_EQ(0u, ObjType::live_obj_count());
    EXPECT_EQ(0u, Size(container()));

    // Remove all of the elements from the container by erasing using direct
    // node pointers which should end up always being somewhere in the
    // middle of the container.
    static_assert(2 < OBJ_COUNT, "OBJ_COUNT too small to run Erase test!");
    ASSERT_NO_FAILURES(Populate(container(), RefAction::HoldAll));
    for (size_t i = 1; i < OBJ_COUNT - 1; ++i) {
      ASSERT_NOT_NULL(objects()[i]);
      DoErase(*objects()[i], i, OBJ_COUNT - i + 1);
    }
  }

  template <typename IterType>
  void DoIterate(const IterType& begin, const IterType& end) {
    IterType iter;

    // Iterate using begin/end
    size_t i = 0;
    for (iter = begin; iter != end;) {
      // Exercise both -> and * dereferencing
      ASSERT_TRUE(iter.IsValid());

      EXPECT_EQ(0u, iter->visited_count());
      iter->Visit();
      EXPECT_EQ(1u, (*iter).visited_count());
      (*iter).Visit();
      EXPECT_EQ(2u, (*iter).visited_count());

      // Exercise both pre and postfix increment
      if ((i++) & 1)
        iter++;
      else
        ++iter;
    }
    EXPECT_FALSE(iter.IsValid());

    for (i = 0; i < OBJ_COUNT; ++i) {
      EXPECT_EQ(2u, objects()[i]->visited_count());
      objects()[i]->ResetVisitedCount();
    }

    // Advancing iter past the end of the container should be a no-op.  Check
    // both pre and post-fix.
    iter = end;
    ++iter;
    EXPECT_FALSE(iter.IsValid());
    EXPECT_TRUE(iter == end);

    // We know that the iterator  is already at the end of the container, but
    // perform the explicit assignment in order to check that the assignment
    // operator is working (the previous version actually exercises the copy
    // constructor or the explicit rvalue constructor, if supplied)
    iter = end;
    iter++;
    EXPECT_FALSE(iter.IsValid());
    EXPECT_TRUE(iter == end);
  }

  void Iterate() {
    // Both begin and cbegin should both be invalid, and to end/cend
    ASSERT_EQ(0u, Size(container()));
    EXPECT_FALSE(container().begin().IsValid());
    EXPECT_TRUE(container().begin() == container().end());

    EXPECT_FALSE(container().cbegin().IsValid());
    EXPECT_TRUE(container().cbegin() == container().cend());

    // Attempting to increment begin() for an empty container should result
    // in an invalid iterator which is still equal to end().  Check both
    // prefix and postfix decrement operators.
    auto iter = container().begin();
    ++iter;
    EXPECT_TRUE(container().end() == iter);
    EXPECT_FALSE(iter.IsValid());

    iter = container().begin();
    iter++;
    EXPECT_TRUE(container().end() == iter);
    EXPECT_FALSE(iter.IsValid());

    // Check const_iterator as well.
    auto const_iter = container().cbegin();
    ++const_iter;
    EXPECT_TRUE(container().cend() == const_iter);
    EXPECT_FALSE(const_iter.IsValid());

    const_iter = container().cbegin();
    const_iter++;
    EXPECT_TRUE(container().cend() == const_iter);
    EXPECT_FALSE(const_iter.IsValid());

    // Make some objects.
    ASSERT_NO_FAILURES(Populate(container()));
    EXPECT_EQ(OBJ_COUNT, Size(container()));

    // Both begin and cbegin should both be valid, and not equal to end/cend
    EXPECT_TRUE(container().begin().IsValid());
    EXPECT_TRUE(container().begin() != container().end());

    EXPECT_TRUE(container().cbegin().IsValid());
    EXPECT_TRUE(container().cbegin() != container().cend());

    DoIterate(container().begin(), container().end());    // Test iterator
    DoIterate(container().cbegin(), container().cend());  // Test const_iterator

    // Iterate using the range-based for loop syntax
    for (auto& obj : container()) {
      EXPECT_EQ(0u, obj.visited_count());
      obj.Visit();
      EXPECT_EQ(1u, obj.visited_count());
    }

    for (size_t i = 0; i < OBJ_COUNT; ++i) {
      EXPECT_EQ(1u, objects()[i]->visited_count());
      objects()[i]->ResetVisitedCount();
    }

    // Iterate using the range-based for loop syntax over const references.
    for (const auto& obj : container()) {
      EXPECT_EQ(0u, obj.visited_count());
      obj.Visit();
      EXPECT_EQ(1u, obj.visited_count());
    }

    for (size_t i = 0; i < OBJ_COUNT; ++i) {
      EXPECT_EQ(1u, objects()[i]->visited_count());
      objects()[i]->ResetVisitedCount();
    }

    // None of the objects should have been destroyed during this test.
    TestEnvTraits::CheckCustomDeleteInvocations(0);
  }

  template <typename IterType>
  void DoReverseIterate(const IterType& begin, const IterType& end) {
    IterType iter;

    // Backing up one from end() should give a valid iterator (either prefix
    // or postfix).
    iter = end;
    EXPECT_FALSE(iter.IsValid());
    iter--;
    EXPECT_TRUE(iter.IsValid());

    iter = end;
    EXPECT_FALSE(iter.IsValid());
    --iter;
    EXPECT_TRUE(iter.IsValid());

    // Make sure that backing up an iterator by one points always points
    // to the previous object in the container.
    iter = begin;
    size_t prev_ndx = iter->value();
    while (++iter != end) {
      ASSERT_LT(prev_ndx, OBJ_COUNT);
      ASSERT_NOT_NULL(objects()[prev_ndx]);

      auto prev_iter = iter;
      --prev_iter;
      ASSERT_TRUE(prev_iter.IsValid());
      EXPECT_FALSE(prev_iter == iter);
      EXPECT_TRUE(*prev_iter == *objects()[prev_ndx]);

      prev_iter = iter;
      prev_iter--;
      ASSERT_TRUE(prev_iter.IsValid());
      EXPECT_FALSE(prev_iter == iter);
      EXPECT_TRUE(*prev_iter == *objects()[prev_ndx]);

      prev_ndx = iter->value();
    }

    // Attempting to back up past the beginning should result in an
    // invalid iterator.
    iter = begin;
    ASSERT_TRUE(iter.IsValid());
    --iter;
    EXPECT_FALSE(iter.IsValid());

    iter = begin;
    ASSERT_TRUE(iter.IsValid());
    iter--;
    EXPECT_FALSE(iter.IsValid());
  }

  void ReverseIterate() {
    // Make sure that backing up from end() for an empty container stays at
    // end.  Check both prefix and postfix decrement operators.
    ASSERT_EQ(0u, Size(container()));
    auto iter = container().end();
    --iter;
    EXPECT_TRUE(container().end() == iter);
    EXPECT_FALSE(iter.IsValid());

    iter = container().end();
    iter--;
    EXPECT_TRUE(container().end() == iter);
    EXPECT_FALSE(iter.IsValid());

    // Check const_iterator as well.
    auto const_iter = container().cend();
    --const_iter;
    EXPECT_TRUE(container().cend() == const_iter);
    EXPECT_FALSE(const_iter.IsValid());

    const_iter = container().cend();
    const_iter--;
    EXPECT_TRUE(container().cend() == const_iter);
    EXPECT_FALSE(const_iter.IsValid());

    // Making some objects.
    ASSERT_NO_FAILURES(Populate(container()));
    EXPECT_EQ(OBJ_COUNT, Size(container()));

    // Test iterator
    ASSERT_NO_FATAL_FAILURES(DoReverseIterate(container().begin(), container().end()));

    // Test const_iterator
    ASSERT_NO_FATAL_FAILURES(DoReverseIterate(container().cbegin(), container().cend()));

    // None of the objects should have been destroyed during this test.
    TestEnvTraits::CheckCustomDeleteInvocations(0);
  }

  void MakeIterator() {
    // Populate the container.  Hold internal refs to everything we add to
    // the container.
    ASSERT_NO_FAILURES(Populate(container(), RefAction::HoldAll));

    // For every member of the container, make an iterator using the
    // internal reference we are holding.  Verify that the iterator is in
    // the position we expect it to be in.
    for (size_t i = 0; i < OBJ_COUNT; ++i) {
      ASSERT_NOT_NULL(objects()[i]);
      auto iter = container().make_iterator(*objects()[i]);

      ASSERT_TRUE(iter != container().end());
      EXPECT_EQ(objects()[i]->value(), iter->value());
      EXPECT_EQ(objects()[i], iter->raw_ptr());

      if (ContainerType::IsSequenced) {
        auto other_iter = container().begin();

        for (size_t j = 0; j < i; ++j) {
          EXPECT_FALSE(other_iter == iter);
          ++other_iter;
        }

        EXPECT_TRUE(other_iter == iter);
      }
    }

    // Repeat using a const iterator.
    for (size_t i = 0; i < OBJ_COUNT; ++i) {
      ASSERT_NOT_NULL(objects()[i]);
      auto iter = const_container().make_iterator(*objects()[i]);

      ASSERT_TRUE(iter != container().cend());
      EXPECT_EQ(objects()[i]->value(), iter->value());
      EXPECT_EQ(objects()[i], iter->raw_ptr());

      if (ContainerType::IsSequenced) {
        auto other_iter = container().cbegin();

        for (size_t j = 0; j < i; ++j) {
          EXPECT_FALSE(other_iter == iter);
          ++other_iter;
        }

        EXPECT_TRUE(other_iter == iter);
      }
    }
  }

  void Swap() {
    {
      ContainerType other_container;  // Make an empty container.
      auto cleanup_other = MakeContainerAutoCleanup(&other_container);

      ASSERT_NO_FAILURES(Populate(container()));  // Fill the internal container with stuff.

      // Sanity check, swap, then check again.
      EXPECT_EQ(OBJ_COUNT, ObjType::live_obj_count());
      EXPECT_FALSE(container().is_empty());
      EXPECT_EQ(OBJ_COUNT, Size(container()));
      EXPECT_TRUE(other_container.is_empty());

      for (auto& obj : container()) {
        ASSERT_EQ(0u, obj.visited_count());
        obj.Visit();
      }

      ASSERT_NO_FATAL_FAILURES(ContainerChecker::SanityCheck(container()));
      ASSERT_NO_FATAL_FAILURES(ContainerChecker::SanityCheck(other_container));

      container().swap(other_container);

      EXPECT_EQ(OBJ_COUNT, ObjType::live_obj_count());
      EXPECT_FALSE(other_container.is_empty());
      EXPECT_EQ(OBJ_COUNT, Size(other_container));
      EXPECT_TRUE(container().is_empty());

      for (auto& obj : other_container) {
        EXPECT_EQ(1u, obj.visited_count());
        obj.Visit();
      }

      ASSERT_NO_FATAL_FAILURES(ContainerChecker::SanityCheck(container()));
      ASSERT_NO_FATAL_FAILURES(ContainerChecker::SanityCheck(other_container));

      // Swap back to check the case where container() was empty, but other_container
      // had elements.
      container().swap(other_container);

      EXPECT_EQ(OBJ_COUNT, ObjType::live_obj_count());
      EXPECT_FALSE(container().is_empty());
      EXPECT_EQ(OBJ_COUNT, Size(container()));
      EXPECT_TRUE(other_container.is_empty());

      for (const auto& obj : container())
        EXPECT_EQ(2u, obj.visited_count());

      ASSERT_NO_FATAL_FAILURES(ContainerChecker::SanityCheck(container()));
      ASSERT_NO_FATAL_FAILURES(ContainerChecker::SanityCheck(other_container));

      // Nothing should have been deleted yet.
      TestEnvTraits::CheckCustomDeleteInvocations(0);

      // Reset;
      Reset();

      // Now all of the objects should be gone.
      TestEnvTraits::CheckCustomDeleteInvocations(OBJ_COUNT);
    }

    // Make a new other_container, this time with some stuff in it.
    EXPECT_EQ(0u, ObjType::live_obj_count());
    {
      ContainerType other_container;  // Make an empty container.
      auto cleanup_other = MakeContainerAutoCleanup(&other_container);
      ASSERT_NO_FAILURES(Populate(container()));  // Fill the internal container with stuff.

      static constexpr size_t OTHER_COUNT = 5;
      static constexpr size_t OTHER_START = 10000;
      ObjType* raw_ptrs[OTHER_COUNT];

      for (size_t i = 0; i < OTHER_COUNT; ++i) {
        PtrType ptr = TestEnvTraits::CreateObject(OTHER_START + OTHER_COUNT - i - 1);
        raw_ptrs[i] = PtrTraits::GetRaw(ptr);
        ContainerUtils<ContainerType>::MoveInto(other_container, std::move(ptr));
      }

      // Sanity check
      EXPECT_EQ(OBJ_COUNT + OTHER_COUNT, ObjType::live_obj_count());
      EXPECT_EQ(OBJ_COUNT, Size(container()));
      EXPECT_EQ(OTHER_COUNT, Size(other_container));

      ASSERT_NO_FATAL_FAILURES(ContainerChecker::SanityCheck(container()));
      ASSERT_NO_FATAL_FAILURES(ContainerChecker::SanityCheck(other_container));

      // Visit everything in container() once, and everything in
      // other_container twice.
      for (auto& obj : container()) {
        ASSERT_EQ(0u, obj.visited_count());
        obj.Visit();
      }

      for (const auto& obj : other_container) {
        ASSERT_EQ(0u, obj.visited_count());
        obj.Visit();
        obj.Visit();
      }

      for (auto& obj : container())
        EXPECT_EQ(1u, obj.visited_count());
      for (auto& obj : other_container)
        EXPECT_EQ(2u, obj.visited_count());

      // Swap and sanity check again
      container().swap(other_container);

      EXPECT_EQ(OBJ_COUNT + OTHER_COUNT, ObjType::live_obj_count());
      EXPECT_EQ(OBJ_COUNT, Size(other_container));
      EXPECT_EQ(OTHER_COUNT, Size(container()));

      ASSERT_NO_FATAL_FAILURES(ContainerChecker::SanityCheck(container()));
      ASSERT_NO_FATAL_FAILURES(ContainerChecker::SanityCheck(other_container));

      // Everything in container() should have been visited twice so far,
      // while everything in other_container should have been visited
      // once.
      for (auto& obj : container())
        EXPECT_EQ(2u, obj.visited_count());
      for (auto& obj : other_container)
        EXPECT_EQ(1u, obj.visited_count());

      // Swap back and sanity check again
      container().swap(other_container);

      EXPECT_EQ(OBJ_COUNT + OTHER_COUNT, ObjType::live_obj_count());
      EXPECT_EQ(OBJ_COUNT, Size(container()));
      EXPECT_EQ(OTHER_COUNT, Size(other_container));

      ASSERT_NO_FATAL_FAILURES(ContainerChecker::SanityCheck(container()));
      ASSERT_NO_FATAL_FAILURES(ContainerChecker::SanityCheck(other_container));

      for (auto& obj : container())
        EXPECT_EQ(1u, obj.visited_count());
      for (auto& obj : other_container)
        EXPECT_EQ(2u, obj.visited_count());

      // No new objects should have been deleted.
      TestEnvTraits::CheckCustomDeleteInvocations(OBJ_COUNT);

      // If we are testing unmanaged pointers clean them up.
      EXPECT_EQ(OBJ_COUNT + OTHER_COUNT, ObjType::live_obj_count());
      other_container.clear();
      if (!PtrTraits::IsManaged) {
        EXPECT_EQ(OBJ_COUNT + OTHER_COUNT, ObjType::live_obj_count());
        for (size_t i = 0; i < OTHER_COUNT; ++i)
          delete raw_ptrs[i];
      }
      EXPECT_EQ(OBJ_COUNT, ObjType::live_obj_count());

      // Now, we should have deleted an additional OTHER_COUNT objects
      TestEnvTraits::CheckCustomDeleteInvocations(OBJ_COUNT + OTHER_COUNT);

      // Reset the internal state
      Reset();
      EXPECT_EQ(0u, ObjType::live_obj_count());

      // Now we should have filled and emptied the test environment twice, and
      // created+destroyed an additional OTHER_COUNT objects..
      TestEnvTraits::CheckCustomDeleteInvocations((2 * OBJ_COUNT) + OTHER_COUNT);
    }
  }

  void RvalueOps() {
    // Populate the internal container.
    ASSERT_NO_FAILURES(Populate(container()));
    EXPECT_EQ(OBJ_COUNT, ObjType::live_obj_count());
    EXPECT_EQ(OBJ_COUNT, Size(container()));
    for (auto& obj : container()) {
      ASSERT_GT(OBJ_COUNT, obj.value());
      EXPECT_EQ(0u, obj.visited_count());
      EXPECT_EQ(objects()[obj.value()], &obj);
      obj.Visit();
    }

    ASSERT_NO_FATAL_FAILURES(ContainerChecker::SanityCheck(container()));

    // Move its contents to a new container by explicitly invoking the Rvalue
    // constructor.
#if TEST_WILL_NOT_COMPILE || 0
    ContainerType other_container(container());
#else
    ContainerType other_container(std::move(container()));
#endif
    auto cleanup_other = MakeContainerAutoCleanup(&other_container);

    EXPECT_EQ(OBJ_COUNT, ObjType::live_obj_count());
    EXPECT_EQ(OBJ_COUNT, Size(other_container));
    EXPECT_TRUE(container().is_empty());
    for (const auto& obj : other_container) {
      ASSERT_GT(OBJ_COUNT, obj.value());
      EXPECT_EQ(1u, obj.visited_count());
      EXPECT_EQ(objects()[obj.value()], &obj);
      obj.Visit();
    }

    ASSERT_NO_FATAL_FAILURES(ContainerChecker::SanityCheck(container()));
    ASSERT_NO_FATAL_FAILURES(ContainerChecker::SanityCheck(other_container));

    // Move the contents again, this time using move-initialization which implicitly
    // invokes the Rvalue constructor.
#if TEST_WILL_NOT_COMPILE || 0
    ContainerType another_container = other_container;
#else
    ContainerType another_container = std::move(other_container);
#endif
    auto cleanup_another = MakeContainerAutoCleanup(&another_container);

    EXPECT_EQ(OBJ_COUNT, ObjType::live_obj_count());
    EXPECT_EQ(OBJ_COUNT, Size(another_container));
    EXPECT_TRUE(other_container.is_empty());
    for (const auto& obj : another_container) {
      ASSERT_GT(OBJ_COUNT, obj.value());
      EXPECT_EQ(2u, obj.visited_count());
      EXPECT_EQ(objects()[obj.value()], &obj);
      obj.Visit();
    }

    ASSERT_NO_FATAL_FAILURES(ContainerChecker::SanityCheck(container()));
    ASSERT_NO_FATAL_FAILURES(ContainerChecker::SanityCheck(other_container));
    ASSERT_NO_FATAL_FAILURES(ContainerChecker::SanityCheck(another_container));

    // Move the contents of the final container back to the internal container.  If we
    // are testing managed pointer types, put some objects into the internal
    // container first and make sure they get released.  Don't try this with
    // unmanaged pointers as it will trigger an assert if you attempt to
    // blow away a non-empty container via Rvalue assignment.
    static constexpr size_t EXTRA_COUNT = 5;
    size_t extras_added = 0;
    if (PtrTraits::IsManaged) {
      while (extras_added < EXTRA_COUNT) {
        ContainerUtils<ContainerType>::MoveInto(
            container(), std::move(TestEnvTraits::CreateObject(extras_added++)));
      }
    }

    // Sanity checks before the assignment
    EXPECT_EQ(OBJ_COUNT + extras_added, ObjType::live_obj_count());
    EXPECT_EQ(extras_added, Size(container()));
    for (const auto& obj : container()) {
      ASSERT_GT(EXTRA_COUNT, obj.value());
      EXPECT_EQ(0u, obj.visited_count());
    }

    ASSERT_NO_FATAL_FAILURES(ContainerChecker::SanityCheck(container()));
    ASSERT_NO_FATAL_FAILURES(ContainerChecker::SanityCheck(other_container));
    ASSERT_NO_FATAL_FAILURES(ContainerChecker::SanityCheck(another_container));

    // No objects should have been deleted yet.
    TestEnvTraits::CheckCustomDeleteInvocations(0);
#if TEST_WILL_NOT_COMPILE || 0
    container() = another_container;
#else
    container() = std::move(another_container);
#endif
    // The extra objects we put into container() should have been released
    // when we moved the contents of another_container into container()
    TestEnvTraits::CheckCustomDeleteInvocations(extras_added);

    // another_container should now be empty, and we should have returned to our
    // starting, post-populated state.
    EXPECT_EQ(OBJ_COUNT, ObjType::live_obj_count());
    EXPECT_EQ(OBJ_COUNT, Size(container()));
    EXPECT_TRUE(another_container.is_empty());
    for (const auto& obj : container()) {
      ASSERT_GT(OBJ_COUNT, obj.value());
      EXPECT_EQ(3u, obj.visited_count());
      EXPECT_EQ(objects()[obj.value()], &obj);
    }

    ASSERT_NO_FATAL_FAILURES(ContainerChecker::SanityCheck(container()));
    ASSERT_NO_FATAL_FAILURES(ContainerChecker::SanityCheck(other_container));
    ASSERT_NO_FATAL_FAILURES(ContainerChecker::SanityCheck(another_container));
  }

  void Scope() {
    // Make sure that both unique_ptrs and RefPtrs handle being moved
    // properly, and that containers of such pointers automatically clean up
    // when the container goes out of scope and destructs.  Note: Don't try
    // this with an unmanaged pointer.  Lists of unmanaged pointers will
    // ZX_ASSERT if they destruct with elements still in them.
    EXPECT_EQ(0U, ObjType::live_obj_count());

    {  // Begin scope for container
      ContainerType container;
      auto cleanup_container = MakeContainerAutoCleanup(&container);

      // Put some stuff into the container.  Don't hold any internal
      // references to anything we add.
      Populate(container, RefAction::HoldNone);
      EXPECT_EQ(OBJ_COUNT, ObjType::live_obj_count());
      EXPECT_EQ(OBJ_COUNT, Size(container));
      TestEnvTraits::CheckCustomDeleteInvocations(0);
    }  // Let the container go out of scope and clean itself up..

    EXPECT_EQ(0U, ObjType::live_obj_count());
    TestEnvTraits::CheckCustomDeleteInvocations(OBJ_COUNT);
  }

  void TwoContainer() {
    // Start by populating the internal container.  We should end up with
    // OBJ_COUNT objects, but we may not be holding internal references to
    // all of them.
    ASSERT_NO_FAILURES(Populate(container()));

    // Create the other type of container that ObjType can exist on and populate
    // it using the default operation for the container type.
    OtherContainerType other_container;
    auto cleanup_other = MakeContainerAutoCleanup(&other_container);
    for (auto iter = container().begin(); iter != container().end(); ++iter) {
      ContainerUtils<OtherContainerType>::MoveInto(other_container, std::move(iter.CopyPointer()));
    }

    // The two containers should be the same length, and nothing should have
    // changed about the live object count.
    EXPECT_EQ(OBJ_COUNT, ObjType::live_obj_count());
    EXPECT_EQ(OBJ_COUNT, Size(container()));
    EXPECT_EQ(OBJ_COUNT, Size(other_container));

    // Make sure that none of the members of container() or other_container
    // have been visited.  Then visit every member of other_container, and
    // make sure that all of the members of container() have been visited
    // once.
    for (auto& obj : container())
      ASSERT_EQ(0u, obj.visited_count());
    for (auto& obj : other_container)
      ASSERT_EQ(0u, obj.visited_count());

    for (auto& obj : other_container) {
      obj.Visit();
      EXPECT_EQ(1u, obj.visited_count());
    }

    for (auto& obj : container()) {
      EXPECT_EQ(1u, obj.visited_count());
      obj.Visit();
      EXPECT_EQ(2u, obj.visited_count());
    }

    // If this is a sequenced container, then other_container should be in
    // the reverse order of container()
    if (OtherContainerType::IsSequenced) {
      auto other_iter = other_container.begin();
      for (const auto& obj : container()) {
        ASSERT_FALSE(other_iter == other_container.end());
        EXPECT_EQ(OBJ_COUNT - obj.value() - 1, other_iter->value());
        ++other_iter;
      }
      EXPECT_TRUE(other_iter == other_container.end());
    }

    ASSERT_NO_FATAL_FAILURES(ContainerChecker::SanityCheck(container()));
    ASSERT_NO_FATAL_FAILURES(ContainerChecker::SanityCheck(other_container));

    // Clear the internal container.  No objects should go away and the other
    // container should be un-affected
    container().clear();

    EXPECT_EQ(OBJ_COUNT, ObjType::live_obj_count());
    EXPECT_EQ(0u, Size(container()));
    EXPECT_EQ(OBJ_COUNT, Size(other_container));

    for (auto& obj : other_container)
      EXPECT_EQ(2u, obj.visited_count());

    if (OtherContainerType::IsSequenced) {
      auto other_iter = other_container.begin();
      for (size_t i = 0; i < OBJ_COUNT; ++i) {
        ASSERT_FALSE(other_iter == other_container.end());
        EXPECT_EQ(OBJ_COUNT - i - 1, other_iter->value());
        ++other_iter;
      }
      EXPECT_TRUE(other_iter == other_container.end());
    }

    // If we are testing a container of managed pointers, release our internal
    // references.  Again, no objects should go away (as they are being
    // referenced by other_container.  Note: Don't try this with an unmanaged
    // pointer.  "releasing" and unmanaged pointer in the context of the
    // TestEnvironment class means to return it to the heap, which is a Very
    // Bad thing if we still have a container referring to the objects which were
    // returned to the heap.
    if (PtrTraits::IsManaged) {
      for (size_t i = 0; i < OBJ_COUNT; ++i)
        ReleaseObject(i);

      EXPECT_EQ(OBJ_COUNT, ObjType::live_obj_count());
      EXPECT_EQ(0u, refs_held());
      EXPECT_EQ(OBJ_COUNT, Size(other_container));
    }
    TestEnvTraits::CheckCustomDeleteInvocations(0);

    // Finally, clear() other_container and reset the internal state.  At this
    // point, all objects should have gone away.
    other_container.clear();
    TestEnvTraits::CheckCustomDeleteInvocations(OBJ_COUNT);
    Reset();

    EXPECT_EQ(0u, ObjType::live_obj_count());
    EXPECT_EQ(0u, refs_held());
    EXPECT_EQ(0u, Size(container()));
    EXPECT_EQ(0u, Size(other_container));
  }

  void ThreeContainerHelper() {
    // Start by populating the internal container.  We should end up with
    // OBJ_COUNT objects, but we may not be holding internal references to
    // all of them.
    ASSERT_NO_FAILURES(Populate(container()));

    // Create the other types of containers that ObjType can exist on and populate
    // them using the default operation for the container type.
    typename ContainerTraits::TaggedType1 tagged1;
    typename ContainerTraits::TaggedType2 tagged2;
    typename ContainerTraits::TaggedType3 tagged3;
    for (auto iter = container().begin(); iter != container().end(); ++iter) {
      ContainerUtils<typename ContainerTraits::TaggedType1>::MoveInto(tagged1, iter.CopyPointer());
      ContainerUtils<typename ContainerTraits::TaggedType2>::MoveInto(tagged2, iter.CopyPointer());
      ContainerUtils<typename ContainerTraits::TaggedType3>::MoveInto(tagged3, iter.CopyPointer());
    }

    for (auto& obj : tagged1) {
      EXPECT_TRUE(InContainer<typename ContainerTraits::Tag1>(obj));
      EXPECT_TRUE(InContainer<typename ContainerTraits::Tag2>(obj));
      EXPECT_TRUE(InContainer<typename ContainerTraits::Tag3>(obj));
    }

    // The three containers should be the same length, and nothing should have
    // changed about the live object count.
    EXPECT_EQ(OBJ_COUNT, ObjType::live_obj_count());
    EXPECT_EQ(OBJ_COUNT, Size(tagged1));
    EXPECT_EQ(OBJ_COUNT, Size(tagged2));
    EXPECT_EQ(OBJ_COUNT, Size(tagged3));

    // Make sure that none of the members of container() or other_container
    // have been visited.  Then visit every member of the other containers,
    // and make sure that all of the members of container() have been
    // visited once.
    for (auto& obj : tagged1)
      ASSERT_EQ(0u, obj.visited_count());
    for (auto& obj : tagged2)
      ASSERT_EQ(0u, obj.visited_count());
    for (auto& obj : tagged3)
      ASSERT_EQ(0u, obj.visited_count());

    for (auto& obj : tagged1) {
      obj.Visit();
      EXPECT_EQ(1u, obj.visited_count());
    }

    for (auto& obj : tagged2) {
      obj.Visit();
      EXPECT_EQ(2u, obj.visited_count());
    }

    for (auto& obj : tagged3) {
      obj.Visit();
      EXPECT_EQ(3u, obj.visited_count());
    }

    // If this is a sequenced container, then the other containers should be in
    // the reverse order of container()
    if constexpr (ContainerTraits::TaggedType1::IsSequenced &&
                  ContainerTraits::TaggedType2::IsSequenced &&
                  ContainerTraits::TaggedType3::IsSequenced) {
      auto iter1 = tagged1.begin();
      for (const auto& obj : container()) {
        ASSERT_FALSE(iter1 == tagged1.end());
        EXPECT_EQ(OBJ_COUNT - obj.value() - 1, iter1->value());
        ++iter1;
      }
      EXPECT_TRUE(iter1 == tagged1.end());

      auto iter2 = tagged2.begin();
      for (const auto& obj : container()) {
        ASSERT_FALSE(iter2 == tagged2.end());
        EXPECT_EQ(OBJ_COUNT - obj.value() - 1, iter2->value());
        ++iter2;
      }
      EXPECT_TRUE(iter2 == tagged2.end());

      auto iter3 = tagged3.begin();
      for (const auto& obj : container()) {
        ASSERT_FALSE(iter3 == tagged3.end());
        EXPECT_EQ(OBJ_COUNT - obj.value() - 1, iter3->value());
        ++iter3;
      }
      EXPECT_TRUE(iter3 == tagged3.end());
    }

    ASSERT_NO_FATAL_FAILURES(ContainerChecker::SanityCheck(tagged1));
    ASSERT_NO_FATAL_FAILURES(ContainerChecker::SanityCheck(tagged2));
    ASSERT_NO_FATAL_FAILURES(ContainerChecker::SanityCheck(tagged3));

    // Clear the internal container.  No objects should go away and the other
    // containers should be un-affected
    container().clear();

    EXPECT_EQ(OBJ_COUNT, ObjType::live_obj_count());
    EXPECT_EQ(0u, Size(container()));
    EXPECT_EQ(OBJ_COUNT, Size(tagged1));
    EXPECT_EQ(OBJ_COUNT, Size(tagged2));
    EXPECT_EQ(OBJ_COUNT, Size(tagged3));

    for (auto& obj : tagged1) {
      EXPECT_EQ(3u, obj.visited_count());
    }
    for (auto& obj : tagged2) {
      EXPECT_EQ(3u, obj.visited_count());
    }
    for (auto& obj : tagged3) {
      EXPECT_EQ(3u, obj.visited_count());
    }

    if constexpr (ContainerTraits::TaggedType1::IsSequenced &&
                  ContainerTraits::TaggedType2::IsSequenced &&
                  ContainerTraits::TaggedType3::IsSequenced) {
      auto iter1 = tagged1.begin();
      for (size_t i = 0; i < OBJ_COUNT; ++i) {
        ASSERT_FALSE(iter1 == tagged1.end());
        EXPECT_EQ(OBJ_COUNT - i - 1, iter1->value());
        ++iter1;
      }
      EXPECT_TRUE(iter1 == tagged1.end());

      auto iter2 = tagged2.begin();
      for (size_t i = 0; i < OBJ_COUNT; ++i) {
        ASSERT_FALSE(iter2 == tagged2.end());
        EXPECT_EQ(OBJ_COUNT - i - 1, iter2->value());
        ++iter2;
      }
      EXPECT_TRUE(iter2 == tagged2.end());

      auto iter3 = tagged3.begin();
      for (size_t i = 0; i < OBJ_COUNT; ++i) {
        ASSERT_FALSE(iter3 == tagged3.end());
        EXPECT_EQ(OBJ_COUNT - i - 1, iter3->value());
        ++iter3;
      }
      EXPECT_TRUE(iter3 == tagged3.end());
    }

    // If we are testing a container of managed pointers, release our internal
    // references.  Again, no objects should go away (as they are being
    // referenced by other_container.  Note: Don't try this with an unmanaged
    // pointer.  "releasing" an unmanaged pointer in the context of the
    // TestEnvironment class means to return it to the heap, which is a Very
    // Bad thing if we still have a container referring to the objects which were
    // returned to the heap.
    if (PtrTraits::IsManaged) {
      for (size_t i = 0; i < OBJ_COUNT; ++i)
        ReleaseObject(i);

      EXPECT_EQ(OBJ_COUNT, ObjType::live_obj_count());
      EXPECT_EQ(0u, refs_held());
      EXPECT_EQ(OBJ_COUNT, Size(tagged1));
      EXPECT_EQ(OBJ_COUNT, Size(tagged2));
      EXPECT_EQ(OBJ_COUNT, Size(tagged3));
    }
    TestEnvTraits::CheckCustomDeleteInvocations(0);

    // Finally, clear() the other other_containers and reset the internal state. At this
    // point, all objects should have gone away.
    tagged1.clear();
    tagged2.clear();
    tagged3.clear();
    TestEnvTraits::CheckCustomDeleteInvocations(OBJ_COUNT);
    Reset();

    EXPECT_EQ(0u, ObjType::live_obj_count());
    EXPECT_EQ(0u, refs_held());
    EXPECT_EQ(0u, Size(container()));
    EXPECT_EQ(0u, Size(tagged1));
    EXPECT_EQ(0u, Size(tagged2));
    EXPECT_EQ(0u, Size(tagged3));
  }

  void IterCopyPointer() {
    PtrType ptr;
    typename ContainerType::iterator iter;

    // A default constructed iterator should give back nullptr when
    // CopyPointer is called.
    ptr = iter.CopyPointer();
    EXPECT_NULL(ptr);

    // The begining/end of an emptry container should also return nullptr.
    ptr = container().begin().CopyPointer();
    EXPECT_NULL(ptr);

    ptr = container().end().CopyPointer();
    EXPECT_NULL(ptr);

    // Populate the container.
    ASSERT_NO_FAILURES(Populate(container(), RefAction::HoldAll));
    EXPECT_EQ(OBJ_COUNT, ObjType::live_obj_count());
    EXPECT_EQ(OBJ_COUNT, refs_held());

    // end().CopyPointer should still return nullptr.
    ptr = container().end().CopyPointer();
    EXPECT_NULL(ptr);

    // begin().CopyPointer() should be non-null.
    ptr = container().begin().CopyPointer();
    EXPECT_NOT_NULL(ptr);

    // clear the container and release all internally held references.
    container().clear();
    for (size_t i = 0; i < OBJ_COUNT; ++i)
      ReleaseObject(i);

    // We should not be holding any references, but we should still have a
    // live object if we are testing a managed pointer type.
    EXPECT_EQ(0u, refs_held());
    if (PtrTraits::IsManaged)
      EXPECT_EQ(1u, ObjType::live_obj_count());
    else
      EXPECT_EQ(0u, ObjType::live_obj_count());

    // null out our pointer.  No matter what, our live_obj_count should now
    // be zero.
    ptr = nullptr;
    EXPECT_EQ(0u, ObjType::live_obj_count());
  }

  void EraseIf() {
    // Populate our container.
    ASSERT_NO_FAILURES(Populate(container()));

    // Erase all of the even members
    size_t even_erased = 0;
    while (even_erased < OBJ_COUNT) {
      if (nullptr ==
          container().erase_if([](const ObjType& obj) -> bool { return !(obj.value() & 1); }))
        break;
      even_erased++;
    }

    EXPECT_EQ(EVEN_OBJ_COUNT, even_erased);
    EXPECT_EQ(OBJ_COUNT, even_erased + Size(container()));
    TestEnvTraits::CheckCustomDeleteInvocations(even_erased);
    for (const auto& obj : container())
      EXPECT_TRUE(obj.value() & 1);

    // Erase all of the odd members
    size_t odd_erased = 0;
    while (even_erased < OBJ_COUNT) {
      if (nullptr ==
          container().erase_if([](const ObjType& obj) -> bool { return obj.value() & 1; }))
        break;
      odd_erased++;
    }

    EXPECT_EQ(ODD_OBJ_COUNT, odd_erased);
    EXPECT_EQ(OBJ_COUNT, even_erased + odd_erased);
    EXPECT_TRUE(container().is_empty());
    TestEnvTraits::CheckCustomDeleteInvocations(OBJ_COUNT);
  }

  void FindIf() {
    // Populate our container.
    ASSERT_NO_FAILURES(Populate(container()));

    // Find all of the members which should be in the container.
    for (size_t i = 0; i < OBJ_COUNT; ++i) {
      auto iter =
          const_container().find_if([i](const ObjType& obj) -> bool { return (obj.value() == i); });

      ASSERT_TRUE(iter.IsValid());
      EXPECT_EQ(0u, iter->visited_count());
      iter->Visit();
    }

    // Every member should have been visited once.
    for (auto& obj : container()) {
      EXPECT_EQ(1u, obj.visited_count());
      obj.ResetVisitedCount();
    }

    // Count all of the odd members.
    size_t total_found = 0;
    while (true) {
      auto iter = const_container().find_if(
          [](const ObjType& obj) -> bool { return (obj.value() & 1) && !obj.visited_count(); });

      if (!iter.IsValid())
        break;

      ++total_found;
      iter->Visit();
    }
    EXPECT_EQ(ODD_OBJ_COUNT, total_found);

    // All of the odd members should have been visited once, while all of
    // the even members should not have been visited.
    for (const auto& obj : container())
      EXPECT_EQ(obj.value() & 1, obj.visited_count());

    // Fail to find a member which should not be in the container.
    auto iter = const_container().find_if(
        [](const ObjType& obj) -> bool { return (obj.value() == OBJ_COUNT); });
    EXPECT_FALSE(iter.IsValid());

    // We should not have destroyed any objects in this test.
    TestEnvTraits::CheckCustomDeleteInvocations(0);
  }

  static PtrType TakePtr(PtrType& ptr) {
    if constexpr (PtrTraits::IsManaged) {
      return std::move(ptr);
    } else {
      PtrType tmp = ptr;
      ptr = nullptr;
      return tmp;
    }
  }

 private:
  // Accessors for base class members so we don't have to type
  // this->base_member all of the time.
  using Sp = TestEnvironmentSpecialized<TestEnvTraits>;
  using Base = TestEnvironmentBase<TestEnvTraits>;
  static constexpr size_t OBJ_COUNT = Base::OBJ_COUNT;
  static constexpr size_t EVEN_OBJ_COUNT = (OBJ_COUNT >> 1) + (OBJ_COUNT & 1);
  static constexpr size_t ODD_OBJ_COUNT = (OBJ_COUNT >> 1);

  ContainerType& container() { return this->container_; }
  const ContainerType& const_container() { return this->container_; }
  ObjType** objects() { return this->objects_; }
  size_t& refs_held() { return this->refs_held_; }

  void ReleaseObject(size_t ndx) { Sp::ReleaseObject(ndx); }
  bool HoldingObject(size_t ndx) const { return Sp::HoldingObject(ndx); }
};

}  // namespace intrusive_containers
}  // namespace tests
}  // namespace fbl

#endif  // FBL_TESTS_INTRUSIVE_CONTAINERS_BASE_TEST_ENVIRONMENTS_H_
