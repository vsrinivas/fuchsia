#include "src/developer/feedback/utils/utc_time_provider.h"

#include <lib/zx/time.h>

#include <memory>
#include <vector>

#include "src/developer/feedback/testing/unit_test_fixture.h"
#include "src/developer/feedback/utils/tests/stub_utc.h"
#include "src/lib/timekeeper/test_clock.h"
#include "third_party/googletest/googlemock/include/gmock/gmock.h"
#include "third_party/googletest/googletest/include/gtest/gtest.h"

namespace feedback {
namespace {

constexpr zx::time_utc kTime((zx::hour(7) + zx::min(14) + zx::sec(52)).get());

class UTCTimeProviderTest : public UnitTestFixture {
 public:
  UTCTimeProviderTest() : utc_provider_(std::make_unique<UTCTimeProvider>(services(), clock_)) {
    clock_.Set(kTime);
  }

 protected:
  void SetUpStub(const std::vector<Response>& responses) {
    stub_utc_ = std::make_unique<StubUtc>(dispatcher(), responses);
    InjectServiceProvider(stub_utc_.get());
  }

 private:
  timekeeper::TestClock clock_;
  std::unique_ptr<StubUtc> stub_utc_;

 protected:
  std::unique_ptr<UTCTimeProvider> utc_provider_;
};

TEST_F(UTCTimeProviderTest, Check_ReturnsExternal) {
  SetUpStub({
      Response(Response::Value::kExternal),
  });
  RunLoopUntilIdle();

  ASSERT_TRUE(utc_provider_->CurrentTime().has_value());
  EXPECT_EQ(utc_provider_->CurrentTime().value(), kTime);
}

TEST_F(UTCTimeProviderTest, Check_ReturnsBackstop) {
  // Upon receiving "backstop", |utc_provider_| will make another call to the stub so we need an
  // extra response. We use "no_response" so that |utc_provider_| just waits and doesn't make any
  // more calls.
  SetUpStub({
      Response(Response::Value::kBackstop),
      Response(Response::Value::kNoResponse),
  });
  RunLoopUntilIdle();

  EXPECT_FALSE(utc_provider_->CurrentTime().has_value());
}

TEST_F(UTCTimeProviderTest, Check_ServerNeverResponds) {
  SetUpStub({
      Response(Response::Value::kNoResponse),
  });
  RunLoopUntilIdle();

  for (size_t i = 0; i < 100; ++i) {
    RunLoopFor(zx::hour(23));
    EXPECT_FALSE(utc_provider_->CurrentTime().has_value());
  }
}

TEST_F(UTCTimeProviderTest, Check_MultipleCalls) {
  constexpr zx::duration kDelay = zx::msec(5);
  SetUpStub({
      Response(Response::Value::kBackstop, kDelay),
      Response(Response::Value::kExternal, kDelay),
  });

  EXPECT_FALSE(utc_provider_->CurrentTime().has_value());

  RunLoopFor(kDelay);
  EXPECT_FALSE(utc_provider_->CurrentTime().has_value());

  RunLoopFor(kDelay);
  ASSERT_TRUE(utc_provider_->CurrentTime().has_value());
  EXPECT_EQ(utc_provider_->CurrentTime().value(), kTime);
}

}  // namespace
}  // namespace feedback
