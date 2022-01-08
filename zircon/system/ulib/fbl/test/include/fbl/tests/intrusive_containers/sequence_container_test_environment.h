// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef FBL_TESTS_INTRUSIVE_CONTAINERS_SEQUENCE_CONTAINER_TEST_ENVIRONMENT_H_
#define FBL_TESTS_INTRUSIVE_CONTAINERS_SEQUENCE_CONTAINER_TEST_ENVIRONMENT_H_

#include <iterator>
#include <utility>

#include <fbl/algorithm.h>
#include <fbl/tests/intrusive_containers/base_test_environments.h>
#include <zxtest/zxtest.h>

namespace fbl {
namespace tests {
namespace intrusive_containers {

// SequenceContainerTestEnvironment<>
//
// Test environment which defines and implements tests and test utilities which
// are applicable to all sequence containers such as lists.
template <typename TestEnvTraits>
class SequenceContainerTestEnvironment : public TestEnvironment<TestEnvTraits> {
 public:
  using ObjType = typename TestEnvTraits::ObjType;
  using PtrType = typename TestEnvTraits::PtrType;
  using ContainerTraits = typename ObjType::ContainerTraits;
  using ContainerType = typename ContainerTraits::ContainerType;
  using ContainerChecker = typename ContainerType::CheckerType;
  using OtherContainerType = typename ContainerTraits::OtherContainerType;
  using IteratorType = typename ContainerType::iterator;
  using PtrTraits = typename ContainerType::PtrTraits;
  using RefAction = typename TestEnvironment<TestEnvTraits>::RefAction;
  using TestEnvironment<TestEnvTraits>::TakePtr;

  enum class SplitAfterFlavor { Iterator, ObjectReference };

  // Utility method for checking the size of the container via either size()
  // or size_slow(), depending on whether or not the container supports a
  // constant order size operation.
  template <typename CType>
  static size_t Size(const CType& container) {
    return SizeUtils<CType>::size(container);
  }

  void Populate(ContainerType& container, RefAction ref_action = RefAction::HoldSome) override {
    EXPECT_EQ(0U, ObjType::live_obj_count());

    for (size_t i = 0; i < OBJ_COUNT; ++i) {
      size_t ndx = OBJ_COUNT - i - 1;
      EXPECT_EQ(i, Size(container));

      // Unless explicitly told to do so, don't hold a reference in the
      // test environment for every 4th object created.  Note, this only
      // affects RefPtr tests.  Unmanaged pointers always hold an
      // unmanaged copy of the pointer (so it can be cleaned up), while
      // unique_ptr tests are not able to hold an extra copy of the
      // pointer (because it is unique)
      bool hold_ref;
      switch (ref_action) {
        case RefAction::HoldNone:
          hold_ref = false;
          break;
        case RefAction::HoldSome:
          hold_ref = (i & 0x3);
          break;
        case RefAction::HoldAll:
        default:
          hold_ref = true;
          break;
      }

      PtrType new_object = this->CreateTrackedObject(ndx, ndx, hold_ref);
      ASSERT_NOT_NULL(new_object);
      EXPECT_EQ(new_object->raw_ptr(), objects()[ndx]);

      // Alternate whether or not we move the pointer, or "transfer" it.
      // Transferring means different things for different pointer types.
      // For unmanaged, it just returns a reference to the pointer and
      // leaves the original unaltered.  For unique, it moves the pointer
      // (clearing the source).  For RefPtr, it makes a new RefPtr
      // instance, bumping the reference count in the process.
      if (i & 1) {
#if TEST_WILL_NOT_COMPILE || 0
        container.push_front(new_object);
#else
        container.push_front(TestEnvTraits::Transfer(new_object));
#endif
        EXPECT_TRUE(TestEnvTraits::WasTransferred(new_object));
      } else {
        container.push_front(std::move(new_object));
        EXPECT_TRUE(TestEnvTraits::WasMoved(new_object));
      }
    }

    EXPECT_EQ(OBJ_COUNT, Size(container));
    EXPECT_EQ(OBJ_COUNT, ObjType::live_obj_count());
    ASSERT_NO_FATAL_FAILURE(ContainerChecker::SanityCheck(container));
  }

  void PushFront() {
    ASSERT_NO_FAILURES(Populate(container()));
    TestEnvTraits::CheckCustomDeleteInvocations(0);
  }

  void PushBack() {
    EXPECT_EQ(0U, ObjType::live_obj_count());

    for (size_t i = 0; i < OBJ_COUNT; ++i) {
      EXPECT_EQ(i, Size(container()));

      PtrType new_object = this->CreateTrackedObject(i, i);
      ASSERT_NOT_NULL(new_object);
      EXPECT_EQ(new_object->raw_ptr(), objects()[i]);

      // Alternate whether or not we move the pointer, or "transfer" it.
      if (i & 1) {
#if TEST_WILL_NOT_COMPILE || 0
        container().push_back(new_object);
#else
        container().push_back(TestEnvTraits::Transfer(new_object));
#endif
        EXPECT_TRUE(TestEnvTraits::WasTransferred(new_object));
      } else {
        container().push_back(std::move(new_object));
        EXPECT_TRUE(TestEnvTraits::WasMoved(new_object));
      }
    }

    EXPECT_EQ(OBJ_COUNT, Size(container()));
    EXPECT_EQ(OBJ_COUNT, ObjType::live_obj_count());

    size_t i = 0;
    for (const auto& obj : container()) {
      ASSERT_LT(i, OBJ_COUNT);
      EXPECT_EQ(objects()[i]->value(), obj.value());
      EXPECT_EQ(objects()[i], obj.raw_ptr());
      i++;
    }

    TestEnvTraits::CheckCustomDeleteInvocations(0);
  }

