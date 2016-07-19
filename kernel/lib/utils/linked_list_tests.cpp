// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <app/tests.h>
#include <err.h>
#include <unittest.h>
#include <utils/intrusive_single_list.h>
#include <utils/newcode_intrusive_double_list.h>
#include <utils/ref_counted.h>
#include <utils/ref_ptr.h>
#include <utils/unique_ptr.h>

namespace utils {

template <typename NodeStateType>
struct OtherListTraits {
    using PtrTraits = typename NodeStateType::PtrTraits;
    static NodeStateType& node_state(typename PtrTraits::RefType obj) {
        return obj.other_list_node_state_;
    }
};

template <typename PtrType>
class SLLTraits {
public:
    using ListType          = SinglyLinkedList<PtrType>;
    using ListableBaseClass = SinglyLinkedListable<PtrType>;
    using NodeStateType     = SinglyLinkedListNodeState<PtrType>;
    using OtherListType     = SinglyLinkedList<PtrType, OtherListTraits<NodeStateType>>;
};

template <typename PtrType>
class DLLTraits {
public:
    using ListType          = newcode::DoublyLinkedList<PtrType>;
    using ListableBaseClass = newcode::DoublyLinkedListable<PtrType>;
    using NodeStateType     = newcode::DoublyLinkedListNodeState<PtrType>;
    using OtherListType     = newcode::DoublyLinkedList<PtrType, OtherListTraits<NodeStateType>>;
};

class TestObjBase {
public:
    ~TestObjBase() { }

    static size_t live_obj_count() { return live_obj_count_; }
    static void ResetLiveObjCount() { live_obj_count_ = 0; }

protected:
    static size_t live_obj_count_;
};

size_t TestObjBase::live_obj_count_ = 0;

template <typename _ContainerTraits>
class TestObj : public TestObjBase,
                public _ContainerTraits::ListableBaseClass {
public:
    using ContainerTraits = _ContainerTraits;
    using NodeStateType   = typename ContainerTraits::NodeStateType;
    using PtrTraits       = typename NodeStateType::PtrTraits;

    explicit TestObj(size_t val) : val_(val) { live_obj_count_++; }
    ~TestObj() { live_obj_count_--; }

    size_t value() const { return val_; }
    const void* raw_ptr() const { return this; }

    bool operator==(const TestObj<ContainerTraits>& other) const { return this == &other; }
    bool operator!=(const TestObj<ContainerTraits>& other) const { return this != &other; }

private:
    friend class OtherListTraits<NodeStateType>;

    size_t val_;
    NodeStateType other_list_node_state_;
};

template <typename ContainerTraits>
class RefedTestObj : public TestObj<ContainerTraits>,
                     public RefCounted<RefedTestObj<ContainerTraits>> {
public:
    explicit RefedTestObj(size_t val) : TestObj<ContainerTraits>(val) { }
};

template <typename _ObjType>
struct UnmanagedTestTraits {
    using ObjType      = _ObjType;
    using PtrType      = ObjType*;
    using ConstPtrType = const ObjType*;
    using ListType     = typename ObjType::ContainerTraits::ListType;

    static PtrType CreateObject(size_t value) { return new ObjType(value); }

    // Unmanaged pointers never get cleared when being moved or transferred.
    static inline PtrType& Transfer(PtrType& ptr)       { return ptr; }
    static bool WasTransferred(const ConstPtrType& ptr) { return ptr != nullptr; }
    static bool WasMoved (const ConstPtrType& ptr)      { return ptr != nullptr; }
};

template <typename _ObjType>
struct UniquePtrTestTraits {
    using ObjType      = _ObjType;
    using PtrType      = ::utils::unique_ptr<ObjType>;
    using ConstPtrType = const PtrType;
    using ListType     = typename ObjType::ContainerTraits::ListType;

    static PtrType CreateObject(size_t value) { return PtrType(new ObjType(value)); }

    // Unique pointers always get cleared when being moved or transferred.
    static inline PtrType&& Transfer(PtrType& ptr)      { return utils::move(ptr); }
    static bool WasTransferred(const ConstPtrType& ptr) { return ptr == nullptr; }
    static bool WasMoved (const ConstPtrType& ptr)      { return ptr == nullptr; }
};

template <typename _ObjType>
struct RefPtrTestTraits {
    using ObjType      = _ObjType;
    using PtrType      = ::utils::RefPtr<ObjType>;
    using ConstPtrType = const PtrType;
    using ListType     = typename ObjType::ContainerTraits::ListType;

    static PtrType CreateObject(size_t value) { return AdoptRef(new ObjType(value)); }

    // RefCounted pointers do not get cleared when being transferred, but do get
    // cleared when being moved.
    static inline PtrType& Transfer(PtrType& ptr)       { return ptr; }
    static bool WasTransferred(const ConstPtrType& ptr) { return ptr != nullptr; }
    static bool WasMoved (const ConstPtrType& ptr)      { return ptr == nullptr; }
};

template <typename Traits>
class TestEnvironmentBase {
public:
    using ObjType   = typename Traits::ObjType;
    using PtrType   = typename Traits::PtrType;
    using ListType  = typename Traits::ListType;
    using PtrTraits = typename ListType::PtrTraits;

protected:
    PtrType CreateTrackedObject(size_t ndx, size_t value, bool ref_held) {
        if ((ndx >= OBJ_COUNT) ||objects_[ndx])
            return PtrType(nullptr);

        PtrType ret = Traits::CreateObject(value);
        if (ret == nullptr)
            return PtrType(nullptr);

        objects_[ndx] = PtrTraits::GetRaw(ret);

        if (ref_held)
            refs_held_++;

        return utils::move(ret);
    }

