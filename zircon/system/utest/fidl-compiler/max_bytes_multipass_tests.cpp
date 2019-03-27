// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <algorithm>
#include <unittest/unittest.h>

#include <fidl/flat_ast.h>
#include <fidl/lexer.h>
#include <fidl/parser.h>
#include <fidl/source_file.h>

#include "test_library.h"

namespace {

class MaxBytesMultiPassLibrary : public TestLibrary {
public:
    MaxBytesMultiPassLibrary()
        : TestLibrary(multipass_main_file) {
        source_files_.push_back(
            MakeSourceFile("max_bytes_multipass_main.fidl", multipass_main_file));
        source_files_.push_back(
            MakeSourceFile("max_bytes_multipass_extern_defs.fidl", multipass_extern_defs_file));
    }

    bool Compile() {
        for (auto& file : source_files_) {
            fidl::Lexer lexer(file, error_reporter_);
            fidl::Parser parser(&lexer, error_reporter_);

            auto ast = parser.Parse();
            if (!parser.Ok() || !library_->ConsumeFile(std::move(ast))) {
                return false;
            }
        }

        return library_->Compile();
    }

    fidl::flat::Library& GetLibrary() const {
        return *library_;
    }

private:
    static constexpr auto multipass_main_file = R"FIDL(
library fidl.test.maxbytesmultipass;

struct SimpleStruct {
    uint32 a;
};

struct OptionalStruct {
    SimpleStruct? a;
    SimpleStruct? b;
};

struct HandleStruct {
    uint32 a;
    handle<vmo> b;
};

struct ArrayOfSimpleStructs {
    array<SimpleStruct>:42 arr;
};

struct ArrayOfOptionalStructs {
    array<OptionalStruct>:42 arr;
};

struct ArrayOfHandleStructs {
    array<HandleStruct>:42 arr;
};

union OptionalAndHandleUnion {
    OptionalStruct opt;
    HandleStruct hnd;
};

struct ArrayOfOptionalAndHandleUnions {
    array<OptionalAndHandleUnion>:42 arr;
};

struct ExternalArrayStruct {
    array<ExternalSimpleStruct>:EXTERNAL_SIZE_DEF a;
};

struct ExternalStringSizeStruct {
    string:EXTERNAL_SIZE_DEF a;
};

struct ExternalVectorSizeStruct {
    vector<handle>:EXTERNAL_SIZE_DEF a;
};

)FIDL";

    static constexpr auto multipass_extern_defs_file = R"FIDL(
library fidl.test.maxbytesmultipass;

const uint32 EXTERNAL_SIZE_DEF = ANOTHER_INDIRECTION;
const uint32 ANOTHER_INDIRECTION = 32;

struct ExternalSimpleStruct {
    uint32 a;
};

)FIDL";
    std::vector<fidl::SourceFile> source_files_;
};

static bool simple_struct_array(void) {
    BEGIN_TEST;

    MaxBytesMultiPassLibrary test_library;
    EXPECT_TRUE(test_library.Compile());

    auto smp_struct = test_library.LookupStruct("SimpleStruct");
    EXPECT_NONNULL(smp_struct);
    EXPECT_EQ(smp_struct->typeshape.Size(), 4);
    EXPECT_EQ(smp_struct->typeshape.MaxOutOfLine(), 0);
    EXPECT_EQ(smp_struct->typeshape.MaxHandles(), 0);

    auto arr_of_smps = test_library.LookupStruct("ArrayOfSimpleStructs");
    EXPECT_NONNULL(arr_of_smps);
    EXPECT_EQ(arr_of_smps->typeshape.Size(), smp_struct->typeshape.Size() * 42);
    EXPECT_EQ(arr_of_smps->typeshape.MaxOutOfLine(), smp_struct->typeshape.MaxOutOfLine() * 42);
    EXPECT_EQ(arr_of_smps->typeshape.MaxHandles(), smp_struct->typeshape.MaxHandles() * 42);

    END_TEST;
}

static bool optional_struct_array(void) {
    BEGIN_TEST;

    MaxBytesMultiPassLibrary test_library;
    EXPECT_TRUE(test_library.Compile());

    auto opt_struct = test_library.LookupStruct("OptionalStruct");
    EXPECT_NONNULL(opt_struct);
    EXPECT_EQ(opt_struct->typeshape.Size(), 16);
    EXPECT_EQ(opt_struct->typeshape.MaxOutOfLine(), 16);
    EXPECT_EQ(opt_struct->typeshape.MaxHandles(), 0);

    auto arr_of_opt_struct = test_library.LookupStruct("ArrayOfOptionalStructs");
    EXPECT_NONNULL(arr_of_opt_struct);
    EXPECT_EQ(arr_of_opt_struct->typeshape.Size(), opt_struct->typeshape.Size() * 42);
    EXPECT_EQ(arr_of_opt_struct->typeshape.MaxOutOfLine(),
              opt_struct->typeshape.MaxOutOfLine() * 42);
    EXPECT_EQ(arr_of_opt_struct->typeshape.MaxHandles(), opt_struct->typeshape.MaxHandles() * 42);

    END_TEST;
}

