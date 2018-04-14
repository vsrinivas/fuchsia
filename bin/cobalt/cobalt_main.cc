// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdlib.h>

#include <chrono>
#include <fstream>
#include <memory>
#include <string>
#include <thread>
#include <utility>

#include <fuchsia/cpp/cobalt.h>
#include <lib/async/cpp/task.h>

#include "config/cobalt_config.pb.h"
#include "garnet/bin/cobalt/product_hack.h"
#include "grpc++/grpc++.h"
#include "lib/app/cpp/application_context.h"
#include "lib/fidl/cpp/binding.h"
#include "lib/fsl/tasks/message_loop.h"
#include "lib/fxl/command_line.h"
#include "lib/fxl/log_settings_command_line.h"
#include "lib/fxl/logging.h"
#include "lib/fxl/macros.h"
#include "third_party/cobalt/config/client_config.h"
#include "third_party/cobalt/encoder/client_secret.h"
#include "third_party/cobalt/encoder/encoder.h"
#include "third_party/cobalt/encoder/project_context.h"
#include "third_party/cobalt/encoder/send_retryer.h"
#include "third_party/cobalt/encoder/shipping_manager.h"
#include "third_party/cobalt/encoder/shuffler_client.h"

namespace {

using cobalt::BucketDistributionEntry;
using cobalt::CobaltController;
using cobalt::CobaltEncoder;
using cobalt::CobaltEncoderFactory;
using cobalt::EncryptedMessage;
using cobalt::ObservationValue;
using cobalt::Status;
using cobalt::Value;
using cobalt::config::ClientConfig;
using cobalt::encoder::ClientSecret;
using cobalt::encoder::Encoder;
using cobalt::encoder::ProjectContext;
using cobalt::encoder::ShippingManager;
using cobalt::encoder::ShufflerClient;
using cobalt::encoder::SystemData;
using cobalt::encoder::send_retryer::SendRetryer;

// Command-line flags

// Used to override kScheduleIntervalDefault;
constexpr fxl::StringView kScheduleIntervalSecondsFlagName =
    "schedule_interval_seconds";

// Used to override kMinIntervalDefault;
constexpr fxl::StringView kMinIntervalSecondsFlagName = "min_interval_seconds";

const char kCloudShufflerUri[] = "shuffler.cobalt-api.fuchsia.com:443";
const int32_t kFuchsiaCustomerId = 1;

const size_t kMaxBytesPerEnvelope = 512 * 1024;  // 0.5 MiB.
const size_t kMaxBytesTotal = 1024 * 1024;       // 1 MiB
const size_t kMinEnvelopeSendSize = 10 * 1024;   // 10 K

// Because we don't yet persist Observations to local, non-volatile storage,
// we send accumulated Observations every 10 seconds. After persistence is
// implemented this value should be changed to something more like one hour.
const std::chrono::seconds kScheduleIntervalDefault(10);

// We send Observations to the Shuffler more frequently than kScheduleInterval
// under some circumstances, namely, if there is memory pressure or if we
// are explicitly asked to do so via the RequestSendSoon() method. This value
// is a safety parameter. We do not make two attempts within a period of this
// specified length.
const std::chrono::seconds kMinIntervalDefault(1);

// Each "send attempt" is actually a cycle of potential retries. These
// two parameters configure the SendRetryer.
const std::chrono::seconds kInitialRpcDeadline(10);
const std::chrono::seconds kDeadlinePerSendAttempt(60);

const char* kConfigBinProtoPath = "/pkg/data/cobalt_config.binproto";

// Maps a ShippingManager::Status to a cobalt::Status.
cobalt::Status ToCobaltStatus(ShippingManager::Status s) {
  switch (s) {
    case ShippingManager::kOk:
      return Status::OK;

    case ShippingManager::kObservationTooBig:
      return Status::OBSERVATION_TOO_BIG;

    case ShippingManager::kFull:
      return Status::TEMPORARILY_FULL;

    case ShippingManager::kShutDown:
    case ShippingManager::kEncryptionFailed:
      return Status::INTERNAL_ERROR;
  }
}

//////////////////////////////////////////////////////////
// class CobaltEncoderImpl
///////////////////////////////////////////////////////////

class CobaltEncoderImpl : public CobaltEncoder {
 public:
  // Does not take ownership of |shipping_manager| or |system_data|.
  CobaltEncoderImpl(std::unique_ptr<ProjectContext> project_context,
                    ClientSecret client_secret,
                    ShippingManager* shipping_manager,
                    const SystemData* system_data);

