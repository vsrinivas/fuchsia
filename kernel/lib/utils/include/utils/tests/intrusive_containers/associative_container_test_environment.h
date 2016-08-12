// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#pragma once

#include <unittest.h>
#include <utils/tests/intrusive_containers/base_test_environments.h>
#include <utils/tests/intrusive_containers/lfsr.h>

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
    using KeyType              = typename ContainerTraits::KeyType;
    using OtherKeyType         = typename OtherContainerType::KeyType;

    enum class PopulateMethod {
        AscendingKey,
        DescendingKey,
        RandomKey,
    };

    static constexpr KeyType      kBannedKeyValue      = 0xF00D;
    static constexpr OtherKeyType kBannedOtherKeyValue = 0xF00D;

    // Utility method for checking the size of the container via either size()
    // or size_slow(), depending on whether or not the container supports a
    // constant order size operation.
    template <typename CType>
    static size_t Size(const CType& container) {
        return SizeUtils<CType>::size(container);
    }

    bool Populate(ContainerType& container,
                  PopulateMethod method,
                  RefAction ref_action = RefAction::HoldSome) {
        BEGIN_TEST;

        EXPECT_EQ(0U, ObjType::live_obj_count(), "");

        for (size_t i = 0; i < OBJ_COUNT; ++i) {
            EXPECT_EQ(i, Size(container), "");

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
            KeyType key = 0;
            OtherKeyType other_key = 0;

            switch (method) {
                case PopulateMethod::RandomKey:
                    do {
                        key = key_lfsr_.GetNext();
                    } while (key == kBannedKeyValue);

                    do {
                        other_key = other_key_lfsr_.GetNext();
                    } while (other_key == kBannedOtherKeyValue);
                    break;

                case PopulateMethod::AscendingKey:
                    key = i;
                    other_key = static_cast<OtherKeyType>(key + OBJ_COUNT);
                    break;

                case PopulateMethod::DescendingKey:
                    key = OBJ_COUNT - i - 1;
                    other_key = static_cast<OtherKeyType>(key + OBJ_COUNT);
                    break;
            }

            DEBUG_ASSERT(key != kBannedKeyValue);
            DEBUG_ASSERT(other_key != kBannedOtherKeyValue);

            // Set the primary key on the object.  Offset the "other" key by OBJ_COUNT
            new_object->SetKey(key);
            OtherContainerTraits::SetKey(*new_object, other_key);

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

        EXPECT_EQ(OBJ_COUNT, Size(container), "");
        EXPECT_EQ(OBJ_COUNT, ObjType::live_obj_count(), "");

        END_TEST;
    }

    bool Populate(ContainerType& container, RefAction ref_action = RefAction::HoldSome) override {
        return Populate(container, PopulateMethod::AscendingKey, ref_action);
    }

    template <PopulateMethod populate_method>
    bool DoInsertByKey() {
        BEGIN_TEST;

        EXPECT_TRUE(Populate(container(), populate_method), "");
        TestEnvironment<TestEnvTraits>::Reset();

        END_TEST;
    }

    bool InsertByKey() {
        BEGIN_TEST;

        EXPECT_TRUE(DoInsertByKey<PopulateMethod::AscendingKey>(), "");
        EXPECT_TRUE(DoInsertByKey<PopulateMethod::DescendingKey>(), "");
        EXPECT_TRUE(DoInsertByKey<PopulateMethod::RandomKey>(), "");

        END_TEST;
    }

    template <PopulateMethod populate_method>
    bool DoFindByKey() {
        BEGIN_TEST;

        EXPECT_TRUE(Populate(container(), populate_method), "");

        // Lookup the various items which should be in the collection by key.
        for (size_t i = 0; i < OBJ_COUNT; ++i) {
            KeyType key   = objects()[i]->GetKey();
            size_t  value = objects()[i]->value();

            const auto& ptr = container().find(key);

            REQUIRE_NONNULL(ptr, "");
            EXPECT_EQ(key, ptr->GetKey(), "");
            EXPECT_EQ(value, ptr->value(), "");
        }

        // Fail to look up something which should not be in the collection.
        const auto& ptr = container().find(kBannedKeyValue);
        EXPECT_NULL(ptr, "");

        TestEnvironment<TestEnvTraits>::Reset();
        END_TEST;
    }

    bool FindByKey() {
        BEGIN_TEST;

        EXPECT_TRUE(DoFindByKey<PopulateMethod::AscendingKey>(), "");
        EXPECT_TRUE(DoFindByKey<PopulateMethod::DescendingKey>(), "");
        EXPECT_TRUE(DoFindByKey<PopulateMethod::RandomKey>(), "");

        END_TEST;
    }

    template <PopulateMethod populate_method>
    bool DoEraseByKey() {
        BEGIN_TEST;

        EXPECT_TRUE(Populate(container(), populate_method), "");
        size_t remaining = OBJ_COUNT;

        // Fail to erase a key which is not in the container.
        EXPECT_NULL(container().erase(kBannedKeyValue), "");

        // Erase all of the even members of the collection by key.
        for (size_t i = 0; i < OBJ_COUNT; ++i) {
            if (objects()[i] == nullptr)
                continue;

            KeyType key = objects()[i]->GetKey();
            if (key & 1)
                continue;

            EXPECT_TRUE(TestEnvironment<TestEnvTraits>::DoErase(key, i, remaining), "");
            --remaining;
        }

        EXPECT_EQ(remaining, Size(container()), "");

        // Erase the remaining odd members.
        for (size_t i = 0; i < OBJ_COUNT; ++i) {
            if (objects()[i] == nullptr)
                continue;

            KeyType key = objects()[i]->GetKey();
            EXPECT_TRUE(key & 1, "");

            EXPECT_TRUE(TestEnvironment<TestEnvTraits>::DoErase(key, i, remaining), "");
            --remaining;
        }

        EXPECT_EQ(0u, Size(container()), "");

        TestEnvironment<TestEnvTraits>::Reset();
        END_TEST;
    }

    bool EraseByKey() {
        BEGIN_TEST;

        EXPECT_TRUE(DoEraseByKey<PopulateMethod::AscendingKey>(), "");
        EXPECT_TRUE(DoEraseByKey<PopulateMethod::DescendingKey>(), "");
        EXPECT_TRUE(DoEraseByKey<PopulateMethod::RandomKey>(), "");

        END_TEST;
    }

private:
    // Accessors for base class memebers so we don't have to type
    // this->base_member all of the time.
    using Sp   = TestEnvironmentSpecialized<TestEnvTraits>;
    using Base = TestEnvironmentBase<TestEnvTraits>;
    static constexpr size_t OBJ_COUNT = Base::OBJ_COUNT;
    static constexpr size_t EVEN_OBJ_COUNT = (OBJ_COUNT >> 1) + (OBJ_COUNT & 1);
    static constexpr size_t ODD_OBJ_COUNT  = (OBJ_COUNT >> 1);

    ContainerType& container() { return this->container_; }
    ObjType**      objects()   { return this->objects_; }
    size_t&        refs_held() { return this->refs_held_; }

    void ReleaseObject(size_t ndx) { Sp::ReleaseObject(ndx); }
    bool HoldingObject(size_t ndx) const { return Sp::HoldingObject(ndx); }

    Lfsr<KeyType>      key_lfsr_        = Lfsr<KeyType>(0xa2328b73e343fd0f);
    Lfsr<OtherKeyType> other_key_lfsr_  = Lfsr<OtherKeyType>(0xbd5a2efcc5ba8344);
};

// Explicit declaration of constexpr storage.
template <typename TestEnvTraits>
constexpr typename AssociativeContainerTestEnvironment<TestEnvTraits>::KeyType
AssociativeContainerTestEnvironment<TestEnvTraits>::kBannedKeyValue;

template <typename TestEnvTraits>
constexpr typename AssociativeContainerTestEnvironment<TestEnvTraits>::OtherKeyType
AssociativeContainerTestEnvironment<TestEnvTraits>::kBannedOtherKeyValue;

}  // namespace intrusive_containers
}  // namespace tests
}  // namespace utils
