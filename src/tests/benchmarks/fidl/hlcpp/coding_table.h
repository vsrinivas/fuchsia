// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_TESTS_BENCHMARKS_FIDL_HLCPP_CODING_TABLE_H_
#define SRC_TESTS_BENCHMARKS_FIDL_HLCPP_CODING_TABLE_H_

#include <lib/fidl/internal.h>

namespace hlcpp_benchmarks {

template <typename FidlType>
constexpr size_t EncodedSize = fidl::CodingTraits<FidlType>::inline_size_v1_no_ee;

template <typename FidlType>
FidlStructField FakeField = FidlStructField(FidlType::FidlType, sizeof(fidl_message_header_t),
                                            FIDL_ALIGN(EncodedSize<FidlType>) -
                                                EncodedSize<FidlType>);

template <typename FidlType>
fidl_type_t FidlTypeWithHeader = {
    .type_tag = kFidlTypeStruct,
    {.coded_struct = {.fields = &FakeField<FidlType>,
                      .field_count = 1,
                      .size = static_cast<uint32_t>(sizeof(fidl_message_header_t) +
                                                    EncodedSize<FidlType>),
                      .name = "Input"}},
};

}  // namespace hlcpp_benchmarks

#endif  // SRC_TESTS_BENCHMARKS_FIDL_HLCPP_CODING_TABLE_H_
