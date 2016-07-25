// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#pragma once

#include <unittest.h>
#include <utils/tests/intrusive_containers/base_test_environments.h>

namespace utils {
namespace tests {
namespace intrusive_containers {

// SequenceContainerTestEnvironment<>
//
// Test environment which defines and implements tests and test utilities which
// are applicable to all associative containers such as trees and hash-tables.
template <typename TestEnvTraits>
class AssociativeContainerTestEnvironment : public TestEnvironment<TestEnvTraits> {
public:
    using ObjType              = typename TestEnvTraits::ObjType;
    using PtrType              = typename TestEnvTraits::PtrType;
    using ContainerTraits      = typename ObjType::ContainerTraits;
    using ContainerType        = typename ContainerTraits::ContainerType;
    using OtherContainerType   = typename ContainerTraits::OtherContainerType;
    using OtherContainerTraits = typename ContainerTraits::OtherContainerTraits;
    using PtrTraits            = typename ContainerType::PtrTraits;
    using SpBase               = TestEnvironmentSpecialized<TestEnvTraits>;
    using RefAction            = typename TestEnvironment<TestEnvTraits>::RefAction;

    enum class PopulateMethod {
        AscendingKey,
        DescendingKey,
        RandomKey,
    };

    bool Populate(ContainerType& container,
                  PopulateMethod method,
                  RefAction ref_action = RefAction::HoldSome) {
        BEGIN_TEST;

        EXPECT_EQ(0U, ObjType::live_obj_count(), "");

        for (size_t i = 0; i < OBJ_COUNT; ++i) {
            EXPECT_EQ(i, container.size_slow(), "");

            // Unless explicitly told to do so, don't hold a reference in the
            // test environment for every 4th object created.  Note, this only
            // affects RefPtr tests.  Unmanaged pointers always hold an
            // unmanaged copy of the pointer (so it can be cleaned up), while
            // unique_ptr tests are not able to hold an extra copy of the
            // pointer (because it is unique)
            bool hold_ref;
            switch (ref_action) {
            case RefAction::HoldNone: hold_ref = false; break;
            case RefAction::HoldSome: hold_ref = (i & 0x3); break;
            case RefAction::HoldAll:
            default:
                hold_ref = true;
                break;
            }

            PtrType new_object = this->CreateTrackedObject(i, i, hold_ref);
            REQUIRE_NONNULL(new_object, "");
            EXPECT_EQ(new_object->raw_ptr(), objects()[i], "");

            // Assign a key to the object based on the chosen populate method.
            typename ContainerTraits::KeyType key = 0;
            switch (method) {
            case PopulateMethod::RandomKey:     // TODO: implement me.  Until then, fall-thru
            case PopulateMethod::AscendingKey:  key = i; break;
            case PopulateMethod::DescendingKey: key = OBJ_COUNT - i - 1; break;
            }

            // Set the primary key on the object.  Offset the "other" key by OBJ_COUNT
            new_object->SetKey(key);
            OtherContainerTraits::SetKey(*new_object, key + OBJ_COUNT);

            // Alternate whether or not we move the pointer, or "transfer" it.
            // Transfering means different things for different pointer types.
            // For unmanaged, it just returns a reference to the pointer and
            // leaves the original unaltered.  For unique, it moves the pointer
            // (clearing the source).  For RefPtr, it makes a new RefPtr
            // instance, bumping the reference count in the process.
            if (i & 1) {
#if TEST_WILL_NOT_COMPILE || 0
                container.insert(new_object);
#else
                container.insert(TestEnvTraits::Transfer(new_object));
#endif
                EXPECT_TRUE(TestEnvTraits::WasTransferred(new_object), "");
            } else {
                container.insert(utils::move(new_object));
                EXPECT_TRUE(TestEnvTraits::WasMoved(new_object), "");
            }
        }

        EXPECT_EQ(OBJ_COUNT, container.size_slow(), "");
        EXPECT_EQ(OBJ_COUNT, ObjType::live_obj_count(), "");

        END_TEST;
    }

    bool Populate(ContainerType& container, RefAction ref_action = RefAction::HoldSome) override {
        return Populate(container, PopulateMethod::AscendingKey, ref_action);
    }

    bool InsertAscending() {
        BEGIN_TEST;
        EXPECT_TRUE(Populate(container(), PopulateMethod::AscendingKey), "");
        END_TEST;
    }

    bool InsertDescending() {
        BEGIN_TEST;
        EXPECT_TRUE(Populate(container(), PopulateMethod::DescendingKey), "");
        END_TEST;
    }

    bool InsertRandom() {
        BEGIN_TEST;
        EXPECT_TRUE(Populate(container(), PopulateMethod::RandomKey), "");
        END_TEST;
    }

#define MAKE_TEST_THUNK(_test_name) \
static bool _test_name ## Test(void* ctx) { \
    AssociativeContainerTestEnvironment<TestEnvTraits> env; \
    BEGIN_TEST; \
    EXPECT_TRUE(env._test_name(), ""); \
    EXPECT_TRUE(env.Reset(), ""); \
    END_TEST; \
}
    // Generic tests
    MAKE_TEST_THUNK(Clear);
    MAKE_TEST_THUNK(IsEmpty);
    MAKE_TEST_THUNK(IterErase);
    MAKE_TEST_THUNK(ReverseIterErase);
    MAKE_TEST_THUNK(DirectErase);
    MAKE_TEST_THUNK(Iterate);
    MAKE_TEST_THUNK(IterateDec);
    MAKE_TEST_THUNK(MakeIterator);
    MAKE_TEST_THUNK(Swap);
    MAKE_TEST_THUNK(RvalueOps);
    MAKE_TEST_THUNK(Scope);
    MAKE_TEST_THUNK(TwoContainer);
    MAKE_TEST_THUNK(EraseIf);
    MAKE_TEST_THUNK(FindIf);

    // Associative container specific tests
    MAKE_TEST_THUNK(InsertAscending);
    MAKE_TEST_THUNK(InsertDescending);
    MAKE_TEST_THUNK(InsertRandom);
#undef MAKE_TEST_THUNK

private:
    // Accessors for base class memebers so we don't have to type
    // this->base_member all of the time.
    using Sp   = TestEnvironmentSpecialized<TestEnvTraits>;
    using Base = TestEnvironmentBase<TestEnvTraits>;
    static constexpr size_t OBJ_COUNT = Base::OBJ_COUNT;

    ContainerType& container() { return this->container_; }
    ObjType**      objects()   { return this->objects_; }
    size_t&        refs_held() { return this->refs_held_; }

    void ReleaseObject(size_t ndx) { Sp::ReleaseObject(ndx); }
    bool HoldingObject(size_t ndx) const { return Sp::HoldingObject(ndx); }
};


}  // namespace intrusive_containers
}  // namespace tests
}  // namespace utils