 private:
  template <class CB>
  void AddEncodedObservation(Encoder::Result* result, CB callback);

  void AddStringObservation(
      uint32_t metric_id,
      uint32_t encoding_id,
      fidl::StringPtr observation,
      AddStringObservationCallback callback) override;

  void AddIntObservation(uint32_t metric_id,
                         uint32_t encoding_id,
                         const int64_t observation,
                         AddIntObservationCallback callback) override;

  void AddDoubleObservation(
      uint32_t metric_id,
      uint32_t encoding_id,
      const double observation,
      AddDoubleObservationCallback callback) override;

  void AddIndexObservation(
      uint32_t metric_id,
      uint32_t encoding_id,
      uint32_t index,
      AddIndexObservationCallback callback) override;

  void AddObservation(uint32_t metric_id,
                      uint32_t encoding_id,
                      Value observation,
                      AddObservationCallback callback) override;

  void AddMultipartObservation(
      uint32_t metric_id,
      fidl::VectorPtr<ObservationValue> observation,
      AddMultipartObservationCallback callback) override;

  void AddIntBucketDistribution(
      uint32_t metric_id,
      uint32_t encoding_id,
      fidl::VectorPtr<BucketDistributionEntry> distribution,
      AddIntBucketDistributionCallback callback) override;

  void StartTimer(
      uint32_t metric_id,
      uint32_t encoding_id,
      fidl::StringPtr timer_id,
      uint64_t timestamp,
      uint32_t timeout_s,
      StartTimerCallback callback) override;

  void EndTimer(
      fidl::StringPtr timer_id,
      uint64_t timestamp,
      uint32_t timeout_s,
      EndTimerCallback callback) override;

  void EndTimerMultiPart(
      fidl::StringPtr timer_id,
      uint64_t timestamp,
      fidl::StringPtr part_name,
      fidl::VectorPtr<ObservationValue> observation,
      uint32_t timeout_s,
      EndTimerMultiPartCallback callback) override;

  void SendObservations(SendObservationsCallback callback) override;

  Encoder encoder_;
  ShippingManager* shipping_manager_;  // not owned

