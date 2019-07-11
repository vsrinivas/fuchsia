// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_DEBUG_ZXDB_CONSOLE_FORMAT_FRAME_H_
#define SRC_DEVELOPER_DEBUG_ZXDB_CONSOLE_FORMAT_FRAME_H_

#include "src/developer/debug/zxdb/console/async_output_buffer.h"
#include "src/developer/debug/zxdb/console/console.h"

namespace zxdb {

struct ConsoleFormatOptions;
class Frame;
class OutputBuffer;
class Thread;

// Generates the list of frames to the console. This will complete asynchronously. Printing of
// function parameter types is controlled by include_params.
fxl::RefPtr<AsyncOutputBuffer> FormatFrameList(Thread* thread, bool include_params,
                                               bool long_format);

// Formats one frame using the short format to the output buffer. The frame ID will be printed if
// supplied. If the ID is -1, it will be omitted.
//
// Printing of function parameter *types* is controlled by include_params. This function never
// prints parameter values.
//
// This does not append a newline at the end of the output.
OutputBuffer FormatFrame(const Frame* frame, bool include_params, int id = -1);

// Formats one frame using the long format. Since the long format includes function parameters which
// are computed asynchronously, this returns an AsyncOutputBuffer. The buffer will be appended to
// and NOT marked complete by this function.
//
// This does not append a newline at the end of the output.
fxl::RefPtr<AsyncOutputBuffer> FormatFrameLong(const Frame* frame, bool include_params,
                                               const ConsoleFormatOptions& options, int id = -1);

}  // namespace zxdb

#endif  // SRC_DEVELOPER_DEBUG_ZXDB_CONSOLE_FORMAT_FRAME_H_
