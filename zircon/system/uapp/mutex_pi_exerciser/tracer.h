// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <fbl/macros.h>
#include <lib/async-loop/cpp/loop.h>
#include <memory>
#include <stdio.h>
#include <trace-engine/types.h>
#include <trace-provider/provider.h>

class Tracer {
public:
    Tracer() = default;
    ~Tracer() = default;
    DISALLOW_COPY_ASSIGN_AND_MOVE(Tracer);

    static void Trace(trace_scope_t scope, const char* fmt, ...) __printflike(2, 3);

    zx_status_t Start();

private:
    std::unique_ptr<async::Loop> loop_;
    std::unique_ptr<trace::TraceProviderWithFdio> trace_provider_;
};
