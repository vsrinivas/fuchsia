// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <locale.h>

#include <fidl/attributes.h>
#include <fidl/flat_ast.h>
#include <fidl/lexer.h>
#include <fidl/parser.h>
#include <fidl/raw_ast.h>
#include <fidl/source_file.h>
#include <zxtest/zxtest.h>

#include "error_test.h"
#include "test_library.h"

namespace {

void AddZxLibraryDep(TestLibrary* lib, SharedAmongstLibraries* shared) {
  // Include a fake "zx" library with every test.
  std::string zx = R"FIDL(
library zx;

enum obj_type : uint32 {
    NONE = 0;
    PROCESS = 1;
    THREAD = 2;
    VMO = 3;
    CHANNEL = 4;
    EVENT = 5;
    PORT = 6;
};

bits rights : uint32 {
    DUPLICATE = 0x00000001;
    TRANSFER = 0x00000002;
};

resource_definition handle : uint32 {
    properties {
        obj_type subtype;
        rights rights;
    };
};
)FIDL";

  fidl::ExperimentalFlags experimental_flags;
  experimental_flags.SetFlag(fidl::ExperimentalFlags::Flag::kEnableHandleRights);
  TestLibrary zx_lib("zx.fidl", zx, shared, experimental_flags);
  zx_lib.Compile();
  lib->AddDependentLibrary(std::move(zx_lib));
}

TEST(HandleTests, HandleRightsTest) {
  SharedAmongstLibraries shared;
  fidl::ExperimentalFlags experimental_flags;
  experimental_flags.SetFlag(fidl::ExperimentalFlags::Flag::kEnableHandleRights);

  TestLibrary library(R"FIDL(
library example;

using zx;

resource struct MyStruct {
    zx.handle:<THREAD, zx.rights.DUPLICATE | zx.rights.TRANSFER> h;
};
)FIDL",
                      std::move(experimental_flags));
  AddZxLibraryDep(&library, &shared);
  EXPECT_TRUE(library.Compile());

  auto h_type_ctor = library.LookupStruct("MyStruct")->members[0].type_ctor.get();

  EXPECT_TRUE(h_type_ctor->handle_subtype_identifier.has_value());
  EXPECT_EQ("THREAD", h_type_ctor->handle_subtype_identifier.value().span()->data());
  EXPECT_EQ(2, h_type_ctor->handle_obj_type_resolved);
  ASSERT_NOT_NULL(h_type_ctor->handle_rights);
  ASSERT_EQ(static_cast<const fidl::flat::NumericConstantValue<uint32_t>&>(
                h_type_ctor->handle_rights->Value())
                .value,
            3);
}

TEST(HandleTests, NoHandleRightsTest) {
  SharedAmongstLibraries shared;
  fidl::ExperimentalFlags experimental_flags;
  experimental_flags.SetFlag(fidl::ExperimentalFlags::Flag::kEnableHandleRights);

  TestLibrary library(R"FIDL(
library example;

using zx;

resource struct MyStruct {
    zx.handle:VMO h;
};
)FIDL",
                      std::move(experimental_flags));

  AddZxLibraryDep(&library, &shared);
  EXPECT_TRUE(library.Compile());

  auto h_type_ctor = library.LookupStruct("MyStruct")->members[0].type_ctor.get();

  EXPECT_TRUE(h_type_ctor->handle_subtype_identifier.has_value());
  ASSERT_TRUE(h_type_ctor->handle_subtype_identifier.value().span()->data() == "VMO");
  EXPECT_EQ(3, h_type_ctor->handle_obj_type_resolved);
  ASSERT_NULL(h_type_ctor->handle_rights);
}

TEST(HandleTests, InvalidHandleRightsTest) {
  SharedAmongstLibraries shared;
  fidl::ExperimentalFlags experimental_flags;
  experimental_flags.SetFlag(fidl::ExperimentalFlags::Flag::kEnableHandleRights);

  TestLibrary library(R"FIDL(
library example;

using zx;

protocol P {
    Method(zx.handle:<VMO, 1> h);  // rights must be zx.rights-typed.
};
)FIDL",
                      std::move(experimental_flags));

  AddZxLibraryDep(&library, &shared);
  EXPECT_FALSE(library.Compile());
  const auto& errors = library.errors();
  ASSERT_EQ(errors.size(), 2);
  ASSERT_ERR(errors[0], fidl::ErrConstantCannotBeInterpretedAsType);
  ASSERT_ERR(errors[1], fidl::ErrCouldNotResolveHandleRights);
}

