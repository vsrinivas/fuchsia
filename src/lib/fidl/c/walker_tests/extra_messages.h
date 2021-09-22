// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// TODO(fxbug.dev/48186) Auto-generate this file.

#ifndef SRC_LIB_FIDL_C_WALKER_TESTS_EXTRA_MESSAGES_H_
#define SRC_LIB_FIDL_C_WALKER_TESTS_EXTRA_MESSAGES_H_

#include <lib/fidl/coding.h>
#include <lib/fidl/internal.h>
#include <lib/fidl/llcpp/envelope.h>
#include <lib/fidl/llcpp/string_view.h>
#include <lib/fidl/llcpp/vector_view.h>

// "extern" definitions copied from <fidl/test/coding/c/fidl.h>

#if defined(__cplusplus)
extern "C" {
#endif

extern const FidlCodedStruct fidl_test_coding_fuchsia_StructWithManyHandlesTable;
extern const FidlCodedStruct fidl_test_coding_fuchsia_StructWithHandleTable;
extern const FidlCodedTable fidl_test_coding_fuchsiaTableOfStructWithHandleTable;
extern const FidlCodedTable fidl_test_coding_OlderSimpleTableTable;
extern const FidlCodedTable fidl_test_coding_NewerSimpleTableTable;
extern const FidlCodedTable fidl_test_coding_SimpleTableTable;
extern const FidlCodedTable fidl_test_coding_ResourceSimpleTableTable;
extern const FidlCodedTable fidl_test_coding_fuchsia_SmallerTableOfStructWithHandleTable;
extern const FidlCodedXUnion fidl_test_coding_SampleUnionTable;
extern const FidlCodedXUnion fidl_test_coding_SampleXUnionTable;
extern const FidlCodedXUnion fidl_test_coding_SampleStrictXUnionTable;
extern const FidlCodedStruct fidl_test_coding_SampleXUnionStructTable;
extern const FidlCodedStruct fidl_test_coding_SampleStrictXUnionStructTable;
extern const FidlCodedStruct fidl_test_coding_SampleNullableXUnionStructTable;

extern const FidlCodedBits fidl_test_coding_Int32BitsTable;
extern const FidlCodedStruct fidl_test_coding_Int32BitsStructTable;
extern const FidlCodedBits fidl_test_coding_Int16BitsTable;
extern const FidlCodedStruct fidl_test_coding_Int16BitsStructTable;

extern const FidlCodedEnum fidl_test_coding_Int32EnumTable;
extern const FidlCodedStruct fidl_test_coding_Int8EnumStructTable;
extern const FidlCodedStruct fidl_test_coding_Int16EnumStructTable;
extern const FidlCodedStruct fidl_test_coding_Int32EnumStructTable;
extern const FidlCodedStruct fidl_test_coding_Int64EnumStructTable;
extern const FidlCodedStruct fidl_test_coding_Uint8EnumStructTable;
extern const FidlCodedStruct fidl_test_coding_Uint16EnumStructTable;
extern const FidlCodedStruct fidl_test_coding_Uint32EnumStructTable;
extern const FidlCodedStruct fidl_test_coding_Uint64EnumStructTable;

extern const FidlCodedStruct fidl_test_coding_LinearizerTestVectorOfUint32RequestTable;
extern const FidlCodedStruct fidl_test_coding_LinearizerTestVectorOfStringRequestTable;

extern const FidlCodedStruct fidl_test_coding_LLCPPStyleUnionStructTable;

extern const FidlCodedStruct fidl_test_coding_Uint32VectorStructTable;
extern const FidlCodedStruct fidl_test_coding_StringStructTable;

extern const FidlCodedStruct fidl_test_coding_RecursiveOptionalTable;
extern const FidlCodedStruct fidl_test_coding_RecursiveTableTable;
extern const FidlCodedStruct fidl_test_coding_BoolStructTable;
extern const FidlCodedStruct fidl_test_coding_OptionalBoolStructTable;
#if defined(__cplusplus)
}
#endif

