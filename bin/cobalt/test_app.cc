// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This application is intenteded to be used for manual testing of
// the Cobalt encoder client on Fuchsia by Cobalt engineers.
//
//
// To build:
// (1) Decide which Shuffler you want to send to and pass the URI
//     of this Shuffler to the ShufflerClient constructor on about
//     line 140. The two string constants in this file at the time
//     of this writing refer to the desktop computer and the personal
//     development GKE cluster of one of the developers and are not
//     appropriate for anybody else.
//
// (2) At the root of the fuchsia repo type:
//     source scripts/env.sh && envprompt
//     fset x86-64 --modules default,cobalt_client
// To run on qemu:
//     frun -m 3000 -g -k -N -u ./scripts/start-dhcp-server.sh
//
// To run:
// (3) From within the running instance of fuchsia type:
//     ./system/test/cobalt_testapp

#include <chrono>
#include <memory>
#include <string>
#include <utility>

#include "cobalt/config/metric_config.h"
#include "cobalt/config/report_config.h"
#include "cobalt/encoder/client_secret.h"
#include "cobalt/encoder/encoder.h"
#include "cobalt/encoder/envelope_maker.h"
#include "cobalt/encoder/project_context.h"
#include "cobalt/encoder/shuffler_client.h"
#include "grpc++/grpc++.h"
#include "lib/ftl/logging.h"

using cobalt::EncryptedMessage;
using cobalt::config::EncodingRegistry;
using cobalt::config::MetricRegistry;
using cobalt::encoder::ClientSecret;
using cobalt::encoder::Encoder;
using cobalt::encoder::EnvelopeMaker;
using cobalt::encoder::ProjectContext;
using cobalt::encoder::ShufflerClient;

// const char kLocalShufflerUri[] = "100.101.108.140:5001";
const char kCloudShufflerUri[] = "130.211.233.218:5001";

const int kFuchsiaCustomerId = 1;
const int kLedgerProjectId = 1;

const int kDailyRareEventCountMetric = 1;
const int kDailyRareEventCountEncoding = 1;
const char kRareEvent1Name[] = "Rare event 1";
// const char kRareEvent2Name[] = "Rare event 2"; // unused
const char kRareEvent3Name[] = "Rare event 3";

// This must be kept in sync with registered_metrics.txt in the Cobalt repo.
const char* kMetricConfigText = R"(
#####################################################################
# Metric (1, 1, 1)
# Name:  Daily rare event counts
# Description: Daily counts of several events that are expected to occur
#              rarely if ever.
# Parts: This metric has one part name "Event name"
# Notes: At least initially, we plan to use Basic RAPPOR with no privacy to
#        collect this metric. Each category will be one of the rare events.
######################################################################
element {
  customer_id: 1
  project_id: 1
  id: 1
  time_zone_policy: UTC
  parts {
    key: "Event name"
    value {
    }
  }
}

)";

const char* kEncodingConfigText = R"(
# customer: Fuchsia
# project:  Ledger
# Encoding: Basic RAPPOR with no random noise for Metric 1.
element {
  customer_id: 1
  project_id: 1
  id: 1
  basic_rappor {
    prob_0_becomes_1: 0.0
    prob_1_stays_1: 1.0
    string_categories: {
      category: "Rare event 1"
      category: "Rare event 2"
      category: "Rare event 3"
    }
  }
}

)";

int main(int argc, char* argv[]) {
  // Parse the metric config string
  auto metric_parse_result =
      MetricRegistry::FromString(kMetricConfigText, nullptr);
  FTL_CHECK(cobalt::config::kOK == metric_parse_result.second);
  std::shared_ptr<MetricRegistry> metric_registry(
      metric_parse_result.first.release());

  // Parse the encoding config string
  auto encoding_parse_result =
      EncodingRegistry::FromString(kEncodingConfigText, nullptr);
  FTL_CHECK(cobalt::config::kOK == encoding_parse_result.second);
  std::shared_ptr<EncodingRegistry> encoding_registry(
      encoding_parse_result.first.release());

  // Create a ProjectContext
  std::shared_ptr<ProjectContext> project_context(
      new ProjectContext(kFuchsiaCustomerId, kLedgerProjectId, metric_registry,
                         encoding_registry));

  // Create an Encoder with a new client secret.
  Encoder encoder(project_context, ClientSecret::GenerateNewSecret());

  // Create an EnvelopeMaker that doesn't do any encryption.
  EnvelopeMaker envelope_maker("", EncryptedMessage::NONE, "",
                               EncryptedMessage::NONE);

  // Add 7 observations of rare event 1 to the envelope.
  for (int i = 0; i < 7; i++) {
    auto result = encoder.EncodeString(kDailyRareEventCountMetric,
      kDailyRareEventCountEncoding, kRareEvent1Name);
    FTL_CHECK(Encoder::kOK == result.status);
    envelope_maker.AddObservation(*result.observation, std::move(result.metadata));
  }

  // Add 1 observation of rare event 3 to the envelope.
  for (int i = 0; i < 1; i++) {
    auto result = encoder.EncodeString(kDailyRareEventCountMetric,
      kDailyRareEventCountEncoding, kRareEvent3Name);
    FTL_CHECK(Encoder::kOK == result.status);
    envelope_maker.AddObservation(*result.observation, std::move(result.metadata));
  }

  // Encrypt the envelope.
  EncryptedMessage encrypted_envelope;
  FTL_CHECK(envelope_maker.MakeEncryptedEnvelope(&encrypted_envelope));

  // Create a ShufflerClient
  ShufflerClient shuffler_client(kCloudShufflerUri, false);

  // Send the encrypted envelope to the Shuffler.
  std::unique_ptr<grpc::ClientContext> context(new grpc::ClientContext());
  context->set_deadline(std::chrono::system_clock::now() +
                        std::chrono::seconds(2));
  auto status =
      shuffler_client.SendToShuffler(encrypted_envelope, context.get());
  FTL_CHECK(status.ok()) << status.error_code() << " " << status.error_message();

  exit(0);
}
