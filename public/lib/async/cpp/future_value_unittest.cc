#include "lib/async/cpp/future_value.h"

#include "garnet/public/lib/gtest/test_with_message_loop.h"
#include "gtest/gtest.h"
#include "lib/fxl/logging.h"

#include <string>

namespace fuchsia {
namespace modular {

namespace {

class FutureValueTest : public gtest::TestWithMessageLoop {
 protected:
  void SetExpected(int expected) { expected_ = expected; }

  void Done() {
    count_++;
    if (count_ == expected_) {
      message_loop_.PostQuitTask();
    }
  }

 private:
  int count_ = 0;
  int expected_ = 1;
};

TEST_F(FutureValueTest, SetValueAndGet) {
  FutureValue<int> fi;
  fi.SetValue(10);
  ASSERT_EQ(fi.get(), 10);
}

TEST_F(FutureValueTest, Assign) {
  FutureValue<int> fi;
  fi = 10;
  EXPECT_EQ(fi.get(), 10);
}

TEST_F(FutureValueTest, OnValue) {
  FutureValue<int> fi;

  fi.OnValue([&](const int& value) {
    EXPECT_EQ(value, 10);
    Done();
  });

  fi = 10;

  EXPECT_FALSE(RunLoopWithTimeout());
}

}  // namespace
}  // namespace modular
}  // namespace fuchsia