namespace fidl {

class LLCPPStyleUnion {
 public:
  LLCPPStyleUnion() : ordinal_(Ordinal::Invalid), envelope_{} {}

  LLCPPStyleUnion(LLCPPStyleUnion&&) = default;
  LLCPPStyleUnion& operator=(LLCPPStyleUnion&&) = default;

  ~LLCPPStyleUnion() { reset_ptr(nullptr); }

  void set_Primitive(::fidl::ObjectView<int32_t> elem) {
    ordinal_ = Ordinal::kPrimitive;
    reset_ptr(fidl::ObjectView<void>::FromExternal(reinterpret_cast<void*>(elem.get())));
  }

 private:
  enum class Ordinal : fidl_xunion_tag_t {
    Invalid = 0,
    kPrimitive = 1,  // 0x1
  };

  void reset_ptr(::fidl::ObjectView<void> new_ptr) {
    reinterpret_cast<::fidl::Envelope<uint64_t>*>(&envelope_)
        ->set_data(
            ::fidl::ObjectView<uint64_t>::FromExternal(static_cast<uint64_t*>(new_ptr.get())));
  }

  static void SizeAndOffsetAssertionHelper();
  Ordinal ordinal_;
  FIDL_ALIGNDECL
  ::fidl::UntypedEnvelope envelope_;
};

struct LLCPPStyleUnionStruct {
  FIDL_ALIGNDECL
  LLCPPStyleUnion u;
};

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

  // Representing out-of-line part.
  // There are three possibilities. All are allocated here, but only one will be set and used.
  FIDL_ALIGNDECL
  IntStruct i;

  FIDL_ALIGNDECL
  SimpleTable st;

  FIDL_ALIGNDECL
  int32_t raw_int;
};
struct SampleXUnionV2 {
  FIDL_ALIGNDECL
  fidl_xunion_v2_t header;

  // Representing out-of-line part.
  // There are three possibilities. All are allocated here, but only one will be set and used.
  FIDL_ALIGNDECL
  IntStruct i;

  FIDL_ALIGNDECL
  SimpleTable st;

  FIDL_ALIGNDECL
  int32_t raw_int;
};
constexpr uint32_t kSampleXUnionIntStructOrdinal = 1;
constexpr uint32_t kSampleXUnionSimpleTableOrdinal = 2;
constexpr uint32_t kSampleXUnionRawIntOrdinal = 3;

struct SampleStrictXUnion {
  FIDL_ALIGNDECL
  fidl_xunion_t header;

  // Representing out-of-line part.
  // There are three possibilities. All are allocated here, but only one will be set and used.
  FIDL_ALIGNDECL
  IntStruct i;

  FIDL_ALIGNDECL
  SimpleTable st;

  FIDL_ALIGNDECL
  int32_t raw_int;
};
constexpr uint32_t kSampleStrictXUnionIntStructOrdinal = 1;
constexpr uint32_t kSampleStrictXUnionSimpleTableOrdinal = 2;
constexpr uint32_t kSampleStrictXUnionRawIntOrdinal = 3;

struct SampleXUnionStruct {
  FIDL_ALIGNDECL
  SampleXUnion xu;
};
struct SampleXUnionV2Struct {
  FIDL_ALIGNDECL
  SampleXUnionV2 xu;
};

struct SampleStrictXUnionStruct {
  FIDL_ALIGNDECL
  SampleStrictXUnion xu;
};

struct SampleNullableXUnionStruct {
  FIDL_ALIGNDECL
  SampleXUnion opt_xu;
};
struct SampleNullableXUnionV2Struct {
  FIDL_ALIGNDECL
  SampleXUnionV2 opt_xu;
};

struct Uint32VectorStruct {
  fidl::VectorView<uint32_t> vec;
};

struct StringStruct {
  fidl::StringView str;
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

#endif  // SRC_LIB_FIDL_C_WALKER_TESTS_EXTRA_MESSAGES_H_