static bool handle_struct_array(void) {
    BEGIN_TEST;

    MaxBytesMultiPassLibrary test_library;
    EXPECT_TRUE(test_library.Compile());

    auto hnd_struct = test_library.LookupStruct("HandleStruct");
    EXPECT_NONNULL(hnd_struct);
    EXPECT_EQ(hnd_struct->typeshape.Size(), 8);
    EXPECT_EQ(hnd_struct->typeshape.MaxOutOfLine(), 0);
    EXPECT_EQ(hnd_struct->typeshape.MaxHandles(), 1);

    auto arr_of_hnd_struct = test_library.LookupStruct("ArrayOfHandleStructs");
    EXPECT_NONNULL(arr_of_hnd_struct);
    EXPECT_EQ(arr_of_hnd_struct->typeshape.Size(), hnd_struct->typeshape.Size() * 42);
    EXPECT_EQ(arr_of_hnd_struct->typeshape.MaxOutOfLine(),
              hnd_struct->typeshape.MaxOutOfLine() * 42);
    EXPECT_EQ(arr_of_hnd_struct->typeshape.MaxHandles(), hnd_struct->typeshape.MaxHandles() * 42);

    END_TEST;
}
static bool optional_handle_union_array(void) {
    BEGIN_TEST;

    MaxBytesMultiPassLibrary test_library;
    EXPECT_TRUE(test_library.Compile());

    auto opt_struct = test_library.LookupStruct("OptionalStruct");
    EXPECT_NONNULL(opt_struct);

    auto hnd_struct = test_library.LookupStruct("HandleStruct");
    EXPECT_NONNULL(hnd_struct);

    auto opt_hnd_union = test_library.LookupUnion("OptionalAndHandleUnion");
    EXPECT_NONNULL(opt_hnd_union);
    EXPECT_EQ(opt_hnd_union->typeshape.Size(), 24);
    EXPECT_EQ(opt_hnd_union->typeshape.MaxOutOfLine(),
              std::max(opt_struct->typeshape.MaxOutOfLine(), hnd_struct->typeshape.MaxOutOfLine()));
    EXPECT_EQ(opt_hnd_union->typeshape.MaxHandles(),
              std::max(opt_struct->typeshape.MaxHandles(), hnd_struct->typeshape.MaxHandles()));

    auto arr_of_unions_struct = test_library.LookupStruct("ArrayOfOptionalAndHandleUnions");
    EXPECT_NONNULL(arr_of_unions_struct);
    EXPECT_EQ(arr_of_unions_struct->typeshape.Size(), opt_hnd_union->typeshape.Size() * 42);
    EXPECT_EQ(arr_of_unions_struct->typeshape.MaxOutOfLine(),
              opt_hnd_union->typeshape.MaxOutOfLine() * 42);
    EXPECT_EQ(arr_of_unions_struct->typeshape.MaxHandles(),
              opt_hnd_union->typeshape.MaxHandles() * 42);

    END_TEST;
}

static bool external_definitions(void) {
    BEGIN_TEST;

    MaxBytesMultiPassLibrary test_library;
    EXPECT_TRUE(test_library.Compile());

    auto ext_struct = test_library.LookupStruct("ExternalSimpleStruct");
    EXPECT_NONNULL(ext_struct);
    EXPECT_EQ(ext_struct->typeshape.Size(), 4);
    EXPECT_EQ(ext_struct->typeshape.MaxOutOfLine(), 0);
    EXPECT_EQ(ext_struct->typeshape.MaxHandles(), 0);

    auto ext_arr_struct = test_library.LookupStruct("ExternalArrayStruct");
    EXPECT_NONNULL(ext_arr_struct);
    EXPECT_EQ(ext_arr_struct->typeshape.Size(), ext_struct->typeshape.Size() * 32);

    auto ext_str_struct = test_library.LookupStruct("ExternalStringSizeStruct");
    EXPECT_NONNULL(ext_str_struct);
    EXPECT_EQ(ext_str_struct->typeshape.MaxOutOfLine(), 32);

    auto ext_vec_struct = test_library.LookupStruct("ExternalVectorSizeStruct");
    EXPECT_NONNULL(ext_vec_struct);
    EXPECT_EQ(ext_vec_struct->typeshape.MaxOutOfLine(), 32 * 4);
    EXPECT_EQ(ext_vec_struct->typeshape.MaxHandles(), 32);

    END_TEST;
}

} // namespace

BEGIN_TEST_CASE(max_bytes_multipass_tests)
RUN_TEST(simple_struct_array)
RUN_TEST(optional_struct_array)
RUN_TEST(handle_struct_array)
RUN_TEST(optional_handle_union_array)
RUN_TEST(external_definitions)
END_TEST_CASE(max_bytes_multipass_tests)
