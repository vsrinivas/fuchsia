// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/lib/cobalt/cobalt.h"

#include "lib/app/cpp/service_provider_impl.h"
#include "lib/fxl/logging.h"
#include "lib/fxl/macros.h"
#include "lib/svc/cpp/service_provider_bridge.h"
#include "peridot/lib/gtest/test_with_message_loop.h"

namespace cobalt {

constexpr int32_t kFakeCobaltProjectId = 1;
constexpr int32_t kFakeCobaltMetricId = 2;
constexpr int32_t kFakeCobaltEncodingId = 3;
constexpr int32_t kFakeCobaltEvent = 4;

class FakeTaskRunner : public fxl::TaskRunner {
 public:
  inline static fxl::RefPtr<FakeTaskRunner> Create() {
    return AdoptRef(new FakeTaskRunner());
  }

  void PostTask(fxl::Closure task) override {}

  void PostTaskForTime(fxl::Closure task, fxl::TimePoint target_time) override {
  }

  void PostDelayedTask(fxl::Closure task, fxl::TimeDelta delay) override {}

  bool RunsTasksOnCurrentThread() override {
    runs_task_on_current_thread_called = true;
    return true;
  }

  bool runs_task_on_current_thread_called = false;
};


class FakeCobaltEncoderImpl : public CobaltEncoder {
 public:
  FakeCobaltEncoderImpl() {}

  void AddStringObservation(
      uint32_t metric_id,
      uint32_t encoding_id,
      const fidl::String& observation,
      const AddStringObservationCallback& callback) override {
    ValuePtr value = Value::New();
    value->set_string_value(observation);
    RecordCall("AddStringObservation", value);
    callback(Status::OK);
  };

  void AddIntObservation(uint32_t metric_id,
                         uint32_t encoding_id,
                         const int64_t observation,
                         const AddIntObservationCallback& callback) override {
    ValuePtr value = Value::New();
    value->set_int_value(observation);
    RecordCall("AddIntObservation", value);
    callback(Status::OK);
  };

  void AddDoubleObservation(
      uint32_t metric_id, uint32_t encoding_id, const double observation,
      const AddDoubleObservationCallback& callback) override {
    ValuePtr value = Value::New();
    value->set_double_value(observation);
    RecordCall("AddDoubleObservation", value);
    callback(Status::OK);
  };

  void AddIndexObservation(
      uint32_t metric_id, uint32_t encoding_id, uint32_t index,
      const AddIndexObservationCallback& callback) override {
    ValuePtr value = Value::New();
    value->set_index_value(index);
    RecordCall("AddIndexObservation", value);
    callback(Status::OK);
  };

  void AddMultipartObservation(
      uint32_t metric_id, fidl::Array<ObservationValuePtr> observation,
      const AddMultipartObservationCallback& callback) override {
    //  TODO(miguelfrde): add support for multipart in peridot/lib/cobalt.
    callback(Status::OK);
  }

  void AddIntBucketDistribution(
      uint32_t metric_id, uint32_t encoding_id,
      fidl::Map<uint32_t, uint64_t> distribution,
      const AddIntBucketDistributionCallback& callback) override {
    ValuePtr value = Value::New();
    value->set_int_bucket_distribution(std::move(distribution));
    RecordCall("AddIntBucketDistribution", value);
    callback(Status::OK);
  }

  void SendObservations(const SendObservationsCallback& callback) override {};

  void ExpectCalledOnceWith(const std::string& func, ValuePtr& expected) {
    EXPECT_EQ(1U, calls.count(func));
    if (calls.count(func) > 0) {
      EXPECT_EQ(1U, calls[func].size());
      ValuePtr actual = calls[func][0].Clone();
      EXPECT_EQ(expected->which(), actual->which());
      switch (expected->which()) {
        case Value::Tag::DOUBLE_VALUE: {
          EXPECT_EQ(expected->get_double_value(), actual->get_double_value());
          break;
        }
        case Value::Tag::INDEX_VALUE: {
          EXPECT_EQ(expected->get_index_value(), actual->get_index_value());
          break;
        }
        case Value::Tag::INT_VALUE: {
          EXPECT_EQ(expected->get_int_value(), actual->get_int_value());
          break;
        }
        case Value::Tag::STRING_VALUE: {
          EXPECT_EQ(expected->get_string_value(), actual->get_string_value());
          break;
        }
        case Value::Tag::INT_BUCKET_DISTRIBUTION: {
          EXPECT_EQ(expected->get_int_bucket_distribution().size(),
                    actual->get_int_bucket_distribution().size());
          break;
        }
        case Value::Tag::__UNKNOWN__: {
          FAIL();
        }
      }
    }
  }

