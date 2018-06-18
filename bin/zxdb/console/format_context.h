// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <string>

namespace zxdb {

class Location;
class OutputBuffer;

// Formats the source information for the given location and adds it to the
// output buffer. Returns true on success. If this file isn't found or doesn't
// have the line number, does nothing and returns false.
bool FormatFileContext(const Location& location, OutputBuffer* out);

// Low-level version that does not depend on any files on disk. The column is
// 1-based, if 0 it will highlight the whole line.
bool FormatFileContext(const std::string& file_contents, int line, int column,
                       OutputBuffer* out);

}  // namespace zxdb