    static constexpr size_t OBJ_COUNT = 17;
    ListType  list_;
    ObjType*  objects_[OBJ_COUNT] = { nullptr };
    size_t    refs_held_ = 0;
};

template <typename Traits>
class TestEnvironmentSpecialized;

template <typename T>
class TestEnvironmentSpecialized<UnmanagedTestTraits<T>> :
    public TestEnvironmentBase<UnmanagedTestTraits<T>> {
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

    bool HoldingObject(size_t ndx) const {
        return ((ndx < OBJ_COUNT) && this->objects_[ndx]);
    }

    PtrType CreateTrackedObject(size_t ndx, size_t value, bool hold_ref = false) {
        return Base::CreateTrackedObject(ndx, value, true);
    }

    const PtrType& GetInternalReference(size_t ndx) {
        static const PtrType null_reference(nullptr);
        return HoldingObject(ndx) ? this->objects_[ndx] : null_reference;
    }
};

template <typename T>
class TestEnvironmentSpecialized<UniquePtrTestTraits<T>> :
    public TestEnvironmentBase<UniquePtrTestTraits<T>> {
protected:
    using Base = TestEnvironmentBase<UniquePtrTestTraits<T>>;
    using PtrType = typename Base::PtrType;
    static constexpr auto OBJ_COUNT = Base::OBJ_COUNT;

    void ReleaseObject(size_t ndx) {
        if (ndx < OBJ_COUNT)
            this->objects_[ndx] = nullptr;
    }

    bool HoldingObject(size_t ndx) const {
        return false;
    }

    PtrType CreateTrackedObject(size_t ndx, size_t value, bool hold_ref = false) {
        return Base::CreateTrackedObject(ndx, value, false);
    }

    // Note: GetInternalReference for a unique_ptr<> does not make a lot of
    // sense.  We cannot actually provide a reference, since we are not any.  We
    // provide an implementation (which returns nullptr) for only one reason.
    // If someone want to check to make sure that the build breaks when
    // attempting to make_iterator for a unique_ptr<> container, that the build
    // fails because the cannot expand the template which implements
    // make_iterator, not because the specialized test environment lacks an
    // implementation of GetInternalReference.
    const PtrType& GetInternalReference(size_t ndx) {
        static const PtrType null_reference(nullptr);
        return null_reference;
    }
};

template <typename T>
class TestEnvironmentSpecialized<RefPtrTestTraits<T>> :
    public TestEnvironmentBase<RefPtrTestTraits<T>> {
protected:
    using Base = TestEnvironmentBase<RefPtrTestTraits<T>>;
    using PtrType = typename Base::PtrType;
    static constexpr auto OBJ_COUNT = Base::OBJ_COUNT;

    PtrType CreateTrackedObject(size_t ndx, size_t value, bool hold_ref = false) {
        PtrType ret = Base::CreateTrackedObject(ndx, value, hold_ref);

        if (hold_ref)
            refed_objects_[ndx] = ret;

        return utils::move(ret);
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

    const PtrType& GetInternalReference(size_t ndx) {
        static const PtrType null_reference(nullptr);
        return HoldingObject(ndx) ? refed_objects_[ndx] : null_reference;
    }

private:
    PtrType refed_objects_[OBJ_COUNT];
};

template <typename Traits>
class TestEnvironment : public TestEnvironmentSpecialized<Traits> {
public:
    using ObjType         = typename Traits::ObjType;
    using PtrType         = typename Traits::PtrType;
    using ContainerTraits = typename ObjType::ContainerTraits;
    using ListType        = typename ContainerTraits::ListType;
    using OtherListType   = typename ContainerTraits::OtherListType;
    using PtrTraits       = typename ListType::PtrTraits;
    using SpBase          = TestEnvironmentSpecialized<Traits>;

    ~TestEnvironment() { Reset(); }

    // Utility methods used to check if the target of an Erase opertaion is
    // valid, whether the target of the operation is expressed as an iterator or
    // as an object pointer.
    bool ValidTarget(const typename ListType::iterator& target) { return target != list().end(); }
    bool ValidTarget(const PtrType& target) { return target != nullptr; }

    bool Reset() {
        BEGIN_TEST;

        list().clear();
        for (size_t i = 0; i < OBJ_COUNT; ++i)
            ReleaseObject(i);

        EXPECT_EQ(0u, refs_held(), "");
        refs_held() = 0;

        EXPECT_EQ(0u, ObjType::live_obj_count(), "");
        ObjType::ResetLiveObjCount();

        END_TEST;
    }

    bool Populate(bool hold_all_refs = false) {
        BEGIN_TEST;

        EXPECT_EQ(0U, ObjType::live_obj_count(), "");

        for (size_t i = 0; i < OBJ_COUNT; ++i) {
            size_t ndx = OBJ_COUNT - i - 1;
            EXPECT_EQ(i, list().size_slow(), "");

            // Unless explicitly told to do so, don't hold a reference in the
            // test environment for every 4th object created.  Note, this only
            // affects RefPtr tests.  Unmanaged pointers always hold an
            // unmanaged copy of the pointer (so it can be cleaned up), while
            // unique_ptr tests are not able to hold an extra copy of the
            // pointer (because it is unique)
            bool hold_ref = hold_all_refs || (i & 0x3);
            PtrType new_object = this->CreateTrackedObject(ndx, ndx, hold_ref);
            REQUIRE_NONNULL(new_object, "");
            EXPECT_EQ(new_object->raw_ptr(), objects()[ndx], "");

            // Alternate whether or not we move the pointer, or "transfer" it.
            // Transfering means different things for different pointer types.
            // For unmanaged, it just returns a reference to the pointer and
            // leaves the original unaltered.  For unique, it moves the pointer
            // (clearing the source).  For RefPtr, it makes a new RefPtr
            // instance, bumping the reference count in the process.
            if (i & 1) {
#if TEST_WILL_NOT_COMPILE || 0
                list().push_front(new_object);
#else
                list().push_front(Traits::Transfer(new_object));
#endif
                EXPECT_TRUE(Traits::WasTransferred(new_object), "");
            } else {
                list().push_front(utils::move(new_object));
                EXPECT_TRUE(Traits::WasMoved(new_object), "");
            }
        }

        EXPECT_EQ(OBJ_COUNT, list().size_slow(), "");
        EXPECT_EQ(OBJ_COUNT, ObjType::live_obj_count(), "");

        END_TEST;
    }

    bool Clear() {
        BEGIN_TEST;

        // Start by making some objects.
        REQUIRE_TRUE(Populate(), "");

        // Clear the list.  Afterwards, the number of live objects we have
        // should be equal to the number of references being held by the test
        // environment.
        list().clear();
        EXPECT_EQ(0u, list().size_slow(), "");
        EXPECT_EQ(refs_held(), ObjType::live_obj_count(), "");

        for (size_t i = 0; i < OBJ_COUNT; ++i) {
            EXPECT_NONNULL(objects()[i], "");

            // If our underlying object it still being kept alive by the test
            // environment, make sure that its next_ pointers have been properly
            // cleared out.
            if (HoldingObject(i)) {
                auto& ns = ListType::NodeTraits::node_state(*objects()[i]);
                EXPECT_NULL(PtrTraits::GetRaw(ns.next_), "");
            }
        }

        END_TEST;
    }

    bool IsEmpty() {
        BEGIN_TEST;

        EXPECT_TRUE(list().is_empty(), "");
        REQUIRE_TRUE(Populate(), "");
        EXPECT_FALSE(list().is_empty(), "");
        EXPECT_TRUE(Reset(), "");
        EXPECT_TRUE(list().is_empty(), "");

        END_TEST;
    }

    bool PopFront() {
        BEGIN_TEST;

        REQUIRE_TRUE(Populate(), "");

        // Remove elements using pop_front.  List should shrink each time we
        // remove an element, but the number of live objects should only shrink
        // when we let the last reference go out of scope.
        for (size_t i = 0; i < OBJ_COUNT; ++i) {
            size_t remaining = OBJ_COUNT - i;
            REQUIRE_TRUE(!list().is_empty(), "");
            EXPECT_EQ(remaining, ObjType::live_obj_count(), "");
            EXPECT_EQ(remaining, list().size_slow(), "");

            {
                // Pop the item and sanity check it against our tracking.
                PtrType tmp = list().pop_front();
                EXPECT_NONNULL(tmp, "");
                EXPECT_EQ(tmp->value(), i, "");
                EXPECT_EQ(objects()[i], tmp->raw_ptr(), "");

                // Make sure that the intrusive bookkeeping is up-to-date.
                auto& ns = ListType::NodeTraits::node_state(*tmp);
                EXPECT_NULL(ns.next_, "");

                // The list has shrunk, but the object should still be around.
                EXPECT_EQ(remaining, ObjType::live_obj_count(), "");
                EXPECT_EQ(remaining - 1, list().size_slow(), "");
            }

            // If we were not holding onto the object using the test
            // environment's tracking, the live object count should have
            // dropped.  Otherwise, it should remain the same.
            if (!HoldingObject(i))
                EXPECT_EQ(remaining - 1, ObjType::live_obj_count(), "");
            else
                EXPECT_EQ(remaining, ObjType::live_obj_count(), "");

            // Let go of the object and verify that it has now gone away.
            ReleaseObject(i);
            EXPECT_EQ(remaining - 1, ObjType::live_obj_count(), "");
        }

        // List should be empty now.  Popping anything else should result in a
        // null pointer.
        EXPECT_TRUE(list().is_empty(), "");
        PtrType should_be_null = list().pop_front();
        EXPECT_NULL(should_be_null, "");

        END_TEST;
    }

    bool PopBack() {
        BEGIN_TEST;

        REQUIRE_TRUE(Populate(), "");

        // Remove elements using pop_back.  List should shrink each time we
        // remove an element, but the number of live objects should only shrink
        // when we let the last reference go out of scope.
        for (size_t i = 0; i < OBJ_COUNT; ++i) {
            size_t remaining = OBJ_COUNT - i;
            size_t obj_ndx   = OBJ_COUNT - i - 1;
            REQUIRE_TRUE(!list().is_empty(), "");
            EXPECT_EQ(remaining, ObjType::live_obj_count(), "");
            EXPECT_EQ(remaining, list().size_slow(), "");

            {
                // Pop the item and sanity check it against our tracking.
                PtrType tmp = list().pop_back();
                EXPECT_NONNULL(tmp, "");
                EXPECT_EQ(tmp->value(), obj_ndx, "");
                EXPECT_EQ(objects()[obj_ndx], tmp->raw_ptr(), "");

                // Make sure that the intrusive bookkeeping is up-to-date.
                auto& ns = ListType::NodeTraits::node_state(*tmp);
                EXPECT_NULL(ns.next_, "");

                // The list has shrunk, but the object should still be around.
                EXPECT_EQ(remaining, ObjType::live_obj_count(), "");
                EXPECT_EQ(remaining - 1, list().size_slow(), "");
            }

            // If we were not holding onto the object using the test
            // environment's tracking, the live object count should have
            // dropped.  Otherwise, it should remain the same.
            if (!HoldingObject(obj_ndx))
                EXPECT_EQ(remaining - 1, ObjType::live_obj_count(), "");
            else
                EXPECT_EQ(remaining, ObjType::live_obj_count(), "");

            // Let go of the object and verify that it has now gone away.
            ReleaseObject(obj_ndx);
            EXPECT_EQ(remaining - 1, ObjType::live_obj_count(), "");
        }

        // List should be empty now.  Popping anything else should result in a
        // null pointer.
        EXPECT_TRUE(list().is_empty(), "");
        PtrType should_be_null = list().pop_back();
        EXPECT_NULL(should_be_null, "");

        END_TEST;
    }

    bool EraseNext() {
        BEGIN_TEST;

        REQUIRE_TRUE(Populate(), "");

        // Remove as many elements as we can using erase_next.
        auto iter = list().begin();
        for (size_t i = 1; i < OBJ_COUNT; ++i) {
            size_t remaining = OBJ_COUNT - i + 1;
            REQUIRE_TRUE(!list().is_empty(), "");
            REQUIRE_TRUE(iter != list().end(), "");
            EXPECT_EQ(remaining, ObjType::live_obj_count(), "");
            EXPECT_EQ(remaining, list().size_slow(), "");

            {
                // Erase the item and sanity check it against our tracking.
                PtrType tmp = list().erase_next(iter);
                EXPECT_NONNULL(tmp, "");
                EXPECT_EQ(tmp->value(), i, "");
                EXPECT_EQ(objects()[i], tmp->raw_ptr(), "");

                // Make sure that the intrusive bookkeeping is up-to-date.
                auto& ns = ListType::NodeTraits::node_state(*tmp);
                EXPECT_TRUE(ns.IsValid(), "");
                EXPECT_FALSE(ns.InContainer(), "");

                // The list has shrunk, but the object should still be around.
                EXPECT_EQ(remaining, ObjType::live_obj_count(), "");
                EXPECT_EQ(remaining - 1, list().size_slow(), "");
            }

            // If we were not holding onto the object using the test
            // environment's tracking, the live object count should have
            // dropped.  Otherwise, it should remain the same.
            if (!HoldingObject(i))
                EXPECT_EQ(remaining - 1, ObjType::live_obj_count(), "");
            else
                EXPECT_EQ(remaining, ObjType::live_obj_count(), "");

            // Let go of the object and verify that it has now gone away.
            ReleaseObject(i);
            EXPECT_EQ(remaining - 1, ObjType::live_obj_count(), "");
        }

        // Iterator should now be one away from the end, and there should be one
        // object left
        EXPECT_EQ(1u, ObjType::live_obj_count(), "");
        EXPECT_EQ(1u, list().size_slow(), "");
        EXPECT_TRUE(iter != list().end(), "");
        iter++;
        EXPECT_TRUE(iter == list().end(), "");

        END_TEST;
    }

    template <typename TargetType>
    bool DoErase(const TargetType& target, size_t ndx, size_t remaining) {
        BEGIN_TEST;

        REQUIRE_TRUE(ndx < OBJ_COUNT, "");
        REQUIRE_TRUE(remaining <= OBJ_COUNT, "");
        REQUIRE_TRUE(!list().is_empty(), "");
        REQUIRE_TRUE(ValidTarget(target), "");
        EXPECT_EQ(remaining, ObjType::live_obj_count(), "");
        EXPECT_EQ(remaining, list().size_slow(), "");

        {
            // Erase the item and sanity check it against our tracking.
            PtrType tmp = list().erase(target);
            EXPECT_NONNULL(tmp, "");
            EXPECT_EQ(tmp->value(), ndx, "");
            EXPECT_EQ(objects()[ndx], tmp->raw_ptr(), "");

            // Make sure that the intrusive bookkeeping is up-to-date.
            auto& ns = ListType::NodeTraits::node_state(*tmp);
            EXPECT_TRUE(ns.IsValid(), "");
            EXPECT_FALSE(ns.InContainer(), "");

            // The list has shrunk, but the object should still be around.
            EXPECT_EQ(remaining, ObjType::live_obj_count(), "");
            EXPECT_EQ(remaining - 1, list().size_slow(), "");
        }

        // If we were not holding onto the object using the test
        // environment's tracking, the live object count should have
        // dropped.  Otherwise, it should remain the same.
        if (!HoldingObject(ndx))
            EXPECT_EQ(remaining - 1, ObjType::live_obj_count(), "");
        else
            EXPECT_EQ(remaining, ObjType::live_obj_count(), "");

        // Let go of the object and verify that it has now gone away.
        ReleaseObject(ndx);
        EXPECT_EQ(remaining - 1, ObjType::live_obj_count(), "");

        END_TEST;
    }

    bool Erase() {
        BEGIN_TEST;

        // Remove all of the elements from the list by erasing from the front.
        REQUIRE_TRUE(Populate(), "");
        for (size_t i = 0; i < OBJ_COUNT; ++i)
            EXPECT_TRUE(DoErase(list().begin(), i, OBJ_COUNT - i), "");

        EXPECT_EQ(0u, ObjType::live_obj_count(), "");
        EXPECT_EQ(0u, list().size_slow(), "");

        // Remove all of the elements from the list by erasing from the back.
        REQUIRE_TRUE(Populate(), "");
        auto iter = list().end();
        iter--;
        for (size_t i = 0; i < OBJ_COUNT; ++i)
            EXPECT_TRUE(DoErase(iter--, OBJ_COUNT - i - 1, OBJ_COUNT - i), "");

        EXPECT_EQ(0u, ObjType::live_obj_count(), "");
        EXPECT_EQ(0u, list().size_slow(), "");

        // Remove all but 2 of the elements from the list by erasing from the middle.
        static_assert(2 < OBJ_COUNT, "OBJ_COUNT too small to run Erase test!");
        REQUIRE_TRUE(Populate(), "");
        iter = list().begin();
        iter++;
        for (size_t i = 1; i < OBJ_COUNT - 1; ++i)
            EXPECT_TRUE(DoErase(iter++, i, OBJ_COUNT - i + 1), "");

        // Attempting to erase end() from a list with more than one element in
        // it should return nullptr.
        EXPECT_NULL(list().erase(list().end()), "");
        EXPECT_TRUE(DoErase(list().begin(), 0, 2), "");

        // Attempting to erase end() from a list with just one element in
        // it should return nullptr.
        EXPECT_NULL(list().erase(list().end()), "");
        EXPECT_TRUE(DoErase(list().begin(), OBJ_COUNT - 1, 1), "");

        // Attempting to erase end() from an empty list should return nullptr.
        EXPECT_EQ(0u, ObjType::live_obj_count(), "");
        EXPECT_EQ(0u, list().size_slow(), "");
        EXPECT_NULL(list().erase(list().end()), "");

        END_TEST;
    }

    bool DirectErase() {
        BEGIN_TEST;

        // Remove all of the elements from the list by erasing using direct node
        // pointers which should end up always being at the front of the list.
        REQUIRE_TRUE(Populate(true), "");
        for (size_t i = 0; i < OBJ_COUNT; ++i)
            EXPECT_TRUE(DoErase(SpBase::GetInternalReference(i), i, OBJ_COUNT - i), "");

        EXPECT_EQ(0u, ObjType::live_obj_count(), "");
        EXPECT_EQ(0u, list().size_slow(), "");

        // Remove all of the elements from the list by erasing using direct node
        // pointers which should end up always being at the back of the list.
        REQUIRE_TRUE(Populate(true), "");
        for (size_t i = 0; i < OBJ_COUNT; ++i) {
            size_t ndx = OBJ_COUNT - i - 1;
            EXPECT_TRUE(DoErase(SpBase::GetInternalReference(ndx), ndx, ndx + 1), "");
        }

        EXPECT_EQ(0u, ObjType::live_obj_count(), "");
        EXPECT_EQ(0u, list().size_slow(), "");

        // Remove all of the elements from the list by erasing using direct node
        // pointers which should end up always being somewhere in the middle of
        // the list.
        static_assert(2 < OBJ_COUNT, "OBJ_COUNT too small to run Erase test!");
        REQUIRE_TRUE(Populate(true), "");
        for (size_t i = 1; i < OBJ_COUNT - 1; ++i)
            EXPECT_TRUE(DoErase(SpBase::GetInternalReference(i), i, OBJ_COUNT - i + 1), "");

        // Attempting to erase a nullptr from a list with more than one element
        // in it should return nullptr.
        EXPECT_NULL(PtrType(nullptr), "");
        EXPECT_TRUE(DoErase(SpBase::GetInternalReference(0), 0, 2), "");

        // Attempting to erase a nullptr from a list with just one element in
        // it should return nullptr.
        EXPECT_NULL(PtrType(nullptr), "");
        EXPECT_TRUE(DoErase(SpBase::GetInternalReference(OBJ_COUNT - 1), OBJ_COUNT - 1, 1), "");

        // Attempting to erase a nullptr from an empty list should return nullptr.
        EXPECT_EQ(0u, ObjType::live_obj_count(), "");
        EXPECT_EQ(0u, list().size_slow(), "");
        EXPECT_NULL(PtrType(nullptr), "");

        END_TEST;
    }

    template <typename IterType>
    bool DoIterate(const IterType& begin, const IterType& end) {
        BEGIN_TEST;
        IterType iter;

        // Iterate using begin/end
        size_t i = 0;
        for (iter = begin; iter != end; ) {
            // Exercise both -> and * dereferencing
            REQUIRE_TRUE(iter.IsValid(), "");
            EXPECT_EQ(objects()[i],   iter->raw_ptr(), "");
            EXPECT_EQ(objects()[i], (*iter).raw_ptr(), "");
            EXPECT_EQ(i,   iter->value(), "");
            EXPECT_EQ(i, (*iter).value(), "");

            // Exercise both pre and postfix increment
            if ((i++) & 1) iter++;
            else           ++iter;
        }
        EXPECT_FALSE(iter.IsValid(), "");

        // Advancing iter past the end of the list should be a no-op.  Check
        // both pre and post-fix.
        iter = end;
        ++iter;
        EXPECT_FALSE(iter.IsValid(), "");
        EXPECT_TRUE(iter == end, "");

        // We know that the iterator  is already at the end of the list, but
        // perform the explicit assignment in order to check that the assignment
        // operator is working (the previous version actually exercises the copy
        // constructor or the explicit rvalue constructor, if supplied)
        iter = end;
        iter++;
        EXPECT_FALSE(iter.IsValid(), "");
        EXPECT_TRUE(iter == end, "");

        END_TEST;
    }

    bool Iterate() {
        BEGIN_TEST;

        // Start by making some objects.
        REQUIRE_TRUE(Populate(), "");
        EXPECT_EQ(OBJ_COUNT, list().size_slow(), "");

        EXPECT_TRUE(DoIterate(list().begin(),  list().end()), "");   // Test iterator
        EXPECT_TRUE(DoIterate(list().cbegin(), list().cend()), "");  // Test const_iterator

        // Iterate using the range-based for loop syntax
        size_t i = 0;
        for (auto& obj : list()) {
            EXPECT_EQ(objects()[i], &obj, "");
            EXPECT_EQ(objects()[i], obj.raw_ptr(), "");
            EXPECT_EQ(i, obj.value(), "");
            i++;
        }

        // Iterate using the range-based for loop syntax over const references.
        i = 0;
        for (const auto& obj : list()) {
            EXPECT_EQ(objects()[i], &obj, "");
            EXPECT_EQ(objects()[i], obj.raw_ptr(), "");
            EXPECT_EQ(i, obj.value(), "");
            i++;
        }

        END_TEST;
    }

    template <typename IterType>
    bool DoIterateDec(const IterType& begin, const IterType& end) {
        BEGIN_TEST;
        IterType iter;

        // Backing up one from end() should give us back().  Check both pre
        // and post-fix behavior.
        iter = end; --iter;
        REQUIRE_TRUE(iter.IsValid(), "");
        REQUIRE_TRUE(iter != end, "");
        EXPECT_TRUE(list().back() == *iter, "");

        iter = end; iter--;
        REQUIRE_TRUE(iter.IsValid(), "");
        REQUIRE_TRUE(iter != end, "");
        EXPECT_TRUE(list().back() == *iter, "");

        // Make sure that backing up an iterator by one points always points
        // to the previous object in the list.
        iter = begin;
        while (++iter != end) {
            size_t prev_ndx = iter->value() - 1;
            REQUIRE_LT(prev_ndx, OBJ_COUNT, "");
            REQUIRE_NONNULL(objects()[prev_ndx], "");

            auto prev_iter = iter;
            --prev_iter;
            REQUIRE_TRUE(prev_iter.IsValid(), "");
            EXPECT_FALSE(prev_iter == iter, "");
            EXPECT_TRUE(*prev_iter == *objects()[prev_ndx], "");

            prev_iter = iter;
            prev_iter--;
            REQUIRE_TRUE(prev_iter.IsValid(), "");
            EXPECT_FALSE(prev_iter == iter, "");
            EXPECT_TRUE(*prev_iter == *objects()[prev_ndx], "");
        }

        // Attempting to back up past the beginning should result in an
        // invalid iterator.
        iter = begin;
        REQUIRE_TRUE(iter.IsValid(), "");
        EXPECT_TRUE(list().front() == *iter, "");
        --iter;
        EXPECT_FALSE(iter.IsValid(), "");

        iter = begin;
        REQUIRE_TRUE(iter.IsValid(), "");
        EXPECT_TRUE(list().front() == *iter, "");
        iter--;
        EXPECT_FALSE(iter.IsValid(), "");

        END_TEST;
    }

    bool IterateDec() {
        BEGIN_TEST;

        // Start by making some objects.
        REQUIRE_TRUE(Populate(), "");
        EXPECT_EQ(OBJ_COUNT, list().size_slow(), "");

        EXPECT_TRUE(DoIterateDec(list().begin(),  list().end()), "");   // Test iterator
        EXPECT_TRUE(DoIterateDec(list().cbegin(), list().cend()), "");  // Test const_iterator

        END_TEST;
    }

    bool MakeIterator() {
        BEGIN_TEST;

        // Populate the list.  Hold internal refs to everything we add to the
        // list.
        REQUIRE_TRUE(Populate(true), "");

        // For every member of the list, make an iterator using the internal
        // reference we are holding.  Verify that the iterator is in the
        // position we expect it to be in.
        for (size_t i = 0; i < OBJ_COUNT; ++i) {
            auto iter = list().make_iterator(SpBase::GetInternalReference(i));

            REQUIRE_TRUE(iter != list().end(), "");
            EXPECT_EQ(objects()[i]->value(), iter->value(), "");
            EXPECT_EQ(objects()[i], iter->raw_ptr(), "");

            auto other_iter = list().begin();
            for (size_t j = 0; j < i; ++j) {
                EXPECT_FALSE(other_iter == iter, "");
                ++other_iter;
            }

            EXPECT_TRUE(other_iter == iter, "");
        }

        // Creating an iterator using nullptr should result in an iterator which
        // is equal to end().
        PtrType null_ptr(nullptr);
        auto iter       = list().make_iterator(null_ptr);
        auto other_iter = list().begin();
        for (size_t i = 0; i < OBJ_COUNT; ++i) {
            EXPECT_FALSE(other_iter == iter, "");
            other_iter++;
        }

        EXPECT_TRUE(iter       == list().end(), "");
        EXPECT_TRUE(other_iter == list().end(), "");
        EXPECT_TRUE(other_iter == iter, "");

        END_TEST;
    }

    template <typename IterType>
    bool DoInsertAfter(IterType&& iter, size_t pos) {
        BEGIN_TEST;

        EXPECT_EQ(ObjType::live_obj_count(), list().size_slow(), "");
        EXPECT_TRUE(iter != list().end(), "");

        size_t orig_list_len = ObjType::live_obj_count();
        size_t orig_iter_pos = iter->value();

        REQUIRE_LT(orig_iter_pos, OBJ_COUNT, "");
        EXPECT_EQ(objects()[orig_iter_pos], iter->raw_ptr(), "");

        PtrType new_object = this->CreateTrackedObject(pos, pos, true);
        REQUIRE_NONNULL(new_object, "");
        EXPECT_EQ(new_object->raw_ptr(), objects()[pos], "");

        if (pos & 1) {
#if TEST_WILL_NOT_COMPILE || 0
            list().insert_after(iter, new_object);
#else
            list().insert_after(iter, Traits::Transfer(new_object));
#endif
            EXPECT_TRUE(Traits::WasTransferred(new_object), "");
        } else {
            list().insert_after(iter, utils::move(new_object));
            EXPECT_TRUE(Traits::WasMoved(new_object), "");
        }

        // List and number of live object should have grown.
        EXPECT_EQ(orig_list_len + 1, ObjType::live_obj_count(), "");
        EXPECT_EQ(orig_list_len + 1, list().size_slow(), "");

        // The iterator should not have moved yet.
        EXPECT_TRUE(iter != list().end(), "");
        EXPECT_EQ(objects()[orig_iter_pos], iter->raw_ptr(), "");
        EXPECT_EQ(orig_iter_pos, iter->value(), "");

        END_TEST;
    }

    bool InsertAfter() {
        BEGIN_TEST;

        // In order to insert_after, we need at least one object already in the
        // list.  Use push_front to make one.
        EXPECT_EQ(0u, ObjType::live_obj_count(), "");
        EXPECT_EQ(0U, list().size_slow(), "");
        EXPECT_TRUE(list().is_empty(), "");
        list().push_front(utils::move(this->CreateTrackedObject(0, 0, true)));

        // Insert some elements after the last element list.
        static constexpr size_t END_INSERT_COUNT = 2;
        static_assert(END_INSERT_COUNT <= OBJ_COUNT,
                      "OBJ_COUNT too small to run InsertAfter test!");

        auto iter = list().begin();
        for (size_t i = (OBJ_COUNT - END_INSERT_COUNT); i < OBJ_COUNT; ++i) {
            REQUIRE_TRUE(DoInsertAfter(iter, i), "");

            // Now that we have inserted after, we should be able to advance the
            // iterator to what we just inserted.
            iter++;

            REQUIRE_TRUE(iter != list().end(), "");
            EXPECT_EQ(objects()[i], iter->raw_ptr(), "");
            EXPECT_EQ(objects()[i], (*iter).raw_ptr(), "");
            EXPECT_EQ(i, iter->value(), "");
            EXPECT_EQ(i, (*iter).value(), "");
        }

        // Advancing iter at this point should bring it to the end.
        EXPECT_TRUE(iter != list().end(), "");
        iter++;
        EXPECT_TRUE(iter == list().end(), "");

        // Reset the iterator to the first element in the list, and test
        // inserting between elements instead of at the end.  To keep the
        // final list in order, we need to insert in reverse order and to not
        // advance the iterator in the process.
        iter = list().begin();
        for (size_t i = (OBJ_COUNT - END_INSERT_COUNT - 1); i > 0; --i) {
            REQUIRE_TRUE(DoInsertAfter(iter, i), "");
        }
        EXPECT_TRUE(iter != list().end(), "");

        // Check to make sure the list has the expected number of elements, and
        // that they are in the proper order.
        EXPECT_EQ(OBJ_COUNT, ObjType::live_obj_count(), "");
        EXPECT_EQ(OBJ_COUNT, list().size_slow(), "");

        size_t i = 0;
        for (const auto& obj : list()) {
            EXPECT_EQ(objects()[i], &obj, "");
            EXPECT_EQ(objects()[i], obj.raw_ptr(), "");
            EXPECT_EQ(i, obj.value(), "");
            i++;
        }

        END_TEST;
    }

    template <typename TargetType>
    bool DoInsert(const TargetType& target, size_t pos) {
        BEGIN_TEST;

        EXPECT_EQ(ObjType::live_obj_count(), list().size_slow(), "");
        size_t orig_list_len = ObjType::live_obj_count();

        PtrType new_object = this->CreateTrackedObject(pos, pos, true);
        REQUIRE_NONNULL(new_object, "");
        EXPECT_EQ(new_object->raw_ptr(), objects()[pos], "");

        if (pos & 1) {
#if TEST_WILL_NOT_COMPILE || 0
            list().insert(target, new_object);
#else
            list().insert(target, Traits::Transfer(new_object));
#endif
            EXPECT_TRUE(Traits::WasTransferred(new_object), "");
        } else {
            list().insert(target, utils::move(new_object));
            EXPECT_TRUE(Traits::WasMoved(new_object), "");
        }

        // List and number of live object should have grown.
        EXPECT_EQ(orig_list_len + 1, ObjType::live_obj_count(), "");
        EXPECT_EQ(orig_list_len + 1, list().size_slow(), "");

        END_TEST;
    }

    bool Insert() {
        BEGIN_TEST;

        EXPECT_EQ(0u, ObjType::live_obj_count(), "");
        EXPECT_EQ(0U, list().size_slow(), "");

        static constexpr size_t END_INSERT_COUNT   = 3;
        static constexpr size_t START_INSERT_COUNT = 3;
        static constexpr size_t MID_INSERT_COUNT   = OBJ_COUNT
                                                   - START_INSERT_COUNT - END_INSERT_COUNT;
        static_assert((END_INSERT_COUNT <= OBJ_COUNT) &&
                      (START_INSERT_COUNT <= (OBJ_COUNT - END_INSERT_COUNT)) &&
                      ((START_INSERT_COUNT + END_INSERT_COUNT) < OBJ_COUNT),
                      "OBJ_COUNT too small to run Insert test!");

        // Insert some elements at the end of an initially empty list using the
        // end() iterator accessor.
        for (size_t i = (OBJ_COUNT - END_INSERT_COUNT); i < OBJ_COUNT; ++i)
            REQUIRE_TRUE(DoInsert(list().end(), i), "");

        // Insert some elements at the start of a non-empty list using the
        // begin() iterator accessor.
        for (size_t i = 0; i < START_INSERT_COUNT; ++i) {
            size_t ndx = START_INSERT_COUNT - i - 1;
            REQUIRE_TRUE(DoInsert(list().begin(), ndx), "");
        }

        // Insert some elements in the middle non-empty list using an iterator
        // we compute.
        auto iter = list().begin();
        for (size_t i = 0; i < START_INSERT_COUNT; ++i)
            ++iter;

        for (size_t i = 0; i < MID_INSERT_COUNT; ++i) {
            size_t ndx = START_INSERT_COUNT + i;
            REQUIRE_TRUE(DoInsert(iter, ndx), "");
        }

        // iter should be END_INSERT_COUNT from the end of the
        // list.
        for (size_t i = 0; i < END_INSERT_COUNT; ++i) {
            EXPECT_TRUE(iter != list().end(), "");
            ++iter;
        }
        EXPECT_TRUE(++iter == list().end(), "");

        // Check to make sure the list has the expected number of elements, and
        // that they are in the proper order.
        EXPECT_EQ(OBJ_COUNT, ObjType::live_obj_count(), "");
        EXPECT_EQ(OBJ_COUNT, list().size_slow(), "");

        size_t i = 0;
        for (const auto& obj : list()) {
            REQUIRE_LT(i, OBJ_COUNT, "");
            EXPECT_EQ(objects()[i], &obj, "");
            EXPECT_EQ(objects()[i], obj.raw_ptr(), "");
            EXPECT_EQ(i, obj.value(), "");
            i++;
        }

        END_TEST;
    }

    bool DirectInsert() {
        BEGIN_TEST;

        EXPECT_EQ(0u, ObjType::live_obj_count(), "");
        EXPECT_EQ(0U, list().size_slow(), "");

        static constexpr size_t END_INSERT_COUNT   = 3;
        static constexpr size_t START_INSERT_COUNT = 3;
        static constexpr size_t MID_INSERT_COUNT   = OBJ_COUNT
                                                   - START_INSERT_COUNT - END_INSERT_COUNT;
        static_assert((END_INSERT_COUNT <= OBJ_COUNT) &&
                      (START_INSERT_COUNT <= (OBJ_COUNT - END_INSERT_COUNT)) &&
                      ((START_INSERT_COUNT + END_INSERT_COUNT) < OBJ_COUNT),
                      "OBJ_COUNT too small to run DirectInsert test!");

        // Insert some elements at the end of an initially empty list using
        // nullptr as the obj target.
        for (size_t i = (OBJ_COUNT - END_INSERT_COUNT); i < OBJ_COUNT; ++i)
            REQUIRE_TRUE(DoInsert(PtrType(nullptr), i), "");

        // Insert some elements at the start of a non-empty list node pointers
        // which are always at the start of the list.
        size_t insert_before_ndx = (OBJ_COUNT - END_INSERT_COUNT);
        for (size_t i = 0; i < START_INSERT_COUNT; ++i) {
            size_t ndx = START_INSERT_COUNT - i - 1;
            REQUIRE_TRUE(DoInsert(SpBase::GetInternalReference(insert_before_ndx), ndx), "");
            insert_before_ndx = ndx;
        }

        // Insert some elements in the middle non-empty list.
        insert_before_ndx = (OBJ_COUNT - END_INSERT_COUNT);
        for (size_t i = 0; i < MID_INSERT_COUNT; ++i) {
            size_t ndx = START_INSERT_COUNT + i;
            REQUIRE_TRUE(DoInsert(SpBase::GetInternalReference(insert_before_ndx), ndx), "");
        }

        // Check to make sure the list has the expected number of elements, and
        // that they are in the proper order.
        EXPECT_EQ(OBJ_COUNT, ObjType::live_obj_count(), "");
        EXPECT_EQ(OBJ_COUNT, list().size_slow(), "");

        size_t i = 0;
        for (const auto& obj : list()) {
            REQUIRE_LT(i, OBJ_COUNT, "");
            EXPECT_EQ(objects()[i], &obj, "");
            EXPECT_EQ(objects()[i], obj.raw_ptr(), "");
            EXPECT_EQ(i, obj.value(), "");
            i++;
        }

        END_TEST;
    }

    bool PushBack() {
        BEGIN_TEST;

        EXPECT_EQ(0U, ObjType::live_obj_count(), "");

        for (size_t i = 0; i < OBJ_COUNT; ++i) {
            EXPECT_EQ(i, list().size_slow(), "");

            PtrType new_object = this->CreateTrackedObject(i, i);
            REQUIRE_NONNULL(new_object, "");
            EXPECT_EQ(new_object->raw_ptr(), objects()[i], "");

            // Alternate whether or not we move the pointer, or "transfer" it.
            if (i & 1) {
#if TEST_WILL_NOT_COMPILE || 0
                list().push_back(new_object);
#else
                list().push_back(Traits::Transfer(new_object));
#endif
                EXPECT_TRUE(Traits::WasTransferred(new_object), "");
            } else {
                list().push_back(utils::move(new_object));
                EXPECT_TRUE(Traits::WasMoved(new_object), "");
            }
        }

        EXPECT_EQ(OBJ_COUNT, list().size_slow(), "");
        EXPECT_EQ(OBJ_COUNT, ObjType::live_obj_count(), "");

        size_t i = 0;
        for (const auto& obj : list()) {
            REQUIRE_LT(i, OBJ_COUNT, "");
            EXPECT_EQ(objects()[i]->value(), obj.value(), "");
            EXPECT_EQ(objects()[i], obj.raw_ptr(), "");
            i++;
        }

        END_TEST;
    }

    bool Swap() {
        BEGIN_TEST;
        size_t i;

        {
            ListType other_list; // Make an empty list.
            REQUIRE_TRUE(Populate(), ""); // Fill the internal list with stuff.

            // Sanity check, swap, then check again.
            EXPECT_EQ(OBJ_COUNT, ObjType::live_obj_count(), "");
            EXPECT_FALSE(list().is_empty(), "");
            EXPECT_EQ(OBJ_COUNT, list().size_slow(), "");
            EXPECT_TRUE(other_list.is_empty(), "");

            i = 0;
            for (const auto& obj : list()) {
                EXPECT_EQ(i, obj.value(), "");
                i++;
            }

            list().swap(other_list);

            EXPECT_EQ(OBJ_COUNT, ObjType::live_obj_count(), "");
            EXPECT_FALSE(other_list.is_empty(), "");
            EXPECT_EQ(OBJ_COUNT, other_list.size_slow(), "");
            EXPECT_TRUE(list().is_empty(), "");

            i = 0;
            for (const auto& obj : other_list) {
                EXPECT_EQ(i, obj.value(), "");
                i++;
            }

            // Swap back to check the case where list() was empty, but other_list
            // had elements.
            list().swap(other_list);

            EXPECT_EQ(OBJ_COUNT, ObjType::live_obj_count(), "");
            EXPECT_FALSE(list().is_empty(), "");
            EXPECT_EQ(OBJ_COUNT, list().size_slow(), "");
            EXPECT_TRUE(other_list.is_empty(), "");

            i = 0;
            for (const auto& obj : list()) {
                EXPECT_EQ(i, obj.value(), "");
                i++;
            }

            // Reset;
            EXPECT_TRUE(Reset(), "");
        }

        // Make a new other_list, this time with some stuff in it.
        EXPECT_EQ(0u, ObjType::live_obj_count(), "");
        {
            ListType other_list; // Make an empty list.
            REQUIRE_TRUE(Populate(), ""); // Fill the internal list with stuff.

            static constexpr size_t OTHER_COUNT = 5;
            static constexpr size_t OTHER_START = 50000;
            ObjType* raw_ptrs[OTHER_COUNT];

            for (i = 0; i < OTHER_COUNT; ++i) {
                PtrType ptr = Traits::CreateObject(OTHER_START + OTHER_COUNT - i - 1);
                raw_ptrs[i] = PtrTraits::GetRaw(ptr);
                other_list.push_front(utils::move(ptr));
            }

            // Sanity check
            EXPECT_EQ(OBJ_COUNT + OTHER_COUNT, ObjType::live_obj_count(), "");
            EXPECT_EQ(OBJ_COUNT, list().size_slow(), "");
            EXPECT_EQ(OTHER_COUNT, other_list.size_slow(), "");

            i = 0;
            for (const auto& obj : list()) {
                EXPECT_EQ(i, obj.value(), "");
                i++;
            }

            i = OTHER_START;
            for (const auto& obj : other_list) {
                EXPECT_EQ(i, obj.value(), "");
                i++;
            }

            // Swap and sanity check again
            list().swap(other_list);

            EXPECT_EQ(OBJ_COUNT + OTHER_COUNT, ObjType::live_obj_count(), "");
            EXPECT_EQ(OBJ_COUNT, other_list.size_slow(), "");
            EXPECT_EQ(OTHER_COUNT, list().size_slow(), "");

            i = OTHER_START;
            for (const auto& obj : list()) {
                EXPECT_EQ(i, obj.value(), "");
                i++;
            }

            i = 0;
            for (const auto& obj : other_list) {
                EXPECT_EQ(i, obj.value(), "");
                i++;
            }

            // Swap back and sanity check again
            list().swap(other_list);

            EXPECT_EQ(OBJ_COUNT + OTHER_COUNT, ObjType::live_obj_count(), "");
            EXPECT_EQ(OBJ_COUNT, list().size_slow(), "");
            EXPECT_EQ(OTHER_COUNT, other_list.size_slow(), "");

            i = 0;
            for (const auto& obj : list()) {
                EXPECT_EQ(i, obj.value(), "");
                i++;
            }

            i = OTHER_START;
            for (const auto& obj : other_list) {
                EXPECT_EQ(i, obj.value(), "");
                i++;
            }

            // If we are testing unmanaged pointers clean them up.
            EXPECT_EQ(OBJ_COUNT + OTHER_COUNT, ObjType::live_obj_count(), "");
            other_list.clear();
            if (!PtrTraits::IsManaged) {
                EXPECT_EQ(OBJ_COUNT + OTHER_COUNT, ObjType::live_obj_count(), "");
                for (i = 0; i < OTHER_COUNT; ++i)
                    delete raw_ptrs[i];
            }
            EXPECT_EQ(OBJ_COUNT, ObjType::live_obj_count(), "");

            // Reset the internal state
            EXPECT_TRUE(Reset(), "");
            EXPECT_EQ(0u, ObjType::live_obj_count(), "");
        }


        END_TEST;
    }

    bool RvalueOps() {
        BEGIN_TEST;
        size_t i;

        // Populate the internal list.
        REQUIRE_TRUE(Populate(), "");
        EXPECT_EQ(OBJ_COUNT, ObjType::live_obj_count(), "");
        EXPECT_EQ(OBJ_COUNT, list().size_slow(), "");
        i = 0;
        for (const auto& obj : list()) {
            EXPECT_EQ(i, obj.value(), "");
            i++;
        }

        // Move its contents to a new list using the Rvalue constructor.
#if TEST_WILL_NOT_COMPILE || 0
        ListType other_list(list());
#else
        ListType other_list(utils::move(list()));
#endif
        EXPECT_EQ(OBJ_COUNT, ObjType::live_obj_count(), "");
        EXPECT_EQ(OBJ_COUNT, other_list.size_slow(), "");
        EXPECT_TRUE(list().is_empty(), "");
        i = 0;
        for (const auto& obj : other_list) {
            EXPECT_EQ(i, obj.value(), "");
            i++;
        }

        // Move the contents of the other list back to the internal list.  If we
        // are testing managed pointer types, put some objects into the internal
        // list first and make sure they get released.  Don't try this with
        // unmanaged pointers as it will trigger an assert if you attempt to
        // blow away a non-empty list via Rvalue assignment.
        static constexpr size_t EXTRA_COUNT = 5;
        size_t extras_added = 0;
        if (PtrTraits::IsManaged) {
            while (extras_added < EXTRA_COUNT)
                list().push_front(utils::move(Traits::CreateObject(extras_added++)));
        }

        // Sanity checks before the assignment
        EXPECT_EQ(OBJ_COUNT + extras_added, ObjType::live_obj_count(), "");
        EXPECT_EQ(extras_added, list().size_slow(), "");
        i = 1;
        for (const auto& obj : list()) {
            EXPECT_EQ(EXTRA_COUNT - i, obj.value(), "");
            i++;
        }

#if TEST_WILL_NOT_COMPILE || 0
        list() = other_list;
#else
        list() = utils::move(other_list);
#endif

        // other_list should now be empty, and we should have returned to our
        // starting, post-populated state.
        EXPECT_EQ(OBJ_COUNT, ObjType::live_obj_count(), "");
        EXPECT_EQ(OBJ_COUNT, list().size_slow(), "");
        EXPECT_TRUE(other_list.is_empty(), "");
        i = 0;
        for (const auto& obj : list()) {
            EXPECT_EQ(i, obj.value(), "");
            i++;
        }

        END_TEST;
    }

    bool TwoList() {
        BEGIN_TEST;

        // Start by populating the internal list.  We should end up with
        // OBJ_COUNT objects, but we may not be holding internal references to
        // all of them.
        REQUIRE_TRUE(Populate(), "");

        // Create the other type of list that ObjType can exist on and populate
        // it using push_front.
        OtherListType other_list;
        for (auto iter = list().begin(); iter != list().end(); ++iter)
            other_list.push_front(utils::move(iter.CopyPointer()));

        // The two lists should be the same length, and nothing should have
        // changed about the live object count.
        EXPECT_EQ(OBJ_COUNT, ObjType::live_obj_count(), "");
        EXPECT_EQ(OBJ_COUNT, list().size_slow(), "");
        EXPECT_EQ(OBJ_COUNT, other_list.size_slow(), "");

        // other_list should be in the reverse order of list()
        auto other_iter = other_list.begin();
        for (const auto& obj : list()) {
            REQUIRE_FALSE(other_iter == other_list.end(), "");
            EXPECT_EQ(OBJ_COUNT - obj.value() - 1, other_iter->value(), "");
            ++other_iter;
        }
        EXPECT_TRUE(other_iter == other_list.end(), "");

        // Clear the internal list.  No objects should go away and the other
        // list should be un-affected
        list().clear();

        EXPECT_EQ(OBJ_COUNT, ObjType::live_obj_count(), "");
        EXPECT_EQ(0u, list().size_slow(), "");
        EXPECT_EQ(OBJ_COUNT, other_list.size_slow(), "");

        other_iter = other_list.begin();
        for (size_t i = 0; i < OBJ_COUNT; ++i) {
            REQUIRE_FALSE(other_iter == other_list.end(), "");
            EXPECT_EQ(OBJ_COUNT - i - 1, other_iter->value(), "");
            ++other_iter;
        }
        EXPECT_TRUE(other_iter == other_list.end(), "");

        // If we are testing a list of managed pointers, release our internal
        // references.  Again, no objects should go away (as they are being
        // referenced by other_list.  Note: Don't try this with an unmanaged
        // pointer.  "releasing" and unmanaged pointer in the context of the
        // TestEnvironment class means to return it to the heap, which is a Very
        // Bad thing if we still have a list refering to the objects which were
        // returned to the heap.
        if (PtrTraits::IsManaged) {
            for (size_t i = 0; i < OBJ_COUNT; ++i)
                ReleaseObject(i);

            EXPECT_EQ(OBJ_COUNT, ObjType::live_obj_count(), "");
            EXPECT_EQ(0u, refs_held(), "");
            EXPECT_EQ(OBJ_COUNT, other_list.size_slow(), "");
        }

        // Finally, clear() other_list and reset the internal state.  At this
        // point, all objects should have gone away.
        other_list.clear();
        EXPECT_TRUE(Reset(), "");

        EXPECT_EQ(0u, ObjType::live_obj_count(), "");
        EXPECT_EQ(0u, refs_held(), "");
        EXPECT_EQ(0u, list().size_slow(), "");
        EXPECT_EQ(0u, other_list.size_slow(), "");

        END_TEST;
    }

    bool EraseIf() {
        BEGIN_TEST;

        // Populate our list.
        REQUIRE_TRUE(Populate(), "");

        // Erase all of the even members
        size_t even_erased = 0;
        while (even_erased < OBJ_COUNT) {
            if (nullptr == list().erase_if([](const ObjType& obj) -> bool {
                    return !(obj.value() & 1);
                }))
                break;
            even_erased++;
        }

        static constexpr size_t EVEN_OBJ_COUNT = (OBJ_COUNT >> 1) + (OBJ_COUNT & 1);
        EXPECT_EQ(EVEN_OBJ_COUNT, even_erased, "");
        EXPECT_EQ(OBJ_COUNT, even_erased + list().size_slow(), "");
        for (const auto& obj : list())
            EXPECT_TRUE(obj.value() & 1, "");

        // Erase all of the odd members
        size_t odd_erased = 0;
        while (even_erased < OBJ_COUNT) {
            if (nullptr == list().erase_if([](const ObjType& obj) -> bool {
                    return obj.value() & 1;
                }))
                break;
            odd_erased++;
        }

        static constexpr size_t ODD_OBJ_COUNT = (OBJ_COUNT >> 1);
        EXPECT_EQ(ODD_OBJ_COUNT, odd_erased, "");
        EXPECT_EQ(OBJ_COUNT, even_erased + odd_erased, "");
        EXPECT_TRUE(list().is_empty(), "");

        END_TEST;
    }

    static bool ScopeTest(void* ctx) {
        BEGIN_TEST;

        // Make sure that both unique_ptrs and RefPtrs handle being moved
        // properly, and that lists of such pointers automatically clean up when
        // the list goes out of scope and destructs.  Note: Don't try this with
        // an unmanaged pointer.  Lists of unmanaged pointers will ASSERT if
        // they destruct with elements still in them.
        EXPECT_EQ(0U, ObjType::live_obj_count(), "");

        {  // Begin scope for list
            ListType list;

            for (size_t i = 0; i < OBJ_COUNT; ++i) {
                // Make a new object
                PtrType obj = Traits::CreateObject(i);
                EXPECT_NONNULL(obj, "");
                EXPECT_EQ(i + 1, ObjType::live_obj_count(), "");
                EXPECT_EQ(i, list.size_slow(), "");

                // Move it into the list
                list.push_front(utils::move(obj));
                EXPECT_NULL(obj, "");
                EXPECT_EQ(i + 1, ObjType::live_obj_count(), "");
                EXPECT_EQ(i + 1, list.size_slow(), "");
            }

            EXPECT_EQ(OBJ_COUNT, ObjType::live_obj_count(), "");
            EXPECT_EQ(OBJ_COUNT, list.size_slow(), "");
        }  // Let the list go out of scope and clean itself up..

        EXPECT_EQ(0U, ObjType::live_obj_count(), "");

        END_TEST;
    }

#define MAKE_TEST_THUNK(_test_name) \
static bool _test_name ## Test(void* ctx) { \
    TestEnvironment<Traits> env; \
    BEGIN_TEST; \
    EXPECT_TRUE(env._test_name(), ""); \
    EXPECT_TRUE(env.Reset(), ""); \
    END_TEST; \
}
    MAKE_TEST_THUNK(Populate);
    MAKE_TEST_THUNK(Clear);
    MAKE_TEST_THUNK(IsEmpty);
    MAKE_TEST_THUNK(Iterate);
    MAKE_TEST_THUNK(IterateDec);
    MAKE_TEST_THUNK(MakeIterator);
    MAKE_TEST_THUNK(InsertAfter);
    MAKE_TEST_THUNK(Insert);
    MAKE_TEST_THUNK(DirectInsert);
    MAKE_TEST_THUNK(PushBack);
    MAKE_TEST_THUNK(PopFront);
    MAKE_TEST_THUNK(PopBack);
    MAKE_TEST_THUNK(EraseNext);
    MAKE_TEST_THUNK(Erase);
    MAKE_TEST_THUNK(DirectErase);
    MAKE_TEST_THUNK(Swap);
    MAKE_TEST_THUNK(RvalueOps);
    MAKE_TEST_THUNK(TwoList);
    MAKE_TEST_THUNK(EraseIf);
#undef MAKE_TEST_THUNK

private:
    // Accessors for base class memebers so we don't have to type
    // this->base_member all of the time.
    using Sp   = TestEnvironmentSpecialized<Traits>;
    using Base = TestEnvironmentBase<Traits>;
    static constexpr size_t OBJ_COUNT = Base::OBJ_COUNT;

    ListType&  list()      { return this->list_; }
    ObjType**  objects()   { return this->objects_; }
    size_t&    refs_held() { return this->refs_held_; }

    void ReleaseObject(size_t ndx) { Sp::ReleaseObject(ndx); }
    bool HoldingObject(size_t ndx) const { return Sp::HoldingObject(ndx); }
};

#define MAKE_TEST_OBJECT(_container_type, _ptr_type, _ptr_prefix, _ptr_suffix, _base_type) \
class _ptr_type ## _container_type ## TestObj :                                            \
    public _base_type<_container_type ## Traits<                                           \
        _ptr_prefix _ptr_type ## _container_type ## TestObj _ptr_suffix>> {                \
public:                                                                                    \
    explicit _ptr_type ## _container_type ## TestObj(size_t val)                           \
            : _base_type(val) { }                                                          \
}