  FXL_DISALLOW_COPY_AND_ASSIGN(CobaltEncoderImpl);
};

CobaltEncoderImpl::CobaltEncoderImpl(
    std::unique_ptr<ProjectContext> project_context,
    ClientSecret client_secret,
    ShippingManager* shipping_manager,
    const SystemData* system_data)
    : encoder_(std::move(project_context),
               std::move(client_secret),
               system_data),
      shipping_manager_(shipping_manager) {}

template <class CB>
void CobaltEncoderImpl::AddEncodedObservation(Encoder::Result* result,
                                              CB callback) {
  switch (result->status) {
    case Encoder::kOK:
      break;
    case Encoder::kInvalidArguments:
      callback(Status::INVALID_ARGUMENTS);
      return;
    case Encoder::kInvalidConfig:
    case Encoder::kEncodingFailed:
      callback(Status::INTERNAL_ERROR);
      FXL_LOG(WARNING) << "Cobalt internal error: " << result->status;
      return;
  }

  Status status = ToCobaltStatus(shipping_manager_->AddObservation(
      *(result->observation), std::move(result->metadata)));
  callback(status);
}
void CobaltEncoderImpl::AddStringObservation(
    uint32_t metric_id,
    uint32_t encoding_id,
    fidl::StringPtr observation,
    AddStringObservationCallback callback) {
  auto result = encoder_.EncodeString(metric_id, encoding_id, observation);
  AddEncodedObservation(&result, callback);
}

void CobaltEncoderImpl::AddIntObservation(
    uint32_t metric_id,
    uint32_t encoding_id,
    const int64_t observation,
    AddIntObservationCallback callback) {
  auto result = encoder_.EncodeInt(metric_id, encoding_id, observation);
  AddEncodedObservation(&result, callback);
}

void CobaltEncoderImpl::AddDoubleObservation(
    uint32_t metric_id,
    uint32_t encoding_id,
    const double observation,
    AddDoubleObservationCallback callback) {
  auto result = encoder_.EncodeDouble(metric_id, encoding_id, observation);
  AddEncodedObservation(&result, callback);
}

void CobaltEncoderImpl::AddIndexObservation(
    uint32_t metric_id,
    uint32_t encoding_id,
    uint32_t index,
    AddIndexObservationCallback callback) {
  auto result = encoder_.EncodeIndex(metric_id, encoding_id, index);
  AddEncodedObservation(&result, callback);
}

void CobaltEncoderImpl::AddObservation(uint32_t metric_id,
                                       uint32_t encoding_id,
                                       Value observation,
                                       AddObservationCallback callback) {
  switch (observation.Which()) {
    case Value::Tag::kStringValue: {
      AddStringObservation(metric_id, encoding_id,
                           observation.string_value(), callback);
      break;
    }
    case Value::Tag::kIntValue: {
      AddIntObservation(metric_id, encoding_id, observation.int_value(),
                        callback);
      break;
    }
    case Value::Tag::kDoubleValue: {
      AddDoubleObservation(metric_id, encoding_id,
                           observation.double_value(), callback);
      break;
    }
    case Value::Tag::kIndexValue: {
      AddIndexObservation(metric_id, encoding_id,
                          observation.index_value(), callback);
      break;
    }
    case Value::Tag::kIntBucketDistribution: {
      AddIntBucketDistribution(
          metric_id, encoding_id,
          std::move(observation.int_bucket_distribution()), callback);
      break;
    }
    default:
      callback(Status::INVALID_ARGUMENTS);
      FXL_LOG(ERROR) << "Cobalt: Unrecognized value type in observation.";
      return;
  }
}

void CobaltEncoderImpl::AddMultipartObservation(
    uint32_t metric_id,
    fidl::VectorPtr<ObservationValue> observation,
    AddMultipartObservationCallback callback) {
  Encoder::Value value;
  for (const auto& obs_val : *observation) {
    switch (obs_val.value.Which()) {
      case Value::Tag::kStringValue: {
        value.AddStringPart(obs_val.encoding_id, obs_val.name,
                            obs_val.value.string_value());
        break;
      }
      case Value::Tag::kIntValue: {
        value.AddIntPart(obs_val.encoding_id, obs_val.name,
                         obs_val.value.int_value());
        break;
      }
      case Value::Tag::kDoubleValue: {
        value.AddDoublePart(obs_val.encoding_id, obs_val.name,
                            obs_val.value.double_value());
        break;
      }
      case Value::Tag::kIndexValue: {
        value.AddIndexPart(obs_val.encoding_id, obs_val.name,
                           obs_val.value.index_value());
        break;
      }
      case Value::Tag::kIntBucketDistribution: {
        std::map<uint32_t, uint64_t> distribution_map;
        for (auto it = obs_val.value.int_bucket_distribution()->begin();
             obs_val.value.int_bucket_distribution()->end() != it; it++) {
          distribution_map[(*it).index] = (*it).count;
        }
        value.AddIntBucketDistributionPart(obs_val.encoding_id, obs_val.name,
                                           distribution_map);
        break;
      }
      default:
        callback(Status::INVALID_ARGUMENTS);
        FXL_LOG(ERROR)
            << "Cobalt: Unrecognized value type for observation part "
            << obs_val.name;
        return;
    }
  }
  auto result = encoder_.Encode(metric_id, value);
  AddEncodedObservation(&result, callback);
}

void CobaltEncoderImpl::AddIntBucketDistribution(
    uint32_t metric_id,
    uint32_t encoding_id,
    fidl::VectorPtr<BucketDistributionEntry> distribution,
    AddIntBucketDistributionCallback callback) {
  std::map<uint32_t, uint64_t> distribution_map;
  for (auto it = distribution->begin(); distribution->end() != it; it++) {
    distribution_map[(*it).index] = (*it).count;
  }
  auto result = encoder_.EncodeIntBucketDistribution(metric_id, encoding_id,
                                                     distribution_map);
  AddEncodedObservation(&result, callback);
}

void CobaltEncoderImpl::StartTimer(
    uint32_t metric_id,
    uint32_t encoding_id,
    fidl::StringPtr timer_id,
    uint64_t timestamp,
    uint32_t timeout_s,
    StartTimerCallback callback) {
  // TODO(ninai)
  callback(Status::OK);
}

void CobaltEncoderImpl::EndTimer(
    fidl::StringPtr timer_id,
    uint64_t timestamp,
    uint32_t timeout_s,
    EndTimerCallback callback) {
  // TODO(ninai)
  callback(Status::OK);
}

void CobaltEncoderImpl::EndTimerMultiPart(
    fidl::StringPtr timer_id,
    uint64_t timestamp,
    fidl::StringPtr part_name,
    fidl::VectorPtr<ObservationValue> observation,
    uint32_t timeout_s,
    EndTimerMultiPartCallback callback) {
  // TODO(ninai)
  callback(Status::OK);
}

void CobaltEncoderImpl::SendObservations(SendObservationsCallback callback) {
  callback(Status::OK);
}

///////////////////////////////////////////////////////////
// class CobaltControllerImpl
///////////////////////////////////////////////////////////
class CobaltControllerImpl : public CobaltController {
 public:
  // Does not take ownerhsip of |shipping_manager|.
  CobaltControllerImpl(async_t* async, ShippingManager* shipping_manager);

