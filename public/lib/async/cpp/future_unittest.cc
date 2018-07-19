#include <lib/async/cpp/future.h>

#include <string>

#include <lib/async-testutils/test_loop.h>
#include <lib/fxl/logging.h>

#include "gtest/gtest-spi.h"
#include "gtest/gtest.h"

#include <memory>
#include <string>

namespace modular {

namespace {

// https://www.reddit.com/r/Stargate/comments/114165/stargate_sg_1_episode_200_what_is_your_favorite/c6j8ryy
// https://en.wikiquote.org/wiki/Stargate_SG-1/Season_10#200_[10.6]
// (The actual duration is immaterial because we control time in unit tests.)
constexpr zx::duration kPause = zx::min(38);

// This is a simple class to enable asynchronous callbacks to be tested. In each
// test:
//
// 1. Instantiate an AsyncExceptions object,
// 2. Call Signal() in each async callback to indicate that an async callback
//    was successfully called. Note that doesn't mean that the callback itself
//    was passed the correct parameters or was successful: to ensure that, your
//    callback should call still EXPECT_* for its expected results.
// 3. Call Verify(n) at the end of your test method to ensure that the number of
//    expected async callbacks is correct. |n| should be the number of Signal()
//    calls that were expected. Verify() will fail if Signal() hasn't been
//    called exactly |n| times.
//
// Note that AsyncExpectations does _not_ integrate with the message loop, since
// its one use case has no need to do that.
//
// THIS CLASS IS NOT THREAD-SAFE.
//
// Example:
//
// TEST(FooTest, FooTestCase) {
//   AsyncExpectations async_expectations;
//   async_function_1([&] {
//     async_expectations.Signal();
//   });
//   async_function_2([&] {
//     async_expectations.Signal();
//   });
//   async_expectations.Verify(2);
// }
class AsyncExpectations {
 public:
  void Signal() { ++count_; }
  void Reset() { count_ = 0; }

  int count() const { return count_; }

 private:
  int count_ = 0;
};

TEST(AsyncExpectationTest, TestLessThanExpectedCountFails) {
  AsyncExpectations async_expectations;

  async_expectations.Signal();
  EXPECT_NONFATAL_FAILURE({ EXPECT_EQ(2, async_expectations.count()); },
                          "Expected equality");
}

TEST(AsyncExpectationTest, TestExpectedCountSucceeds) {
  AsyncExpectations async_expectations;

  async_expectations.Signal();
  EXPECT_EQ(1, async_expectations.count());
}

TEST(AsyncExpectationTest, TestMoreThanExpectedCountFails) {
  AsyncExpectations async_expectations;

  async_expectations.Signal();
  async_expectations.Signal();
  EXPECT_NONFATAL_FAILURE({ EXPECT_EQ(1, async_expectations.count()); },
                          "Expected equality");
}

}  // namespace

// FutureTest must be in the ::modular namespace (not in an anonymous namespace)
// so that the "friend class FutureTest;" declaration in the Future class can
// properly befriend this test.
class FutureTest : public ::testing::Test {
 protected:
  template <typename... Result>
  const std::tuple<Result...>& get(const FuturePtr<Result...>& future) const {
    return future->get();
  }