#define MAKE_TEST_OBJECTS(_container_type)                                      \
    MAKE_TEST_OBJECT(_container_type, Unmanaged,            , *, TestObj);      \
    MAKE_TEST_OBJECT(_container_type, UniquePtr, unique_ptr<, >, TestObj);      \
    MAKE_TEST_OBJECT(_container_type, RefPtr,        RefPtr<, >, RefedTestObj)

MAKE_TEST_OBJECTS(SLL);
MAKE_TEST_OBJECTS(DLL);

#undef MAKE_TEST_OBJECTS
#undef MAKE_TEST_OBJECT

using UM_SLL_TE = TestEnvironment<UnmanagedTestTraits<UnmanagedSLLTestObj>>;
using UP_SLL_TE = TestEnvironment<UniquePtrTestTraits<UniquePtrSLLTestObj>>;
using RP_SLL_TE = TestEnvironment<RefPtrTestTraits<RefPtrSLLTestObj>>;
UNITTEST_START_TESTCASE(single_linked_list_tests)
UNITTEST("Populate (unmanaged)",        UM_SLL_TE::PopulateTest)
UNITTEST("Populate (unique)",           UP_SLL_TE::PopulateTest)
UNITTEST("Populate (RefPtr)",           RP_SLL_TE::PopulateTest)

