// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ZIRCON_SYSTEM_UTEST_FIDL_EXTRA_MESSAGES_H_
#define ZIRCON_SYSTEM_UTEST_FIDL_EXTRA_MESSAGES_H_

#include <lib/fidl/coding.h>
#include <lib/fidl/llcpp/string_view.h>
#include <lib/fidl/llcpp/vector_view.h>
#include <lib/fidl/internal.h>

// "extern" definitions copied from extra_messages.c

#if defined(__cplusplus)
extern "C" {
#endif

extern const fidl_type_t fidl_test_coding_StructWithManyHandlesTable;
extern const fidl_type_t fidl_test_coding_StructWithHandleTable;
extern const fidl_type_t fidl_test_coding_TableOfStructWithHandleTable;
extern const fidl_type_t fidl_test_coding_OlderSimpleTableTable;
extern const fidl_type_t fidl_test_coding_NewerSimpleTableTable;
extern const fidl_type_t fidl_test_coding_SimpleTableTable;
extern const fidl_type_t fidl_test_coding_SmallerTableOfStructWithHandleTable;
extern const fidl_type_t fidl_test_coding_SampleUnionTable;
extern const fidl_type_t fidl_test_coding_SampleXUnionTable;
extern const fidl_type_t fidl_test_coding_SampleStrictXUnionTable;
extern const fidl_type_t fidl_test_coding_SampleXUnionStructTable;
extern const fidl_type_t fidl_test_coding_SampleStrictXUnionStructTable;
extern const fidl_type_t fidl_test_coding_SampleNullableXUnionStructTable;

extern const fidl_type_t fidl_test_coding_Int32BitsTable;
extern const fidl_type_t fidl_test_coding_Int32BitsStructTable;
extern const fidl_type_t fidl_test_coding_Int16BitsTable;
extern const fidl_type_t fidl_test_coding_Int16BitsStructTable;

extern const fidl_type_t fidl_test_coding_Int32EnumTable;
extern const fidl_type_t fidl_test_coding_Int8EnumStructTable;
extern const fidl_type_t fidl_test_coding_Int16EnumStructTable;
extern const fidl_type_t fidl_test_coding_Int32EnumStructTable;
extern const fidl_type_t fidl_test_coding_Int64EnumStructTable;
extern const fidl_type_t fidl_test_coding_Uint8EnumStructTable;
extern const fidl_type_t fidl_test_coding_Uint16EnumStructTable;
extern const fidl_type_t fidl_test_coding_Uint32EnumStructTable;
extern const fidl_type_t fidl_test_coding_Uint64EnumStructTable;

extern const fidl_type_t fidl_test_coding_LinearizerTestVectorOfUint32RequestTable;
extern const fidl_type_t fidl_test_coding_LinearizerTestVectorOfStringRequestTable;

#if defined(__cplusplus)
}
#endif

namespace fidl {

using SimpleTable = fidl::VectorView<fidl_envelope_t>;
struct SimpleTableEnvelopes {
  alignas(FIDL_ALIGNMENT) fidl_envelope_t x;
  fidl_envelope_t reserved1;
  fidl_envelope_t reserved2;
  fidl_envelope_t reserved3;
  fidl_envelope_t y;
};
struct IntStruct {
  alignas(FIDL_ALIGNMENT) int64_t v;
};

using TableOfStruct = fidl::VectorView<fidl_envelope_t>;
struct TableOfStructEnvelopes {
  alignas(FIDL_ALIGNMENT) fidl_envelope_t a;
  fidl_envelope_t b;
};
struct OrdinalOneStructWithHandle {
  alignas(FIDL_ALIGNMENT) zx_handle_t h;
  int32_t foo;
};
struct OrdinalTwoStructWithManyHandles {
  alignas(FIDL_ALIGNMENT) zx_handle_t h1;
  zx_handle_t h2;
  fidl::VectorView<zx_handle_t> hs;
};
struct TableOfStructLayout {
  TableOfStruct envelope_vector;
  TableOfStructEnvelopes envelopes;
  OrdinalOneStructWithHandle a;
  OrdinalTwoStructWithManyHandles b;
};

using SmallerTableOfStruct = fidl::VectorView<fidl_envelope_t>;
struct SmallerTableOfStructEnvelopes {
  alignas(FIDL_ALIGNMENT) fidl_envelope_t b;
};

struct SampleXUnion {
  FIDL_ALIGNDECL
  fidl_xunion_t header;

  // Representing out-of-line part
  union {
    FIDL_ALIGNDECL
    IntStruct i;

    FIDL_ALIGNDECL
    SimpleTable st;

    FIDL_ALIGNDECL
    int32_t raw_int;
  };
};
constexpr uint32_t kSampleXUnionIntStructOrdinal = 376675050;
constexpr uint32_t kSampleXUnionSimpleTableOrdinal = 586453270;
constexpr uint32_t kSampleXUnionRawIntOrdinal = 319709411;

struct SampleStrictXUnion {
  FIDL_ALIGNDECL
  fidl_xunion_t header;

  // Representing out-of-line part
  union {
    FIDL_ALIGNDECL
    IntStruct i;

    FIDL_ALIGNDECL
    SimpleTable st;

    FIDL_ALIGNDECL
    int32_t raw_int;
  };
};
constexpr uint32_t kSampleStrictXUnionIntStructOrdinal = 1928460319;
constexpr uint32_t kSampleStrictXUnionSimpleTableOrdinal = 915108668;
constexpr uint32_t kSampleStrictXUnionRawIntOrdinal = 419938224;

struct SampleXUnionStruct {
  FIDL_ALIGNDECL
  SampleXUnion xu;
};

struct SampleStrictXUnionStruct {
  FIDL_ALIGNDECL
  SampleStrictXUnion xu;
};

struct SampleNullableXUnionStruct {
  FIDL_ALIGNDECL
  SampleXUnion opt_xu;
};

struct Int16Bits {
  FIDL_ALIGNDECL
  uint16_t bits;
};

struct Int32Bits {
  FIDL_ALIGNDECL
  uint32_t bits;
};

#define TEST_ENUM_DEF(name, t) \
  struct name##Enum {          \
    FIDL_ALIGNDECL t e;        \
  };
TEST_ENUM_DEF(Int8, int8_t)
TEST_ENUM_DEF(Int16, int16_t)
TEST_ENUM_DEF(Int32, int32_t)
TEST_ENUM_DEF(Int64, int64_t)
TEST_ENUM_DEF(Uint8, uint8_t)
TEST_ENUM_DEF(Uint16, uint16_t)
TEST_ENUM_DEF(Uint32, uint32_t)
TEST_ENUM_DEF(Uint64, uint64_t)

}  // namespace fidl

#endif  // ZIRCON_SYSTEM_UTEST_FIDL_EXTRA_MESSAGES_H_
