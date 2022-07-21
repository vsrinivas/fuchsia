// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/forensics/feedback/annotations/annotation_manager.h"

#include <lib/async/cpp/executor.h>
#include <lib/async/cpp/task.h>
#include <lib/fit/function.h>
#include <lib/syslog/cpp/macros.h>
#include <lib/zx/time.h>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "src/developer/forensics/feedback/annotations/provider.h"
#include "src/developer/forensics/feedback/annotations/types.h"
#include "src/developer/forensics/testing/unit_test_fixture.h"

namespace forensics::feedback {
namespace {

using ::testing::HasSubstr;
using ::testing::IsEmpty;
using ::testing::Pair;
using ::testing::UnorderedElementsAreArray;

constexpr bool kIsMissingNonPlatform = true;
constexpr bool kNotIsMissingNonPlatform = false;

auto MakePair(const char* key, const char* value) { return Pair(key, ErrorOr<std::string>(value)); }
auto MakePair(const char* key, const Error error) { return Pair(key, ErrorOr<std::string>(error)); }

class DynamicNonPlatform : public NonPlatformAnnotationProvider {
 public:
  DynamicNonPlatform(const bool exempt = kNotIsMissingNonPlatform)
      : is_missing_annotations_(exempt) {}

  Annotations Get() override {
    ++calls_;
    if (is_missing_annotations_) {
      return {};
    }

    return {{"num_calls", std::to_string(calls_)}};
  }

  bool IsMissingAnnotations() const override { return is_missing_annotations_; }

 private:
  size_t calls_{0};
  bool is_missing_annotations_;
};

using AnnotationManagerTest = UnitTestFixture;

TEST_F(AnnotationManagerTest, ImmediatelyAvailable) {
  const Annotations static_annotations({
      {"annotation1", "value1"},
      {"annotation2", Error::kMissingValue},
  });

  DynamicNonPlatform non_platform;

  {
    AnnotationManager manager(dispatcher(), {"annotation1", "annotation2"}, static_annotations,
                              &non_platform);

    EXPECT_THAT(manager.ImmediatelyAvailable(), UnorderedElementsAreArray({
                                                    MakePair("annotation1", "value1"),
                                                    MakePair("annotation2", Error::kMissingValue),
                                                    MakePair("num_calls", "1"),
                                                }));
  }
}

TEST_F(AnnotationManagerTest, StaticAllowlist) {
  const Annotations static_annotations({
      {"annotation1", "value1"},
      {"annotation2", Error::kMissingValue},
  });

  DynamicNonPlatform counter;
  {
    AnnotationManager manager(dispatcher(), {}, static_annotations, &counter);

    EXPECT_THAT(manager.ImmediatelyAvailable(), UnorderedElementsAreArray({
                                                    MakePair("num_calls", "1"),
                                                }));
  }

  {
    AnnotationManager manager(dispatcher(), {"annotation1"}, static_annotations, &counter);

    EXPECT_THAT(manager.ImmediatelyAvailable(), UnorderedElementsAreArray({
                                                    MakePair("annotation1", "value1"),
                                                    MakePair("num_calls", "2"),
                                                }));
  }

  {
    AnnotationManager manager(dispatcher(), {"annotation1"}, static_annotations, &counter);

    EXPECT_THAT(manager.ImmediatelyAvailable(), UnorderedElementsAreArray({
                                                    MakePair("annotation1", "value1"),
                                                    MakePair("num_calls", "3"),
                                                }));
  }
}

TEST_F(AnnotationManagerTest, IsNotMissingNonPlatform) {
  DynamicNonPlatform non_platform(kNotIsMissingNonPlatform);

  {
    AnnotationManager manager(dispatcher(), {}, {}, &non_platform);
    EXPECT_THAT(manager.ImmediatelyAvailable(), UnorderedElementsAreArray({
                                                    MakePair("num_calls", "1"),
                                                }));
    EXPECT_FALSE(manager.IsMissingNonPlatformAnnotations());
  }
}

TEST_F(AnnotationManagerTest, IsMissingNonPlatform) {
  DynamicNonPlatform non_platform(kIsMissingNonPlatform);

  {
    AnnotationManager manager(dispatcher(), {}, {}, &non_platform);

    EXPECT_THAT(manager.ImmediatelyAvailable(), IsEmpty());
    EXPECT_TRUE(manager.IsMissingNonPlatformAnnotations());
  }
}

class SimpleStaticAsync : public StaticAsyncAnnotationProvider {
 public:
  SimpleStaticAsync(async_dispatcher_t* dispatcher, Annotations annotations,
                    const zx::duration delay = zx::sec(0))
      : dispatcher_(dispatcher), annotations_(std::move(annotations)), delay_(delay) {}

