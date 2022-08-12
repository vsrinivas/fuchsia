// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stddef.h>

#include <memory>

#include <gtest/gtest.h>

#include "src/sys/fuzzing/common/component-context.h"
#include "src/sys/fuzzing/common/input.h"
#include "src/sys/fuzzing/common/options.h"
#include "src/sys/fuzzing/common/testing/async-test.h"
#include "src/sys/fuzzing/realmfuzzer/engine/adapter-client.h"
#include "src/sys/fuzzing/realmfuzzer/engine/corpus.h"

// These tests replaces the engine when building a fuzzer test instead of a fuzzer.

namespace fuzzing {

class FuzzerTest : public AsyncTest {
 protected:
  void SetUp() override {
    AsyncTest::SetUp();
    options_ = MakeOptions();
  }

  const OptionsPtr& options() const { return options_; }

  std::unique_ptr<TargetAdapterClient> MakeClient() {
    auto context = ComponentContext::CreateWithExecutor(executor());
    auto client = std::make_unique<TargetAdapterClient>(context->executor());
    client->set_handler(context->MakeRequestHandler<TargetAdapter>());
    client->Configure(options_);
    return client;
  }

 private:
  OptionsPtr options_;
};

TEST_F(FuzzerTest, EmptyInputs) {
  auto client = MakeClient();

  // Should be able to handle empty inputs and repeated inputs.
  Input input;
  auto task = client->TestOneInput(input).and_then(client->TestOneInput(input));
  FUZZING_EXPECT_OK(std::move(task));
  RunUntilIdle();
}

TEST_F(FuzzerTest, SeedCorpus) {
  auto client = MakeClient();

  std::vector<std::string> parameters;
  FUZZING_EXPECT_OK(client->GetParameters(), &parameters);
  RunUntilIdle();

  auto seed_corpus_dirs = client->GetSeedCorpusDirectories(parameters);
  Corpus seed_corpus;
  seed_corpus.Configure(options());
  EXPECT_EQ(seed_corpus.Load(seed_corpus_dirs), ZX_OK);

  // Ensure only one call to |TestOneInput| is active at a time.
  auto task = fpromise::make_promise(
      [&, i = size_t(0), test_one = Future<>()](Context& context) mutable -> Result<> {
        while (true) {
          if (!test_one) {
            Input input;
            if (!seed_corpus.At(i, &input)) {
              return fpromise::ok();
            }
            test_one = client->TestOneInput(input);
          }
          if (!test_one(context)) {
            return fpromise::pending();
          }
          if (test_one.is_error()) {
            return fpromise::error();
          }
          test_one = nullptr;
          ++i;
        }
      });
  FUZZING_EXPECT_OK(std::move(task));
  RunUntilIdle();
}

}  // namespace fuzzing
