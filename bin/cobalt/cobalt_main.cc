// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <chrono>
#include <memory>
#include <string>
#include <thread>
#include <utility>

#include "application/lib/app/application_context.h"
#include "apps/cobalt_client/services/cobalt.fidl.h"
#include "apps/cobalt_client/src/config.h"
#include "cobalt/config/metric_config.h"
#include "cobalt/config/report_config.h"
#include "cobalt/encoder/client_secret.h"
#include "cobalt/encoder/encoder.h"
#include "cobalt/encoder/envelope_maker.h"
#include "cobalt/encoder/project_context.h"
#include "cobalt/encoder/shuffler_client.h"
#include "grpc++/grpc++.h"
#include "lib/fidl/cpp/bindings/binding.h"
#include "lib/ftl/logging.h"
#include "lib/ftl/macros.h"
#include "lib/mtl/tasks/message_loop.h"

namespace {

using cobalt::EncryptedMessage;
using cobalt::config::EncodingRegistry;
using cobalt::config::MetricRegistry;
using cobalt::encoder::ClientSecret;
using cobalt::encoder::Encoder;
using cobalt::encoder::EnvelopeMaker;
using cobalt::encoder::ProjectContext;
using cobalt::encoder::ShufflerClient;

using namespace cobalt;

// TODO(azani): Change to DNS-looked-up address.
const char kCloudShufflerUri[] = "130.211.218.95:5001";
const int32_t kFuchsiaCustomerId = 1;

// Returns whether or not an operation should be retried based on its returned
// status.
bool ShouldRetry(const grpc::Status& status) {
  switch (status.error_code()) {
    case grpc::ABORTED:
    case grpc::CANCELLED:
    case grpc::DEADLINE_EXCEEDED:
    case grpc::INTERNAL:
    case grpc::UNAVAILABLE:
      return true;

    default:
      return false;
  }
}

class CobaltEncoderImpl : public CobaltEncoder {
 public:
  CobaltEncoderImpl(std::unique_ptr<ProjectContext> project_context)
      // TODO(azani): Generate a client secret only once, store it persistently
      // and reuse it in future instances.
      : encoder_(std::move(project_context), ClientSecret::GenerateNewSecret()),
        // TODO(azani): Enable encryption.
        envelope_maker_("", EncryptedMessage::NONE, "", EncryptedMessage::NONE),
        // TODO(azani): Enable TLS.
        shuffler_client_(kCloudShufflerUri, false) {}

 private:
  void AddStringObservation(uint32_t metric_id,
                            uint32_t encoding_id,
                            const fidl::String& observation,
                            const AddStringObservationCallback& callback) {
    auto result = encoder_.EncodeString(metric_id, encoding_id, observation);
    switch (result.status) {
      case Encoder::kOK:
        break;
      case Encoder::kInvalidArguments:
        callback(Status::INVALID_ARGUMENTS);
        return;
      case Encoder::kInvalidConfig:
      case Encoder::kEncodingFailed:
        callback(Status::INTERNAL_ERROR);
        FTL_LOG(WARNING) << "Cobalt internal error: " << result.status;
        return;
    }

    envelope_maker_.AddObservation(*result.observation,
                                   std::move(result.metadata));
    callback(Status::OK);
  }

  void SendObservations(const SendObservationsCallback& callback) {
    if (envelope_maker_.envelope().batch_size() == 0) {
      FTL_LOG(WARNING) << "SendObservations without any added observations.";
      callback(Status::FAILED_PRECONDITION);
      return;
    }

    // Encrypt the envelope.
    EncryptedMessage encrypted_envelope;
    {
      bool status = envelope_maker_.MakeEncryptedEnvelope(&encrypted_envelope);
      if (!status) {
        FTL_LOG(WARNING) << "MakeEncryptedEnvelope failed.";
        callback(Status::INTERNAL_ERROR);
        return;
      }
    }

    // Send the encrypted envelope to the Shuffler.
    {
      Status status = SendToShuffler(encrypted_envelope);
      if (status == Status::OK) {
        envelope_maker_.Clear();
      }
      callback(status);
    }
  }