UNITTEST("Clear (unmanaged)",           UM_SLL_TE::ClearTest)
UNITTEST("Clear (unique)",              UP_SLL_TE::ClearTest)
UNITTEST("Clear (RefPtr)",              RP_SLL_TE::ClearTest)

UNITTEST("IsEmpty (unmanaged)",         UM_SLL_TE::IsEmptyTest)
UNITTEST("IsEmpty (unique)",            UP_SLL_TE::IsEmptyTest)
UNITTEST("IsEmpty (RefPtr)",            RP_SLL_TE::IsEmptyTest)

UNITTEST("Iterate (unmanaged)",         UM_SLL_TE::IterateTest)
UNITTEST("Iterate (unique)",            UP_SLL_TE::IterateTest)
UNITTEST("Iterate (RefPtr)",            RP_SLL_TE::IterateTest)

UNITTEST("MakeIterator (unmanaged)",    UM_SLL_TE::MakeIteratorTest)
#if TEST_WILL_NOT_COMPILE || 0
UNITTEST("MakeIterator (unique)",       UP_SLL_TE::MakeIteratorTest)
#endif
UNITTEST("MakeIterator (RefPtr)",       RP_SLL_TE::MakeIteratorTest)

UNITTEST("InsertAfter (unmanaged)",     UM_SLL_TE::InsertAfterTest)
UNITTEST("InsertAfter (unique)",        UP_SLL_TE::InsertAfterTest)
UNITTEST("InsertAfter (RefPtr)",        RP_SLL_TE::InsertAfterTest)