TEST(HandleTests, PlainHandleTest) {
  SharedAmongstLibraries shared;
  fidl::ExperimentalFlags experimental_flags;
  experimental_flags.SetFlag(fidl::ExperimentalFlags::Flag::kEnableHandleRights);

  TestLibrary library(R"FIDL(
library example;

using zx;

resource struct MyStruct {
    zx.handle h;
};
)FIDL",
                      std::move(experimental_flags));
  AddZxLibraryDep(&library, &shared);
  EXPECT_TRUE(library.Compile());

  auto h_type_ctor = library.LookupStruct("MyStruct")->members[0].type_ctor.get();

  EXPECT_EQ(0, h_type_ctor->handle_obj_type_resolved);
  ASSERT_NULL(h_type_ctor->handle_rights);
}

TEST(HandleTests, HandleFidlDefinedTest) {
  SharedAmongstLibraries shared;
  fidl::ExperimentalFlags experimental_flags;
  experimental_flags.SetFlag(fidl::ExperimentalFlags::Flag::kEnableHandleRights);

  TestLibrary library(R"FIDL(
library example;

using zx;

resource struct MyStruct {
  zx.handle:THREAD a;
  zx.handle:<PROCESS> b;
  zx.handle:<VMO, zx.rights.TRANSFER> c;
};
)FIDL",
                      std::move(experimental_flags));

  AddZxLibraryDep(&library, &shared);
  EXPECT_TRUE(library.Compile());
  auto a = library.LookupStruct("MyStruct")->members[0].type_ctor.get();
  EXPECT_TRUE(a->handle_subtype_identifier.has_value());
  ASSERT_TRUE(a->handle_subtype_identifier.value().span()->data() == "THREAD");
  EXPECT_EQ(2, a->handle_obj_type_resolved);
  ASSERT_EQ(fidl::flat::Type::Kind::kHandle, a->type->kind);
  auto a_handle_type = static_cast<const fidl::flat::HandleType*>(a->type);
  ASSERT_EQ(fidl::types::HandleSubtype::kThread, a_handle_type->subtype);
  ASSERT_NULL(a->handle_rights);

  auto b = library.LookupStruct("MyStruct")->members[1].type_ctor.get();
  EXPECT_TRUE(b->handle_subtype_identifier.has_value());
  ASSERT_TRUE(b->handle_subtype_identifier.value().span()->data() == "PROCESS");
  EXPECT_EQ(1, b->handle_obj_type_resolved);
  ASSERT_EQ(fidl::flat::Type::Kind::kHandle, b->type->kind);
  auto b_handle_type = static_cast<const fidl::flat::HandleType*>(b->type);
  ASSERT_EQ(fidl::types::HandleSubtype::kProcess, b_handle_type->subtype);
  ASSERT_NULL(b->handle_rights);

  auto c = library.LookupStruct("MyStruct")->members[2].type_ctor.get();
  EXPECT_TRUE(c->handle_subtype_identifier.has_value());
  ASSERT_TRUE(c->handle_subtype_identifier.value().span()->data() == "VMO");
  EXPECT_EQ(3, c->handle_obj_type_resolved);
  ASSERT_NOT_NULL(c->type);
  ASSERT_EQ(fidl::flat::Type::Kind::kHandle, c->type->kind);
  auto c_handle_type = static_cast<const fidl::flat::HandleType*>(c->type);
  ASSERT_EQ(fidl::types::HandleSubtype::kVmo, c_handle_type->subtype);
  ASSERT_NOT_NULL(c->handle_rights);
  ASSERT_EQ(
      static_cast<const fidl::flat::NumericConstantValue<uint32_t>&>(c->handle_rights->Value())
          .value,
      2);
}

