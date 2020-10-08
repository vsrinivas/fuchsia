// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_DEBUG_DEBUG_AGENT_MOCK_LIMBO_PROVIDER_H_
#define SRC_DEVELOPER_DEBUG_DEBUG_AGENT_MOCK_LIMBO_PROVIDER_H_

#include <map>
#include <vector>

#include "src/developer/debug/debug_agent/limbo_provider.h"
#include "src/developer/debug/debug_agent/mock_exception_handle.h"
#include "src/developer/debug/debug_agent/mock_process_handle.h"
#include "src/developer/debug/debug_agent/mock_thread_handle.h"

namespace debug_agent {

class MockLimboProvider final : public LimboProvider {
 public:
  struct MockRecord {
    MockRecord(MockProcessHandle p, MockThreadHandle t, MockExceptionHandle e)
        : process(std::move(p)), thread(std::move(t)), exception(std::move(e)) {}

    MockProcessHandle process;
    MockThreadHandle thread;
    MockExceptionHandle exception;
  };

  explicit MockLimboProvider() : LimboProvider() {}
  virtual ~MockLimboProvider() = default;

  const std::vector<zx_koid_t>& release_calls() const { return release_calls_; }

  // LimboProvider implementation.
  bool Valid() const override { return true; }
  bool IsProcessInLimbo(zx_koid_t process_koid) const override;
  const RecordMap& GetLimboRecords() const override;
  fitx::result<zx_status_t, RetrievedException> RetrieveException(zx_koid_t process_koid) override;
  zx_status_t ReleaseProcess(zx_koid_t process_koid) override;

  void AppendException(MockProcessHandle process, MockThreadHandle thread,
                       MockExceptionHandle exception);
  void CallOnEnterLimbo();

 private:
  static Record FromMockRecord(const MockRecord& mock);

  // Current contents of limbo.
  std::map<zx_koid_t, MockRecord> mock_records_;

  // Recomputed from mock_records_ for every call to GetLimboRecords() because it must return a
  // reference.
  mutable RecordMap limbo_;

  std::vector<zx_koid_t> release_calls_;
};

}  // namespace debug_agent

#endif  // SRC_DEVELOPER_DEBUG_DEBUG_AGENT_MOCK_LIMBO_PROVIDER_H_
