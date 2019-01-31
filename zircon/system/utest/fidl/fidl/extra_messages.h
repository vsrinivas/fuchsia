// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/fidl/coding.h>
#include <lib/fidl/cpp/string_view.h>
#include <lib/fidl/cpp/vector_view.h>
#include <lib/fidl/internal.h>

// "extern" definitions copied from extra_messages.cpp

#if defined(__cplusplus)
extern "C" {
#endif

extern const fidl_type_t fidl_test_coding_StructWithManyHandlesTable;
extern const fidl_type_t fidl_test_coding_StructWithHandleTable;
extern const fidl_type_t fidl_test_coding_TableOfStructWithHandleTable;
extern const fidl_type_t fidl_test_coding_OlderSimpleTableTable;
extern const fidl_type_t fidl_test_coding_NewerSimpleTableTable;
extern const fidl_type_t fidl_test_coding_SimpleTableTable;
extern const fidl_type_t fidl_test_coding_SampleXUnionTable;
extern const fidl_type_t fidl_test_coding_SampleXUnionStructTable;

extern const fidl_type_t fidl_test_coding_LinearizerTestVectorOfUint32RequestTable;
extern const fidl_type_t fidl_test_coding_LinearizerTestVectorOfStringRequestTable;

using SimpleTable = fidl::VectorView<fidl_envelope_t>;
struct SimpleTableEnvelopes {
    alignas(FIDL_ALIGNMENT)
    fidl_envelope_t x;
    fidl_envelope_t reserved1;
    fidl_envelope_t reserved2;
    fidl_envelope_t reserved3;
    fidl_envelope_t y;
};
struct IntStruct {
    alignas(FIDL_ALIGNMENT)
    int64_t v;
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
    };
};
constexpr uint32_t kSampleXUnionIntStructOrdinal = 376675050;

struct SampleXUnionStruct {
    FIDL_ALIGNDECL
    SampleXUnion xu;
};

#if defined(__cplusplus)
}
#endif
