// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/lib/test/test_fixture.h"

#include <zircon/status.h>

#include <ostream>

#include "src/lib/fxl/strings/string_printf.h"

namespace media::audio::test {

#ifdef NDEBUG
constexpr zx::duration kLoopTimeout = zx::sec(10);
#else
constexpr zx::duration kLoopTimeout = zx::sec(30);
#endif

void TestFixture::TearDown() {
  ExpectNoUnexpectedErrors("during TearDown");
  ::gtest::RealLoopFixture::TearDown();
}

void TestFixture::ExpectCallbacks() {
  std::vector<PendingCallback> retired_callbacks;

  while (!pending_callbacks_.empty()) {
    auto callback = pending_callbacks_.front();
    pending_callbacks_.pop_front();

    RunLoopWithTimeoutOrUntil(
        [this, callback]() { return new_error_ || callback->sequence_num > 0; }, kLoopTimeout);

    if (new_error_) {
      new_error_ = false;
      auto error_str = fxl::StringPrintf("while waiting for '%s'", callback->name.c_str());
      ADD_FAILURE() << "Unexpected error " << error_str;
      ExpectNoUnexpectedErrors(error_str);
      pending_callbacks_.clear();
      return;
    }
    if (callback->sequence_num == 0) {
      ADD_FAILURE() << "Did not get a '" << callback->name << "' callback within "
                    << kLoopTimeout.to_msecs() << " ms";
      pending_callbacks_.clear();
      return;
    }

    if (callback->ordered) {
      if (!retired_callbacks.empty()) {
        auto prev_callback = retired_callbacks.back();

        if (callback->sequence_num <= prev_callback.sequence_num) {
          std::ostringstream out_stream;
          auto format_cb_entry = [&out_stream](const PendingCallback callback_entry) {
            out_stream << std::right << std::setw(20)
                       << (std::string("'") + callback_entry.name + "'  [")
                       << callback_entry.sequence_num << "]" << std::endl;
          };

          out_stream << "   Expected order  [Actual order]" << std::endl;
          std::for_each(retired_callbacks.begin(), retired_callbacks.end(), format_cb_entry);
          format_cb_entry(*callback);

          ADD_FAILURE() << "Out-of-order callbacks: '" << callback->name
                        << "' completed too early -- should have been after '" << prev_callback.name
                        << "'" << std::endl
                        << out_stream.str();
        }
      }
      retired_callbacks.push_back(*callback);
    }
  }
}

void TestFixture::ExpectNoCallbacks(zx::duration timeout, const std::string& msg_for_failure) {
  RunLoopWithTimeout(timeout);
  ExpectNoUnexpectedErrors(msg_for_failure);
  pending_callbacks_.clear();
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
  auto callback = std::make_shared<PendingCallback>();
  callback->name = name;
  callback->ordered = ordered;
  pending_callbacks_.push_back(callback);
  return callback;
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