  void PopFront() {
    ASSERT_NO_FAILURES(Populate(container()));

    // Remove elements using pop_front.  List should shrink each time we
    // remove an element, but the number of live objects should only shrink
    // when we let the last reference go out of scope.
    for (size_t i = 0; i < OBJ_COUNT; ++i) {
      size_t remaining = OBJ_COUNT - i;
      ASSERT_TRUE(!container().is_empty());
      EXPECT_EQ(remaining, ObjType::live_obj_count());
      EXPECT_EQ(remaining, Size(container()));

      {
        // Pop the item and sanity check it against our tracking.
        PtrType tmp = container().pop_front();
        EXPECT_NOT_NULL(tmp);
        EXPECT_EQ(tmp->value(), i);
        EXPECT_EQ(objects()[i], tmp->raw_ptr());

        // Make sure that the intrusive bookkeeping is up-to-date.
        auto& ns = ContainerType::NodeTraits::node_state(*tmp);
        EXPECT_NULL(ns.next_);

        // The container has shrunk, but the object should still be around.
        EXPECT_EQ(remaining, ObjType::live_obj_count());
        EXPECT_EQ(remaining - 1, Size(container()));
      }

      // If we were not holding onto the object using the test
      // environment's tracking, the live object count should have
      // dropped.  Otherwise, it should remain the same.
      if (!HoldingObject(i))
        EXPECT_EQ(remaining - 1, ObjType::live_obj_count());
      else
        EXPECT_EQ(remaining, ObjType::live_obj_count());

      // Let go of the object and verify that it has now gone away.
      ReleaseObject(i);
      EXPECT_EQ(remaining - 1, ObjType::live_obj_count());
      TestEnvTraits::CheckCustomDeleteInvocations(i + 1);
    }

    // List should be empty now.  Popping anything else should result in a
    // null pointer.
    EXPECT_TRUE(container().is_empty());
    PtrType should_be_null = container().pop_front();
    EXPECT_NULL(should_be_null);
    TestEnvTraits::CheckCustomDeleteInvocations(OBJ_COUNT);
  }

  void PopBack() {
    ASSERT_NO_FAILURES(Populate(container()));

    // Remove elements using pop_back.  List should shrink each time we
    // remove an element, but the number of live objects should only shrink
    // when we let the last reference go out of scope.
    for (size_t i = 0; i < OBJ_COUNT; ++i) {
      size_t remaining = OBJ_COUNT - i;
      size_t obj_ndx = OBJ_COUNT - i - 1;
      ASSERT_TRUE(!container().is_empty());
      EXPECT_EQ(remaining, ObjType::live_obj_count());
      EXPECT_EQ(remaining, Size(container()));

      {
        // Pop the item and sanity check it against our tracking.
        PtrType tmp = container().pop_back();
        EXPECT_NOT_NULL(tmp);
        EXPECT_EQ(tmp->value(), obj_ndx);
        EXPECT_EQ(objects()[obj_ndx], tmp->raw_ptr());

        // Make sure that the intrusive bookkeeping is up-to-date.
        auto& ns = ContainerType::NodeTraits::node_state(*tmp);
        EXPECT_NULL(ns.next_);

        // The container has shrunk, but the object should still be around.
        EXPECT_EQ(remaining, ObjType::live_obj_count());
        EXPECT_EQ(remaining - 1, Size(container()));
      }

      // If we were not holding onto the object using the test
      // environment's tracking, the live object count should have
      // dropped.  Otherwise, it should remain the same.
      if (!HoldingObject(obj_ndx)) {
        EXPECT_EQ(remaining - 1, ObjType::live_obj_count());
      } else {
        EXPECT_EQ(remaining, ObjType::live_obj_count());
      }

      // Let go of the object and verify that it has now gone away.
      ReleaseObject(obj_ndx);
      EXPECT_EQ(remaining - 1, ObjType::live_obj_count());
      TestEnvTraits::CheckCustomDeleteInvocations(i + 1);
    }

    // List should be empty now.  Popping anything else should result in a
    // null pointer.
    EXPECT_TRUE(container().is_empty());
    PtrType should_be_null = container().pop_back();
    EXPECT_NULL(should_be_null);
    TestEnvTraits::CheckCustomDeleteInvocations(OBJ_COUNT);
  }

