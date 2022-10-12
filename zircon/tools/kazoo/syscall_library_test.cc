// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "tools/kazoo/syscall_library.h"

#include "tools/kazoo/test.h"
#include "tools/kazoo/test_ir_test_kernelwrappers.test.h"
#include "tools/kazoo/test_ir_test_no_methods.test.h"
#include "tools/kazoo/test_ir_test_one_protocol_one_method.test.h"
#include "tools/kazoo/test_ir_test_pointers_and_vectors.test.h"

namespace {

TEST(SyscallLibrary, LoaderSimpleEmpty) {
  SyscallLibrary library;
  ASSERT_TRUE(SyscallLibraryLoader::FromJson(k_test_no_methods, &library));
  EXPECT_EQ(library.name(), "zx");
  EXPECT_TRUE(library.syscalls().empty());
}

TEST(SyscallLibrary, LoaderSingleMethod) {
  SyscallLibrary library;
  ASSERT_TRUE(SyscallLibraryLoader::FromJson(k_test_one_protocol_one_method, &library));
  EXPECT_EQ(library.name(), "zx");
  ASSERT_EQ(library.syscalls().size(), 1u);

  const auto& sc = library.syscalls()[0];
  EXPECT_EQ(sc->id(), "zx/Single");
  EXPECT_EQ(sc->name(), "DoThing");
  EXPECT_EQ(sc->category(), "single");
  EXPECT_EQ(sc->snake_name(), "single_do_thing");
  EXPECT_EQ(sc->attributes().size(), 1u);  // Doc is an attribute.
  EXPECT_TRUE(sc->HasAttribute("Doc"));
  EXPECT_FALSE(sc->is_noreturn());

  const Struct& req = sc->request();
  EXPECT_EQ(req.members().size(), 1u);
  EXPECT_EQ(req.members()[0].name(), "an_input");

  EXPECT_TRUE(sc->response().members().empty());
  const std::optional<Type>& error_type = sc->error_type();
  ASSERT_TRUE(error_type);
  ASSERT_TRUE(error_type->IsZxBasicAlias());
  EXPECT_EQ(error_type->DataAsZxBasicAlias().name(), "Status");
}

TEST(SyscallLibrary, LoaderVectors) {
  SyscallLibrary library;
  ASSERT_TRUE(SyscallLibraryLoader::FromJson(k_test_pointers_and_vectors, &library));
  EXPECT_EQ(library.name(), "zx");
  ASSERT_EQ(library.syscalls().size(), 3u);

  const auto& sc0 = library.syscalls()[0];
  ASSERT_EQ(sc0->num_kernel_args(), 4u);
  EXPECT_EQ(sc0->kernel_arguments()[0].name(), "bytes");
  EXPECT_EQ(sc0->kernel_arguments()[1].name(), "num_bytes");
  EXPECT_EQ(sc0->kernel_arguments()[2].name(), "str");
  EXPECT_EQ(sc0->kernel_arguments()[3].name(), "str_size");

  const auto& sc1 = library.syscalls()[1];
  ASSERT_EQ(sc1->num_kernel_args(), 2u);
  EXPECT_EQ(sc1->kernel_arguments()[0].name(), "ins");
  EXPECT_EQ(sc1->kernel_arguments()[1].name(), "num_ins");

  const auto& sc2 = library.syscalls()[2];
  ASSERT_EQ(sc2->num_kernel_args(), 0u);
  EXPECT_TRUE(sc2->kernel_return_type().IsVoid());
}

TEST(SyscallLibrary, AttributeBasedFilter) {
  // CompiledOut should be included normally.
  SyscallLibrary library1;
  ASSERT_TRUE(SyscallLibraryLoader::FromJson(k_test_kernelwrappers, &library1));
  library1.FilterSyscalls(std::set<std::string>());
  EXPECT_EQ(library1.name(), "zx");
  ASSERT_EQ(library1.syscalls().size(), 8u);
  bool debug_found = false;
  for (const auto& sc : library1.syscalls()) {
    if (sc->snake_name() == "kwrap_compiled_out") {
      debug_found = true;
    }
  }
  EXPECT_TRUE(debug_found);

  // CompiledOut should be excluded in when testonly are stripped.
  SyscallLibrary library2;
  ASSERT_TRUE(SyscallLibraryLoader::FromJson(k_test_kernelwrappers, &library2));
  std::set<std::string> exclude1{"testonly"};
  library2.FilterSyscalls(exclude1);
  EXPECT_EQ(library2.name(), "zx");
  ASSERT_EQ(library2.syscalls().size(), 7u);
  bool testonly_found = false;
  for (const auto& sc : library2.syscalls()) {
    if (sc->name() == "kwrap_compiled_out") {
      testonly_found = true;
    }
  }
  EXPECT_FALSE(testonly_found);

  // Neither CompiledOut nor ANoRetFunc should be included when both attributes
  // are excluded.
  SyscallLibrary library3;
  ASSERT_TRUE(SyscallLibraryLoader::FromJson(k_test_kernelwrappers, &library3));
  std::set<std::string> exclude2{"testonly", "noreturn"};
  library3.FilterSyscalls(exclude2);
  EXPECT_EQ(library3.name(), "zx");
  ASSERT_EQ(library3.syscalls().size(), 6u);
  bool stripped_found = false;
  for (const auto& sc : library3.syscalls()) {
    if (sc->name() == "kwrap_a_no_ret_func" || sc->name() == "kwrap_compiled_out") {
      stripped_found = true;
    }
  }
  EXPECT_FALSE(stripped_found);
}

}  // namespace
