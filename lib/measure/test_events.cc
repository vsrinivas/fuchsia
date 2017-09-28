// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/lib/measure/test_events.h"

namespace tracing {
namespace measure {
namespace test {

trace::Record::Event DurationBegin(fbl::String name,
                                    fbl::String category,
                                    uint64_t timestamp) {
  return trace::Record::Event{
      timestamp, {}, category,
      name,      {}, trace::EventData(trace::EventData::DurationBegin{})};
}

trace::Record::Event DurationEnd(fbl::String name,
                                  fbl::String category,
                                  uint64_t timestamp) {
  return trace::Record::Event{
      timestamp, {}, category,
      name,      {}, trace::EventData(trace::EventData::DurationEnd{})};
}

trace::Record::Event AsyncBegin(uint64_t id,
                                 fbl::String name,
                                 fbl::String category,
                                 uint64_t timestamp) {
  return trace::Record::Event{
      timestamp, {}, category,
      name,      {}, trace::EventData(trace::EventData::AsyncBegin{id})};
}

trace::Record::Event AsyncEnd(uint64_t id,
                               fbl::String name,
                               fbl::String category,
                               uint64_t timestamp) {
  return trace::Record::Event{
      timestamp, {}, category,
      name,      {}, trace::EventData(trace::EventData::AsyncEnd{id})};
}

trace::Record::Event Instant(fbl::String name,
                              fbl::String category,
                              uint64_t timestamp) {
  return trace::Record::Event{
      timestamp, {}, category,
      name,      {}, trace::EventData(trace::EventData::Instant{})};
}
}  // namespace test

}  // namespace measure
}  // namespace tracing