 private:
  void RequestSendSoon(RequestSendSoonCallback callback) override;

  void BlockUntilEmpty(uint32_t max_wait_seconds,
                       BlockUntilEmptyCallback callback) override;

  void NumSendAttempts(NumSendAttemptsCallback callback) override;

  void FailedSendAttempts(FailedSendAttemptsCallback callback) override;

  async_t* const async_;
  ShippingManager* shipping_manager_;  // not owned

  FXL_DISALLOW_COPY_AND_ASSIGN(CobaltControllerImpl);
};

CobaltControllerImpl::CobaltControllerImpl(
    async_t* async,
    ShippingManager* shipping_manager)
    : async_(async), shipping_manager_(shipping_manager) {}

void CobaltControllerImpl::RequestSendSoon(RequestSendSoonCallback callback) {
  // callback_adapter invokes |callback| on the main thread.
  std::function<void(bool)> callback_adapter = [this, callback](bool success) {
    async::PostTask(async_, [callback, success]() { callback(success); });
  };
  shipping_manager_->RequestSendSoon(callback_adapter);
}

void CobaltControllerImpl::BlockUntilEmpty(uint32_t max_wait_seconds,
                                           BlockUntilEmptyCallback callback) {
  shipping_manager_->WaitUntilIdle(std::chrono::seconds(max_wait_seconds));
  callback();
}

void CobaltControllerImpl::NumSendAttempts(NumSendAttemptsCallback callback) {
  callback(shipping_manager_->num_send_attempts());
}

void CobaltControllerImpl::FailedSendAttempts(
    FailedSendAttemptsCallback callback) {
  callback(shipping_manager_->num_failed_attempts());
}

///////////////////////////////////////////////////////////
// class CobaltEncoderFactoryImpl
///////////////////////////////////////////////////////////

class CobaltEncoderFactoryImpl : public CobaltEncoderFactory {
 public:
  // Does not take ownerhsip of |shipping_manager| or |system_data|.
  CobaltEncoderFactoryImpl(std::shared_ptr<ClientConfig> client_config,
                           ClientSecret client_secret,
                           ShippingManager* shipping_manager,
                           const SystemData* system_data);

