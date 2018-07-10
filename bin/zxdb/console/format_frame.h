// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

namespace zxdb {

class Frame;
class OutputBuffer;
class Thread;

// Outputs the list of frames to the console. This will complete asynchronously
// if the frames are not currently available.
void OutputFrameList(Thread* thread, bool long_format);

// Formats one frame to the output buffer. The frame ID will be printed if
// supplied. If the ID is -1, it will be omitted.
void FormatFrame(const Frame* frame, OutputBuffer* out, bool long_format,
                 int id = -1);

}  // namespace zxdb
