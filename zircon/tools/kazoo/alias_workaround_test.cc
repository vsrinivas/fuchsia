// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "tools/kazoo/output_util.h"
#include "tools/kazoo/syscall_library.h"
#include "tools/kazoo/test.h"
#include "tools/kazoo/test_ir_test_aliases.test.h"

namespace {

TEST(AliasWorkaround, Mappings) {
  SyscallLibrary library;
  ASSERT_TRUE(SyscallLibraryLoader::FromJson(k_test_aliases, &library));

  EXPECT_EQ(library.name(), "zx");
  ASSERT_EQ(library.syscalls().size(), 1u);

  const auto& sc = library.syscalls()[0];
  EXPECT_EQ(sc->snake_name(), "aliases_some_func");
  EXPECT_EQ(GetCUserModeName(sc->kernel_return_type()), "zx_status_t");

  // See test_aliases.test.fidl for this giant function's fidl spec. This covers all the aliases
  // required to map all syscalls today. We should be able to whittle these down over time and
  // eventually delete this mapping and test entirely.
  size_t cur_arg = 0;

#define CHECK_ARG(_type, _name)                                               \
  EXPECT_EQ(sc->kernel_arguments()[cur_arg].name(), _name);                   \
  EXPECT_EQ(GetCUserModeName(sc->kernel_arguments()[cur_arg].type()), _type); \
  ++cur_arg;

  // ConstFutexPtr
  CHECK_ARG("const zx_futex_t*", "b");

  // ConstVoidPtr
  CHECK_ARG("const void*", "c");

  // MutableString
  CHECK_ARG("char*", "d");
  CHECK_ARG("size_t", "d_size");

  // MutableUint32
  CHECK_ARG("uint32_t*", "e");

  // MutableUsize
  CHECK_ARG("size_t*", "f");

  // MutableVectorHandleDispositionU32Size
  CHECK_ARG("zx_handle_disposition_t*", "g");
  CHECK_ARG("uint32_t", "num_g");

  // MutableVectorWaitItem
  CHECK_ARG("zx_wait_item_t*", "h");
  CHECK_ARG("size_t", "num_h");

  // MutableVectorHandleU32Size
  CHECK_ARG("zx_handle_t*", "i");
  CHECK_ARG("uint32_t", "num_i");

  // MutableVectorVoid
  CHECK_ARG("void*", "j");
  CHECK_ARG("size_t", "j_size");

  // MutableVectorVoidU32Size
  CHECK_ARG("void*", "k");
  CHECK_ARG("uint32_t", "k_size");

  // VectorHandleInfoU32Size
  CHECK_ARG("const zx_handle_info_t*", "l");
  CHECK_ARG("uint32_t", "num_l");

  // VectorHandleU32Size
  CHECK_ARG("const zx_handle_t*", "m");
  CHECK_ARG("uint32_t", "num_m");

  // VectorPaddr
  CHECK_ARG("const zx_paddr_t*", "n");
  CHECK_ARG("size_t", "num_n");

  // VectorVoid
  CHECK_ARG("const void*", "o");
  CHECK_ARG("size_t", "o_size");

  // VectorVoidU32Size
  CHECK_ARG("const void*", "p");
  CHECK_ARG("uint32_t", "p_size");

  // VoidPtr
  CHECK_ARG("void*", "q");

  CHECK_ARG("zx_string_view_t*", "y");

#undef CHECK_ARG

  EXPECT_EQ(cur_arg, 28u);
}

}  // namespace
