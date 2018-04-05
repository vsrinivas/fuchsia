// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/lib/cobalt/cobalt.h"

#include <lib/async/default.h>

#include "garnet/lib/gtest/test_with_message_loop.h"
#include "lib/app/cpp/service_provider_impl.h"
#include "lib/fidl/cpp/clone.h"
#include "lib/fxl/logging.h"
#include "lib/fxl/macros.h"
#include "lib/svc/cpp/service_provider_bridge.h"

namespace cobalt {
namespace {

template <typename T>
T CloneType(const T& other) {
  T result;
  zx_status_t status = fidl::Clone(other, &result);
  FXL_DCHECK(status == ZX_OK);
  return result;
}

bool Equals(const Value& v1, const Value& v2) {
  if (v1.Which() != v2.Which()) {
    return false;
  }
  switch (v1.Which()) {
    case Value::Tag::Invalid:
      return true;
    case Value::Tag::kDoubleValue:
      return v1.double_value() == v2.double_value();
    case Value::Tag::kIndexValue:
      return v1.index_value() == v2.index_value();
    case Value::Tag::kIntBucketDistribution: {
      const auto& bucket1 = v1.int_bucket_distribution();
      const auto& bucket2 = v2.int_bucket_distribution();
      if (bucket1.is_null() != bucket2.is_null()) {
        return false;
      }
      if (!bucket1) {
        return true;
      }
      if (bucket1->size() != bucket2->size()) {
        return false;
      }
      for (size_t i = 0; i < bucket1->size(); ++i) {
        const auto& entry1 = bucket1->at(i);
        const auto& entry2 = bucket2->at(i);
        if (entry1.count != entry2.count || entry1.index != entry2.index) {
          return false;
        }
      }
      return true;
    }
    case Value::Tag::kIntValue:
      return v1.int_value() == v2.int_value();
    case Value::Tag::kStringValue:
      return v1.string_value() == v1.string_value();
  }
}

bool Equals(const fidl::VectorPtr<ObservationValue>& v1,
            const fidl::VectorPtr<ObservationValue>& v2) {
  if (v1.is_null() != v2.is_null()) {
    return false;
  }
  if (!v1) {
    return true;
  }
  if (v1->size() != v2->size()) {
    return false;
  }
  for (size_t i = 0; i < v1->size(); ++i) {
    const auto& value1 = v1->at(i);
    const auto& value2 = v2->at(i);
    if (value1.encoding_id != value2.encoding_id ||
        value1.name != value2.name || !Equals(value1.value, value2.value)) {
      return false;
    }
  }
  return true;
}

constexpr int32_t kFakeCobaltProjectId = 1;
constexpr int32_t kFakeCobaltMetricId = 2;
constexpr int32_t kFakeCobaltEncodingId = 3;

class FakeCobaltEncoderImpl : public CobaltEncoder {
 public:
  FakeCobaltEncoderImpl() {}

  void AddObservation(uint32_t metric_id,
                      uint32_t encoding_id,
                      Value observation,
                      AddObservationCallback callback) override {
    RecordCall("AddObservation", std::move(observation));
    callback(Status::OK);
  };

  void AddStringObservation(uint32_t metric_id,
                            uint32_t encoding_id,
                            fidl::StringPtr observation,
                            AddStringObservationCallback callback) override{};

  void AddIntObservation(uint32_t metric_id,
                         uint32_t encoding_id,
                         const int64_t observation,
                         AddIntObservationCallback callback) override{};

  void AddDoubleObservation(uint32_t metric_id,
                            uint32_t encoding_id,
                            const double observation,
                            AddDoubleObservationCallback callback) override{};

  void AddIndexObservation(uint32_t metric_id,
                           uint32_t encoding_id,
                           uint32_t index,
                           AddIndexObservationCallback callback) override{};

  void AddIntBucketDistribution(
      uint32_t metric_id,
      uint32_t encoding_id,
      fidl::VectorPtr<BucketDistributionEntry> distribution,
      AddIntBucketDistributionCallback callback) override {}

  void StartTimer(
      uint32_t metric_id,
      uint32_t encoding_id,
      fidl::StringPtr timer_id,
      uint64_t timestamp,
      uint32_t timeout_s,
      StartTimerCallback callback) override{};

