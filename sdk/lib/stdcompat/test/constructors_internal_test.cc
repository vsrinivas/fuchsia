// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/stdcompat/internal/constructors.h>

#include <type_traits>

#include "gtest.h"

namespace {

struct DeletedCopyConstructor {
  DeletedCopyConstructor(const DeletedCopyConstructor&) noexcept = delete;
  DeletedCopyConstructor& operator=(const DeletedCopyConstructor&) noexcept = default;
};

struct DeletedCopyAssignment {
  DeletedCopyAssignment(const DeletedCopyAssignment&) noexcept = default;
  DeletedCopyAssignment& operator=(const DeletedCopyAssignment&) noexcept = delete;
};

struct DeletedMoveConstructor {
  DeletedMoveConstructor(DeletedMoveConstructor&&) noexcept = delete;
  DeletedMoveConstructor& operator=(DeletedMoveConstructor&&) noexcept = default;
};

struct DeletedMoveAssignament {
  DeletedMoveAssignament(DeletedMoveAssignament&&) noexcept = default;
  DeletedMoveAssignament& operator=(DeletedMoveAssignament&&) noexcept = delete;
};

TEST(ModulateCopyAndMoveTest, SingleTypeIsModulatedCorrectly) {
  static_assert(!std::is_copy_constructible<
                cpp17::internal::modulate_copy_and_move<DeletedCopyConstructor>>::value);
  static_assert(std::is_copy_assignable<
                cpp17::internal::modulate_copy_and_move<DeletedCopyConstructor>>::value);

  static_assert(std::is_copy_constructible<
                cpp17::internal::modulate_copy_and_move<DeletedCopyAssignment>>::value);
  static_assert(!std::is_copy_assignable<
                cpp17::internal::modulate_copy_and_move<DeletedCopyAssignment>>::value);

  static_assert(!std::is_move_constructible<
                cpp17::internal::modulate_copy_and_move<DeletedMoveConstructor>>::value);
  static_assert(std::is_move_assignable<
                cpp17::internal::modulate_copy_and_move<DeletedMoveConstructor>>::value);

  static_assert(std::is_move_constructible<
                cpp17::internal::modulate_copy_and_move<DeletedMoveAssignament>>::value);
  static_assert(!std::is_move_assignable<
                cpp17::internal::modulate_copy_and_move<DeletedMoveAssignament>>::value);
}

TEST(ModulateCopyAndMoveTest, MultipleTypesAreModulatedCorrectly) {
  {
    using MultipleTypes = cpp17::internal::modulate_copy_and_move<DeletedCopyConstructor>;

    static_assert(!std::is_copy_constructible<MultipleTypes>::value);
  }

  {
    using MultipleTypes =
        cpp17::internal::modulate_copy_and_move<DeletedCopyConstructor, DeletedCopyAssignment>;

    static_assert(!std::is_copy_constructible<MultipleTypes>::value);
    static_assert(!std::is_copy_assignable<MultipleTypes>::value);
  }

  {
    using MultipleTypes =
        cpp17::internal::modulate_copy_and_move<DeletedCopyConstructor, DeletedMoveAssignament>;

    static_assert(!std::is_copy_constructible<MultipleTypes>::value);
    static_assert(!std::is_move_assignable<MultipleTypes>::value);
  }

  {
    using MultipleTypes =
        cpp17::internal::modulate_copy_and_move<DeletedCopyConstructor, DeletedMoveAssignament,
                                                DeletedMoveConstructor>;

    static_assert(!std::is_copy_constructible<MultipleTypes>::value);
    static_assert(!std::is_move_constructible<MultipleTypes>::value);
    static_assert(!std::is_move_assignable<MultipleTypes>::value);
  }

  {
    using MultipleTypes =
        cpp17::internal::modulate_copy_and_move<DeletedCopyConstructor, DeletedCopyAssignment,
                                                DeletedMoveAssignament, DeletedMoveConstructor>;

    static_assert(!std::is_copy_constructible<MultipleTypes>::value);
    static_assert(!std::is_copy_assignable<MultipleTypes>::value);
    static_assert(!std::is_move_constructible<MultipleTypes>::value);
    static_assert(!std::is_move_assignable<MultipleTypes>::value);
  }
}

TEST(ModulateDefaultConstructorTest, SingleTypeModulatedCorrectly) {
  {
    struct NonDefaultConstructible {
      NonDefaultConstructible() = delete;
    };

    using ModulatedType = cpp17::internal::modulate_default_constructor<NonDefaultConstructible>;
    static_assert(!std::is_default_constructible<ModulatedType>::value);
  }

  {
    struct DefaultConstructible {};

    using ModulatedType = cpp17::internal::modulate_default_constructor<DefaultConstructible>;
    static_assert(std::is_default_constructible<ModulatedType>::value);
  }
}

}  // namespace
