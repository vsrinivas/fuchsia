// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/zxdb/console/format_value.h"

#include "garnet/bin/zxdb/client/symbols/value.h"
#include "garnet/bin/zxdb/console/output_buffer.h"

namespace zxdb {

void FormatValue(const Value* value, OutputBuffer* out) {
  out->Append(Syntax::kVariable, value->GetAssignedName());
  out->Append(" = TODO");
}

}  // namespace zxdb
