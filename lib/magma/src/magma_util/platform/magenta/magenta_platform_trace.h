// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef MAGENTA_PLATFORM_TRACE_H
#define MAGENTA_PLATFORM_TRACE_H

#include "mx/channel.h"
#include "platform_trace.h"
#include <thread>

namespace magma {


class MagentaPlatformTrace : public PlatformTrace {
public:
    MagentaPlatformTrace();

private:
    std::thread trace_thread_;
};

} //

#endif // MAGENTA_PLATFORM_TRACE_H