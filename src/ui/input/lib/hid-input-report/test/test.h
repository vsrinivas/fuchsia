// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_INPUT_LIB_HID_INPUT_REPORT_TEST_TEST_H_
#define SRC_UI_INPUT_LIB_HID_INPUT_REPORT_TEST_TEST_H_

namespace hid_input_report {

// This is the static size of the buffer to allocate test
// descriptors. This was chosen heuristically, a single descriptor shouldn't
// really need to be larger than this size.
static constexpr size_t kFidlDescriptorBufferSize = 4096 * 2;
using TestDescriptorAllocator = fidl::Arena<kFidlDescriptorBufferSize>;

// This is the static size of the buffer to allocate test
// reports. This was chosen heuristically, a single report shouldn't
// really need to be larger than this size.
static constexpr size_t kFidlReportBufferSize = 4096 * 2;
using TestReportAllocator = fidl::Arena<kFidlReportBufferSize>;

}  // namespace hid_input_report

#endif  // SRC_UI_INPUT_LIB_HID_INPUT_REPORT_TEST_TEST_H_
