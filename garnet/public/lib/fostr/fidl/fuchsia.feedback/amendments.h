// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_FOSTR_FIDL_FUCHSIA_FEEDBACK_AMENDMENTS_H_
#define LIB_FOSTR_FIDL_FUCHSIA_FEEDBACK_AMENDMENTS_H_

#include <fuchsia/feedback/cpp/fidl.h>

#include <ostream>

namespace fuchsia {
namespace feedback {

// NOTE:
// //garnet/public/lib/fostr/fidl/fuchsia.feedback automatically generates ostream
// formatters for fuchsia.feedback *except* those formatters that are listed here.
// The code generator knows which formatters to exclude from the generated code
// by consulting the 'amendments.json' file.
//
// If you add or remove formatters from this file, please be sure that the
// amendments.json file is updated accordingly.

std::ostream& operator<<(std::ostream& os, const Annotation& value);
std::ostream& operator<<(std::ostream& os, const ComponentData& value);

}  // namespace feedback
}  // namespace fuchsia

#endif  // LIB_FOSTR_FIDL_FUCHSIA_FEEDBACK_AMENDMENTS_H_
