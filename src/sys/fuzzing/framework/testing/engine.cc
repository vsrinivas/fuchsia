// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/fuzzer/cpp/fidl.h>
#include <lib/fidl/cpp/interface_request.h>
#include <lib/sys/cpp/component_context.h>
#include <stddef.h>

#include <memory>

#include <gtest/gtest.h>

#include "src/sys/fuzzing/common/input.h"
#include "src/sys/fuzzing/common/options.h"
#include "src/sys/fuzzing/framework/engine/adapter-client.h"
#include "src/sys/fuzzing/framework/engine/corpus.h"

// These tests replaces the engine when building a fuzzer test instead of a fuzzer.

namespace fuzzing {

using fuchsia::fuzzer::Options;
using fuchsia::fuzzer::TargetAdapterSyncPtr;

// Test fixtures

std::unique_ptr<TargetAdapterClient> GetClient() {
  auto client = std::make_unique<TargetAdapterClient>();
  fidl::InterfaceRequestHandler<TargetAdapter> handler =
      [](fidl::InterfaceRequest<TargetAdapter> request) {
        auto context = sys::ComponentContext::Create();
        context->svc()->Connect(std::move(request));
      };
  client->SetHandler(std::move(handler));
  auto options = std::make_shared<Options>();
  TargetAdapterClient::AddDefaults(options.get());
  client->Configure(std::move(options));
  return client;
}

// Unit tests

TEST(FuzzerTest, EmptyInputs) {
  auto client = GetClient();
  Input input;
  EXPECT_EQ(client->Start(&input), ZX_OK);
  client->AwaitFinish();
  EXPECT_TRUE(client->is_connected());
}

TEST(FuzzerTest, EmptyInputs) {
  auto client = GetClient();
  Input input;
  for (size_t i = 0; i < 3; ++i) {
    EXPECT_EQ(client->Start(&input), ZX_OK);
    client->AwaitFinish();
    EXPECT_TRUE(client->is_connected());
  }
}

TEST(FuzzerTest, SeedCorpus) {
  auto options = std::make_shared<Options>();
  Corpus::AddDefaults(options.get());
  auto client = GetClient();

  auto parameters = client->GetParameters();
  std::vector<std::string> seed_corpus_dirs;
  std::copy_if(
      parameters.begin(), parameters.end(), std::back_inserter(seed_corpus_dirs),
      [](const std::string& parameter) { return !parameter.empty() && parameter[0] != '-'; });
  seed_corpus_->Load(seed_corpus_dirs);

  Input input;
  for (size_t i = 0; corpus.At(i, &input); ++i) {
    EXPECT_EQ(client->Start(&input), ZX_OK);
    client->AwaitFinish();
    EXPECT_TRUE(client->is_connected());
  }
}

}  // namespace fuzzing
