// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "tools/kazoo/syscall_library.h"

#include "gtest/gtest.h"
#include "tools/kazoo/test_ir_test_no_methods.test.h"
#include "tools/kazoo/test_ir_test_one_protocol_one_method.test.h"

namespace {

TEST(SyscallLibrary, LoaderSimpleEmpty) {
  SyscallLibrary library;
  ASSERT_TRUE(SyscallLibraryLoader::FromJson(k_test_no_methods, &library));
  EXPECT_EQ(library.name(), "zz");
  EXPECT_TRUE(library.syscalls().empty());
}

TEST(SyscallLibrary, LoaderSingleMethod) {
  SyscallLibrary library;
  ASSERT_TRUE(SyscallLibraryLoader::FromJson(k_test_one_protocol_one_method, &library));
  EXPECT_EQ(library.name(), "zz");
  ASSERT_EQ(library.syscalls().size(), 1u);

  const auto& sc = library.syscalls()[0];
  EXPECT_EQ(sc->original_interface(), "zz/Single");
  EXPECT_EQ(sc->original_name(), "DoThing");
  EXPECT_EQ(sc->category(), "single");
  EXPECT_EQ(sc->name(), "single_do_thing");
  EXPECT_EQ(sc->short_description(), "This does a single thing.");
  EXPECT_EQ(sc->attributes().size(), 1u);  // Doc is an attribute.
  EXPECT_TRUE(sc->HasAttribute("Doc"));
  EXPECT_FALSE(sc->is_noreturn());

  const Struct& req = sc->request();
  EXPECT_EQ(req.members().size(), 1u);
  EXPECT_EQ(req.members()[0].name(), "an_input");

  const Struct& resp = sc->response();
  EXPECT_EQ(resp.members().size(), 1u);
  EXPECT_EQ(resp.members()[0].name(), "status");
}

}  // namespace