 private:
  void RecordCall(const std::string& func, ValuePtr& value) {
    calls[func].push_back(std::move(value));
  }

  std::map<std::string, std::vector<ValuePtr>> calls;
};


class FakeCobaltEncoderFactoryImpl : public CobaltEncoderFactory {
 public:
  FakeCobaltEncoderFactoryImpl() {}

  void GetEncoder(int32_t project_id,
                  fidl::InterfaceRequest<CobaltEncoder> request) override {
    cobalt_encoder_.reset(new FakeCobaltEncoderImpl());
    cobalt_encoder_bindings_.AddBinding(cobalt_encoder_.get(),
                                        std::move(request));
  }

  FakeCobaltEncoderImpl* cobalt_encoder() {
    return cobalt_encoder_.get();
  }

 private:
  std::unique_ptr<FakeCobaltEncoderImpl> cobalt_encoder_;
  fidl::BindingSet<CobaltEncoder> cobalt_encoder_bindings_;
};


class CobaltTest : public gtest::TestWithMessageLoop {
 public:
  CobaltTest()
      : app_context_(InitApplicationContext()),
        task_runner_(FakeTaskRunner::Create()) {}
  ~CobaltTest() override {}

  app::ApplicationContext* app_context() { return app_context_.get(); }

  fxl::RefPtr<FakeTaskRunner> task_runner() { return task_runner_; }

  FakeCobaltEncoderImpl* cobalt_encoder() {
    return factory_impl_->cobalt_encoder();
  }

 private:
  std::unique_ptr<app::ApplicationContext> InitApplicationContext() {
    factory_impl_.reset(new FakeCobaltEncoderFactoryImpl());
    service_provider.AddService<CobaltEncoderFactory>(
        [this](fidl::InterfaceRequest<CobaltEncoderFactory> request) {
          factory_bindings_.AddBinding(factory_impl_.get(), std::move(request));
        });
    service_provider.AddService<app::ApplicationEnvironment>(
        [this](fidl::InterfaceRequest<app::ApplicationEnvironment> request) {
          app_environment_request_ = std::move(request);
        });
    service_provider.AddService<app::ApplicationLauncher>(
        [this](fidl::InterfaceRequest<app::ApplicationLauncher> request) {
          app_launcher_request_ = std::move(request);
        });
    return std::make_unique<app::ApplicationContext>(
        service_provider.OpenAsDirectory(), zx::channel(), nullptr);
  }

  app::ServiceProviderBridge service_provider;
  std::unique_ptr<FakeCobaltEncoderFactoryImpl> factory_impl_;
  std::unique_ptr<FakeCobaltEncoderImpl> cobalt_encoder_;
  std::unique_ptr<app::ApplicationContext> app_context_;
  fidl::BindingSet<CobaltEncoderFactory> factory_bindings_;
  fxl::RefPtr<FakeTaskRunner> task_runner_;
  fidl::InterfaceRequest<app::ApplicationLauncher> app_launcher_request_;
  fidl::InterfaceRequest<app::ApplicationEnvironment> app_environment_request_;
  FXL_DISALLOW_COPY_AND_ASSIGN(CobaltTest);
};

TEST_F(CobaltTest, InitializeCobalt) {
  CobaltContext* cobalt_context = nullptr;
  auto ac = InitializeCobalt(task_runner(), app_context(), kFakeCobaltProjectId,
                             kFakeCobaltMetricId, kFakeCobaltEncodingId,
                             &cobalt_context);
  EXPECT_NE(cobalt_context, nullptr);
  ac.call();
  EXPECT_EQ(cobalt_context, nullptr);
}

TEST_F(CobaltTest, ReportEvent) {
  ValuePtr expected_value = Value::New();
  expected_value->set_index_value(kFakeCobaltEvent);
  CobaltContext* cobalt_context = nullptr;
  auto ac = InitializeCobalt(task_runner(), app_context(), kFakeCobaltProjectId,
                             kFakeCobaltMetricId, kFakeCobaltEncodingId,
                             &cobalt_context);
  ReportEvent(kFakeCobaltEvent, cobalt_context);
  RunLoopUntilIdle();
  cobalt_encoder()->ExpectCalledOnceWith("AddIndexObservation", expected_value);
}

}  // namespace cobalt
