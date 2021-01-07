// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <utility>

#include <fbl/no_destructor.h>
#include <zxtest/zxtest.h>

namespace {

TEST(NoDestructor, SimpleTest) {
  // Set a boolean flag to true when the class instance is destructed.
  class SetFlagOnDestruct {
   public:
    explicit SetFlagOnDestruct(bool* destructor_run) : destructor_run_(destructor_run) {}
    ~SetFlagOnDestruct() { *destructor_run_ = true; }

   private:
    bool* destructor_run_;
  };

  // A standard instance of "SetFlagOnDestruct" should set the flag.
  bool destructed = false;
  {
    SetFlagOnDestruct x(&destructed);
    EXPECT_FALSE(destructed);
  }
  EXPECT_TRUE(destructed);

  // An instance wrapped by "gtl::NoDestructor" should not set the flag.
  destructed = false;
  {
    fbl::NoDestructor<SetFlagOnDestruct> x(&destructed);
    EXPECT_FALSE(destructed);
  }
  EXPECT_FALSE(destructed);
}

// A missing symbol, used to generate a linker error if code ends up in the final
// image.
extern "C" void __destructor_incorrectly_compiled_into_final_image();

TEST(NoDestructor, DestructorNotLinked) {
  // This class will produce a linker error to a missing symbol if the destructor
  // is emitted.
  class DestructorWithLinkError {
   public:
    DestructorWithLinkError() = default;
    ~DestructorWithLinkError() { __destructor_incorrectly_compiled_into_final_image(); }
  };

  static fbl::NoDestructor<DestructorWithLinkError> value{};
}

// Ensure the getters function correctly.
TEST(NoDestructor, Get) {
  struct Struct {
    int a;
  };
  fbl::NoDestructor<Struct> value;
  value->a = 1;
  EXPECT_EQ(value->a, 1);
  EXPECT_EQ(value.get()->a, 1);
  EXPECT_EQ((*value).a, 1);
}

// Test object with non-standard alignment constraints.
TEST(NoDestructor, Alignment) {
  struct alignas(128) LargeAlignment {
    char data[128];
  };
  fbl::NoDestructor<LargeAlignment> object{};
  EXPECT_EQ(alignof(decltype(object)), 128);
  EXPECT_EQ(reinterpret_cast<uintptr_t>(object.get()) % 128, 0);
}

// Test object with copy construction.
TEST(NoDestructor, CopyConstruction) {
  fbl::NoDestructor<int> x = int(42);
  EXPECT_EQ(*x, 42);
}

// Test object with move construction.
TEST(NoDestructor, MoveConstruction) {
  auto ptr = std::make_unique<int>(42);
  fbl::NoDestructor<std::unique_ptr<int>> x = std::move(ptr);
  EXPECT_EQ(**x, 42);
  x->reset();
}

TEST(NoDestructor, NoExcept) {
  // Ensure noexcept constructors are exposed as such.
  static_assert(noexcept(fbl::NoDestructor<int>()));

  // Ensure noexcept values propagate.
  struct MaybeException {
    MaybeException() noexcept(false) = default;
  };
  static_assert(!noexcept(fbl::NoDestructor<MaybeException>()));
}

}  // namespace