  void EraseNext() {
    ASSERT_NO_FAILURES(Populate(container()));

    // Remove as many elements as we can using erase_next.
    auto iter = container().begin();
    for (size_t i = 1; i < OBJ_COUNT; ++i) {
      size_t remaining = OBJ_COUNT - i + 1;
      ASSERT_TRUE(!container().is_empty());
      ASSERT_TRUE(iter != container().end());
      EXPECT_EQ(remaining, ObjType::live_obj_count());
      EXPECT_EQ(remaining, Size(container()));

      {
        // Erase the item and sanity check it against our tracking.
        PtrType tmp = container().erase_next(iter);
        EXPECT_NOT_NULL(tmp);
        EXPECT_EQ(tmp->value(), i);
        EXPECT_EQ(objects()[i], tmp->raw_ptr());

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
      if (!HoldingObject(i))
        EXPECT_EQ(remaining - 1, ObjType::live_obj_count());
      else
        EXPECT_EQ(remaining, ObjType::live_obj_count());

      // Let go of the object and verify that it has now gone away.
      ReleaseObject(i);
      EXPECT_EQ(remaining - 1, ObjType::live_obj_count());
      TestEnvTraits::CheckCustomDeleteInvocations(i);
    }

    // Iterator should now be one away from the end, and there should be one
    // object left
    EXPECT_EQ(1u, ObjType::live_obj_count());
    EXPECT_EQ(1u, Size(container()));
    EXPECT_TRUE(iter != container().end());
    iter++;
    EXPECT_TRUE(iter == container().end());

    // Attempt to erase the element after the final element.  This should
    // fail, and indicate that it has failed by returning null.
    iter = container().begin();
    PtrType tmp = container().erase_next(iter);
    EXPECT_NULL(tmp);
    TestEnvTraits::CheckCustomDeleteInvocations(OBJ_COUNT - 1);
  }

  void DoInsertAfter(IteratorType iter, size_t pos) {
    EXPECT_EQ(ObjType::live_obj_count(), Size(container()));
    EXPECT_TRUE(iter != container().end());

    size_t orig_container_len = ObjType::live_obj_count();
    size_t orig_iter_pos = iter->value();

    ASSERT_LT(orig_iter_pos, OBJ_COUNT);
    EXPECT_EQ(objects()[orig_iter_pos], iter->raw_ptr());

    PtrType new_object = this->CreateTrackedObject(pos, pos, true);
    ASSERT_NOT_NULL(new_object);
    EXPECT_EQ(new_object->raw_ptr(), objects()[pos]);

    IteratorType new_obj_iter;
    if (pos & 1) {
#if TEST_WILL_NOT_COMPILE || 0
      new_obj_iter = container().insert_after(iter, new_object);
#else
      new_obj_iter = container().insert_after(iter, TestEnvTraits::Transfer(new_object));
#endif
      EXPECT_TRUE(TestEnvTraits::WasTransferred(new_object));
    } else {
      new_obj_iter = container().insert_after(iter, std::move(new_object));
      EXPECT_TRUE(TestEnvTraits::WasMoved(new_object));
    }

    // Ensure the iterator returned by `insert_after` refers to the new item.
    EXPECT_EQ(&(*new_obj_iter), objects()[pos]);

    // List and number of live object should have grown.
    EXPECT_EQ(orig_container_len + 1, ObjType::live_obj_count());
    EXPECT_EQ(orig_container_len + 1, Size(container()));

    // The iterator should not have moved yet.
    EXPECT_TRUE(iter != container().end());
    EXPECT_EQ(objects()[orig_iter_pos], iter->raw_ptr());
    EXPECT_EQ(orig_iter_pos, iter->value());

    // This test should delete no objects
    TestEnvTraits::CheckCustomDeleteInvocations(0);
  }

  void InsertAfter() {
    // In order to insert_after, we need at least one object already in the
    // container.  Use push_front to make one.
    EXPECT_EQ(0u, ObjType::live_obj_count());
    EXPECT_EQ(0U, Size(container()));
    EXPECT_TRUE(container().is_empty());
    container().push_front(std::move(this->CreateTrackedObject(0, 0, true)));

    // Insert some elements after the last element container.
    static constexpr size_t END_INSERT_COUNT = 2;
    static_assert(END_INSERT_COUNT <= OBJ_COUNT, "OBJ_COUNT too small to run InsertAfter test!");

    auto iter = container().begin();
    for (size_t i = (OBJ_COUNT - END_INSERT_COUNT); i < OBJ_COUNT; ++i) {
      ASSERT_NO_FAILURES(DoInsertAfter(iter, i));

      // Now that we have inserted after, we should be able to advance the
      // iterator to what we just inserted.
      iter++;

      ASSERT_TRUE(iter != container().end());
      EXPECT_EQ(objects()[i], iter->raw_ptr());
      EXPECT_EQ(objects()[i], (*iter).raw_ptr());
      EXPECT_EQ(i, iter->value());
      EXPECT_EQ(i, (*iter).value());
    }

    // Advancing iter at this point should bring it to the end.
    EXPECT_TRUE(iter != container().end());
    iter++;
    EXPECT_TRUE(iter == container().end());

    // Reset the iterator to the first element in the container, and test
    // inserting between elements instead of at the end.  To keep the
    // final container in order, we need to insert in reverse order and to not
    // advance the iterator in the process.
    iter = container().begin();
    for (size_t i = (OBJ_COUNT - END_INSERT_COUNT - 1); i > 0; --i) {
      ASSERT_NO_FAILURES(DoInsertAfter(iter, i));
    }
    EXPECT_TRUE(iter != container().end());

    // Check to make sure the container has the expected number of elements, and
    // that they are in the proper order.
    EXPECT_EQ(OBJ_COUNT, ObjType::live_obj_count());
    EXPECT_EQ(OBJ_COUNT, Size(container()));

    size_t i = 0;
    for (const auto& obj : container()) {
      EXPECT_EQ(objects()[i], &obj);
      EXPECT_EQ(objects()[i], obj.raw_ptr());
      EXPECT_EQ(i, obj.value());
      i++;
    }

    // This test should delete no objects
    TestEnvTraits::CheckCustomDeleteInvocations(0);
  }

  template <typename TargetType>
  void DoInsert(TargetType&& target, size_t pos) {
    EXPECT_EQ(ObjType::live_obj_count(), Size(container()));
    size_t orig_container_len = ObjType::live_obj_count();

    PtrType new_object = this->CreateTrackedObject(pos, pos, true);
    ASSERT_NOT_NULL(new_object);
    EXPECT_EQ(new_object->raw_ptr(), objects()[pos]);

    IteratorType new_obj_iter;
    if (pos & 1) {
#if TEST_WILL_NOT_COMPILE || 0
      new_obj_iter = container().insert(target, new_object);
#else
      new_obj_iter = container().insert(target, TestEnvTraits::Transfer(new_object));
#endif
      EXPECT_TRUE(TestEnvTraits::WasTransferred(new_object));
    } else {
      new_obj_iter = container().insert(target, std::move(new_object));
      EXPECT_TRUE(TestEnvTraits::WasMoved(new_object));
    }

    // Ensure the iterator returned by `insert` refers to the new item.
    EXPECT_EQ(&(*new_obj_iter), objects()[pos]);

    // List and number of live object should have grown.
    EXPECT_EQ(orig_container_len + 1, ObjType::live_obj_count());
    EXPECT_EQ(orig_container_len + 1, Size(container()));
  }

  void Insert() {
    EXPECT_EQ(0u, ObjType::live_obj_count());
    EXPECT_EQ(0U, Size(container()));

    static constexpr size_t END_INSERT_COUNT = 3;
    static constexpr size_t START_INSERT_COUNT = 3;
    static constexpr size_t MID_INSERT_COUNT = OBJ_COUNT - START_INSERT_COUNT - END_INSERT_COUNT;
    static_assert((END_INSERT_COUNT <= OBJ_COUNT) &&
                      (START_INSERT_COUNT <= (OBJ_COUNT - END_INSERT_COUNT)) &&
                      ((START_INSERT_COUNT + END_INSERT_COUNT) < OBJ_COUNT),
                  "OBJ_COUNT too small to run Insert test!");

    // Insert some elements at the end of an initially empty container using the
    // end() iterator accessor.
    for (size_t i = (OBJ_COUNT - END_INSERT_COUNT); i < OBJ_COUNT; ++i)
      ASSERT_NO_FAILURES(DoInsert(container().end(), i));

    // Insert some elements at the start of a non-empty container using the
    // begin() iterator accessor.
    for (size_t i = 0; i < START_INSERT_COUNT; ++i) {
      size_t ndx = START_INSERT_COUNT - i - 1;
      ASSERT_NO_FAILURES(DoInsert(container().begin(), ndx));
    }

    // Insert some elements in the middle non-empty container using an iterator
    // we compute.
    auto iter = container().begin();
    for (size_t i = 0; i < START_INSERT_COUNT; ++i)
      ++iter;

    for (size_t i = 0; i < MID_INSERT_COUNT; ++i) {
      size_t ndx = START_INSERT_COUNT + i;
      ASSERT_NO_FAILURES(DoInsert(iter, ndx));
    }

    // iter should be END_INSERT_COUNT from the end of the
    // container.
    for (size_t i = 0; i < END_INSERT_COUNT; ++i) {
      EXPECT_TRUE(iter != container().end());
      ++iter;
    }
    EXPECT_TRUE(++iter == container().end());

    // Check to make sure the container has the expected number of elements, and
    // that they are in the proper order.
    EXPECT_EQ(OBJ_COUNT, ObjType::live_obj_count());
    EXPECT_EQ(OBJ_COUNT, Size(container()));

    size_t i = 0;
    for (const auto& obj : container()) {
      ASSERT_LT(i, OBJ_COUNT);
      EXPECT_EQ(objects()[i], &obj);
      EXPECT_EQ(objects()[i], obj.raw_ptr());
      EXPECT_EQ(i, obj.value());
      i++;
    }

    // This test should delete no objects
    TestEnvTraits::CheckCustomDeleteInvocations(0);
  }

  void DirectInsert() {
    EXPECT_EQ(0u, ObjType::live_obj_count());
    EXPECT_EQ(0U, Size(container()));

    static constexpr size_t END_INSERT_COUNT = 3;
    static constexpr size_t START_INSERT_COUNT = 3;
    static constexpr size_t MID_INSERT_COUNT = OBJ_COUNT - START_INSERT_COUNT - END_INSERT_COUNT;
    static_assert((END_INSERT_COUNT <= OBJ_COUNT) &&
                      (START_INSERT_COUNT <= (OBJ_COUNT - END_INSERT_COUNT)) &&
                      ((START_INSERT_COUNT + END_INSERT_COUNT) < OBJ_COUNT),
                  "OBJ_COUNT too small to run DirectInsert test!");

    // Insert some elements at the end of an initially empty container using
    // the end() iterator as the target.
    for (size_t i = (OBJ_COUNT - END_INSERT_COUNT); i < OBJ_COUNT; ++i) {
      ASSERT_NO_FAILURES(DoInsert(container().end(), i));
    }

    // Insert some elements at the start of a non-empty container node
    // pointers which are always at the start of the container.
    size_t insert_before_ndx = (OBJ_COUNT - END_INSERT_COUNT);
    for (size_t i = 0; i < START_INSERT_COUNT; ++i) {
      size_t ndx = START_INSERT_COUNT - i - 1;
      ASSERT_NOT_NULL(objects()[insert_before_ndx]);
      ASSERT_NO_FAILURES(DoInsert(*objects()[insert_before_ndx], ndx));
      insert_before_ndx = ndx;
    }

    // Insert some elements in the middle non-empty container.
    insert_before_ndx = (OBJ_COUNT - END_INSERT_COUNT);
    for (size_t i = 0; i < MID_INSERT_COUNT; ++i) {
      size_t ndx = START_INSERT_COUNT + i;
      ASSERT_NOT_NULL(objects()[insert_before_ndx]);
      ASSERT_NO_FAILURES(DoInsert(*objects()[insert_before_ndx], ndx));
    }

    // Check to make sure the container has the expected number of elements,
    // and that they are in the proper order.
    EXPECT_EQ(OBJ_COUNT, ObjType::live_obj_count());
    EXPECT_EQ(OBJ_COUNT, Size(container()));

    size_t i = 0;
    for (const auto& obj : container()) {
      ASSERT_LT(i, OBJ_COUNT);
      EXPECT_EQ(objects()[i], &obj);
      EXPECT_EQ(objects()[i], obj.raw_ptr());
      EXPECT_EQ(i, obj.value());
      i++;
    }

    // This test should delete no objects
    TestEnvTraits::CheckCustomDeleteInvocations(0);
  }

  void FillN(size_t begin, size_t end) {
    for (size_t i = begin; i < end; ++i) {
      PtrType new_object = this->CreateTrackedObject(i, i);
      ASSERT_NOT_NULL(new_object);
      container().push_back(std::move(new_object));
      EXPECT_TRUE(TestEnvTraits::WasMoved(new_object));
    }
  }

  void BidirectionalEquals(const ContainerType& sequence, const size_t* values, size_t size) {
    // We use SupportsConstantOrderErase as a way to discriminate a singly-
    // linked list from a doubly-linked list.
    static_assert(ContainerType::IsSequenced && ContainerType::SupportsConstantOrderErase,
                  "BidirectionalEquals must be used with a bi-directional sequence");

    ASSERT_EQ(size, Size(sequence));

    // Iterate forwards and verify values.
    const size_t* val_iter = values;
    for (const auto& node : sequence) {
      EXPECT_EQ(node.value(), *val_iter);
      ++val_iter;
    }

    // Iterate backwards and verify values.
    for (auto begin = sequence.begin(), end = sequence.end(); begin != end;) {
      --val_iter;
      --end;
      EXPECT_EQ(end->value(), *val_iter);
    }

    // This test should delete no objects
    TestEnvTraits::CheckCustomDeleteInvocations(0);
  }

  void Splice() {
    constexpr size_t LIST_COUNT = 2;
    static_assert(LIST_COUNT * 4 < OBJ_COUNT, "OBJ_COUNT too small to run Splice test!");

    EXPECT_EQ(0U, Size(container()));
    ContainerType target;
    auto cleanup_target = MakeContainerAutoCleanup(&target);
    EXPECT_EQ(0U, Size(target));

    // Splice empty source into end of empty target list.
    target.splice(target.end(), container());
    EXPECT_EQ(0U, Size(target));
    EXPECT_EQ(0u, Size(container()));

    // Populate the source list.
    ASSERT_NO_FATAL_FAILURE(FillN(0, LIST_COUNT));
    EXPECT_EQ(LIST_COUNT, Size(container()));

    // Splice into end of empty target list.
    target.splice(target.end(), container());
    static const size_t expected_1[] = {0, 1};
    ASSERT_NO_FATAL_FAILURE(BidirectionalEquals(target, expected_1, std::size(expected_1)));
    EXPECT_EQ(0u, Size(container()));

    // Populate the source list again.
    ASSERT_NO_FATAL_FAILURE(FillN(LIST_COUNT, LIST_COUNT * 2));
    EXPECT_EQ(LIST_COUNT, Size(container()));

    // Splice into end of non-empty target list.
    target.splice(target.end(), container());
    static const size_t expected_2[] = {0, 1, 2, 3};
    ASSERT_NO_FATAL_FAILURE(BidirectionalEquals(target, expected_2, std::size(expected_2)));
    EXPECT_EQ(0u, Size(container()));

    // Populate the source list again.
    ASSERT_NO_FATAL_FAILURE(FillN(LIST_COUNT * 2, LIST_COUNT * 3));
    EXPECT_EQ(LIST_COUNT, Size(container()));

    // Splice into start of non-empty target list.
    target.splice(target.begin(), container());
    static const size_t expected_3[] = {4, 5, 0, 1, 2, 3};
    ASSERT_NO_FATAL_FAILURE(BidirectionalEquals(target, expected_3, std::size(expected_3)));
    EXPECT_EQ(0u, Size(container()));

    // Populate the source list again.
    ASSERT_NO_FATAL_FAILURE(FillN(LIST_COUNT * 3, LIST_COUNT * 4));
    EXPECT_EQ(LIST_COUNT, Size(container()));

    // Splice into second element of non-empty target list.
    target.splice(++target.begin(), container());
    static const size_t expected_4[] = {4, 6, 7, 5, 0, 1, 2, 3};
    ASSERT_NO_FATAL_FAILURE(BidirectionalEquals(target, expected_4, std::size(expected_4)));
    EXPECT_EQ(0u, Size(container()));

    // Splice empty source into end of non-empty target list.
    target.splice(target.end(), container());
    ASSERT_NO_FATAL_FAILURE(BidirectionalEquals(target, expected_4, std::size(expected_4)));
    EXPECT_EQ(0u, Size(container()));

    // No objects should have been deleted yet.
    TestEnvTraits::CheckCustomDeleteInvocations(0);

    // Finally clear the target.
    target.clear();
    EXPECT_EQ(0u, Size(target));

    // By now, we should have created LIST_COUNT * 4 objects.  They should all be gone now.
    TestEnvTraits::CheckCustomDeleteInvocations(LIST_COUNT * 4);
  }

  template <SplitAfterFlavor kFlavor>
  void SplitAfterHelper() {
    // Test splitting the list at all possible points.
    for (size_t i = 0; i < OBJ_COUNT; ++i) {
      // Make sure we are starting with an empty container.
      EXPECT_EQ(0u, ObjType::live_obj_count());
      EXPECT_EQ(0U, Size(container()));

      // Make sure we are starting with an populate the container.
      ASSERT_NO_FAILURES(Populate(container()));
      EXPECT_EQ(OBJ_COUNT, ObjType::live_obj_count());
      ASSERT_EQ(OBJ_COUNT, Size(container()));

      // Find our split point
      auto split_iter = container().begin();
      for (size_t j = 0; j < i; ++j) {
        ++split_iter;
      }

      // Now split the list at the specified point, using either the iterator or
      // the object reference depending on the test flavor.
      ContainerType split_list;
      if constexpr (kFlavor == SplitAfterFlavor::Iterator) {
        split_list = container().split_after(split_iter);
      } else {
        split_list = container().split_after(*split_iter);
      }

      // Basic sanity checks
      ContainerChecker::SanityCheck(container());
      ContainerChecker::SanityCheck(split_list);

      // Check sizes
      EXPECT_EQ(i + 1, Size(container()));
      EXPECT_EQ(OBJ_COUNT - (i + 1), Size(split_list));
      EXPECT_EQ(OBJ_COUNT, ObjType::live_obj_count());

      // The order of the objects in the lists should remain unchanged.
      size_t expected = 0;
      for (const auto& item : container()) {
        EXPECT_EQ(expected, item.value());
        ++expected;
      }
      for (const auto& item : split_list) {
        EXPECT_EQ(expected, item.value());
        ++expected;
      }

      // Reset the environment for the next pass
      split_list.clear();
      this->Reset();
    }
  }

  void SplitAfter() {
    ASSERT_NO_FATAL_FAILURE(SplitAfterHelper<SplitAfterFlavor::Iterator>());
    ASSERT_NO_FATAL_FAILURE(SplitAfterHelper<SplitAfterFlavor::ObjectReference>());
  }

  template <typename IterType>
  void DoSeqIterate(const IterType& begin, const IterType& end) {
    IterType iter;

    // begin() should point to the front of the sequence.
    iter = begin;
    ASSERT_TRUE(iter.IsValid());
    EXPECT_TRUE(container().front() == *iter);

    // Iterate using begin/end
    size_t i = 0;
    for (iter = begin; iter != end;) {
      // Exercise both -> and * dereferencing
      ASSERT_TRUE(iter.IsValid());
      EXPECT_EQ(objects()[i], iter->raw_ptr());
      EXPECT_EQ(objects()[i], (*iter).raw_ptr());
      EXPECT_EQ(i, iter->value());
      EXPECT_EQ(i, (*iter).value());

      // Exercise both pre and postfix increment
      if ((i++) & 1)
        iter++;
      else
        ++iter;
    }
    EXPECT_FALSE(iter.IsValid());
  }

  void SeqIterate() {
    // Start by making some objects.
    ASSERT_NO_FAILURES(Populate(container()));
    EXPECT_EQ(OBJ_COUNT, Size(container()));

    // Test iterator
    ASSERT_NO_FATAL_FAILURE(DoSeqIterate(container().begin(), container().end()));

    // Test const_iterator
    ASSERT_NO_FATAL_FAILURE(DoSeqIterate(container().cbegin(), container().cend()));

    // Iterate using the range-based for loop syntax
    size_t i = 0;
    for (auto& obj : container()) {
      EXPECT_EQ(objects()[i], &obj);
      EXPECT_EQ(objects()[i], obj.raw_ptr());
      EXPECT_EQ(i, obj.value());
      i++;
    }

    // Iterate using the range-based for loop syntax over const references.
    i = 0;
    for (const auto& obj : container()) {
      EXPECT_EQ(objects()[i], &obj);
      EXPECT_EQ(objects()[i], obj.raw_ptr());
      EXPECT_EQ(i, obj.value());
      i++;
    }

    // This test should delete no objects
    TestEnvTraits::CheckCustomDeleteInvocations(0);
  }

  template <typename IterType>
  void DoSeqReverseIterate(const IterType& begin, const IterType& end) {
    IterType iter;

    // Backing up one from end() should give us back().  Check both pre
    // and post-fix behavior.
    iter = end;
    --iter;
    ASSERT_TRUE(iter.IsValid());
    ASSERT_TRUE(iter != end);
    EXPECT_TRUE(container().back() == *iter);

    iter = end;
    iter--;
    ASSERT_TRUE(iter.IsValid());
    ASSERT_TRUE(iter != end);
    EXPECT_TRUE(container().back() == *iter);

    // Make sure that backing up an iterator by one points always points
    // to the previous object in the container.
    iter = begin;
    while (++iter != end) {
      size_t prev_ndx = iter->value() - 1;
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
    }

    // This test should delete no objects
    TestEnvTraits::CheckCustomDeleteInvocations(0);
  }

  void SeqReverseIterate() {
    // Start by making some objects.
    ASSERT_NO_FAILURES(Populate(container()));
    EXPECT_EQ(OBJ_COUNT, Size(container()));

    // Test iterator
    ASSERT_NO_FATAL_FAILURE(DoSeqReverseIterate(container().begin(), container().end()));

    // Test const_iterator
    ASSERT_NO_FATAL_FAILURE(DoSeqReverseIterate(container().cbegin(), container().cend()));

    // This test should delete no objects
    TestEnvTraits::CheckCustomDeleteInvocations(0);
  }

  void ReplaceIfCopy() {
    ASSERT_NO_FATAL_FAILURE(ContainerChecker::SanityCheck(container()));

    // Try (and fail) to replace an element in an empty container.
    {
      PtrType new_obj = TestEnvTraits::CreateObject(0);
      auto raw_obj = PtrTraits::GetRaw(new_obj);
      ASSERT_NOT_NULL(new_obj);
      EXPECT_EQ(0, Size(container()));
      EXPECT_EQ(1, ObjType::live_obj_count());

      PtrType replaced =
          container().replace_if([](const ObjType& obj) -> bool { return true; }, new_obj);

      ASSERT_NO_FATAL_FAILURE(ContainerChecker::SanityCheck(container()));
      ASSERT_NOT_NULL(new_obj);
      EXPECT_NULL(replaced);
      EXPECT_EQ(raw_obj, PtrTraits::GetRaw(new_obj));
      EXPECT_EQ(0, Size(container()));
      EXPECT_EQ(1, ObjType::live_obj_count());

      TestEnvTraits::ReleaseObject(new_obj);
      EXPECT_EQ(0, Size(container()));
      EXPECT_EQ(0, ObjType::live_obj_count());
    }

    // The object which we create in an attempt to replace an object in an
    // empty container should be gone now.
    TestEnvTraits::CheckCustomDeleteInvocations(1);

    // Populate our container.
    for (size_t i = 0; i < OBJ_COUNT; ++i) {
      EXPECT_EQ(i, ObjType::live_obj_count());

      PtrType new_obj = TestEnvTraits::CreateObject(OBJ_COUNT - i - 1);
      ASSERT_NOT_NULL(new_obj);
      EXPECT_EQ(i + 1, ObjType::live_obj_count());

      container().push_front(std::move(new_obj));
    }
    ASSERT_NO_FATAL_FAILURE(ContainerChecker::SanityCheck(container()));

    // Replace all of the members of the contianer with new members which
    // have a value never created during the populate phase.
    for (size_t i = 0; i < OBJ_COUNT; ++i) {
      EXPECT_EQ(OBJ_COUNT, ObjType::live_obj_count());

      PtrType new_obj = TestEnvTraits::CreateObject(OBJ_COUNT + i);
      ASSERT_NOT_NULL(new_obj);

      PtrType replaced = container().replace_if(
          [i](const ObjType& obj) -> bool { return (obj.value() == i); }, new_obj);

      ASSERT_NO_FATAL_FAILURE(ContainerChecker::SanityCheck(container()));
      ASSERT_NOT_NULL(replaced);
      EXPECT_TRUE(new_obj->ContainerTraits::ContainableBaseClass::InContainer());
      EXPECT_FALSE(replaced->ContainerTraits::ContainableBaseClass::InContainer());
      EXPECT_EQ(i, replaced->value());
      EXPECT_EQ(OBJ_COUNT + 1, ObjType::live_obj_count());

      TestEnvTraits::ReleaseObject(replaced);
      EXPECT_EQ(OBJ_COUNT, ObjType::live_obj_count());
    }

    // All of the replaced objects should have gone away now too.
    TestEnvTraits::CheckCustomDeleteInvocations(OBJ_COUNT + 1);

    // Try again, but this time fail each time (since all of the original
    // element values have already been replaced).
    for (size_t i = 0; i < OBJ_COUNT; ++i) {
      EXPECT_EQ(OBJ_COUNT, ObjType::live_obj_count());

      PtrType new_obj = TestEnvTraits::CreateObject(OBJ_COUNT + (2 * i));
      ASSERT_NOT_NULL(new_obj);

      PtrType replaced = container().replace_if(
          [i](const ObjType& obj) -> bool { return (obj.value() == i); }, new_obj);
      ASSERT_NO_FATAL_FAILURE(ContainerChecker::SanityCheck(container()));

      ASSERT_NULL(replaced);
      EXPECT_FALSE(new_obj->ContainerTraits::ContainableBaseClass::InContainer());
      EXPECT_EQ(OBJ_COUNT + 1, ObjType::live_obj_count());

      TestEnvTraits::ReleaseObject(new_obj);
      EXPECT_EQ(OBJ_COUNT, ObjType::live_obj_count());
    }

    // The new objects we created (but failed to replace in the container) should now be gone.
    TestEnvTraits::CheckCustomDeleteInvocations((2 * OBJ_COUNT) + 1);

    // Make sure that the object are in order and have the values we expect.
    size_t i = 0;
    while (!container().is_empty()) {
      PtrType ptr = container().pop_front();
      EXPECT_EQ(OBJ_COUNT + i, ptr->value());
      TestEnvTraits::ReleaseObject(ptr);
      ++i;
    }
    EXPECT_EQ(0, ObjType::live_obj_count());
    ASSERT_NO_FATAL_FAILURE(ContainerChecker::SanityCheck(container()));

    // Now all of the objects we created during the test should be gone.
    TestEnvTraits::CheckCustomDeleteInvocations((3 * OBJ_COUNT) + 1);
  }

  void ReplaceIfMove() {
    // Try (and fail) to replace an element in an empty container.
    {
      PtrType new_obj = TestEnvTraits::CreateObject(0);
      auto raw_obj = PtrTraits::GetRaw(new_obj);
      ASSERT_NOT_NULL(new_obj);
      EXPECT_EQ(0, Size(container()));
      EXPECT_EQ(1, ObjType::live_obj_count());

      PtrType replaced =
          container().replace_if([](const ObjType& obj) -> bool { return true; }, TakePtr(new_obj));

      EXPECT_NULL(new_obj);
      ASSERT_NOT_NULL(replaced);
      EXPECT_EQ(raw_obj, PtrTraits::GetRaw(replaced));
      EXPECT_EQ(0, Size(container()));
      EXPECT_EQ(1, ObjType::live_obj_count());

      TestEnvTraits::ReleaseObject(replaced);
      EXPECT_EQ(0, Size(container()));
      EXPECT_EQ(0, ObjType::live_obj_count());
    }

    // The object which we create in an attempt to replace an object in an
    // empty container should be gone now.
    TestEnvTraits::CheckCustomDeleteInvocations(1);

    // Populate our container.
    for (size_t i = 0; i < OBJ_COUNT; ++i) {
      EXPECT_EQ(i, ObjType::live_obj_count());

      PtrType new_obj = TestEnvTraits::CreateObject(OBJ_COUNT - i - 1);
      ASSERT_NOT_NULL(new_obj);
      EXPECT_EQ(i + 1, ObjType::live_obj_count());

      container().push_front(std::move(new_obj));
    }

    // Replace all of the members of the contianer with new members which
    // have a value never created during Populate().
    for (size_t i = 0; i < OBJ_COUNT; ++i) {
      EXPECT_EQ(OBJ_COUNT, ObjType::live_obj_count());

      PtrType new_obj = TestEnvTraits::CreateObject(OBJ_COUNT + i);
      ASSERT_NOT_NULL(new_obj);

      PtrType replaced = container().replace_if(
          [i](const ObjType& obj) -> bool { return (obj.value() == i); }, TakePtr(new_obj));

      EXPECT_NULL(new_obj);
      ASSERT_NOT_NULL(replaced);
      EXPECT_FALSE(replaced->ContainerTraits::ContainableBaseClass::InContainer());
      EXPECT_EQ(i, replaced->value());
      EXPECT_EQ(OBJ_COUNT + 1, ObjType::live_obj_count());

      TestEnvTraits::ReleaseObject(replaced);
      EXPECT_EQ(OBJ_COUNT, ObjType::live_obj_count());
    }

    // All of the replaced objects should have gone away now too.
    TestEnvTraits::CheckCustomDeleteInvocations(OBJ_COUNT + 1);

    // Try again, but this time fail each time (since all of the original
    // element values have already been replaced).
    for (size_t i = 0; i < OBJ_COUNT; ++i) {
      EXPECT_EQ(OBJ_COUNT, ObjType::live_obj_count());

      PtrType new_obj = TestEnvTraits::CreateObject(OBJ_COUNT + (2 * i));
      ASSERT_NOT_NULL(new_obj);

      auto orig_raw = PtrTraits::GetRaw(new_obj);
      PtrType replaced = container().replace_if(
          [i](const ObjType& obj) -> bool { return (obj.value() == i); }, TakePtr(new_obj));

      EXPECT_NULL(new_obj);
      ASSERT_NOT_NULL(replaced);
      EXPECT_EQ(PtrTraits::GetRaw(replaced), orig_raw);
      EXPECT_FALSE(replaced->ContainerTraits::ContainableBaseClass::InContainer());
      EXPECT_EQ(OBJ_COUNT + (2 * i), replaced->value());
      EXPECT_EQ(OBJ_COUNT + 1, ObjType::live_obj_count());

      TestEnvTraits::ReleaseObject(replaced);
      EXPECT_EQ(OBJ_COUNT, ObjType::live_obj_count());
    }

    // The new objects we created (but failed to replace in the container) should now be gone.
    TestEnvTraits::CheckCustomDeleteInvocations((2 * OBJ_COUNT) + 1);

    // Make sure that the object are in order and have the values we expect.
    size_t i = 0;
    while (!container().is_empty()) {
      PtrType ptr = container().pop_front();
      EXPECT_EQ(OBJ_COUNT + i, ptr->value());
      TestEnvTraits::ReleaseObject(ptr);
      ++i;
    }
    EXPECT_EQ(0, ObjType::live_obj_count());

    // Now all of the objects we created during the test should be gone.
    TestEnvTraits::CheckCustomDeleteInvocations((3 * OBJ_COUNT) + 1);
  }

  void ReplaceCopy() {
    ASSERT_NO_FATAL_FAILURE(ContainerChecker::SanityCheck(container()));

    // Populate our container.
    for (size_t i = 0; i < OBJ_COUNT; ++i) {
      EXPECT_EQ(i, ObjType::live_obj_count());

      PtrType new_obj = TestEnvTraits::CreateObject(OBJ_COUNT - i - 1);
      ASSERT_NOT_NULL(new_obj);
      EXPECT_EQ(i + 1, ObjType::live_obj_count());

      container().push_front(std::move(new_obj));
    }
    ASSERT_NO_FATAL_FAILURE(ContainerChecker::SanityCheck(container()));

    // Replace all of the members of the contianer with new members which
    // have a value never created during the populate phase.
    for (size_t i = 0; i < OBJ_COUNT; ++i) {
      EXPECT_EQ(OBJ_COUNT, ObjType::live_obj_count());

      PtrType new_obj = TestEnvTraits::CreateObject(OBJ_COUNT + i);
      ASSERT_NOT_NULL(new_obj);

      auto iter =
          container().find_if([i](const ObjType& obj) -> bool { return (obj.value() == i); });

      ASSERT_TRUE(iter.IsValid());
      EXPECT_EQ(i, iter->value());

      PtrType replaced = container().replace(*iter, new_obj);

      ASSERT_NO_FATAL_FAILURE(ContainerChecker::SanityCheck(container()));
      ASSERT_NOT_NULL(replaced);
      EXPECT_TRUE(new_obj->ContainerTraits::ContainableBaseClass::InContainer());
      EXPECT_FALSE(replaced->ContainerTraits::ContainableBaseClass::InContainer());
      EXPECT_EQ(i, replaced->value());
      EXPECT_EQ(OBJ_COUNT + 1, ObjType::live_obj_count());

      TestEnvTraits::ReleaseObject(replaced);
      EXPECT_EQ(OBJ_COUNT, ObjType::live_obj_count());
    }

    TestEnvTraits::CheckCustomDeleteInvocations(OBJ_COUNT);

    // Make sure that the object are in order and have the values we expect.
    size_t i = 0;
    while (!container().is_empty()) {
      PtrType ptr = container().pop_front();
      EXPECT_EQ(OBJ_COUNT + i, ptr->value());
      TestEnvTraits::ReleaseObject(ptr);
      ++i;
    }
    EXPECT_EQ(0, ObjType::live_obj_count());
    ASSERT_NO_FATAL_FAILURE(ContainerChecker::SanityCheck(container()));
    TestEnvTraits::CheckCustomDeleteInvocations(2 * OBJ_COUNT);
  }

  void ReplaceMove() {
    // Populate our container.
    for (size_t i = 0; i < OBJ_COUNT; ++i) {
      EXPECT_EQ(i, ObjType::live_obj_count());

      PtrType new_obj = TestEnvTraits::CreateObject(OBJ_COUNT - i - 1);
      ASSERT_NOT_NULL(new_obj);
      EXPECT_EQ(i + 1, ObjType::live_obj_count());

      container().push_front(std::move(new_obj));
    }

    // Replace all of the members of the contianer with new members which
    // have a value never created during Populate().
    for (size_t i = 0; i < OBJ_COUNT; ++i) {
      EXPECT_EQ(OBJ_COUNT, ObjType::live_obj_count());

      PtrType new_obj = TestEnvTraits::CreateObject(OBJ_COUNT + i);
      ASSERT_NOT_NULL(new_obj);

      auto iter =
          container().find_if([i](const ObjType& obj) -> bool { return (obj.value() == i); });

      ASSERT_TRUE(iter.IsValid());
      EXPECT_EQ(i, iter->value());

      PtrType replaced = container().replace(*iter, TakePtr(new_obj));

      EXPECT_NULL(new_obj);
      ASSERT_NOT_NULL(replaced);
      EXPECT_FALSE(replaced->ContainerTraits::ContainableBaseClass::InContainer());
      EXPECT_EQ(i, replaced->value());
      EXPECT_EQ(OBJ_COUNT + 1, ObjType::live_obj_count());

      TestEnvTraits::ReleaseObject(replaced);
      EXPECT_EQ(OBJ_COUNT, ObjType::live_obj_count());
    }
    TestEnvTraits::CheckCustomDeleteInvocations(OBJ_COUNT);

    // Make sure that the object are in order and have the values we expect.
    size_t i = 0;
    while (!container().is_empty()) {
      PtrType ptr = container().pop_front();
      EXPECT_EQ(OBJ_COUNT + i, ptr->value());
      TestEnvTraits::ReleaseObject(ptr);
      ++i;
    }
    EXPECT_EQ(0, ObjType::live_obj_count());
    TestEnvTraits::CheckCustomDeleteInvocations(2 * OBJ_COUNT);
  }

 private:
  // Accessors for base class members so we don't have to type
  // this->base_member all of the time.
  using Sp = TestEnvironmentSpecialized<TestEnvTraits>;
  using Base = TestEnvironmentBase<TestEnvTraits>;
  static constexpr size_t OBJ_COUNT = Base::OBJ_COUNT;

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

#endif  // FBL_TESTS_INTRUSIVE_CONTAINERS_SEQUENCE_CONTAINER_TEST_ENVIRONMENT_H_