UNITTEST("PopFront (unmanaged)",        UM_SLL_TE::PopFrontTest)
UNITTEST("PopFront (unique)",           UP_SLL_TE::PopFrontTest)
UNITTEST("PopFront (RefPtr)",           RP_SLL_TE::PopFrontTest)

UNITTEST("EraseNext (unmanaged)",       UM_SLL_TE::EraseNextTest)
UNITTEST("EraseNext (unique)",          UP_SLL_TE::EraseNextTest)
UNITTEST("EraseNext (RefPtr)",          RP_SLL_TE::EraseNextTest)

UNITTEST("Swap (unmanaged)",            UM_SLL_TE::SwapTest)
UNITTEST("Swap (unique)",               UP_SLL_TE::SwapTest)
UNITTEST("Swap (RefPtr)",               RP_SLL_TE::SwapTest)

UNITTEST("Rvalue Ops (unmanaged)",      UM_SLL_TE::RvalueOpsTest)
UNITTEST("Rvalue Ops (unique)",         UP_SLL_TE::RvalueOpsTest)
UNITTEST("Rvalue Ops (RefPtr)",         RP_SLL_TE::RvalueOpsTest)

UNITTEST("TwoList (unmanaged)",         UM_SLL_TE::TwoListTest)
#if TEST_WILL_NOT_COMPILE || 0
UNITTEST("TwoList (unique)",            UP_SLL_TE::TwoListTest)
#endif
UNITTEST("TwoList (RefPtr)",            RP_SLL_TE::TwoListTest)

