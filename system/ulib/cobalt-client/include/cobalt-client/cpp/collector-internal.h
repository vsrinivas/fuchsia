// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <stdint.h>

#include <cobalt-client/cpp/counter-internal.h>
#include <cobalt-client/cpp/histogram-internal.h>

namespace cobalt_client {
namespace internal {
// Interface for persisting collected data.
class Logger {
public:
    Logger(const Logger&) = delete;
    Logger(Logger&&) = delete;
    Logger& operator=(const Logger&) = delete;
    Logger& operator=(Logger&&) = delete;
    virtual ~Logger() = default;

    // Returns true if the histogram was persisted.
    virtual bool Log(uint64_t metric_id, const RemoteHistogram::EventBuffer& histogram) = 0;

    // Returns true if the counter was persisted.
    virtual bool Log(uint64_t metric_id, const RemoteCounter::EventBuffer& counter) = 0;

protected:
    Logger() = default;
};

} // namespace internal
} // namespace cobalt_client
