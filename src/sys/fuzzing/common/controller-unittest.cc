// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/sys/fuzzing/common/controller.h"

#include <lib/sync/completion.h>
#include <lib/syslog/cpp/macros.h>
#include <zircon/status.h>

#include <gtest/gtest.h>

#include "src/sys/fuzzing/common/binding.h"
#include "src/sys/fuzzing/common/status.h"
#include "src/sys/fuzzing/common/testing/corpus-reader.h"
#include "src/sys/fuzzing/common/testing/dispatcher.h"
#include "src/sys/fuzzing/common/testing/monitor.h"
#include "src/sys/fuzzing/common/testing/runner.h"
#include "src/sys/fuzzing/common/testing/transceiver.h"

namespace fuzzing {
namespace {

using fuchsia::fuzzer::ControllerPtr;
using fuchsia::fuzzer::ProcessProxySyncPtr;
using fuchsia::fuzzer::UpdateReason;

// Test fixtures.

// Base class for |Controller| unit tests.
class ControllerTest : public ::testing::Test {
 public:
  // Implicitly tests |Controller::SetRunner| and |Controller::Bind|.
  ControllerPtr Bind() {
    // The shared_ptr will be responsible for deleting the FakeRunner memory.
    auto runner = std::make_shared<FakeRunner>();
    runner_ = runner.get();
    controller_.SetRunner(runner);

    ControllerPtr controller;
    controller_.Bind(controller.NewRequest(client_.get()), server_.get());
    return controller;
  }

  async_dispatcher_t* dispatcher() const { return client_.get(); }

  void AddToCorpus(CorpusType corpus_type, Input input) {
    runner_->AddToCorpus(corpus_type, std::move(input));
  }
  Input ReadFromCorpus(CorpusType corpus_type, size_t offset) {
    return runner_->ReadFromCorpus(corpus_type, offset);
  }

  zx_status_t ParseDictionary(const Input& input) { return runner_->ParseDictionary(input); }

  void SetError(zx_status_t error) { runner_->set_error(error); }
  void SetResult(Result result) { runner_->set_result(result); }
  void SetResultInput(const Input& input) { runner_->set_result_input(input); }
  void SetStatus(Status status) { runner_->set_status(std::move(status)); }
  void UpdateMonitors(UpdateReason reason) { runner_->UpdateMonitors(reason); }

  FidlInput Transmit(const Input& input) { return transceiver_.Transmit(input); }

  // Synchronously receives and returns an |Input| from a provided |FidlInput|.
  Input Receive(FidlInput fidl_input) { return transceiver_.Receive(std::move(fidl_input)); }