  template <typename... Result>
  fxl::WeakPtrFactory<Future<Result...>>& weak_factory(
      const FuturePtr<Result...>& future) const {
    return future->weak_factory_;
  }
};

namespace {

TEST_F(FutureTest, Create) {
  auto f = Future<>::Create(__PRETTY_FUNCTION__);

  EXPECT_NE(f.get(), nullptr);
}

TEST_F(FutureTest, CompleteAndGet) {
  auto f = Future<int>::Create(__PRETTY_FUNCTION__);

  f->Complete(10);
  ASSERT_EQ(get(f), std::tuple<int>(10));
}

TEST_F(FutureTest, ThenWith0Argument) {
  auto f = Future<>::Create(__PRETTY_FUNCTION__);

  AsyncExpectations async_expectations;

  f->Then([&]() { async_expectations.Signal(); });

  f->Complete();
  ASSERT_EQ(get(f), std::tuple<>());

  EXPECT_EQ(1, async_expectations.count());
}

TEST_F(FutureTest, ThenWith1Argument) {
  auto f = Future<int>::Create(__PRETTY_FUNCTION__);

  AsyncExpectations async_expectations;

  f->Then([&](const int& value) {
    EXPECT_EQ(value, 10);
    async_expectations.Signal();
  });

  f->Complete(10);

  EXPECT_EQ(1, async_expectations.count());
}

TEST_F(FutureTest, ThenWith2Argument) {
  auto f = Future<int, float>::Create(__PRETTY_FUNCTION__);

  AsyncExpectations async_expectations;

  f->Then([&](const int& i, const float& f) {
    EXPECT_EQ(i, 10);
    EXPECT_FLOAT_EQ(f, 3.14f);
    async_expectations.Signal();
  });

  f->Complete(10, 3.14f);

  EXPECT_EQ(1, async_expectations.count());
}

TEST_F(FutureTest, CompleteBeforeThen) {
  auto f = Future<int>::Create(__PRETTY_FUNCTION__);

  f->Complete(10);

  AsyncExpectations async_expectations;

  f->Then([&](const int& value) {
    EXPECT_EQ(value, 10);
    async_expectations.Signal();
  });

  EXPECT_EQ(1, async_expectations.count());
}

TEST_F(FutureTest, ThenCorrectlyMoves) {
  std::shared_ptr<int> p(new int(10));
  auto f = Future<std::shared_ptr<int>>::Create(__PRETTY_FUNCTION__);

  AsyncExpectations async_expectations;

  f->Then([&](std::shared_ptr<int> p2) {
    EXPECT_EQ(p.get(), nullptr);
    EXPECT_NE(p2.get(), nullptr);

    EXPECT_EQ(*p2, 10);
    async_expectations.Signal();
  });

  f->Complete(std::move(p));

  EXPECT_EQ(1, async_expectations.count());
}

TEST_F(FutureTest, ConstThen) {
  std::shared_ptr<int> p(new int(10));
  auto f = Future<std::shared_ptr<int>>::Create(__PRETTY_FUNCTION__);

  AsyncExpectations async_expectations;

  f->ConstThen([&](const std::shared_ptr<int>& p2) {
    EXPECT_NE(p2.get(), nullptr);
    EXPECT_EQ(p.get(), nullptr);

    EXPECT_EQ(*p2, 10);
    async_expectations.Signal();
  });

  f->ConstThen([&](const std::shared_ptr<int>& p3) {
    EXPECT_NE(p3.get(), nullptr);
    EXPECT_EQ(p.get(), nullptr);

    EXPECT_EQ(*p3, 10);
    async_expectations.Signal();
  });

  f->Complete(std::move(p));

  EXPECT_EQ(2, async_expectations.count());
}

TEST_F(FutureTest, ConstThenAfterComplete) {
  auto f = Future<int>::Create(__PRETTY_FUNCTION__);

  AsyncExpectations async_expectations;

  bool first_const_then{};
  f->ConstThen([&](const int& i) {
    EXPECT_FALSE(first_const_then);
    first_const_then = true;
    async_expectations.Signal();
  });

  f->Complete(10);

  bool second_const_then{};
  f->ConstThen([&](const int& i) {
    EXPECT_FALSE(second_const_then);
    second_const_then = true;
    async_expectations.Signal();
  });

  EXPECT_EQ(2, async_expectations.count());
}

TEST_F(FutureTest, ConstThenAfterThen) {
  std::shared_ptr<int> p(new int(10));
  auto f = Future<std::shared_ptr<int>>::Create(__PRETTY_FUNCTION__);

  AsyncExpectations async_expectations;

  f->ConstThen([&](const std::shared_ptr<int>& p2) {
    EXPECT_NE(p2.get(), nullptr);
    EXPECT_EQ(p.get(), nullptr);

    EXPECT_EQ(*p2, 10);
    async_expectations.Signal();
  });

  f->Then([&](std::shared_ptr<int> p4) {
    EXPECT_NE(p4.get(), nullptr);
    EXPECT_EQ(p.get(), nullptr);

    EXPECT_EQ(*p4, 10);
    async_expectations.Signal();
  });

  f->ConstThen([&](const std::shared_ptr<int>& p3) {
    EXPECT_NE(p3.get(), nullptr);
    EXPECT_EQ(p.get(), nullptr);

    EXPECT_EQ(*p3, 10);
    async_expectations.Signal();
  });

  f->Complete(std::move(p));

  EXPECT_EQ(3, async_expectations.count());
}

TEST_F(FutureTest, WeakThen) {
  // WeakThen() will break an execution chain if its weakptr is invalidated.
  int data = 0;
  fxl::WeakPtrFactory<int> factory(&data);
  AsyncExpectations async_expectations;

  // This time we expect all three to run because we haven't invalidated any
  // WeakPtrs.
  auto f =
      Future<>::Create(std::string(__PRETTY_FUNCTION__) + std::string("1"));
  f->Then([&] { async_expectations.Signal(); })
      ->WeakThen(factory.GetWeakPtr(), [&] { async_expectations.Signal(); })
      ->Then([&] { async_expectations.Signal(); });

  f->Complete();
  EXPECT_EQ(3, async_expectations.count());

  // But this time we'll invalidate WeakPtrs in the first Then(). Only the first
  // Then() will run.
  async_expectations.Reset();
  f = Future<>::Create(std::string(__PRETTY_FUNCTION__) + std::string("2"));
  f->Then([&] {
     async_expectations.Signal();
     factory.InvalidateWeakPtrs();
   })
      ->WeakThen(factory.GetWeakPtr(), [&] { async_expectations.Signal(); })
      ->Then([&] { async_expectations.Signal(); });

  f->Complete();
  EXPECT_EQ(1, async_expectations.count());
}

TEST_F(FutureTest, WeakConstThen) {
  // WeakConstThen() will not run if its weakptr is invalidated.
  int data = 0;
  fxl::WeakPtrFactory<int> factory(&data);
  AsyncExpectations async_expectations;

  auto f =
      Future<>::Create(std::string(__PRETTY_FUNCTION__) + std::string("1"));
  f->WeakConstThen(factory.GetWeakPtr(), [&] { async_expectations.Signal(); });
  f->Complete();
  EXPECT_EQ(1, async_expectations.count());

  async_expectations.Reset();
  f = Future<>::Create(std::string(__PRETTY_FUNCTION__) + std::string("2"));
  f->WeakConstThen(factory.GetWeakPtr(), [&] { async_expectations.Signal(); });
  factory.InvalidateWeakPtrs();
  f->Complete();
  EXPECT_EQ(0, async_expectations.count());
}

TEST_F(FutureTest, WeakMap) {
  // Similar to WeakThen(), but using Map calls instead.
  int data = 0;
  fxl::WeakPtrFactory<int> factory(&data);

  auto f =
      Future<int>::Create(std::string(__PRETTY_FUNCTION__) + std::string("1"));

  // First let everything run.
  AsyncExpectations async_expectations;
  f->Map([&](int i) {
     async_expectations.Signal();
     return 42;
   })
      ->WeakMap(factory.GetWeakPtr(),
                [&](int i) {
                  async_expectations.Signal();
                  return 10;
                })
      ->Then([&](int i) { async_expectations.Signal(); });

  f->Complete(10);
  EXPECT_EQ(3, async_expectations.count());

  // Now invalidate it.
  f = Future<int>::Create(std::string(__PRETTY_FUNCTION__) + std::string("2"));
  async_expectations.Reset();
  f->Map([&](int i) {
     async_expectations.Signal();
     factory.InvalidateWeakPtrs();
     return 42;
   })
      ->WeakMap(factory.GetWeakPtr(),
                [&](int i) {
                  async_expectations.Signal();
                  return 10;
                })
      ->Then([&](int i) { async_expectations.Signal(); });

  f->Complete(10);
  EXPECT_EQ(1, async_expectations.count());
}

TEST_F(FutureTest, WeakAsyncMap) {
  // Similar to WeakThen(), but using Map calls instead.
  int data = 0;
  fxl::WeakPtrFactory<int> factory(&data);

  auto f =
      Future<int>::Create(std::string(__PRETTY_FUNCTION__) + std::string("1"));

  // First let everything run.
  AsyncExpectations async_expectations;
  f->Map([&](int i) {
     async_expectations.Signal();
     return 42;
   })
      ->WeakAsyncMap(factory.GetWeakPtr(),
                     [&](int i) {
                       async_expectations.Signal();
                       return Future<int>::CreateCompleted(
                           std::string(__PRETTY_FUNCTION__) + std::string("2"),
                           10);
                     })
      ->Then([&](int i) { async_expectations.Signal(); });

  f->Complete(10);
  EXPECT_EQ(3, async_expectations.count());

  // Now invalidate it.
  f = Future<int>::Create(std::string(__PRETTY_FUNCTION__) + std::string("3"));
  async_expectations.Reset();
  f->Map([&](int i) {
     async_expectations.Signal();
     factory.InvalidateWeakPtrs();
     return 42;
   })
      ->WeakAsyncMap(factory.GetWeakPtr(),
                     [&](int i) {
                       async_expectations.Signal();
                       return Future<int>::CreateCompleted(
                           std::string(__PRETTY_FUNCTION__) + std::string("4"),
                           10);
                     })
      ->Then([&](int i) { async_expectations.Signal(); });

  f->Complete(10);
  EXPECT_EQ(1, async_expectations.count());
}

TEST_F(FutureTest, CreateCompleted) {
  auto f = Future<>::CreateCompleted(__PRETTY_FUNCTION__);

  AsyncExpectations async_expectations;

  f->Then([&]() { async_expectations.Signal(); });

  ASSERT_EQ(get(f), std::tuple<>());

  EXPECT_EQ(1, async_expectations.count());
}

TEST_F(FutureTest, Wait) {
  auto f1 = Future<int, std::string>::Create(std::string(__PRETTY_FUNCTION__) +
                                             std::string("1"));
  auto f2 = Future<int, std::string>::Create(std::string(__PRETTY_FUNCTION__) +
                                             std::string("2"));
  auto f3 = Future<int, std::string>::Create(std::string(__PRETTY_FUNCTION__) +
                                             std::string("3"));

  AsyncExpectations async_expectations;

  auto f =
      Wait(std::string(__PRETTY_FUNCTION__) + std::string("4"), {f1, f2, f3});
  f->Then([&](std::vector<std::tuple<int, std::string>> v) {
    EXPECT_EQ(v.size(), 3ul);

    EXPECT_EQ(v[0], std::make_tuple(10, "10"));
    EXPECT_EQ(v[1], std::make_tuple(20, "20"));
    EXPECT_EQ(v[2], std::make_tuple(30, "30"));

    async_expectations.Signal();
  });

  // Go ahead and insert out of order to also ensure that Wait produces results
  // in insertion order rather than completion order.
  f2->Complete(20, "20");
  EXPECT_EQ(0, async_expectations.count());
  f1->Complete(10, "10");
  EXPECT_EQ(0, async_expectations.count());
  f3->Complete(30, "30");

  EXPECT_EQ(1, async_expectations.count());
}

TEST_F(FutureTest, WaitOnZeroFutures) {
  auto f = Wait(__PRETTY_FUNCTION__, std::vector<FuturePtr<int>>{});

  AsyncExpectations async_expectations;

  f->Then([&](std::vector<int> v) {
    EXPECT_EQ(v.size(), 0ul);
    async_expectations.Signal();
  });

  EXPECT_EQ(1, async_expectations.count());
}

TEST_F(
    FutureTest,
    WaitReleasesReturnedFutureIfAllComponentFuturesHaveBeenCompletedOrDestroyed) {
  fxl::WeakPtr<Future<std::vector<int>>> weak_f;

  auto f1 = Future<int>::Create(__PRETTY_FUNCTION__ + std::string("1"));
  {
    auto f2 = Future<int>::Create(__PRETTY_FUNCTION__ + std::string("2"));
    weak_f =
        weak_factory(Wait(__PRETTY_FUNCTION__ + std::string("Wait"), {f1, f2}))
            .GetWeakPtr();
  }

  ASSERT_TRUE(weak_f);
  weak_f->Then([&](std::vector<int>) { FAIL(); });
  f1->Complete(42);
  EXPECT_FALSE(weak_f);
}

// This test is the happy case, more or less identical to |Wait|.
TEST_F(FutureTest, WaitWithTimeout) {
  auto f1 = Future<int, std::string>::Create(std::string(__PRETTY_FUNCTION__) +
                                             std::string("1"));
  auto f2 = Future<int, std::string>::Create(std::string(__PRETTY_FUNCTION__) +
                                             std::string("2"));
  auto f3 = Future<int, std::string>::Create(std::string(__PRETTY_FUNCTION__) +
                                             std::string("3"));

  AsyncExpectations async_expectations;
  async::TestLoop loop;

  auto f = WaitWithTimeout(
      std::string(__PRETTY_FUNCTION__) + std::string("4"), loop.dispatcher(),
      kPause, [](const std::string& msg) { FAIL() << msg; }, {f1, f2, f3});

  f->Then([&](std::vector<std::tuple<int, std::string>> v) {
    EXPECT_EQ(v.size(), 3ul);

    EXPECT_EQ(v[0], std::make_tuple(10, "10"));
    EXPECT_EQ(v[1], std::make_tuple(20, "20"));
    EXPECT_EQ(v[2], std::make_tuple(30, "30"));

    async_expectations.Signal();
  });

  loop.RunUntilIdle();  // ensure the timeout does not occur

  // Go ahead and insert out of order to also ensure that Wait produces results
  // in insertion order rather than completion order.
  f2->Complete(20, "20");
  EXPECT_EQ(0, async_expectations.count());
  f1->Complete(10, "10");
  EXPECT_EQ(0, async_expectations.count());
  f3->Complete(30, "30");

  EXPECT_EQ(1, async_expectations.count());
}

// Zero futures should instantly complete regardless of timeout.
TEST_F(FutureTest, WaitWithTimeoutOnZeroFutures) {
  AsyncExpectations async_expectations;
  async::TestLoop loop;

  auto f = WaitWithTimeout(__PRETTY_FUNCTION__, loop.dispatcher(), {},
                           [](const std::string& msg) { FAIL() << msg; },
                           std::vector<FuturePtr<int>>{});

  loop.RunUntilIdle();

  f->Then([&](std::vector<int> v) {
    EXPECT_EQ(v.size(), 0ul);
    async_expectations.Signal();
  });

  EXPECT_EQ(1, async_expectations.count());
}

TEST_F(FutureTest, WaitWithTimeoutCallsCallbackOnTimeout) {
  AsyncExpectations async_expectations;
  async::TestLoop loop;

  auto f1 =
      Future<>::Create(std::string(__PRETTY_FUNCTION__) + std::string("1"));

  auto f = WaitWithTimeout(std::string(__PRETTY_FUNCTION__) + std::string("2"),
                           loop.dispatcher(), {},
                           [&](const std::string& msg) {
                             EXPECT_EQ('1', msg[msg.size() - 1]);
                             async_expectations.Signal();
                           },
                           {f1});

  loop.RunUntilIdle();
  EXPECT_EQ(1, async_expectations.count());
}

TEST_F(FutureTest, WaitWithTimeoutWithDefaultDispatcher) {
  AsyncExpectations async_expectations;
  async::TestLoop loop;

  async_set_default_dispatcher(loop.dispatcher());

  auto f1 =
      Future<>::Create(std::string(__PRETTY_FUNCTION__) + std::string("1"));

  auto f =
      WaitWithTimeout(std::string(__PRETTY_FUNCTION__) + std::string("2"), {},
                      [&](const std::string& msg) {
                        EXPECT_EQ('1', msg[msg.size() - 1]);
                        async_expectations.Signal();
                      },
                      {f1});

  loop.RunUntilIdle();
  EXPECT_EQ(1, async_expectations.count());
}

TEST_F(FutureTest, WaitWithTimeoutDoesNotCompleteAfterTimeout) {
  async::TestLoop loop;

  auto f1 =
      Future<>::Create(std::string(__PRETTY_FUNCTION__) + std::string("1"));

  auto f = WaitWithTimeout(std::string(__PRETTY_FUNCTION__) + std::string("2"),
                           loop.dispatcher(), {}, [](const auto&) {}, {f1});

  f->Then([] { FAIL(); });

  loop.RunUntilIdle();
  f1->Complete();
}

// The following several tests also test additional Wait functionality, since
// WaitWithTimeout is implemented in terms of Wait.
TEST_F(FutureTest, WaitWithTimeoutOnMoveOnlyType) {
  AsyncExpectations async_expectations;
  async::TestLoop loop;

  auto f1 = Future<std::unique_ptr<int>>::Create(
      std::string(__PRETTY_FUNCTION__) + std::string("1"));

  auto f = WaitWithTimeout(std::string(__PRETTY_FUNCTION__) + std::string("2"),
                           loop.dispatcher(), kPause,
                           [](const std::string& msg) { FAIL() << msg; }, {f1});

  f->Then([&](std::vector<std::unique_ptr<int>> v) {
    EXPECT_EQ(v.size(), 1u);

    EXPECT_EQ(*v[0], 42);

    async_expectations.Signal();
  });

  f1->Complete(std::make_unique<int>(42));
  EXPECT_EQ(1, async_expectations.count());
}

TEST_F(FutureTest, WaitWithTimeoutNoTypeStreamlining) {
  // This test contains no assertions as it is a compile-time test.
  async::TestLoop loop;
  auto f1 =
      Future<>::Create(std::string(__PRETTY_FUNCTION__) + std::string("1"));

  auto f = WaitWithTimeout<Future<std::vector<std::tuple<>>>>(
      std::string(__PRETTY_FUNCTION__) + std::string("2"), loop.dispatcher(),
      kPause, [](const auto&) {}, {f1});
  f->Then([](std::vector<std::tuple<>> v) {});
}

// This tests a specific bug case where some futures started completed and some
// did not. In this bug, WaitWithTimeout added its notification const callbacks
// after invoking Wait. The futures that were completed would invoke the
// callback added by Wait immediately, making the const callbacks then added by
// WaitWithTimeout illegal. This did not manifest when all futures started
// completed due to an optimization in WaitWithTimeout that returns immediately
// if the Wait future starts complete.
TEST_F(FutureTest, WaitWithTimeoutOnPartiallyCompletedFutures) {
  AsyncExpectations async_expectations;
  async::TestLoop loop;

  auto f1 = Future<int>::Create(__PRETTY_FUNCTION__ + std::string("1"));
  auto f2 = Future<int>::Create(__PRETTY_FUNCTION__ + std::string("2"));
  f1->Complete(42);
  auto f =
      WaitWithTimeout(__PRETTY_FUNCTION__ + std::string("Wait"),
                      loop.dispatcher(), kPause, [](const auto&) {}, {f1, f2})
          ->Then([&](const std::vector<int>& results) {
            ASSERT_EQ(2u, results.size());
            EXPECT_EQ(42, results[0]);
            EXPECT_EQ(54, results[1]);
            async_expectations.Signal();
          });
  f2->Complete(54);  // http://hitchhikers.wikia.com/wiki/42
  EXPECT_EQ(1, async_expectations.count());
}

TEST_F(FutureTest, WaitWithTimeoutReleasesFuturesOnCompletion) {
  async::TestLoop loop;
  fxl::WeakPtr<Future<>> weak_f1;

  {
    auto f1 = Future<>::Create(__PRETTY_FUNCTION__ + std::string("1"));
    weak_f1 = weak_factory(f1).GetWeakPtr();

    auto f =
        WaitWithTimeout(__PRETTY_FUNCTION__ + std::string("Wait"),
                        loop.dispatcher(), kPause, [](const auto&) {}, {f1});
    f1->Complete();
  }

  EXPECT_FALSE(weak_f1);
}

TEST_F(FutureTest, WaitWithTimeoutReleasesFuturesOnTimeout) {
  async::TestLoop loop;
  fxl::WeakPtr<Future<>> weak_f1;

  {
    auto f1 = Future<>::Create(__PRETTY_FUNCTION__ + std::string("1"));
    weak_f1 = weak_factory(f1).GetWeakPtr();

    WaitWithTimeout(__PRETTY_FUNCTION__ + std::string("Wait"),
                    loop.dispatcher(), {}, [](const auto&) {}, {f1});
    loop.RunUntilIdle();
  }

  EXPECT_FALSE(weak_f1);
}

TEST_F(FutureTest,
       WaitWithTimeoutCallsCallbackOnTimeoutEvenIfFuturesAreDestroyed) {
  AsyncExpectations async_expectations;
  async::TestLoop loop;

  {
    auto f1 = Future<>::Create(__PRETTY_FUNCTION__ + std::string("1"));
    WaitWithTimeout(__PRETTY_FUNCTION__ + std::string("Wait"),
                    loop.dispatcher(), {},
                    [&](const auto&) { async_expectations.Signal(); }, {f1});
  }

  loop.RunUntilIdle();
  EXPECT_EQ(1, async_expectations.count());
}

TEST_F(
    FutureTest,
    WaitWithTimeoutReleasesReturnedFutureIfAllComponentFuturesHaveBeenCompletedOrDestroyed) {
  async::TestLoop loop;
  fxl::WeakPtr<Future<std::vector<int>>> weak_f;

  auto f1 = Future<int>::Create(__PRETTY_FUNCTION__ + std::string("1"));
  {
    auto f2 = Future<int>::Create(__PRETTY_FUNCTION__ + std::string("2"));
    weak_f =
        weak_factory(WaitWithTimeout(__PRETTY_FUNCTION__ + std::string("Wait"),
                                     loop.dispatcher(), kPause,
                                     [](const auto&) {}, {f1, f2}))
            .GetWeakPtr();
  }

  ASSERT_TRUE(weak_f);
  weak_f->Then([&](std::vector<int>) { FAIL(); });
  f1->Complete(42);
  EXPECT_FALSE(weak_f);
}

TEST_F(FutureTest, WaitWithTimeoutReleasesReturnedFutureOnTimeout) {
  async::TestLoop loop;
  fxl::WeakPtr<Future<std::vector<int>>> weak_f;

  {
    auto f1 = Future<int>::Create(__PRETTY_FUNCTION__ + std::string("1"));
    weak_f = weak_factory(WaitWithTimeout(
                              __PRETTY_FUNCTION__ + std::string("Wait"),
                              loop.dispatcher(), {}, [](const auto&) {}, {f1}))
                 .GetWeakPtr();
  }

  loop.RunUntilIdle();
  EXPECT_FALSE(weak_f);
}

TEST_F(FutureTest, Completer) {
  auto f = Future<int>::Create(__PRETTY_FUNCTION__);

  auto completer = f->Completer();
  completer(5);

  AsyncExpectations async_expectations;

  f->Then([&](int i) {
    EXPECT_EQ(i, 5);
    async_expectations.Signal();
  });

  EXPECT_EQ(1, async_expectations.count());
}

TEST_F(FutureTest, AsyncMap) {
  auto future =
      Future<int>::Create(std::string(__PRETTY_FUNCTION__) + std::string("1"));

  AsyncExpectations async_expectations;

  future
      ->AsyncMap([&](int i) {
        EXPECT_EQ(i, 10);
        async_expectations.Signal();

        auto f = Future<std::string>::Create(std::string(__PRETTY_FUNCTION__) +
                                             std::string("2"));
        f->Complete(std::to_string(i * 2));
        return f;
      })
      ->AsyncMap([&](std::string s) {
        EXPECT_EQ(s, "20");
        async_expectations.Signal();

        auto f = Future<int>::Create(std::string(__PRETTY_FUNCTION__) +
                                     std::string("3"));
        f->Complete(std::stoi(s));
        return f;
      })
      ->Then([&](int i) {
        EXPECT_EQ(i, 20);
        async_expectations.Signal();
      });

  future->Complete(10);

  EXPECT_EQ(3, async_expectations.count());
}

TEST_F(FutureTest, Map) {
  auto f = Future<int>::Create(__PRETTY_FUNCTION__);

  AsyncExpectations async_expectations;

  f->Map([&](int i) {
     EXPECT_EQ(i, 10);
     async_expectations.Signal();

     return std::to_string(i * 2);
   })
      ->Map([&](std::string s) {
        EXPECT_EQ(s, "20");
        async_expectations.Signal();

        return std::stoi(s);
      })
      ->Then([&](int i) {
        EXPECT_EQ(i, 20);
        async_expectations.Signal();
      });

  f->Complete(10);

  EXPECT_EQ(3, async_expectations.count());
}

TEST_F(FutureTest, trace_name) {
  auto f = Future<>::Create("test");
  EXPECT_EQ(f->trace_name(), "test");
}

TEST_F(FutureTest, ThenCallbackDestroysFuture) {
  struct S {
    FuturePtr<> f;
  };
  S* s = new S;

  AsyncExpectations async_expectations;

  s->f = Future<>::Create(__PRETTY_FUNCTION__);

  // Explicitly retain the subfuture returned by Then() to keep it alive. This
  // tests whether the original future will still complete its subfuture if the
  // original future is destroyed, but the subfuture is still kept alive. (That
  // should not happen, as per documentation for Then().)
  auto subfuture = s->f->Then([&] {
    delete s;
    async_expectations.Signal();
  });

  subfuture->Then([&] {
    // This lambda shouldn't be executed because s--and therefore s->f--was
    // deleted in the Then() callback above.
    async_expectations.Signal();
  });

  s->f->Complete();

  EXPECT_EQ(1, async_expectations.count());  // not 2!
}

}  // namespace
}  // namespace modular
