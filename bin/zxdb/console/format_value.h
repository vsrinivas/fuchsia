// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

namespace zxdb {

class OutputBuffer;
class Value;

void FormatValue(const Value* value, OutputBuffer* out);

}  // namespace zxdb
