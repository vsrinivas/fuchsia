// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/debug_agent/mock_limbo_provider.h"

namespace debug_agent {

bool MockLimboProvider::IsProcessInLimbo(zx_koid_t process_koid) const {
  const auto& records = GetLimboRecords();
  return records.find(process_koid) != records.end();
}

const LimboProvider::RecordMap& MockLimboProvider::GetLimboRecords() const {
  // Recreate limbo_ contents from the current mock records.
  limbo_.clear();
  for (const auto& [process_koid, mock_record] : mock_records_)
    limbo_[mock_record.process.GetKoid()] = FromMockRecord(mock_record);
  return limbo_;
}

fit::result<debug::Status, LimboProvider::RetrievedException> MockLimboProvider::RetrieveException(
    zx_koid_t process_koid) {
  auto it = mock_records_.find(process_koid);
  if (it == mock_records_.end())
    return fit::error(debug::Status("Not found"));

  RetrievedException result;
  result.process = std::make_unique<MockProcessHandle>(it->second.process);
  result.thread = std::make_unique<MockThreadHandle>(it->second.thread);
  result.exception = std::make_unique<MockExceptionHandle>(it->second.exception);

  mock_records_.erase(it);
  limbo_.erase(process_koid);

  return fit::ok(std::move(result));
}

debug::Status MockLimboProvider::ReleaseProcess(zx_koid_t process_koid) {
  release_calls_.push_back(process_koid);

  auto it = mock_records_.find(process_koid);
  if (it == mock_records_.end())
    return debug::Status("Process not found to release from limbo");

  mock_records_.erase(it);
  limbo_.erase(process_koid);
  return debug::Status();
}

void MockLimboProvider::AppendException(MockProcessHandle process, MockThreadHandle thread,
                                        MockExceptionHandle exception) {
  zx_koid_t process_koid = process.GetKoid();
  mock_records_.insert(
      {process_koid, MockRecord(std::move(process), std::move(thread), std::move(exception))});
}

void MockLimboProvider::CallOnEnterLimbo() {
  FX_DCHECK(on_enter_limbo_);

  // The callback may mutate the list from under us, so make a copy of what to call.
  auto record_copy = mock_records_;
  for (const auto& [process_koid, mock_record] : record_copy)
    on_enter_limbo_(FromMockRecord(mock_record));
}

LimboProvider::Record MockLimboProvider::FromMockRecord(const MockRecord& mock) {
  Record record;
  record.process = std::make_unique<MockProcessHandle>(mock.process);
  record.thread = std::make_unique<MockThreadHandle>(mock.thread);
  return record;
}

}  // namespace debug_agent
