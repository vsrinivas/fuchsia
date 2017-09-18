// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/lib/measure/test_events.h"

namespace tracing {
namespace measure {
namespace test {

reader::Record::Event DurationBegin(std::string name,
                                    std::string category,
                                    uint64_t timestamp) {
  return reader::Record::Event{
      timestamp, {}, category,
      name,      {}, reader::EventData(reader::EventData::DurationBegin{})};
}

reader::Record::Event DurationEnd(std::string name,
                                  std::string category,
                                  uint64_t timestamp) {
  return reader::Record::Event{
      timestamp, {}, category,
      name,      {}, reader::EventData(reader::EventData::DurationEnd{})};
}

reader::Record::Event AsyncBegin(uint64_t id,
                                 std::string name,
                                 std::string category,
                                 uint64_t timestamp) {
  return reader::Record::Event{
      timestamp, {}, category,
      name,      {}, reader::EventData(reader::EventData::AsyncBegin{id})};
}

reader::Record::Event AsyncEnd(uint64_t id,
                               std::string name,
                               std::string category,
                               uint64_t timestamp) {
  return reader::Record::Event{
      timestamp, {}, category,
      name,      {}, reader::EventData(reader::EventData::AsyncEnd{id})};
}

reader::Record::Event Instant(std::string name,
                              std::string category,
                              uint64_t timestamp) {
  return reader::Record::Event{
      timestamp, {}, category,
      name,      {}, reader::EventData(reader::EventData::Instant{})};
}
}  // namespace test

}  // namespace measure
}  // namespace tracing