  void EndTimer(
      fidl::StringPtr timer_id,
      uint64_t timestamp,
      uint32_t timeout_s,
      EndTimerCallback callback) override{};

  void EndTimerMultiPart(
      fidl::StringPtr timer_id,
      uint64_t timestamp,
      fidl::StringPtr part_name,
      fidl::VectorPtr<ObservationValue> observations,
      uint32_t timeout_s,
      EndTimerMultiPartCallback callback) override{};

  void AddMultipartObservation(
      uint32_t metric_id,
      fidl::VectorPtr<ObservationValue> observation,
      AddMultipartObservationCallback callback) override {
    RecordCall("AddMultipartObservation", std::move(observation));
    callback(Status::OK);
  }

  void SendObservations(SendObservationsCallback callback) override{};

  void ExpectCalledOnceWith(const std::string& func, const Value& expected) {
    EXPECT_EQ(1U, calls_.count(func));
    if (calls_.count(func) > 0) {
      EXPECT_EQ(1U, calls_[func].size());
      Value& actual = calls_[func][0];
      EXPECT_EQ(expected.Which(), actual.Which());
      EXPECT_TRUE(Equals(actual, expected));
    }
  }

  void ExpectCalledOnceWith(const std::string& func,
                            fidl::VectorPtr<ObservationValue>& expected_parts) {
    EXPECT_EQ(1U, multipart_calls_.count(func));
    if (multipart_calls_.count(func) > 0) {
      EXPECT_EQ(1U, multipart_calls_[func].size());
      fidl::VectorPtr<ObservationValue>& actual = multipart_calls_[func][0];
      EXPECT_TRUE(Equals(actual, expected_parts));
    }
  }

 private:
  void RecordCall(const std::string& func, Value value) {
    calls_[func].push_back(std::move(value));
  }

  void RecordCall(const std::string& func,
                  fidl::VectorPtr<ObservationValue> parts) {
    multipart_calls_[func].push_back(std::move(parts));
  }

  std::map<std::string, std::vector<Value>> calls_;
  std::map<std::string, std::vector<fidl::VectorPtr<ObservationValue>>>
      multipart_calls_;
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
  CobaltTest() : app_context_(InitApplicationContext()) {  }
  ~CobaltTest() override {}

  component::ApplicationContext* app_context() { return app_context_.get(); }

  FakeCobaltEncoderImpl* cobalt_encoder() {
    return factory_impl_->cobalt_encoder();
  }

 private:
  std::unique_ptr<component::ApplicationContext> InitApplicationContext() {
    factory_impl_.reset(new FakeCobaltEncoderFactoryImpl());
    service_provider.AddService<CobaltEncoderFactory>(
        [this](fidl::InterfaceRequest<CobaltEncoderFactory> request) {
          factory_bindings_.AddBinding(factory_impl_.get(), std::move(request));
        });
    service_provider.AddService<component::ApplicationEnvironment>(
        [this](
            fidl::InterfaceRequest<component::ApplicationEnvironment> request) {
          app_environment_request_ = std::move(request);
        });
    service_provider.AddService<component::ApplicationLauncher>(
        [this](fidl::InterfaceRequest<component::ApplicationLauncher> request) {
          app_launcher_request_ = std::move(request);
        });
    return std::make_unique<component::ApplicationContext>(
        service_provider.OpenAsDirectory(), zx::channel());
  }