TEST(HandleTests, InvalidFidlDefinedHandleSubtype) {
  SharedAmongstLibraries shared;
  fidl::ExperimentalFlags experimental_flags;
  experimental_flags.SetFlag(fidl::ExperimentalFlags::Flag::kEnableHandleRights);

  TestLibrary library(R"FIDL(
library example;

using zx;

struct MyStruct {
  zx.handle:ZIPPY a;
};
)FIDL",
                      std::move(experimental_flags));

  AddZxLibraryDep(&library, &shared);
  EXPECT_FALSE(library.Compile());
  const auto& errors = library.errors();
  ASSERT_EQ(errors.size(), 1);
  ASSERT_ERR(errors[0], fidl::ErrCouldNotResolveHandleSubtype);
  EXPECT_TRUE(errors[0]->msg.find("ZIPPY") != std::string::npos);
}

TEST(HandleTests, DisallowOldHandles) {
  SharedAmongstLibraries shared;
  fidl::ExperimentalFlags experimental_flags;

  TestLibrary library(R"FIDL(
library example;

using zx;

struct MyStruct {
    handle<vmo> h;
};
)FIDL",
                      std::move(experimental_flags));

  AddZxLibraryDep(&library, &shared);
  EXPECT_FALSE(library.Compile());
  const auto& errors = library.errors();
  ASSERT_EQ(errors.size(), 1);
  ASSERT_ERR(errors[0], fidl::ErrUnknownType);
}

// TODO(fxbug.dev/64629): Consider how we could validate resource_declaration without any use.
TEST(HandleTests, ResourceDefinitionOnlySubtypeNoRightsTest) {
  fidl::ExperimentalFlags experimental_flags;
  experimental_flags.SetFlag(fidl::ExperimentalFlags::Flag::kEnableHandleRights);

  TestLibrary library(R"FIDL(
library example;

enum obj_type : uint32 {
    NONE = 0;
    VMO = 3;
};

resource_definition handle : uint32 {
    properties {
        obj_type subtype;
    };
};

resource struct MyStruct {
    handle:<VMO> h;
};
)FIDL",
                      std::move(experimental_flags));

  EXPECT_TRUE(library.Compile());

  auto h_type_ctor = library.LookupStruct("MyStruct")->members[0].type_ctor.get();

  EXPECT_TRUE(h_type_ctor->handle_subtype_identifier.has_value());
  ASSERT_TRUE(h_type_ctor->handle_subtype_identifier.value().span()->data() == "VMO");
  EXPECT_EQ(3, h_type_ctor->handle_obj_type_resolved);
  ASSERT_NULL(h_type_ctor->handle_rights);
}

TEST(HandleTests, ResourceDefinitionMissingRightsPropertyTest) {
  fidl::ExperimentalFlags experimental_flags;
  experimental_flags.SetFlag(fidl::ExperimentalFlags::Flag::kEnableHandleRights);

  TestLibrary library(R"FIDL(
library example;

enum obj_type : uint32 {
    NONE = 0;
    VMO = 3;
};

resource_definition handle : uint32 {
    properties {
        obj_type subtype;
    };
};

resource struct MyStruct {
    handle:<VMO, 1> h;
};
)FIDL",
                      std::move(experimental_flags));

  EXPECT_FALSE(library.Compile());
  const auto& errors = library.errors();
  ASSERT_EQ(errors.size(), 2);
  ASSERT_ERR(errors[0], fidl::ErrResourceMissingRightsProperty);
  ASSERT_ERR(errors[1], fidl::ErrCouldNotResolveHandleRights);
}

// TODO(fxbug.dev/64629): Consider how we could validate resource_declaration without any use.
TEST(HandleTests, ResourceDefinitionMissingSubtypePropertyTest) {
  fidl::ExperimentalFlags experimental_flags;
  experimental_flags.SetFlag(fidl::ExperimentalFlags::Flag::kEnableHandleRights);

  TestLibrary library(R"FIDL(
library example;

resource_definition handle : uint32 {
    properties {
        uint32 rights;
    };
};

resource struct MyStruct {
    handle:VMO h;
};
)FIDL",
                      std::move(experimental_flags));

  EXPECT_FALSE(library.Compile());
  const auto& errors = library.errors();
  ASSERT_EQ(errors.size(), 2);
  ASSERT_ERR(errors[0], fidl::ErrResourceMissingSubtypeProperty);
  ASSERT_ERR(errors[1], fidl::ErrCouldNotResolveHandleSubtype);
}

}  // namespace