UNITTEST("EraseIf (unmanaged)",         UM_SLL_TE::EraseIfTest)
UNITTEST("EraseIf (unique)",            UP_SLL_TE::EraseIfTest)
UNITTEST("EraseIf (RefPtr)",            RP_SLL_TE::EraseIfTest)

UNITTEST("Scope (unique)",              UP_SLL_TE::ScopeTest)
UNITTEST("Scope (RefPtr)",              RP_SLL_TE::ScopeTest)
UNITTEST_END_TESTCASE(single_linked_list_tests,
                      "sll",
                      "Intrusive singly linked list tests.",
                      NULL, NULL);

using UM_DLL_TE = TestEnvironment<UnmanagedTestTraits<UnmanagedDLLTestObj>>;
using UP_DLL_TE = TestEnvironment<UniquePtrTestTraits<UniquePtrDLLTestObj>>;
using RP_DLL_TE = TestEnvironment<RefPtrTestTraits<RefPtrDLLTestObj>>;
UNITTEST_START_TESTCASE(double_linked_list_tests)
UNITTEST("Populate (unmanaged)",        UM_DLL_TE::PopulateTest)
UNITTEST("Populate (unique)",           UP_DLL_TE::PopulateTest)
UNITTEST("Populate (RefPtr)",           RP_DLL_TE::PopulateTest)