  component::ServiceProviderBridge service_provider;
  std::unique_ptr<FakeCobaltEncoderFactoryImpl> factory_impl_;
  std::unique_ptr<FakeCobaltEncoderImpl> cobalt_encoder_;
  std::unique_ptr<component::ApplicationContext> app_context_;
  fidl::BindingSet<CobaltEncoderFactory> factory_bindings_;
  fidl::InterfaceRequest<component::ApplicationLauncher> app_launcher_request_;
  fidl::InterfaceRequest<component::ApplicationEnvironment>
      app_environment_request_;
  FXL_DISALLOW_COPY_AND_ASSIGN(CobaltTest);
};

TEST_F(CobaltTest, InitializeCobalt) {
  CobaltContext* cobalt_context = nullptr;
  auto ac = InitializeCobalt(async_get_default(), app_context(),
                             kFakeCobaltProjectId, &cobalt_context);
  EXPECT_NE(cobalt_context, nullptr);
  ac.call();
  EXPECT_EQ(cobalt_context, nullptr);
}

TEST_F(CobaltTest, ReportIndexObservation) {
  Value value;
  value.set_index_value(123);
  CobaltObservation observation(kFakeCobaltMetricId, kFakeCobaltEncodingId,
                                CloneType(value));
  CobaltContext* cobalt_context = nullptr;
  auto ac = InitializeCobalt(async_get_default(), app_context(),
                             kFakeCobaltProjectId, &cobalt_context);
  ReportObservation(observation, cobalt_context);
  RunLoopUntilIdle();
  cobalt_encoder()->ExpectCalledOnceWith("AddObservation", value);
}

TEST_F(CobaltTest, ReportIntObservation) {
  Value value;
  value.set_int_value(123);
  CobaltObservation observation(kFakeCobaltMetricId, kFakeCobaltEncodingId,
                                CloneType(value));
  CobaltContext* cobalt_context = nullptr;
  auto ac = InitializeCobalt(async_get_default(), app_context(),
                             kFakeCobaltProjectId, &cobalt_context);
  ReportObservation(observation, cobalt_context);
  RunLoopUntilIdle();
  cobalt_encoder()->ExpectCalledOnceWith("AddObservation", value);
}

TEST_F(CobaltTest, ReportDoubleObservation) {
  Value value;
  value.set_double_value(1.5);
  CobaltObservation observation(kFakeCobaltMetricId, kFakeCobaltEncodingId,
                                CloneType(value));
  CobaltContext* cobalt_context = nullptr;
  auto ac = InitializeCobalt(async_get_default(), app_context(),
                             kFakeCobaltProjectId, &cobalt_context);
  ReportObservation(observation, cobalt_context);
  RunLoopUntilIdle();
  cobalt_encoder()->ExpectCalledOnceWith("AddObservation", value);
}

TEST_F(CobaltTest, ReportStringObservation) {
  Value value;
  value.set_string_value("test");
  CobaltObservation observation(kFakeCobaltMetricId, kFakeCobaltEncodingId,
                                CloneType(value));
  CobaltContext* cobalt_context = nullptr;
  auto ac = InitializeCobalt(async_get_default(), app_context(),
                             kFakeCobaltProjectId, &cobalt_context);
  ReportObservation(observation, cobalt_context);
  RunLoopUntilIdle();
  cobalt_encoder()->ExpectCalledOnceWith("AddObservation", value);
}

TEST_F(CobaltTest, ReportIntBucketObservation) {
  Value value;
  auto distribution = fidl::VectorPtr<BucketDistributionEntry>::New(2);
  distribution->at(0).index = 1;
  distribution->at(0).count = 2;
  distribution->at(1).index = 2;
  distribution->at(1).count = 3;
  value.set_int_bucket_distribution(std::move(distribution));
  CobaltObservation observation(kFakeCobaltMetricId, kFakeCobaltEncodingId,
                                CloneType(value));
  CobaltContext* cobalt_context = nullptr;
  auto ac = InitializeCobalt(async_get_default(), app_context(),
                             kFakeCobaltProjectId, &cobalt_context);
  ReportObservation(observation, cobalt_context);
  RunLoopUntilIdle();
  cobalt_encoder()->ExpectCalledOnceWith("AddObservation", value);
}

TEST_F(CobaltTest, ReportMultipartObservation) {
  auto parts = fidl::VectorPtr<cobalt::ObservationValue>::New(2);
  parts->at(0).name = "part1";
  parts->at(0).encoding_id = kFakeCobaltEncodingId;
  parts->at(0).value.set_string_value("test");

  parts->at(1).name = "part2";
  parts->at(1).encoding_id = kFakeCobaltEncodingId;
  parts->at(1).value.set_int_value(2);

  CobaltObservation observation(static_cast<uint32_t>(kFakeCobaltMetricId),
                                CloneType(parts));
  CobaltContext* cobalt_context = nullptr;
  auto ac = InitializeCobalt(async_get_default(), app_context(),
                             kFakeCobaltProjectId, &cobalt_context);
  ReportObservation(observation, cobalt_context);
  RunLoopUntilIdle();
  cobalt_encoder()->ExpectCalledOnceWith("AddMultipartObservation", parts);
}

}  // namespace
}  // namespace cobalt
