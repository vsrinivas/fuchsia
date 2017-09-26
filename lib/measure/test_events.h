// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_LIB_MEASURE_TEST_EVENTS_H_
#define GARNET_LIB_MEASURE_TEST_EVENTS_H_

#include "garnet/lib/trace/reader.h"

namespace tracing {
namespace measure {
namespace test {

reader::Record::Event DurationBegin(std::string name,
                                    std::string category,
                                    uint64_t timestamp);

reader::Record::Event DurationEnd(std::string name,
                                  std::string category,
                                  uint64_t timestamp);

reader::Record::Event AsyncBegin(uint64_t id,
                                 std::string name,
                                 std::string category,
                                 uint64_t timestamp);

reader::Record::Event AsyncEnd(uint64_t id,
                               std::string name,
                               std::string category,
                               uint64_t timestamp);

reader::Record::Event Instant(std::string name,
                              std::string category,
                              uint64_t timestamp);
}  // namespace test

}  // namespace measure
}  // namespace tracing

#endif  // GARNET_LIB_MEASURE_TEST_EVENTS_H_