  std::set<std::string> GetKeys() const override {
    std::set<std::string> keys;
    for (const auto& [k, _] : annotations_) {
      keys.insert(k);
    }

    return keys;
  }

  void GetOnce(::fit::callback<void(Annotations)> callback) override {
    async::PostDelayedTask(
        dispatcher_, [this, cb = std::move(callback)]() mutable { cb(annotations_); }, delay_);
  }

 private:
  async_dispatcher_t* dispatcher_;
  Annotations annotations_;
  zx::duration delay_;
};

class SimpleCachedAsync : public CachedAsyncAnnotationProvider {
 public:
  SimpleCachedAsync(async_dispatcher_t* dispatcher, std::set<std::string> keys,
                    const zx::duration period = zx::sec(1))
      : dispatcher_(dispatcher), keys_(std::move(keys)), period_(period) {}

  std::set<std::string> GetKeys() const override { return keys_; }

  void GetOnUpdate(::fit::function<void(Annotations)> callback) override {
    FX_CHECK(callback_ == nullptr);
    callback_ = std::move(callback);

    PostNext();
  }

 private:
  void PostNext() {
    async::PostDelayedTask(
        dispatcher_,
        [this]() mutable {
          const std::string value = "value" + std::to_string(++count_);

          Annotations annotations;
          for (const auto& key : keys_) {
            annotations.insert_or_assign(key, value);
          }
          callback_(annotations);
          PostNext();
        },
        period_);
  }

  async_dispatcher_t* dispatcher_;
  std::set<std::string> keys_;
  zx::duration period_;