 private:
  ControllerImpl controller_;
  FakeDispatcher client_;
  FakeDispatcher server_;
  FakeRunner* runner_;
  FakeTransceiver transceiver_;
};

// Unit tests.

TEST_F(ControllerTest, ConfigureAndGetOptions) {
  auto controller = Bind();
  sync_completion_t sync;

  // GetOptions without Configure.
  Options options1;
  controller->GetOptions([&](Options result) {
    options1 = std::move(result);
    sync_completion_signal(&sync);
  });
  sync_completion_wait(&sync, ZX_TIME_INFINITE);
  sync_completion_reset(&sync);
  EXPECT_NE(options1.seed(), 0U);

  // Configure.
  uint32_t runs = 1000;
  zx::duration max_total_time = zx::sec(300);
  uint32_t seed = 42;
  uint32_t max_input_size = 1ULL << 10;
  uint16_t mutation_depth = 8;
  bool detect_exits = true;
  bool detect_leaks = false;
  zx::duration run_limit = zx::sec(20);
  options1.set_runs(runs);
  options1.set_max_total_time(max_total_time.get());
  options1.set_seed(seed);
  options1.set_max_input_size(max_input_size);
  options1.set_mutation_depth(mutation_depth);
  options1.set_detect_exits(detect_exits);
  options1.set_detect_leaks(detect_leaks);
  options1.set_run_limit(run_limit.get());
  zx_status_t status;
  auto options2 = CopyOptions(options1);
  controller->Configure(std::move(options1), [&](zx_status_t result) {
    status = result;
    sync_completion_signal(&sync);
  });
  sync_completion_wait(&sync, ZX_TIME_INFINITE);
  sync_completion_reset(&sync);
  EXPECT_EQ(status, ZX_OK);

  // Can Configure again.
  uint64_t malloc_limit = 64ULL << 10;
  uint64_t oom_limit = 1ULL << 20;
  zx::duration purge_interval = zx::sec(10);
  int32_t malloc_exitcode = 1000;
  int32_t death_exitcode = 1001;
  int32_t leak_exitcode = 1002;
  int32_t oom_exitcode = 1003;
  zx::duration pulse_interval = zx::sec(3);
  options2.set_malloc_limit(malloc_limit);
  options2.set_oom_limit(oom_limit);
  options2.set_purge_interval(purge_interval.get());
  options2.set_malloc_exitcode(malloc_exitcode);
  options2.set_death_exitcode(death_exitcode);
  options2.set_leak_exitcode(leak_exitcode);
  options2.set_oom_exitcode(oom_exitcode);
  options2.set_pulse_interval(pulse_interval.get());
  controller->Configure(std::move(options2), [&](zx_status_t result) {
    status = result;
    sync_completion_signal(&sync);
  });
  sync_completion_wait(&sync, ZX_TIME_INFINITE);
  sync_completion_reset(&sync);
  EXPECT_EQ(status, ZX_OK);

  // Changes are reflected.
  Options options3;
  controller->GetOptions([&](Options result) {
    options3 = std::move(result);
    sync_completion_signal(&sync);
  });
  sync_completion_wait(&sync, ZX_TIME_INFINITE);
  sync_completion_reset(&sync);
  EXPECT_EQ(options3.runs(), runs);
  EXPECT_EQ(options3.max_total_time(), max_total_time.get());
  EXPECT_EQ(options3.seed(), seed);
  EXPECT_EQ(options3.max_input_size(), max_input_size);
  EXPECT_EQ(options3.mutation_depth(), mutation_depth);
  EXPECT_EQ(options3.detect_exits(), detect_exits);
  EXPECT_EQ(options3.detect_leaks(), detect_leaks);
  EXPECT_EQ(options3.run_limit(), run_limit.get());
  EXPECT_EQ(options3.malloc_limit(), malloc_limit);
  EXPECT_EQ(options3.oom_limit(), oom_limit);
  EXPECT_EQ(options3.purge_interval(), purge_interval.get());
  EXPECT_EQ(options3.malloc_exitcode(), malloc_exitcode);
  EXPECT_EQ(options3.death_exitcode(), death_exitcode);
  EXPECT_EQ(options3.leak_exitcode(), leak_exitcode);
  EXPECT_EQ(options3.oom_exitcode(), oom_exitcode);
  EXPECT_EQ(options3.pulse_interval(), pulse_interval.get());
}

TEST_F(ControllerTest, AddToCorpus) {
  ControllerPtr controller = Bind();
  sync_completion_t sync;
  Input input0;
  Input seed_input1({0xde, 0xad});
  Input seed_input2({0xbe, 0xef});
  Input live_input3({0xfe, 0xed});
  Input live_input4({0xfa, 0xce});
  zx_status_t result;

  // Interleave the calls.
  controller->AddToCorpus(CorpusType::LIVE, Transmit(live_input3), [&](zx_status_t response) {
    result = response;
    sync_completion_signal(&sync);
  });
  sync_completion_wait(&sync, ZX_TIME_INFINITE);
  sync_completion_reset(&sync);
  controller->AddToCorpus(CorpusType::SEED, Transmit(seed_input1), [&](zx_status_t response) {
    result = response;
    sync_completion_signal(&sync);
  });
  sync_completion_wait(&sync, ZX_TIME_INFINITE);
  sync_completion_reset(&sync);

  controller->AddToCorpus(CorpusType::SEED, Transmit(seed_input2), [&](zx_status_t response) {
    result = response;
    sync_completion_signal(&sync);
  });
  sync_completion_wait(&sync, ZX_TIME_INFINITE);
  sync_completion_reset(&sync);

  controller->AddToCorpus(CorpusType::LIVE, Transmit(live_input4), [&](zx_status_t response) {
    result = response;
    sync_completion_signal(&sync);
  });
  sync_completion_wait(&sync, ZX_TIME_INFINITE);
  sync_completion_reset(&sync);

  EXPECT_EQ(ReadFromCorpus(CorpusType::SEED, 0).ToHex(), input0.ToHex());
  EXPECT_EQ(ReadFromCorpus(CorpusType::SEED, 1).ToHex(), seed_input1.ToHex());
  EXPECT_EQ(ReadFromCorpus(CorpusType::SEED, 2).ToHex(), seed_input2.ToHex());
  EXPECT_EQ(ReadFromCorpus(CorpusType::SEED, 3).ToHex(), input0.ToHex());

  EXPECT_EQ(ReadFromCorpus(CorpusType::LIVE, 0).ToHex(), input0.ToHex());
  EXPECT_EQ(ReadFromCorpus(CorpusType::LIVE, 1).ToHex(), live_input3.ToHex());
  EXPECT_EQ(ReadFromCorpus(CorpusType::LIVE, 2).ToHex(), live_input4.ToHex());
  EXPECT_EQ(ReadFromCorpus(CorpusType::LIVE, 3).ToHex(), input0.ToHex());
}

TEST_F(ControllerTest, ReadCorpus) {
  ControllerPtr controller = Bind();
  sync_completion_t sync;
  Input input0;
  Input input1({0xde, 0xad});
  Input input2({0xbe, 0xef});
  Input input3({0xfe, 0xed});
  Input input4({0xfa, 0xce});

  AddToCorpus(CorpusType::SEED, input1.Duplicate());
  AddToCorpus(CorpusType::SEED, input2.Duplicate());

  AddToCorpus(CorpusType::LIVE, input3.Duplicate());
  AddToCorpus(CorpusType::LIVE, input4.Duplicate());

  FakeCorpusReader seed_reader;
  FakeCorpusReader live_reader;
  controller->ReadCorpus(CorpusType::SEED, seed_reader.NewBinding(dispatcher()),
                         [&]() { sync_completion_signal(&sync); });
  sync_completion_wait(&sync, ZX_TIME_INFINITE);
  sync_completion_reset(&sync);

  controller->ReadCorpus(CorpusType::LIVE, live_reader.NewBinding(dispatcher()),
                         [&]() { sync_completion_signal(&sync); });
  sync_completion_wait(&sync, ZX_TIME_INFINITE);
  sync_completion_reset(&sync);

  // Interleave the calls.
  EXPECT_TRUE(live_reader.AwaitNext());
  EXPECT_EQ(live_reader.GetNext().ToHex(), input3.ToHex());

  EXPECT_TRUE(seed_reader.AwaitNext());
  EXPECT_EQ(seed_reader.GetNext().ToHex(), input1.ToHex());

  EXPECT_TRUE(live_reader.AwaitNext());
  EXPECT_EQ(live_reader.GetNext().ToHex(), input4.ToHex());

  EXPECT_TRUE(seed_reader.AwaitNext());
  EXPECT_EQ(seed_reader.GetNext().ToHex(), input2.ToHex());

  // The connection is closed after all inputs have been sent.
  EXPECT_FALSE(live_reader.AwaitNext());
  EXPECT_FALSE(live_reader.AwaitNext());
}

TEST_F(ControllerTest, WriteDictionary) {
  ControllerPtr controller = Bind();
  sync_completion_t sync;
  auto invalid = FakeRunner::invalid_dictionary();
  auto valid = FakeRunner::valid_dictionary();
  zx_status_t result;

  controller->WriteDictionary(Transmit(invalid), [&](zx_status_t response) {
    result = response;
    sync_completion_signal(&sync);
  });
  sync_completion_wait(&sync, ZX_TIME_INFINITE);
  sync_completion_reset(&sync);
  EXPECT_EQ(result, ZX_ERR_INVALID_ARGS);

  controller->WriteDictionary(Transmit(valid), [&](zx_status_t response) {
    result = response;
    sync_completion_signal(&sync);
  });
  sync_completion_wait(&sync, ZX_TIME_INFINITE);
  sync_completion_reset(&sync);
  EXPECT_EQ(result, ZX_OK);
}

TEST_F(ControllerTest, ReadDictionary) {
  ControllerPtr controller = Bind();
  sync_completion_t sync;
  FidlInput result;

  auto dict = FakeRunner::valid_dictionary();
  EXPECT_EQ(ParseDictionary(dict), ZX_OK);
  controller->ReadDictionary([&](FidlInput response) {
    result = std::move(response);
    sync_completion_signal(&sync);
  });
  sync_completion_wait(&sync, ZX_TIME_INFINITE);
  sync_completion_reset(&sync);
  EXPECT_EQ(Receive(std::move(result)).ToHex(), dict.ToHex());
}

TEST_F(ControllerTest, GetStatus) {
  ControllerPtr controller = Bind();
  sync_completion_t sync;
  Status result;

  Status status;
  status.set_running(true);
  status.set_runs(42);
  status.set_elapsed(zx::sec(15).get());
  status.set_covered_pcs(5);
  status.set_covered_features(10);
  status.set_corpus_num_inputs(15);
  status.set_corpus_total_size(25);
  auto expected = CopyStatus(status);
  SetStatus(std::move(status));

  controller->GetStatus([&](Status response) {
    result = std::move(response);
    sync_completion_signal(&sync);
  });
  sync_completion_wait(&sync, ZX_TIME_INFINITE);
  sync_completion_reset(&sync);
  EXPECT_EQ(result.running(), expected.running());
  EXPECT_EQ(result.runs(), expected.runs());
  EXPECT_EQ(result.elapsed(), expected.elapsed());
  EXPECT_EQ(result.covered_pcs(), expected.covered_pcs());
  EXPECT_EQ(result.covered_features(), expected.covered_features());
  EXPECT_EQ(result.corpus_num_inputs(), expected.corpus_num_inputs());
  EXPECT_EQ(result.corpus_total_size(), expected.corpus_total_size());
}

TEST_F(ControllerTest, AddMonitor) {
  ControllerPtr controller = Bind();
  sync_completion_t sync;
  FakeMonitor monitor;

  Status status;
  status.set_runs(13);
  auto expected = CopyStatus(status);
  SetStatus(std::move(status));
  controller->AddMonitor(monitor.NewBinding(), [&]() { sync_completion_signal(&sync); });
  sync_completion_wait(&sync, ZX_TIME_INFINITE);
  sync_completion_reset(&sync);

  UpdateMonitors(UpdateReason::PULSE);

  UpdateReason reason;
  auto updated = monitor.NextStatus(&reason);
  EXPECT_EQ(updated.runs(), expected.runs());
  EXPECT_EQ(reason, UpdateReason::PULSE);
}

TEST_F(ControllerTest, GetResults) {
  ControllerPtr controller = Bind();
  sync_completion_t sync;
  Result result;
  FidlInput fidl_input;
  Input result_input({0xde, 0xad, 0xbe, 0xef});

  SetResult(Result::DEATH);
  SetResultInput(result_input);
  controller->GetResults([&](Result response_result, FidlInput response_fidl_input) {
    result = response_result;
    fidl_input = std::move(response_fidl_input);
    sync_completion_signal(&sync);
  });
  sync_completion_wait(&sync, ZX_TIME_INFINITE);
  sync_completion_reset(&sync);
  EXPECT_EQ(result, Result::DEATH);
  EXPECT_EQ(Receive(std::move(fidl_input)).ToHex(), result_input.ToHex());
}

TEST_F(ControllerTest, Execute) {
  ControllerPtr controller = Bind();
  sync_completion_t sync;
  fit::result<Result, zx_status_t> result;
  Input input({0xde, 0xad, 0xbe, 0xef});

  SetError(ZX_ERR_WRONG_TYPE);
  controller->Execute(Transmit(input), [&](fit::result<Result, zx_status_t> response) {
    result = std::move(response);
    sync_completion_signal(&sync);
  });
  sync_completion_wait(&sync, ZX_TIME_INFINITE);
  sync_completion_reset(&sync);
  EXPECT_TRUE(result.is_error());
  EXPECT_EQ(result.error(), ZX_ERR_WRONG_TYPE);

  SetError(ZX_OK);
  SetResult(Result::OOM);
  controller->Execute(Transmit(input), [&](fit::result<Result, zx_status_t> response) {
    result = std::move(response);
    sync_completion_signal(&sync);
  });
  sync_completion_wait(&sync, ZX_TIME_INFINITE);
  sync_completion_reset(&sync);
  EXPECT_TRUE(result.is_ok());
  EXPECT_EQ(result.value(), Result::OOM);
}

TEST_F(ControllerTest, Minimize) {
  ControllerPtr controller = Bind();
  sync_completion_t sync;
  fit::result<FidlInput, zx_status_t> result;
  Input input({0xde, 0xad, 0xbe, 0xef});
  Input minimized({0xde, 0xbe});

  SetError(ZX_ERR_WRONG_TYPE);
  controller->Minimize(Transmit(input), [&](fit::result<FidlInput, zx_status_t> response) {
    result = std::move(response);
    sync_completion_signal(&sync);
  });
  sync_completion_wait(&sync, ZX_TIME_INFINITE);
  sync_completion_reset(&sync);
  EXPECT_TRUE(result.is_error());
  EXPECT_EQ(result.error(), ZX_ERR_WRONG_TYPE);

  SetError(ZX_OK);
  SetResultInput(minimized);
  controller->Minimize(Transmit(input), [&](fit::result<FidlInput, zx_status_t> response) {
    result = std::move(response);
    sync_completion_signal(&sync);
  });
  sync_completion_wait(&sync, ZX_TIME_INFINITE);
  sync_completion_reset(&sync);
  EXPECT_TRUE(result.is_ok());
  EXPECT_EQ(Receive(std::move(result.value())).ToHex(), minimized.ToHex());
}

TEST_F(ControllerTest, Cleanse) {
  ControllerPtr controller = Bind();
  sync_completion_t sync;
  fit::result<FidlInput, zx_status_t> result;
  Input input({0xde, 0xad, 0xbe, 0xef});
  Input cleansed({0x20, 0x20, 0xbe, 0xff});

  SetError(ZX_ERR_WRONG_TYPE);
  controller->Cleanse(Transmit(input), [&](fit::result<FidlInput, zx_status_t> response) {
    result = std::move(response);
    sync_completion_signal(&sync);
  });
  sync_completion_wait(&sync, ZX_TIME_INFINITE);
  sync_completion_reset(&sync);
  EXPECT_TRUE(result.is_error());
  EXPECT_EQ(result.error(), ZX_ERR_WRONG_TYPE);

  SetError(ZX_OK);
  SetResultInput(cleansed);
  controller->Cleanse(Transmit(input), [&](fit::result<FidlInput, zx_status_t> response) {
    result = std::move(response);
    sync_completion_signal(&sync);
  });
  sync_completion_wait(&sync, ZX_TIME_INFINITE);
  sync_completion_reset(&sync);
  EXPECT_TRUE(result.is_ok());
  EXPECT_EQ(Receive(std::move(result.value())).ToHex(), cleansed.ToHex());
}

TEST_F(ControllerTest, Fuzz) {
  ControllerPtr controller = Bind();
  sync_completion_t sync;
  fit::result<std::tuple<Result, FidlInput>, zx_status_t> fuzz_result;
  Input fuzzed({0xde, 0xad, 0xbe, 0xef});

  SetError(ZX_ERR_WRONG_TYPE);
  controller->Fuzz([&](fit::result<std::tuple<Result, FidlInput>, zx_status_t> response) {
    fuzz_result = std::move(response);
    sync_completion_signal(&sync);
  });
  sync_completion_wait(&sync, ZX_TIME_INFINITE);
  sync_completion_reset(&sync);
  EXPECT_TRUE(fuzz_result.is_error());
  EXPECT_EQ(fuzz_result.error(), ZX_ERR_WRONG_TYPE);

  SetError(ZX_OK);
  SetResult(Result::CRASH);
  SetResultInput(fuzzed);
  controller->Fuzz([&](fit::result<std::tuple<Result, FidlInput>, zx_status_t> response) {
    fuzz_result = std::move(response);
    sync_completion_signal(&sync);
  });
  sync_completion_wait(&sync, ZX_TIME_INFINITE);
  sync_completion_reset(&sync);
  EXPECT_TRUE(fuzz_result.is_ok());
  auto [result, result_input] = std::move(fuzz_result.value());
  EXPECT_EQ(result, Result::CRASH);
  EXPECT_EQ(Receive(std::move(result_input)).ToHex(), fuzzed.ToHex());
}

TEST_F(ControllerTest, Merge) {
  ControllerPtr controller = Bind();
  sync_completion_t sync;
  zx_status_t result;

  SetError(ZX_ERR_WRONG_TYPE);
  controller->Merge([&](zx_status_t response) {
    result = response;
    sync_completion_signal(&sync);
  });
  sync_completion_wait(&sync, ZX_TIME_INFINITE);
  sync_completion_reset(&sync);
  EXPECT_EQ(result, ZX_ERR_WRONG_TYPE);

  SetError(ZX_OK);
  controller->Merge([&](zx_status_t response) {
    result = response;
    sync_completion_signal(&sync);
  });
  sync_completion_wait(&sync, ZX_TIME_INFINITE);
  sync_completion_reset(&sync);
  EXPECT_EQ(result, ZX_OK);
}

}  // namespace
}  // namespace fuzzing
