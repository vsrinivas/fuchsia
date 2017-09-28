// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_LIB_MEASURE_TEST_EVENTS_H_
#define GARNET_LIB_MEASURE_TEST_EVENTS_H_

#include <trace-reader/reader.h>

namespace tracing {
namespace measure {
namespace test {

trace::Record::Event DurationBegin(fbl::String name,
                                    fbl::String category,
                                    uint64_t timestamp);

trace::Record::Event DurationEnd(fbl::String name,
                                  fbl::String category,
                                  uint64_t timestamp);

trace::Record::Event AsyncBegin(uint64_t id,
                                 fbl::String name,
                                 fbl::String category,
                                 uint64_t timestamp);

trace::Record::Event AsyncEnd(uint64_t id,
                               fbl::String name,
                               fbl::String category,
                               uint64_t timestamp);

trace::Record::Event Instant(fbl::String name,
                              fbl::String category,
                              uint64_t timestamp);
}  // namespace test

}  // namespace measure
}  // namespace tracing

#endif  // GARNET_LIB_MEASURE_TEST_EVENTS_H_