  ::fit::function<void(Annotations)> callback_;
  size_t count_{0};
};

TEST_F(AnnotationManagerTest, GetAllNoStaticAsyncProviders) {
  async::Executor executor(dispatcher());

  const Annotations static_annotations({
      {"annotation1", "value1"},
      {"annotation2", Error::kMissingValue},
  });

  DynamicNonPlatform non_platform;

  {
    AnnotationManager manager(dispatcher(), {"annotation1", "annotation2"}, static_annotations,
                              &non_platform);

    Annotations annotations;

    // Use a timeout of 0 because only immediately available annotations are returned.
    executor.schedule_task(
        manager.GetAll(zx::sec(0))
            .and_then([&annotations](Annotations& result) { annotations = std::move(result); })
            .or_else([]() { FX_LOGS(FATAL) << "Unreachable error reached"; }));

    RunLoopUntilIdle();
    EXPECT_THAT(annotations, UnorderedElementsAreArray({
                                 MakePair("annotation1", "value1"),
                                 MakePair("annotation2", Error::kMissingValue),
                                 MakePair("num_calls", "1"),
                             }));
  }
}

TEST_F(AnnotationManagerTest, GetAllStaticAsyncProviders) {
  async::Executor executor(dispatcher());

  const Annotations static_annotations({
      {"annotation1", "value1"},
      {"annotation2", Error::kMissingValue},
  });

  SimpleStaticAsync immediate_static(dispatcher(), {
                                                       {"annotation3", "value3"},
                                                   });
  SimpleStaticAsync five_second_static(dispatcher(),
                                       {
                                           {"annotation4", "value4"},
                                       },
                                       zx::sec(5));
  SimpleStaticAsync ten_second_static(dispatcher(),
                                      {
                                          {"annotation5", "value5"},
                                      },
                                      zx::sec(10));
  DynamicNonPlatform non_platform;

  AnnotationManager manager(dispatcher(),
                            {
                                "annotation1",
                                "annotation2",
                                "annotation3",
                                "annotation4",
                                "annotation5",
                            },
                            static_annotations, &non_platform, {},
                            {&immediate_static, &five_second_static, &ten_second_static});
  {
    Annotations annotations;

    executor.schedule_task(
        manager.GetAll(zx::sec(0))
            .and_then([&annotations](Annotations& result) { annotations = std::move(result); })
            .or_else([]() { FX_LOGS(FATAL) << "Unreachable error reached"; }));

    RunLoopUntilIdle();
    EXPECT_THAT(annotations, UnorderedElementsAreArray({
                                 MakePair("annotation1", "value1"),
                                 MakePair("annotation2", Error::kMissingValue),
                                 MakePair("num_calls", "1"),
                                 MakePair("annotation3", "value3"),
                                 MakePair("annotation4", Error::kTimeout),
                                 MakePair("annotation5", Error::kTimeout),
                             }));
  }

  {
    Annotations annotations;

    executor.schedule_task(
        manager.GetAll(zx::sec(5))
            .and_then([&annotations](Annotations& result) { annotations = std::move(result); })
            .or_else([]() { FX_LOGS(FATAL) << "Unreachable error reached"; }));

    RunLoopFor(zx::sec(5));
    EXPECT_THAT(annotations, UnorderedElementsAreArray({
                                 MakePair("annotation1", "value1"),
                                 MakePair("annotation2", Error::kMissingValue),
                                 MakePair("num_calls", "2"),
                                 MakePair("annotation3", "value3"),
                                 MakePair("annotation4", "value4"),
                                 MakePair("annotation5", Error::kTimeout),
                             }));
  }

  {
    Annotations annotations;

    executor.schedule_task(
        manager.GetAll(zx::sec(5))
            .and_then([&annotations](Annotations& result) { annotations = std::move(result); })
            .or_else([]() { FX_LOGS(FATAL) << "Unreachable error reached"; }));

    RunLoopFor(zx::sec(5));
    EXPECT_THAT(annotations, UnorderedElementsAreArray({
                                 MakePair("annotation1", "value1"),
                                 MakePair("annotation2", Error::kMissingValue),
                                 MakePair("num_calls", "3"),
                                 MakePair("annotation3", "value3"),
                                 MakePair("annotation4", "value4"),
                                 MakePair("annotation5", "value5"),
                             }));
  }
}

class SimpleDynamicAsync : public DynamicAsyncAnnotationProvider {
 public:
  SimpleDynamicAsync(async_dispatcher_t* dispatcher, std::set<std::string> keys,
                     const zx::duration delay = zx::sec(0))
      : dispatcher_(dispatcher), keys_(std::move(keys)), delay_(delay) {}

  std::set<std::string> GetKeys() const override { return keys_; }

  void Get(::fit::callback<void(Annotations)> callback) override {
    async::PostDelayedTask(
        dispatcher_,
        [this, cb = std::move(callback)]() mutable {
          const std::string value = "call" + std::to_string(++count_);

          Annotations annotations;
          for (const auto& key : keys_) {
            annotations.insert_or_assign(key, value);
          }
          cb(annotations);
        },
        delay_);
  }

 private:
  async_dispatcher_t* dispatcher_;
  std::set<std::string> keys_;
  zx::duration delay_;