UNITTEST("Clear (unmanaged)",           UM_DLL_TE::ClearTest)
UNITTEST("Clear (unique)",              UP_DLL_TE::ClearTest)
UNITTEST("Clear (RefPtr)",              RP_DLL_TE::ClearTest)

UNITTEST("IsEmpty (unmanaged)",         UM_DLL_TE::IsEmptyTest)
UNITTEST("IsEmpty (unique)",            UP_DLL_TE::IsEmptyTest)
UNITTEST("IsEmpty (RefPtr)",            RP_DLL_TE::IsEmptyTest)

UNITTEST("Iterate (unmanaged)",         UM_DLL_TE::IterateTest)
UNITTEST("Iterate (unique)",            UP_DLL_TE::IterateTest)
UNITTEST("Iterate (RefPtr)",            RP_DLL_TE::IterateTest)

UNITTEST("IterateDec (unmanaged)",      UM_DLL_TE::IterateDecTest)
UNITTEST("IterateDec (unique)",         UP_DLL_TE::IterateDecTest)
UNITTEST("IterateDec (RefPtr)",         RP_DLL_TE::IterateDecTest)

UNITTEST("MakeIterator (unmanaged)",    UM_DLL_TE::MakeIteratorTest)
#if TEST_WILL_NOT_COMPILE || 0
UNITTEST("MakeIterator (unique)",       UP_DLL_TE::MakeIteratorTest)
#endif
UNITTEST("MakeIterator (RefPtr)",       RP_DLL_TE::MakeIteratorTest)

