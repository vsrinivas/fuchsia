// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

class StreamBuffer;

// Sink for data read from the debug command socket. This will deserialize
// the messages and execute the commands reqesuted.
void HandleDebugCommandData(StreamBuffer* stream);