  size_t count_{0};
};

TEST_F(AnnotationManagerTest, GetAllNoDyanmicAsyncProviders) {
  async::Executor executor(dispatcher());

  const Annotations static_annotations({
      {"annotation1", "value1"},
      {"annotation2", Error::kMissingValue},
  });

  DynamicNonPlatform non_platform;

  {
    AnnotationManager manager(dispatcher(), {"annotation1", "annotation2"}, static_annotations,
                              &non_platform);

    Annotations annotations;

    // Use a timeout of 0 because only immediately available annotations are returned.
    executor.schedule_task(
        manager.GetAll(zx::sec(0))
            .and_then([&annotations](Annotations& result) { annotations = std::move(result); })
            .or_else([]() { FX_LOGS(FATAL) << "Unreachable error reached"; }));

    RunLoopUntilIdle();
    EXPECT_THAT(annotations, UnorderedElementsAreArray({
                                 MakePair("annotation1", "value1"),
                                 MakePair("annotation2", Error::kMissingValue),
                                 MakePair("num_calls", "1"),
                             }));
  }
}

TEST_F(AnnotationManagerTest, GetAllCachedAsyncProviders) {
  async::Executor executor(dispatcher());

  const Annotations static_annotations({
      {"annotation1", "value1"},
      {"annotation2", Error::kMissingValue},
  });

  SimpleCachedAsync one_second_cached(dispatcher(), {"annotation3"}, zx::sec(1));
  SimpleCachedAsync five_second_cached(dispatcher(), {"annotation4"}, zx::sec(5));
  SimpleCachedAsync ten_second_cached(dispatcher(), {"annotation5"}, zx::sec(10));
  DynamicNonPlatform non_platform;

  AnnotationManager manager(dispatcher(),
                            {
                                "annotation1",
                                "annotation2",
                                "annotation3",
                                "annotation4",
                                "annotation5",
                            },
                            static_annotations, &non_platform, {}, {},
                            {&one_second_cached, &five_second_cached, &ten_second_cached}, {});
  {
    Annotations annotations;

    executor.schedule_task(
        manager.GetAll(zx::sec(0))
            .and_then([&annotations](Annotations& result) { annotations = std::move(result); })
            .or_else([]() { FX_LOGS(FATAL) << "Unreachable error reached"; }));

    RunLoopUntilIdle();
    EXPECT_THAT(annotations, UnorderedElementsAreArray({
                                 MakePair("annotation1", "value1"),
                                 MakePair("annotation2", Error::kMissingValue),
                                 MakePair("num_calls", "1"),
                                 MakePair("annotation3", Error::kTimeout),
                                 MakePair("annotation4", Error::kTimeout),
                                 MakePair("annotation5", Error::kTimeout),
                             }));
  }

  {
    Annotations annotations;

    executor.schedule_task(
        manager.GetAll(zx::sec(5))
            .and_then([&annotations](Annotations& result) { annotations = std::move(result); })
            .or_else([]() { FX_LOGS(FATAL) << "Unreachable error reached"; }));

    RunLoopFor(zx::sec(5));
    EXPECT_THAT(annotations, UnorderedElementsAreArray({
                                 MakePair("annotation1", "value1"),
                                 MakePair("annotation2", Error::kMissingValue),
                                 MakePair("num_calls", "2"),
                                 MakePair("annotation3", "value5"),
                                 MakePair("annotation4", "value1"),
                                 MakePair("annotation5", Error::kTimeout),
                             }));
  }

  {
    Annotations annotations;

    executor.schedule_task(
        manager.GetAll(zx::sec(5))
            .and_then([&annotations](Annotations& result) { annotations = std::move(result); })
            .or_else([]() { FX_LOGS(FATAL) << "Unreachable error reached"; }));

    RunLoopFor(zx::sec(5));
    EXPECT_THAT(annotations, UnorderedElementsAreArray({
                                 MakePair("annotation1", "value1"),
                                 MakePair("annotation2", Error::kMissingValue),
                                 MakePair("num_calls", "3"),
                                 MakePair("annotation3", "value10"),
                                 MakePair("annotation4", "value2"),
                                 MakePair("annotation5", "value1"),
                             }));
  }
}

TEST_F(AnnotationManagerTest, GetAllDynamicAsyncProviders) {
  async::Executor executor(dispatcher());

  const Annotations static_annotations({
      {"annotation1", "value1"},
      {"annotation2", Error::kMissingValue},
  });

  SimpleDynamicAsync immediate_dynamic(dispatcher(), {"annotation3"});
  SimpleDynamicAsync five_second_dynamic(dispatcher(), {"annotation4"}, zx::sec(5));
  SimpleDynamicAsync ten_second_dynamic(dispatcher(), {"annotation5"}, zx::sec(10));
  DynamicNonPlatform non_platform;

  AnnotationManager manager(dispatcher(),
                            {
                                "annotation1",
                                "annotation2",
                                "annotation3",
                                "annotation4",
                                "annotation5",
                            },
                            static_annotations, &non_platform, {}, {}, {},
                            {&immediate_dynamic, &five_second_dynamic, &ten_second_dynamic});
  {
    Annotations annotations;

    executor.schedule_task(
        manager.GetAll(zx::sec(0))
            .and_then([&annotations](Annotations& result) { annotations = std::move(result); })
            .or_else([]() { FX_LOGS(FATAL) << "Unreachable error reached"; }));

    RunLoopUntilIdle();
    EXPECT_THAT(annotations, UnorderedElementsAreArray({
                                 MakePair("annotation1", "value1"),
                                 MakePair("annotation2", Error::kMissingValue),
                                 MakePair("num_calls", "1"),
                                 MakePair("annotation3", "call1"),
                                 MakePair("annotation4", Error::kTimeout),
                                 MakePair("annotation5", Error::kTimeout),
                             }));
  }

  {
    Annotations annotations;

    executor.schedule_task(
        manager.GetAll(zx::sec(5))
            .and_then([&annotations](Annotations& result) { annotations = std::move(result); })
            .or_else([]() { FX_LOGS(FATAL) << "Unreachable error reached"; }));

    RunLoopFor(zx::sec(5));
    EXPECT_THAT(annotations, UnorderedElementsAreArray({
                                 MakePair("annotation1", "value1"),
                                 MakePair("annotation2", Error::kMissingValue),
                                 MakePair("num_calls", "2"),
                                 MakePair("annotation3", "call2"),
                                 MakePair("annotation4", "call2"),
                                 MakePair("annotation5", Error::kTimeout),
                             }));
  }

  {
    Annotations annotations;

    executor.schedule_task(
        manager.GetAll(zx::sec(10))
            .and_then([&annotations](Annotations& result) { annotations = std::move(result); })
            .or_else([]() { FX_LOGS(FATAL) << "Unreachable error reached"; }));

    RunLoopFor(zx::sec(10));
    EXPECT_THAT(annotations, UnorderedElementsAreArray({
                                 MakePair("annotation1", "value1"),
                                 MakePair("annotation2", Error::kMissingValue),
                                 MakePair("num_calls", "3"),
                                 MakePair("annotation3", "call3"),
                                 MakePair("annotation4", "call3"),
                                 MakePair("annotation5", "call3"),
                             }));
  }
}

TEST_F(AnnotationManagerTest, GetAll) {
  async::Executor executor(dispatcher());

  const Annotations static_annotations({
      {"annotation1", "value1"},
      {"annotation2", Error::kMissingValue},
  });

  SimpleStaticAsync three_second_static(dispatcher(), {{"annotation3", "value3"}}, zx::sec(3));
  SimpleCachedAsync one_second_cached(dispatcher(), {"annotation4"}, zx::sec(1));
  SimpleDynamicAsync five_second_dynamic(dispatcher(), {"annotation5"}, zx::sec(5));
  DynamicNonPlatform non_platform;

  AnnotationManager manager(dispatcher(),
                            {
                                "annotation1",
                                "annotation2",
                                "annotation3",
                                "annotation4",
                                "annotation5",
                            },
                            static_annotations, &non_platform, {}, {&three_second_static},
                            {&one_second_cached}, {&five_second_dynamic});
  {
    Annotations annotations;

    executor.schedule_task(
        manager.GetAll(zx::sec(0))
            .and_then([&annotations](Annotations& result) { annotations = std::move(result); })
            .or_else([]() { FX_LOGS(FATAL) << "Unreachable error reached"; }));

    RunLoopUntilIdle();
    EXPECT_THAT(annotations, UnorderedElementsAreArray({
                                 MakePair("annotation1", "value1"),
                                 MakePair("annotation2", Error::kMissingValue),
                                 MakePair("num_calls", "1"),
                                 MakePair("annotation3", Error::kTimeout),
                                 MakePair("annotation4", Error::kTimeout),
                                 MakePair("annotation5", Error::kTimeout),
                             }));
  }

  {
    Annotations annotations;

    executor.schedule_task(
        manager.GetAll(zx::sec(3))
            .and_then([&annotations](Annotations& result) { annotations = std::move(result); })
            .or_else([]() { FX_LOGS(FATAL) << "Unreachable error reached"; }));

    RunLoopFor(zx::sec(3));
    EXPECT_THAT(annotations, UnorderedElementsAreArray({
                                 MakePair("annotation1", "value1"),
                                 MakePair("annotation2", Error::kMissingValue),
                                 MakePair("num_calls", "2"),
                                 MakePair("annotation3", "value3"),
                                 MakePair("annotation4", "value3"),
                                 MakePair("annotation5", Error::kTimeout),
                             }));
  }

  {
    Annotations annotations;

    executor.schedule_task(
        manager.GetAll(zx::sec(5))
            .and_then([&annotations](Annotations& result) { annotations = std::move(result); })
            .or_else([]() { FX_LOGS(FATAL) << "Unreachable error reached"; }));

    RunLoopFor(zx::sec(5));
    EXPECT_THAT(annotations, UnorderedElementsAreArray({
                                 MakePair("annotation1", "value1"),
                                 MakePair("annotation2", Error::kMissingValue),
                                 MakePair("num_calls", "3"),
                                 MakePair("annotation3", "value3"),
                                 MakePair("annotation4", "value8"),
                                 MakePair("annotation5", "call3"),
                             }));
  }
}

TEST_F(AnnotationManagerTest, NoProvider) {
  ASSERT_DEATH({ AnnotationManager manager(dispatcher(), {"annotation"}); },
               HasSubstr("Annotation \"annotation\" collected by 0 providers"));
}

TEST_F(AnnotationManagerTest, MultipleProviders) {
  SimpleStaticAsync static_async(dispatcher(), {{"annotation", Error::kMissingValue}});
  SimpleCachedAsync cached_async(dispatcher(), {"annotation"});
  SimpleDynamicAsync dynamic_async(dispatcher(), {"annotation"});

  ASSERT_DEATH(
      {
        AnnotationManager manager(dispatcher(), {"annotation"},
                                  {{"annotation", Error::kMissingValue}}, nullptr, {},
                                  {&static_async}, {&cached_async}, {&dynamic_async});
      },
      HasSubstr("Annotation \"annotation\" collected by 4 providers"));
}

class MultipleTypeProvider : public StaticAsyncAnnotationProvider,
                             public DynamicAsyncAnnotationProvider {
 public:
  MultipleTypeProvider(async_dispatcher_t* dispatcher, Annotations annotations,
                       const zx::duration delay = zx::sec(0))
      : dispatcher_(dispatcher), annotations_(std::move(annotations)), delay_(delay) {}

  std::set<std::string> GetKeys() const override {
    std::set<std::string> keys;
    for (const auto& [k, _] : annotations_) {
      keys.insert(k);
    }

    return keys;
  }

  void GetOnce(::fit::callback<void(Annotations)> callback) override {
    async::PostDelayedTask(
        dispatcher_, [this, cb = std::move(callback)]() mutable { cb(annotations_); }, delay_);
  }

  void Get(::fit::callback<void(Annotations)> callback) override {
    async::PostDelayedTask(
        dispatcher_, [this, cb = std::move(callback)]() mutable { cb(annotations_); }, delay_);
  }

 private:
  async_dispatcher_t* dispatcher_;
  Annotations annotations_;
  zx::duration delay_;
};

TEST_F(AnnotationManagerTest, DuplicateProviders) {
  MultipleTypeProvider multiple_provider(dispatcher(), {{"annotation", Error::kMissingValue}});
  // This should NOT exit abnormally
  AnnotationManager manager(dispatcher(), {"annotation"}, {}, nullptr, {}, {&multiple_provider}, {},
                            {&multiple_provider});
}

}  // namespace
}  // namespace forensics::feedback