  Status SendToShuffler(EncryptedMessage& encrypted_envelope) {
    auto start_time = std::chrono::system_clock::now();
    grpc::Status status;
    // We retry multiple times with exponential backoff. We try up to 10
    // times or up to 80 seconds, whichever comes first. On receiving
    // DEADLINE_EXCEEDED we double the deadline. On receiving a
    // non-retryable error we give up.
    static const size_t kMaxAttempts = 10;
    static const int kMaxDurationSeconds = 80;
    int sleepmillis = 10;
    int deadline_seconds = 10;

    for (size_t attempt = 0; attempt < kMaxAttempts; attempt++) {
      std::unique_ptr<grpc::ClientContext> context(new grpc::ClientContext());
      context->set_deadline(std::chrono::system_clock::now() +
                            std::chrono::seconds(deadline_seconds));
      auto status =
          shuffler_client_.SendToShuffler(encrypted_envelope, context.get());
      if (status.error_code() == grpc::OK) {
        return Status::OK;
      } else {
        FTL_LOG(WARNING) << "Cobalt send failed with: [" << status.error_code()
                         << "] " << status.error_message() << ".";
      }

      if (!ShouldRetry(status)) {
        FTL_LOG(WARNING) << "Cobalt send had non-retryable error. Giving up.";
        return Status::SEND_FAILED;
      }

      if (status.error_code() == grpc::DEADLINE_EXCEEDED) {
        deadline_seconds *= 2;
      }

      std::chrono::duration<double> elapsed =
          std::chrono::system_clock::now() - start_time;
      int elapsed_seconds = int(elapsed.count());
      if (elapsed_seconds + deadline_seconds >= kMaxDurationSeconds) {
        FTL_LOG(WARNING)
            << "Multiple attempts to send Cobalt observations failed for "
            << elapsed_seconds << " seconds. Giving up.";
        return Status::SEND_FAILED;
      }

      FTL_LOG(INFO) << "Cobalt send: Will retry in " << sleepmillis
                    << "ms with an RPC deadline of " << deadline_seconds
                    << " seconds.";

      std::this_thread::sleep_for(std::chrono::milliseconds(sleepmillis));
      sleepmillis *= 2;
    }

    FTL_LOG(WARNING)
        << "Cobalt send failed too many times. Giving up. Last error: ["
        << status.error_code() << "] " << status.error_message();
    return Status::SEND_FAILED;
  }

  Encoder encoder_;
  EnvelopeMaker envelope_maker_;
  ShufflerClient shuffler_client_;

  FTL_DISALLOW_COPY_AND_ASSIGN(CobaltEncoderImpl);
};

class CobaltEncoderFactoryImpl : public CobaltEncoderFactory {
 public:
  CobaltEncoderFactoryImpl() {
    // Parse the metric config string
    auto metric_parse_result =
        MetricRegistry::FromString(kMetricConfigText, nullptr);
    FTL_CHECK(cobalt::config::kOK == metric_parse_result.second);
    metric_registry_.reset(metric_parse_result.first.release());

    // Parse the encoding config string
    auto encoding_parse_result =
        EncodingRegistry::FromString(kEncodingConfigText, nullptr);
    FTL_CHECK(cobalt::config::kOK == encoding_parse_result.second);
    encoding_registry_.reset(encoding_parse_result.first.release());
  }

 private:
  void GetEncoder(int32_t project_id,
                  fidl::InterfaceRequest<CobaltEncoder> request) {
    // Create a ProjectContext
    std::unique_ptr<ProjectContext> project_context(new ProjectContext(
        kFuchsiaCustomerId, project_id, metric_registry_, encoding_registry_));

    std::unique_ptr<CobaltEncoderImpl> cobalt_encoder_impl(
        new CobaltEncoderImpl(std::move(project_context)));
    cobalt_encoder_bindings_.AddBinding(std::move(cobalt_encoder_impl),
                                        std::move(request));
  }

  std::shared_ptr<MetricRegistry> metric_registry_;
  std::shared_ptr<EncodingRegistry> encoding_registry_;
  fidl::BindingSet<CobaltEncoder, std::unique_ptr<CobaltEncoder>>
      cobalt_encoder_bindings_;

  FTL_DISALLOW_COPY_AND_ASSIGN(CobaltEncoderFactoryImpl);
};

class CobaltApp {
 public:
  CobaltApp() : context_(app::ApplicationContext::CreateFromStartupInfo()) {
    std::unique_ptr<CobaltEncoderFactory> factory_impl(
        new CobaltEncoderFactoryImpl());
    factory_impl_.swap(factory_impl);

    // Singleton service
    context_->outgoing_services()->AddService<CobaltEncoderFactory>(
        [this](fidl::InterfaceRequest<CobaltEncoderFactory> request) {
          factory_bindings_.AddBinding(factory_impl_.get(), std::move(request));
        });
  }

 private:
  std::unique_ptr<app::ApplicationContext> context_;
  std::unique_ptr<CobaltEncoderFactory> factory_impl_;
  fidl::BindingSet<CobaltEncoderFactory> factory_bindings_;

  FTL_DISALLOW_COPY_AND_ASSIGN(CobaltApp);
};

}  // namespace

int main(int argc, const char** argv) {
  mtl::MessageLoop loop;
  CobaltApp app;
  loop.Run();
  return 0;
}