 private:
  void GetEncoder(int32_t project_id,
                  fidl::InterfaceRequest<CobaltEncoder> request);

  std::shared_ptr<ClientConfig> client_config_;
  ClientSecret client_secret_;
  fidl::BindingSet<CobaltEncoder, std::unique_ptr<CobaltEncoder>>
      cobalt_encoder_bindings_;
  ShippingManager* shipping_manager_;  // not owned
  const SystemData* system_data_;      // not owned

  FXL_DISALLOW_COPY_AND_ASSIGN(CobaltEncoderFactoryImpl);
};

CobaltEncoderFactoryImpl::CobaltEncoderFactoryImpl(
    std::shared_ptr<ClientConfig> client_config,
    ClientSecret client_secret,
    ShippingManager* shipping_manager,
    const SystemData* system_data)
    : client_config_(client_config),
      client_secret_(std::move(client_secret)),
      shipping_manager_(shipping_manager),
      system_data_(system_data) {}

void CobaltEncoderFactoryImpl::GetEncoder(
    int32_t project_id,
    fidl::InterfaceRequest<CobaltEncoder> request) {
  std::unique_ptr<ProjectContext> project_context(
      new ProjectContext(kFuchsiaCustomerId, project_id, client_config_));

  std::unique_ptr<CobaltEncoderImpl> cobalt_encoder_impl(
      new CobaltEncoderImpl(std::move(project_context), client_secret_,
                            shipping_manager_, system_data_));
  cobalt_encoder_bindings_.AddBinding(std::move(cobalt_encoder_impl),
                                      std::move(request));
}

///////////////////////////////////////////////////////////
// class CobaltApp
///////////////////////////////////////////////////////////

class CobaltApp {
 public:
  CobaltApp(async_t* async,
            std::chrono::seconds schedule_interval,
            std::chrono::seconds min_interval,
            const std::string& product_name);

 private:
  static ClientSecret getClientSecret();

  SystemData system_data_;

  std::unique_ptr<component::ApplicationContext> context_;

  ShufflerClient shuffler_client_;
  SendRetryer send_retryer_;
  ShippingManager shipping_manager_;

  std::shared_ptr<ClientConfig> client_config_;

  std::unique_ptr<CobaltController> controller_impl_;
  fidl::BindingSet<CobaltController> controller_bindings_;

  std::unique_ptr<CobaltEncoderFactory> factory_impl_;
  fidl::BindingSet<CobaltEncoderFactory> factory_bindings_;

