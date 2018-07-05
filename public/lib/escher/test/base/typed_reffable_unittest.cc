// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/escher/base/typed_reffable.h"
#include "lib/escher/base/type_info.h"

#include "gtest/gtest.h"
#include "lib/fxl/memory/ref_ptr.h"

namespace {

enum class TestTypes {
  kOne = 1,
  kTwo = 1 << 1,
  kSubOne = 1 << 2,
  kSubTwo = 1 << 3,
  kSubSubTwo = 1 << 4,
  kSubSubSubTwo = 1 << 5,
};
typedef escher::TypeInfo<TestTypes> TestTypeInfo;

// Base class.  Direct subclasses: One and Two.
class Base : public escher::TypedReffable<TestTypeInfo> {
 public:
  static const TypeInfo kTypeInfo;
};

// Subclass of Base.  Direct subclasses: SubOne.
class One : public Base {
 public:
  static const TypeInfo kTypeInfo;
  const TypeInfo& type_info() const override { return kTypeInfo; }
};

// Subclass of Base.  Direct subclasses: SubTwo.
class Two : public Base {
 public:
  static const TypeInfo kTypeInfo;
  const TypeInfo& type_info() const override { return kTypeInfo; }
};

// Subclass of One.  No subclasses.
class SubOne : public One {
 public:
  static const TypeInfo kTypeInfo;
  const TypeInfo& type_info() const override { return kTypeInfo; }
};

// Subclass of Two.  Direct subclasses: SubSubTwo.
class SubTwo : public Two {
 public:
  static const TypeInfo kTypeInfo;
  const TypeInfo& type_info() const override { return kTypeInfo; }
};

// Subclass of SubTwo.  Direct subclasses: SubSubSubTwo.
class SubSubTwo : public SubTwo {
 public:
  static const TypeInfo kTypeInfo;
  const TypeInfo& type_info() const override { return kTypeInfo; }
};

// Subclass of SubSubTwo.  No subclasses.
class SubSubSubTwo : public SubSubTwo {
 public:
  static const TypeInfo kTypeInfo;
  const TypeInfo& type_info() const override { return kTypeInfo; }
};

// Static variables.  Note how the type information for the entire inheritance
// chain is baked into each class' TypeInfo.
const TestTypeInfo Base::kTypeInfo("Base");
const TestTypeInfo One::kTypeInfo("One", TestTypes::kOne);
const TestTypeInfo Two::kTypeInfo("Two", TestTypes::kTwo);
const TestTypeInfo SubOne::kTypeInfo("SubOne", TestTypes::kOne,
                                     TestTypes::kSubOne);
const TestTypeInfo SubTwo::kTypeInfo("SubTwo", TestTypes::kTwo,
                                     TestTypes::kSubTwo);
const TestTypeInfo SubSubTwo::kTypeInfo("SubSubTwo", TestTypes::kTwo,
                                        TestTypes::kSubTwo,
                                        TestTypes::kSubSubTwo);
const TestTypeInfo SubSubSubTwo::kTypeInfo("SubSubSubTwo", TestTypes::kTwo,
                                           TestTypes::kSubTwo,
                                           TestTypes::kSubSubTwo,
                                           TestTypes::kSubSubSubTwo);

TEST(TypedReffable, ShallowHierarchy) {
  auto o = escher::Make<One>();
  auto so = escher::Make<SubOne>();
  auto t = escher::Make<Two>();
  auto st = escher::Make<SubTwo>();

  EXPECT_TRUE(o->IsKindOf(Base::kTypeInfo));
  EXPECT_TRUE(so->IsKindOf(Base::kTypeInfo));
  EXPECT_TRUE(t->IsKindOf(Base::kTypeInfo));
  EXPECT_TRUE(st->IsKindOf(Base::kTypeInfo));
  EXPECT_TRUE(o->IsKindOf<Base>());
  EXPECT_TRUE(so->IsKindOf<Base>());
  EXPECT_TRUE(t->IsKindOf<Base>());
  EXPECT_TRUE(st->IsKindOf<Base>());

  EXPECT_TRUE(o->IsKindOf(One::kTypeInfo));
  EXPECT_TRUE(so->IsKindOf(One::kTypeInfo));
  EXPECT_FALSE(t->IsKindOf(One::kTypeInfo));
  EXPECT_FALSE(st->IsKindOf(One::kTypeInfo));
  EXPECT_TRUE(o->IsKindOf<One>());
  EXPECT_TRUE(so->IsKindOf<One>());
  EXPECT_FALSE(t->IsKindOf<One>());
  EXPECT_FALSE(st->IsKindOf<One>());

  EXPECT_FALSE(o->IsKindOf(Two::kTypeInfo));
  EXPECT_FALSE(so->IsKindOf(Two::kTypeInfo));
  EXPECT_TRUE(t->IsKindOf(Two::kTypeInfo));
  EXPECT_TRUE(st->IsKindOf(Two::kTypeInfo));
  EXPECT_FALSE(o->IsKindOf<Two>());
  EXPECT_FALSE(so->IsKindOf<Two>());
  EXPECT_TRUE(t->IsKindOf<Two>());
  EXPECT_TRUE(st->IsKindOf<Two>());

  EXPECT_FALSE(o->IsKindOf(SubOne::kTypeInfo));
  EXPECT_TRUE(so->IsKindOf(SubOne::kTypeInfo));
  EXPECT_FALSE(t->IsKindOf(SubOne::kTypeInfo));
  EXPECT_FALSE(st->IsKindOf(SubOne::kTypeInfo));
  EXPECT_FALSE(o->IsKindOf<SubOne>());
  EXPECT_TRUE(so->IsKindOf<SubOne>());
  EXPECT_FALSE(t->IsKindOf<SubOne>());
  EXPECT_FALSE(st->IsKindOf<SubOne>());

  EXPECT_FALSE(o->IsKindOf(SubTwo::kTypeInfo));
  EXPECT_FALSE(so->IsKindOf(SubTwo::kTypeInfo));
  EXPECT_FALSE(t->IsKindOf(SubTwo::kTypeInfo));
  EXPECT_TRUE(st->IsKindOf(SubTwo::kTypeInfo));
  EXPECT_FALSE(o->IsKindOf<SubTwo>());
  EXPECT_FALSE(so->IsKindOf<SubTwo>());
  EXPECT_FALSE(t->IsKindOf<SubTwo>());
  EXPECT_TRUE(st->IsKindOf<SubTwo>());
}

TEST(TypedReffable, DeepHierarchy) {
  auto t = escher::Make<Two>();
  auto st = escher::Make<SubTwo>();
  auto sst = escher::Make<SubSubTwo>();
  auto ssst = escher::Make<SubSubSubTwo>();

  // Each's type matches itself.
  EXPECT_TRUE(t->IsKindOf<Two>());
  EXPECT_TRUE(st->IsKindOf<SubTwo>());
  EXPECT_TRUE(sst->IsKindOf<SubSubTwo>());
  EXPECT_TRUE(ssst->IsKindOf<SubSubSubTwo>());

  // Each type matches its parent.
  EXPECT_TRUE(st->IsKindOf<Two>());
  EXPECT_TRUE(sst->IsKindOf<SubTwo>());
  EXPECT_TRUE(ssst->IsKindOf<SubSubTwo>());

  // No type matches its child.
  EXPECT_FALSE(t->IsKindOf<SubTwo>());
  EXPECT_FALSE(st->IsKindOf<SubSubTwo>());
  EXPECT_FALSE(sst->IsKindOf<SubSubSubTwo>());
}

TEST(TypedReffable, Names) {
  auto o = escher::Make<One>();
  auto so = escher::Make<SubOne>();
  auto t = escher::Make<Two>();
  auto st = escher::Make<SubTwo>();
  auto sst = escher::Make<SubSubTwo>();
  auto ssst = escher::Make<SubSubSubTwo>();

  EXPECT_EQ("One", o->type_name());
  EXPECT_EQ("SubOne", so->type_name());
  EXPECT_EQ("Two", t->type_name());
  EXPECT_EQ("SubTwo", st->type_name());
  EXPECT_EQ("SubSubTwo", sst->type_name());
  EXPECT_EQ("SubSubSubTwo", ssst->type_name());
}

}  // namespace