UNITTEST("InsertAfter (unmanaged)",     UM_DLL_TE::InsertAfterTest)
UNITTEST("InsertAfter (unique)",        UP_DLL_TE::InsertAfterTest)
UNITTEST("InsertAfter (RefPtr)",        RP_DLL_TE::InsertAfterTest)

UNITTEST("Insert (unmanaged)",          UM_DLL_TE::InsertTest)
UNITTEST("Insert (unique)",             UP_DLL_TE::InsertTest)
UNITTEST("Insert (RefPtr)",             RP_DLL_TE::InsertTest)

UNITTEST("DirectInsert (unmanaged)",    UM_DLL_TE::DirectInsertTest)
#if TEST_WILL_NOT_COMPILE || 0
UNITTEST("DirectInsert (unique)",       UP_DLL_TE::DirectInsertTest)
#endif
UNITTEST("DirectInsert (RefPtr)",       RP_DLL_TE::DirectInsertTest)

UNITTEST("PushBack (unmanaged)",        UM_DLL_TE::PushBackTest)
UNITTEST("PushBack (unique)",           UP_DLL_TE::PushBackTest)
UNITTEST("PushBack (RefPtr)",           RP_DLL_TE::PushBackTest)

UNITTEST("PopFront (unmanaged)",        UM_DLL_TE::PopFrontTest)
UNITTEST("PopFront (unique)",           UP_DLL_TE::PopFrontTest)
UNITTEST("PopFront (RefPtr)",           RP_DLL_TE::PopFrontTest)

UNITTEST("PopBack (unmanaged)",         UM_DLL_TE::PopBackTest)
UNITTEST("PopBack (unique)",            UP_DLL_TE::PopBackTest)
UNITTEST("PopBack (RefPtr)",            RP_DLL_TE::PopBackTest)

UNITTEST("EraseNext (unmanaged)",       UM_DLL_TE::EraseNextTest)
UNITTEST("EraseNext (unique)",          UP_DLL_TE::EraseNextTest)
UNITTEST("EraseNext (RefPtr)",          RP_DLL_TE::EraseNextTest)

UNITTEST("Erase (unmanaged)",           UM_DLL_TE::EraseTest)
UNITTEST("Erase (unique)",              UP_DLL_TE::EraseTest)
UNITTEST("Erase (RefPtr)",              RP_DLL_TE::EraseTest)

UNITTEST("DirectErase (unmanaged)",     UM_DLL_TE::DirectEraseTest)
#if TEST_WILL_NOT_COMPILE || 0
UNITTEST("DirectErase (unique)",        UP_DLL_TE::DirectEraseTest)
#endif
UNITTEST("DirectErase (RefPtr)",        RP_DLL_TE::DirectEraseTest)

UNITTEST("Swap (unmanaged)",            UM_DLL_TE::SwapTest)
UNITTEST("Swap (unique)",               UP_DLL_TE::SwapTest)
UNITTEST("Swap (RefPtr)",               RP_DLL_TE::SwapTest)

UNITTEST("Rvalue Ops (unmanaged)",      UM_DLL_TE::RvalueOpsTest)
UNITTEST("Rvalue Ops (unique)",         UP_DLL_TE::RvalueOpsTest)
UNITTEST("Rvalue Ops (RefPtr)",         RP_DLL_TE::RvalueOpsTest)

UNITTEST("TwoList (unmanaged)",         UM_DLL_TE::TwoListTest)
#if TEST_WILL_NOT_COMPILE || 0
UNITTEST("TwoList (unique)",            UP_DLL_TE::TwoListTest)
#endif
UNITTEST("TwoList (RefPtr)",            RP_DLL_TE::TwoListTest)

UNITTEST("EraseIf (unmanaged)",         UM_DLL_TE::EraseIfTest)
UNITTEST("EraseIf (unique)",            UP_DLL_TE::EraseIfTest)
UNITTEST("EraseIf (RefPtr)",            RP_DLL_TE::EraseIfTest)

UNITTEST("Scope (unique)",              UP_DLL_TE::ScopeTest)
UNITTEST("Scope (RefPtr)",              RP_DLL_TE::ScopeTest)
UNITTEST_END_TESTCASE(double_linked_list_tests,
                      "dll",
                      "Intrusive doubly linked list tests.",
                      NULL, NULL);

}  // namespace utils

