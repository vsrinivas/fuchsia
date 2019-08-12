// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "tools/kazoo/syscall_library.h"

#include "gtest/gtest.h"
#include "tools/kazoo/test_input_ir.h"

namespace {

TEST(SyscallLibrary, LoaderSimpleEmpty) {
  SyscallLibrary library;
  ASSERT_TRUE(SyscallLibraryLoader::FromJson(kOneProtocolNoMethods, &library));
  EXPECT_EQ(library.name(), "zx");
  EXPECT_TRUE(library.syscalls().empty());
}

TEST(SyscallLibrary, LoaderSingleMethod) {
  SyscallLibrary library;
  ASSERT_TRUE(SyscallLibraryLoader::FromJson(kOneProtocolOneMethod, &library));
  EXPECT_EQ(library.name(), "zx");
  ASSERT_EQ(library.syscalls().size(), 1u);

  const auto& sc = library.syscalls()[0];
  EXPECT_EQ(sc->original_interface(), "zx/Single");
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

TEST(SyscallLibrary, CamelToSnake) {
  EXPECT_EQ(CamelToSnake(""), "");
  EXPECT_EQ(CamelToSnake("A"), "a");
  EXPECT_EQ(CamelToSnake("AA"), "aa");
  EXPECT_EQ(CamelToSnake("Aa"), "aa");
  EXPECT_EQ(CamelToSnake("Stuff"), "stuff");
  EXPECT_EQ(CamelToSnake("SomeThing"), "some_thing");
  EXPECT_EQ(CamelToSnake("SomeOtherThing"), "some_other_thing");
  EXPECT_EQ(CamelToSnake("someThing"), "some_thing");
  EXPECT_EQ(CamelToSnake("ThisIsASCII"), "this_is_ascii");
  EXPECT_EQ(CamelToSnake("getHTTPResponseCode"), "get_http_response_code");
  EXPECT_EQ(CamelToSnake("get2HTTPResponseCode"), "get2_http_response_code");
  EXPECT_EQ(CamelToSnake("HTTPResponseCode"), "http_response_code");
  EXPECT_EQ(CamelToSnake("HTTPResponseCodeNEW"), "http_response_code_new");
  EXPECT_EQ(CamelToSnake("DoubleIEEE754"), "double_ieee754");
  EXPECT_EQ(CamelToSnake("MemVTable"), "mem_vtable");
  EXPECT_EQ(CamelToSnake("SList"), "slist");
  EXPECT_EQ(CamelToSnake("ThisIsASCII"), "this_is_ascii");
  EXPECT_EQ(CamelToSnake("ThisIsASCIIText"), "this_is_ascii_text");
  EXPECT_EQ(CamelToSnake("WaCkYsTuFf"), "wa_ck_ys_tu_ff");
  EXPECT_EQ(CamelToSnake("WAcK"), "wac_k");
}

}  // namespace
