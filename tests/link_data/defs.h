// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_TESTS_LINK_DATA_DEFS_H_
#define PERIDOT_TESTS_LINK_DATA_DEFS_H_

namespace {

// Package URLs and actions of the test components used here.
constexpr char kModule0Url[] = "link_data_test_module0";
constexpr char kModule0Action[] = "action";
constexpr char kModule0Name[] = "module0";

constexpr char kModule1Url[] = "link_data_test_module1";
constexpr char kModule1Action[] = "action";
constexpr char kModule1Name[] = "module1";

constexpr char kModule2Url[] = "link_data_test_module2";
constexpr char kModule2Action[] = "action";
constexpr char kModule2Name[] = "module2";

// Names for links of Module0.
constexpr char kModule0Link[] = "module0";
constexpr char kModule1Link[] = "module1";
constexpr char kModule2Link[] = "module2";

// Names for properties inside link values.
constexpr char kCount[] = "count";

// Used as link values.
constexpr char kRootJson0[] = "\"123450\"";
constexpr char kRootJson1[] = "\"123451\"";

}  // namespace

#endif  // PERIDOT_TESTS_LINK_DATA_DEFS_H_
