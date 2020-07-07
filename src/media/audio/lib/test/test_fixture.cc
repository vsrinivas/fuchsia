// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/lib/test/test_fixture.h"

#include <zircon/status.h>

#include "src/lib/fxl/strings/string_printf.h"

namespace media::audio::test {

constexpr zx::duration kLoopTimeout = zx::sec(10);

void TestFixture::TearDown() {
  ExpectNoUnexpectedErrors("during TearDown");
  ::gtest::RealLoopFixture::TearDown();
}

void TestFixture::ExpectCallback() {
  int64_t last_seqno = 0;
  while (!pending_callbacks_.empty()) {
    auto pcb = pending_callbacks_.front();
    pending_callbacks_.pop_front();

    RunLoopWithTimeoutOrUntil([this, pcb]() { return new_error_ || pcb->seqno > 0; }, kLoopTimeout);

    if (new_error_) {
      new_error_ = false;
      ADD_FAILURE() << "Unexpected error while waiting for " << pcb->name;
      ExpectNoUnexpectedErrors(fxl::StringPrintf("while waiting for %s", pcb->name.c_str()));
      pending_callbacks_.clear();
      return;
    }
    if (pcb->seqno == 0) {
      ADD_FAILURE() << "Did not get a " << pcb->name << " callback within "
                    << kLoopTimeout.to_msecs() << "ms";
      pending_callbacks_.clear();
      return;
    }

    if (pcb->ordered) {
      EXPECT_GT(pcb->seqno, last_seqno) << pcb->name << " called out-of-order";
      last_seqno = pcb->seqno;
    }
  }
}

void TestFixture::ExpectErrors(const std::vector<std::shared_ptr<ErrorHandler>>& errors) {
  std::string names = "{";
  std::string sep;
  for (auto& eh : errors) {
    names += eh->name;
    sep = ", ";
  }
  names += "}";

  RunLoopWithTimeoutOrUntil(
      [errors]() {
        for (auto& eh : errors) {
          if (eh->error_code != eh->expected_error_code) {
            return false;
          }
        }
        return true;
      },
      kLoopTimeout);

  new_error_ = false;
  ExpectNoUnexpectedErrors(fxl::StringPrintf("when waiting error in %s", names.c_str()));
}

void TestFixture::ExpectNoUnexpectedErrors(const std::string& msg_for_failure) {
  for (auto& [_, eh] : error_handlers_) {
    EXPECT_EQ(eh->error_code, eh->expected_error_code)
        << msg_for_failure << ": " << eh->name << " had an unexpected error\nExpected error is "
        << zx_status_get_string(eh->expected_error_code) << "\nActual error is "
        << zx_status_get_string(eh->error_code);
  }
}

std::pair<std::shared_ptr<TestFixture::ErrorHandler>, fit::function<void(zx_status_t)>>
TestFixture::NewErrorHandler(const std::string& name) {
  auto eh = std::make_shared<ErrorHandler>();
  eh->name = name;
  return std::make_pair(eh, [this, eh](zx_status_t status) {
    eh->error_code = status;
    new_error_ = true;
  });
}

std::shared_ptr<TestFixture::PendingCallback> TestFixture::NewPendingCallback(
    const std::string& name, bool ordered) {
  auto pcb = std::make_shared<PendingCallback>();
  pcb->name = name;
  pcb->ordered = ordered;
  pending_callbacks_.push_back(pcb);
  return pcb;
}

bool TestFixture::ErrorOccurred() {
  for (auto& [_, eh] : error_handlers_) {
    if (eh->error_code != ZX_OK) {
      return true;
    }
  }
  return false;
}

}  // namespace media::audio::test
