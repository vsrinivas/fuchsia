// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/feedback/feedback_agent/annotations/channel_provider.h"

#include <lib/async/cpp/executor.h>
#include <lib/fit/single_threaded_executor.h>
#include <lib/gtest/test_loop_fixture.h>
#include <lib/sys/cpp/testing/service_directory_provider.h>
#include <lib/zx/time.h>
#include <zircon/errors.h>

#include <memory>
#include <string>

#include "src/developer/feedback/feedback_agent/tests/stub_channel_provider.h"
#include "src/lib/fxl/logging.h"
#include "src/lib/fxl/test/test_settings.h"
#include "src/lib/syslog/cpp/logger.h"
#include "third_party/googletest/googlemock/include/gmock/gmock.h"
#include "third_party/googletest/googletest/include/gtest/gtest.h"

namespace feedback {
namespace {

using fuchsia::feedback::Annotation;

class ChannelProviderTest : public gtest::TestLoopFixture {
 public:
  ChannelProviderTest() : executor_(dispatcher()), service_directory_provider_(dispatcher()) {}

 protected:
  void SetUpChannelProviderPtr(std::unique_ptr<StubChannelProvider> stub_channel_provider) {
    stub_channel_provider_ = std::move(stub_channel_provider);
    if (stub_channel_provider_) {
      FXL_CHECK(service_directory_provider_.AddService(stub_channel_provider_->GetHandler()) ==
                ZX_OK);
    }
  }

  fit::result<Annotation> RetrieveCurrentChannel(const zx::duration timeout = zx::sec(1)) {
    fit::result<Annotation> annotation;
    ChannelProvider provider(dispatcher(), service_directory_provider_.service_directory(),
                             timeout);
    auto promises = provider.GetAnnotations();
    if (promises.size() >= 1) {
      executor_.schedule_task(
          std::move(promises.back()).then([&annotation](fit::result<Annotation>& res) {
            annotation = std::move(res);
          }));
    }
    RunLoopFor(timeout);
    return annotation;
  }

  async::Executor executor_;
  sys::testing::ServiceDirectoryProvider service_directory_provider_;

 private:
  std::unique_ptr<StubChannelProvider> stub_channel_provider_;
};

TEST_F(ChannelProviderTest, Succeed_SomeChannel) {
  std::unique_ptr<StubChannelProvider> stub_channel_provider =
      std::make_unique<StubChannelProvider>();
  stub_channel_provider->set_channel("my-channel");
  SetUpChannelProviderPtr(std::move(stub_channel_provider));

  fit::result<Annotation> result = RetrieveCurrentChannel();

  ASSERT_TRUE(result.is_ok());
  EXPECT_EQ(result.take_value().value, "my-channel");
}

TEST_F(ChannelProviderTest, Succeed_EmptyChannel) {
  SetUpChannelProviderPtr(std::make_unique<StubChannelProvider>());

  fit::result<Annotation> result = RetrieveCurrentChannel();

  ASSERT_TRUE(result.is_ok());
  EXPECT_EQ(result.take_value().value, "");
}

TEST_F(ChannelProviderTest, Fail_ChannelProviderPtrNotAvailable) {
  SetUpChannelProviderPtr(nullptr);

  fit::result<Annotation> result = RetrieveCurrentChannel();

  ASSERT_TRUE(result.is_error());
}

TEST_F(ChannelProviderTest, Fail_ChannelProviderPtrClosesConnection) {
  SetUpChannelProviderPtr(std::make_unique<StubChannelProviderClosesConnection>());

  fit::result<Annotation> result = RetrieveCurrentChannel();

  ASSERT_TRUE(result.is_error());
}

TEST_F(ChannelProviderTest, Fail_ChannelProviderPtrNeverReturns) {
  SetUpChannelProviderPtr(std::make_unique<StubChannelProviderNeverReturns>());

  fit::result<Annotation> result = RetrieveCurrentChannel();

  ASSERT_TRUE(result.is_error());
}

TEST_F(ChannelProviderTest, Fail_CallGetCurrentTwice) {
  SetUpChannelProviderPtr(std::make_unique<StubChannelProvider>());

  const zx::duration unused_timeout = zx::sec(1);
  internal::ChannelProviderPtr channel_provider(dispatcher(),
                                                service_directory_provider_.service_directory());
  executor_.schedule_task(channel_provider.GetCurrent(unused_timeout));
  ASSERT_DEATH(channel_provider.GetCurrent(unused_timeout),
               testing::HasSubstr("GetCurrent() is not intended to be called twice"));
}

}  // namespace
}  // namespace feedback

int main(int argc, char** argv) {
  if (!fxl::SetTestSettings(argc, argv)) {
    return EXIT_FAILURE;
  }

  testing::InitGoogleTest(&argc, argv);
  syslog::InitLogger({"feedback", "test"});
  return RUN_ALL_TESTS();
}
