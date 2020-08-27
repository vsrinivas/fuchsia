// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "data-provider.h"

#include <lib/sys/cpp/testing/component_context_provider.h>

#include <memory>
#include <thread>

#include <gtest/gtest.h>

#include "test/test-data-provider.h"

namespace fuzzing {

using ::fuchsia::fuzzer::DataProviderPtr;

////////////////////////////////////////////////////////////////////////////////////////////////////
// Test fixture

class DataProviderTest : public ::testing::Test {
 public:
  const char *as_str(const TestInput &input) {
    char terminator = '\0';
    input.Write(&terminator, sizeof(terminator));
    return reinterpret_cast<const char *>(input.data());
  }

 protected:
  TestInput fuzzer_input_;
  TestDataProvider data_provider_;
};

////////////////////////////////////////////////////////////////////////////////////////////////////
// Unit tests

TEST_F(DataProviderTest, Configure) {
  // Nothing is mapped initially.
  EXPECT_FALSE(data_provider_.HasLabel(""));
  EXPECT_FALSE(data_provider_.IsMapped(""));

  zx::vmo vmo;
  EXPECT_EQ(fuzzer_input_.Create(), ZX_OK);
  EXPECT_EQ(fuzzer_input_.Share(&vmo), ZX_OK);

  std::vector<std::string> labels{"foo", "bar", "baz"};
  data_provider_.Configure(std::move(vmo), labels, []() {});

  EXPECT_TRUE(data_provider_.HasLabel(""));
  EXPECT_TRUE(data_provider_.IsMapped(""));

  for (const std::string &label : labels) {
    EXPECT_TRUE(data_provider_.HasLabel(label));
    EXPECT_FALSE(data_provider_.IsMapped(label));
  }

  EXPECT_FALSE(data_provider_.HasLabel("qux"));
}

TEST_F(DataProviderTest, AddConsumer) {
  zx::vmo vmo;
  EXPECT_EQ(fuzzer_input_.Create(), ZX_OK);
  EXPECT_EQ(fuzzer_input_.Share(&vmo), ZX_OK);

  // Labels are unregnozied without a call to |Configure".
  std::vector<std::string> labels{"foo", "bar", "baz"};
  std::map<std::string, TestInput> inputs;
  zx_status_t status;
  for (const std::string &label : labels) {
    TestInput *input = &inputs[label];
    EXPECT_EQ(input->Create(), ZX_OK);
    EXPECT_EQ(input->Share(&vmo), ZX_OK);
    data_provider_.AddConsumer(label, std::move(vmo), [&status](zx_status_t res) { status = res; });
    EXPECT_EQ(status, ZX_ERR_INVALID_ARGS);
  }
  for (const std::string &label : labels) {
    EXPECT_FALSE(data_provider_.HasLabel(label));
    EXPECT_FALSE(data_provider_.IsMapped(label));
  }

  // Valid
  data_provider_.Configure(std::move(vmo), labels, []() {});
  for (auto &[label, input] : inputs) {
    EXPECT_EQ(input.Share(&vmo), ZX_OK);
    data_provider_.AddConsumer(label, std::move(vmo), [&status](zx_status_t res) { status = res; });
    EXPECT_EQ(status, ZX_OK);
  }
  for (const std::string &label : labels) {
    EXPECT_TRUE(data_provider_.HasLabel(label));
    EXPECT_TRUE(data_provider_.IsMapped(label));
  }
}

TEST_F(DataProviderTest, PartitionTestInput) {
  zx::vmo vmo;
  EXPECT_EQ(fuzzer_input_.Create(), ZX_OK);
  EXPECT_EQ(fuzzer_input_.Share(&vmo), ZX_OK);

  std::vector<std::string> labels;
  data_provider_.Configure(std::move(vmo), labels, []() {});

  // No labels provided
  std::string data = "AB#[foo]CD#[bar]EF";
  EXPECT_EQ(data_provider_.PartitionTestInput(data.c_str(), data.size()), ZX_OK);
  EXPECT_EQ(fuzzer_input_.size(), data.size());
  EXPECT_STREQ(as_str(fuzzer_input_), data.c_str());

  // One of each label
  std::map<std::string, TestInput> inputs;
  labels.push_back("foo");
  labels.push_back("bar");

  fuzzer_input_.Share(&vmo);
  data_provider_.Reset();
  data_provider_.Configure(std::move(vmo), labels, []() {});

  zx_status_t status;
  for (const std::string &label : labels) {
    EXPECT_EQ(inputs[label].Create(), ZX_OK);
    EXPECT_EQ(inputs[label].Share(&vmo), ZX_OK);
    data_provider_.AddConsumer(label, std::move(vmo), [&status](zx_status_t res) { status = res; });
    EXPECT_EQ(status, ZX_OK);
  }

  data = "AB#[foo]CD#[bar]EF";
  EXPECT_EQ(data_provider_.PartitionTestInput(data.c_str(), data.size()), ZX_OK);
  EXPECT_EQ(fuzzer_input_.size(), 2u);
  EXPECT_STREQ(as_str(fuzzer_input_), "AB");
  EXPECT_EQ(inputs["foo"].size(), 2u);
  EXPECT_STREQ(as_str(inputs["foo"]), "CD");
  EXPECT_EQ(inputs["bar"].size(), 2u);
  EXPECT_STREQ(as_str(inputs["bar"]), "EF");

  // Not all labels present
  data = "ABCD#[bar]EF";
  EXPECT_EQ(data_provider_.PartitionTestInput(data.c_str(), data.size()), ZX_OK);
  EXPECT_EQ(fuzzer_input_.size(), 4u);
  EXPECT_STREQ(as_str(fuzzer_input_), "ABCD");
  EXPECT_EQ(inputs["foo"].size(), 0u);
  EXPECT_EQ(inputs["bar"].size(), 2u);
  EXPECT_STREQ(as_str(inputs["bar"]), "EF");

  // Repeated label
  data = "AB#[foo]C#[bar]D#[foo]EF";
  EXPECT_EQ(data_provider_.PartitionTestInput(data.c_str(), data.size()), ZX_OK);
  EXPECT_EQ(fuzzer_input_.size(), 2u);
  EXPECT_STREQ(as_str(fuzzer_input_), "AB");
  EXPECT_EQ(inputs["foo"].size(), 3u);
  EXPECT_STREQ(as_str(inputs["foo"]), "CEF");
  EXPECT_EQ(inputs["bar"].size(), 1u);
  EXPECT_STREQ(as_str(inputs["bar"]), "D");

  // Unrecognized label
  data = "AB#[foo]CD#[baz]EF";
  EXPECT_EQ(data_provider_.PartitionTestInput(data.c_str(), data.size()), ZX_OK);
  EXPECT_EQ(fuzzer_input_.size(), 2u);
  EXPECT_STREQ(as_str(fuzzer_input_), "AB");
  EXPECT_EQ(inputs["foo"].size(), 10u);
  EXPECT_STREQ(as_str(inputs["foo"]), "CD#[baz]EF");
  EXPECT_EQ(inputs["bar"].size(), 0u);

  // Escaped label
  data = "AB##[foo]CD#[foo]EF";
  EXPECT_EQ(data_provider_.PartitionTestInput(data.c_str(), data.size()), ZX_OK);
  EXPECT_EQ(fuzzer_input_.size(), 10u);
  EXPECT_STREQ(as_str(fuzzer_input_), "AB#[foo]CD");
  EXPECT_EQ(inputs["foo"].size(), 2u);
  EXPECT_STREQ(as_str(inputs["foo"]), "EF");
  EXPECT_EQ(inputs["bar"].size(), 0u);

  // Adjacent labels
  data = "ABC#[foo]#[bar]DEF";
  EXPECT_EQ(data_provider_.PartitionTestInput(data.c_str(), data.size()), ZX_OK);
  EXPECT_EQ(fuzzer_input_.size(), 3u);
  EXPECT_STREQ(as_str(fuzzer_input_), "ABC");
  EXPECT_EQ(inputs["foo"].size(), 0u);
  EXPECT_EQ(inputs["bar"].size(), 3u);
  EXPECT_STREQ(as_str(inputs["bar"]), "DEF");

  // Null data
  EXPECT_EQ(data_provider_.PartitionTestInput(nullptr, data.size()), ZX_OK);
  EXPECT_EQ(fuzzer_input_.size(), 0u);
  EXPECT_EQ(inputs["foo"].size(), 0u);
  EXPECT_EQ(inputs["bar"].size(), 0u);

  // Zero size
  data = "ABC#[foo]#[bar]DEF";
  EXPECT_EQ(data_provider_.PartitionTestInput(data.c_str(), 0), ZX_OK);
  EXPECT_EQ(fuzzer_input_.size(), 0u);
  EXPECT_EQ(inputs["foo"].size(), 0u);
  EXPECT_EQ(inputs["bar"].size(), 0u);

  // Empty label
  data = "AB#[foo]CD#[]EF";
  EXPECT_EQ(data_provider_.PartitionTestInput(data.c_str(), data.size()), ZX_OK);
  EXPECT_EQ(fuzzer_input_.size(), 4u);
  EXPECT_STREQ(as_str(fuzzer_input_), "ABEF");
  EXPECT_EQ(inputs["foo"].size(), 2u);
  EXPECT_STREQ(as_str(inputs["foo"]), "CD");
  EXPECT_EQ(inputs["bar"].size(), 0u);

  // Open label at end
  data = "AB#[foo]CDEF#[bar";
  EXPECT_EQ(data_provider_.PartitionTestInput(data.c_str(), data.size()), ZX_OK);
  EXPECT_EQ(fuzzer_input_.size(), 2u);
  EXPECT_STREQ(as_str(fuzzer_input_), "AB");
  EXPECT_EQ(inputs["foo"].size(), 9u);
  EXPECT_STREQ(as_str(inputs["foo"]), "CDEF#[bar");
  EXPECT_EQ(inputs["bar"].size(), 0u);

  // Ends with #.
  data = "AB#[foo]CDEF#";
  EXPECT_EQ(data_provider_.PartitionTestInput(data.c_str(), data.size()), ZX_OK);
  EXPECT_EQ(fuzzer_input_.size(), 2u);
  EXPECT_STREQ(as_str(fuzzer_input_), "AB");
  EXPECT_EQ(inputs["foo"].size(), 5u);
  EXPECT_STREQ(as_str(inputs["foo"]), "CDEF#");
  EXPECT_EQ(inputs["bar"].size(), 0u);
}

TEST_F(DataProviderTest, CompleteIteration) {
  zx::vmo vmo;
  EXPECT_EQ(fuzzer_input_.Create(), ZX_OK);
  EXPECT_EQ(fuzzer_input_.Share(&vmo), ZX_OK);

  std::vector<std::string> labels;
  labels.push_back("foo");
  labels.push_back("bar");

  std::string data = "ABEF";
  zx_status_t status = ZX_ERR_STOP;  // Not used by |PartitionTestInput|.
  std::thread t1([this, &data, &status]() {
    // Blocks until the call to |Configure|.
    status = data_provider_.PartitionTestInput(data.c_str(), data.size());
  });

  EXPECT_EQ(status, ZX_ERR_STOP);
  data_provider_.Configure(std::move(vmo), labels, []() {});
  t1.join();
  EXPECT_EQ(status, ZX_OK);
  zx_signals_t observed = 0;
  EXPECT_EQ(fuzzer_input_.vmo().wait_one(kInIteration, zx::time(0), &observed), ZX_OK);
  EXPECT_EQ(observed & kBetweenIterations, 0u);

  // Configure again without a call to |PartitionTestInput|; this should leave the fuzzer input
  // between iterations.
  EXPECT_EQ(fuzzer_input_.Share(&vmo), ZX_OK);
  data_provider_.Reset();
  data_provider_.Configure(std::move(vmo), labels, []() {});
  EXPECT_EQ(fuzzer_input_.vmo().wait_one(kBetweenIterations, zx::time(0), &observed), ZX_OK);
  EXPECT_EQ(observed & kInIteration, 0u);
  EXPECT_EQ(data_provider_.PartitionTestInput(data.c_str(), data.size()), ZX_OK);
  EXPECT_EQ(fuzzer_input_.vmo().wait_one(kInIteration, zx::time(0), &observed), ZX_OK);
  EXPECT_EQ(observed & kBetweenIterations, 0u);

  // Add more consumers.
  std::map<std::string, TestInput> inputs;
  for (const std::string &label : labels) {
    TestInput *input = &inputs[label];
    EXPECT_EQ(input->Create(), ZX_OK);
    EXPECT_EQ(input->Share(&vmo), ZX_OK);
    data_provider_.AddConsumer(label, std::move(vmo), [&status](zx_status_t res) { status = res; });
    EXPECT_EQ(status, ZX_OK);

    // New consumers are included in the *next* iteration.
    EXPECT_EQ(input->vmo().wait_one(kBetweenIterations, zx::time(0), &observed), ZX_OK);
    EXPECT_EQ(observed & kInIteration, 0u);
  }

  // Complete the iteration
  EXPECT_EQ(data_provider_.CompleteIteration(), ZX_OK);
  EXPECT_EQ(fuzzer_input_.vmo().wait_one(kBetweenIterations, zx::time(0), &observed), ZX_OK);
  EXPECT_EQ(observed & kInIteration, 0u);
  for (const auto &[label, input] : inputs) {
    EXPECT_EQ(input.vmo().wait_one(kBetweenIterations, zx::time(0), &observed), ZX_OK);
    EXPECT_EQ(observed & kInIteration, 0u);
  }

  // Start a new iteration
  EXPECT_EQ(data_provider_.PartitionTestInput(data.c_str(), data.size()), ZX_OK);
  EXPECT_EQ(fuzzer_input_.vmo().wait_one(kInIteration, zx::time(0), &observed), ZX_OK);
  EXPECT_EQ(observed & kBetweenIterations, 0u);
  for (const auto &[label, input] : inputs) {
    EXPECT_EQ(input.vmo().wait_one(kInIteration, zx::time(0), &observed), ZX_OK);
    EXPECT_EQ(observed & kBetweenIterations, 0u);
  }
}

}  // namespace fuzzing