  FXL_DISALLOW_COPY_AND_ASSIGN(CobaltApp);
};

CobaltApp::CobaltApp(async_t* async,
                     std::chrono::seconds schedule_interval,
                     std::chrono::seconds min_interval,
                     const std::string& product_name)
    : system_data_(product_name),
      context_(component::ApplicationContext::CreateFromStartupInfo()),
      shuffler_client_(kCloudShufflerUri, true),
      send_retryer_(&shuffler_client_),
      shipping_manager_(
          ShippingManager::SizeParams(cobalt::kMaxBytesPerObservation,
                                      kMaxBytesPerEnvelope, kMaxBytesTotal,
                                      kMinEnvelopeSendSize),
          ShippingManager::ScheduleParams(schedule_interval, min_interval),
          // TODO(rudominer): Enable encryption.
          ShippingManager::EnvelopeMakerParams("", EncryptedMessage::NONE, "",
                                               EncryptedMessage::NONE),
          ShippingManager::SendRetryerParams(kInitialRpcDeadline,
                                             kDeadlinePerSendAttempt),
          &send_retryer_),
      controller_impl_(new CobaltControllerImpl(async, &shipping_manager_)) {
  shipping_manager_.Start();

  // Open the cobalt config file.
  std::ifstream config_file_stream;
  config_file_stream.open(kConfigBinProtoPath);
  FXL_CHECK(config_file_stream && config_file_stream.good())
      << "Could not open the Cobalt config file: " << kConfigBinProtoPath;
  std::string cobalt_config_bytes;
  cobalt_config_bytes.assign(
      (std::istreambuf_iterator<char>(config_file_stream)),
      std::istreambuf_iterator<char>());
  FXL_CHECK(!cobalt_config_bytes.empty())
      << "Could not read the Cobalt config file: " << kConfigBinProtoPath;

  // Parse the data as a CobaltConfig, then extract the metric and encoding
  // configs and construct a ClientConfig to house them.
  client_config_.reset(
      ClientConfig::CreateFromCobaltConfigBytes(cobalt_config_bytes).release());
  FXL_CHECK(client_config_)
      << "Could not parse the Cobalt config file: " << kConfigBinProtoPath;

  factory_impl_.reset(new CobaltEncoderFactoryImpl(
      client_config_, getClientSecret(), &shipping_manager_, &system_data_));

  context_->outgoing_services()->AddService<CobaltEncoderFactory>(
      [this](fidl::InterfaceRequest<CobaltEncoderFactory> request) {
        factory_bindings_.AddBinding(factory_impl_.get(), std::move(request));
      });

  context_->outgoing_services()->AddService<CobaltController>(
      [this](fidl::InterfaceRequest<CobaltController> request) {
        controller_bindings_.AddBinding(controller_impl_.get(),
                                        std::move(request));
      });
}

ClientSecret CobaltApp::getClientSecret() {
  // TODO(rudominer): Generate a client secret only once, store it
  // persistently and reuse it in future instances.
  return ClientSecret::GenerateNewSecret();
}

}  // namespace

int main(int argc, const char** argv) {
  setenv("GRPC_DEFAULT_SSL_ROOTS_FILE_PATH", "/config/ssl/cert.pem", 1);

  // Parse the flags.
  const auto command_line = fxl::CommandLineFromArgcArgv(argc, argv);
  fxl::SetLogSettingsFromCommandLine(command_line);

  if (fxl::GetVlogVerbosity() >= 10) {
    setenv("GRPC_VERBOSITY", "DEBUG", 1);
    setenv("GRPC_TRACE", "all,-timer,-timer_check", 1);
  }

  // Parse the schedule_interval_seconds flag.
  std::chrono::seconds schedule_interval = kScheduleIntervalDefault;
  std::string flag_value;
  if (command_line.GetOptionValue(kScheduleIntervalSecondsFlagName,
                                  &flag_value)) {
    int num_seconds = std::stoi(flag_value);
    if (num_seconds > 0) {
      schedule_interval = std::chrono::seconds(num_seconds);
    }
  }

  // Parse the min_interval_seconds flag.
  std::chrono::seconds min_interval = kMinIntervalDefault;
  flag_value.clear();
  if (command_line.GetOptionValue(kMinIntervalSecondsFlagName, &flag_value)) {
    int num_seconds = std::stoi(flag_value);
    // We allow min_interval = 0.
    if (num_seconds >= 0) {
      min_interval = std::chrono::seconds(num_seconds);
    }
  }

  FXL_LOG(INFO) << "Cobalt client schedule params: schedule_interval="
                << schedule_interval.count()
                << " seconds, min_interval=" << min_interval.count()
                << " seconds.";

  fsl::MessageLoop loop;
  CobaltApp app(loop.async(), schedule_interval, min_interval,
                cobalt::hack::GetLayer());
  loop.Run();
  return 0;
}
