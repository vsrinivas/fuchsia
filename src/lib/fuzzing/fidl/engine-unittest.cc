// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "engine.h"

#include <lib/gtest/test_loop_fixture.h>
#include <lib/sys/cpp/testing/component_context_provider.h>

#include <vector>

#include "llvm-fuzzer.h"
#include "sanitizer-cov-proxy.h"
#include "test/fake-inline-8bit-counters.h"
#include "test/fake-sanitizer-cov-proxy.h"

namespace fuzzing {

using ::fuchsia::fuzzer::CoveragePtr;
using ::fuchsia::fuzzer::DataProviderPtr;

// Globals to control the behavior of |LLVMFuzzerInitialize| below.
static const size_t kMaxOptionLength = 32;
char gReplacementOption[kMaxOptionLength];
char gAdditionalOption[kMaxOptionLength];

////////////////////////////////////////////////////////////////////////////////////////////////////
// Test fixture

class EngineTest : public gtest::TestLoopFixture {
 public:
  const char *kArgv0 = "argv0";

  void SetUp() override {
    TestLoopFixture::SetUp();
    memset(gReplacementOption, 0, sizeof(gReplacementOption));
    memset(gAdditionalOption, 0, sizeof(gAdditionalOption));

    // Start the engine.
    auto context = provider_.TakeContext();
    context->outgoing()->AddPublicService(llvm_fuzzer_.GetHandler());
    EngineImpl::UseContext(std::move(context));
    engine_ = EngineImpl::GetInstance();

    // Connect the engine to the client
    LlvmFuzzerPtr llvm_fuzzer;
    provider_.ConnectToPublicService(llvm_fuzzer.NewRequest());
    EXPECT_EQ(engine_->SetLlvmFuzzer(std::move(llvm_fuzzer)), ZX_OK);

    // Start the proxy, and connect to engine.
    FakeSanitizerCovProxy::Reset();
    auto proxy = SanitizerCovProxy::GetInstance(false /* autoconnect */);

    CoveragePtr coverage;
    provider_.ConnectToPublicService(coverage.NewRequest());
    ASSERT_EQ(proxy->SetCoverage(std::move(coverage)), ZX_OK);

    // Fake call to __sanitizer_cov_8bit_counters_init.
    while (!FakeInline8BitCounters::Reset()) {
      RunLoopUntilIdle();
    }
  }

  // Test helper to construct a fake set of command line arguments.
  void SetCommandLine(const std::vector<std::string> &args) {
    cmdline_.clear();
    std::transform(args.begin(), args.end(), std::back_inserter(cmdline_),
                   [](const std::string &s) { return const_cast<char *>(s.c_str()); });
    argc_ = cmdline_.size();
    argv_ = &cmdline_[0];
  }

  // Test helper that does a complete roundtrip from the DataProvider in the engine, to the fuzz
  // target function, to the SanitizerCovProxy, back to the Coverage in the engine.
  void PerformFuzzingIteration(const std::vector<uint8_t> &data);

  void TearDown() override {
    // Simulate engine process exit.
    engine_->Stop(ZX_OK);
    RunLoopUntilIdle();
    TestLoopFixture::TearDown();
  }

 protected:
  // Fake command line arguments to the engine.
  std::vector<char *> cmdline_;
  int argc_;
  char **argv_;

  EngineImpl *engine_;
  LlvmFuzzerImpl llvm_fuzzer_;

 private:
  sys::testing::ComponentContextProvider provider_;
};

}  // namespace fuzzing

// Fake implementation of LLVM interface functions.

extern "C" int LLVMFuzzerInitialize(int *argc, char ***argv) {
  if (strlen(::fuzzing::gReplacementOption) != 0 && *argc > 1) {
    (*argv)[1] = ::fuzzing::gReplacementOption;
    *argc = 2;

  } else if (strlen(::fuzzing::gAdditionalOption) != 0) {
    static std::vector<char *> mod_argv;
    mod_argv.clear();
    for (int i = 0; i < *argc; ++i) {
      mod_argv.push_back((*argv)[i]);
    }
    mod_argv.push_back(::fuzzing::gAdditionalOption);
    *argv = &mod_argv[0];
    *argc = mod_argv.size();
  }
  return 0;
}

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
  return fuzzing::FakeInline8BitCounters::Write(data, size);
}

////////////////////////////////////////////////////////////////////////////////////////////////////
// Unit tests

namespace fuzzing {

// |Initialize| works with an empty command line. It blocks until |Start| is called and can only be
// called once. |Start| continues running until stop is called.
TEST_F(EngineTest, Initialize_Simple) {
  std::vector<std::string> args{"fidl_fuzzing_engine"};
  SetCommandLine(args);

  int result = -1;
  std::thread t1([this]() { EXPECT_EQ(engine_->Initialize(&argc_, &argv_), ZX_OK); });
  engine_->Start(std::vector<std::string>(), [&result](int rc) { result = rc; });
  RunLoopUntilIdle();
  t1.join();

  int i = 0;
  EXPECT_EQ(size_t(argc_), args.size());
  for (const std::string &arg : args) {
    ASSERT_LT(i, argc_);
    EXPECT_STREQ(argv_[i++], arg.c_str());
  }

  // |Start| is still running.
  EXPECT_EQ(result, -1);

  EXPECT_EQ(engine_->Initialize(&argc_, &argv_), ZX_ERR_BAD_STATE);
  engine_->Start(std::vector<std::string>(), [&result](int rc) { result = rc; });
  RunLoopUntilIdle();
  EXPECT_EQ(result, ZX_ERR_BAD_STATE);

  engine_->Stop(ZX_OK);
  RunLoopUntilIdle();
  EXPECT_EQ(result, ZX_OK);
}

// Data consumer labels can be passed via the command line, i.e. from a component manifest.
TEST_F(EngineTest, InitializeWithLabels) {
  std::vector<std::string> args{"fidl_fuzzing_engine"};
  std::vector<std::string> labels{"foo", "bar"};

  for (const std::string &label : labels) {
    args.push_back(std::string("--label=") + label);
    EXPECT_FALSE(engine_->data_provider().HasLabel(label));
  }
  SetCommandLine(args);

  std::thread t1([this]() { EXPECT_EQ(engine_->Initialize(&argc_, &argv_), ZX_OK); });
  engine_->Start(std::vector<std::string>(), [](int rc) {});
  RunLoopUntilIdle();
  t1.join();

  EXPECT_EQ(size_t(argc_), args.size() - labels.size());
  for (int i = 0; i < argc_; ++i) {
    EXPECT_STREQ(argv_[i], args[i].c_str());
  }
  for (const std::string &label : labels) {
    EXPECT_TRUE(engine_->data_provider().HasLabel(label));
  }
}

// libFuzzer options can be passed both on the command line and via FIDL, i.e. statically via a
// component manifest and dynamically via the framework calling |fuchsia.fuzzer.Engine.Start|.
TEST_F(EngineTest, InitializeWithOptions) {
  std::vector<std::string> args{"fidl_fuzzing_engine", "-artifact_prefix=data",
                                "-dict=pkg/data/fuzzer.dict"};
  std::vector<std::string> options = {"-seed=1", "-runs=1000"};
  SetCommandLine(args);

  std::thread t1([this]() { EXPECT_EQ(engine_->Initialize(&argc_, &argv_), ZX_OK); });
  engine_->Start(std::vector<std::string>(options), [](int rc) {});
  RunLoopUntilIdle();
  t1.join();

  int i = 0;
  for (const std::string &arg : args) {
    EXPECT_STREQ(argv_[i++], arg.c_str());
  }
  for (const std::string &option : options) {
    EXPECT_STREQ(argv_[i++], option.c_str());
  }
  EXPECT_EQ(i, argc_);
}

// The client can modify the libFuzzer options.
TEST_F(EngineTest, InitializeWithModifications) {
  std::vector<std::string> args{"fidl_fuzzing_engine"};
  std::vector<std::string> options = {"-seed=1", "-runs=1000"};
  snprintf(gReplacementOption, sizeof(gAdditionalOption), "-seed=2");
  SetCommandLine(args);

  std::thread t1([this]() { EXPECT_EQ(engine_->Initialize(&argc_, &argv_), ZX_OK); });
  engine_->Start(std::vector<std::string>(options), [](int rc) {});
  RunLoopUntilIdle();
  t1.join();

  int i = 0;
  for (const std::string &arg : args) {
    ASSERT_LT(i, argc_);
    EXPECT_STREQ(argv_[i++], arg.c_str());
  }
  EXPECT_EQ(i + 1, argc_);
  EXPECT_STREQ(argv_[i], gReplacementOption);
}

// Test all the aspects above simultaenouesly.
TEST_F(EngineTest, Initialize) {
  std::vector<std::string> args{"fidl_fuzzing_engine", "-artifact_prefix=data",
                                "-dict=pkg/data/fuzzer.dict"};
  std::vector<std::string> labels{"foo", "bar"};
  std::vector<std::string> options = {"-seed=1", "-runs=1000"};
  snprintf(gAdditionalOption, sizeof(gAdditionalOption), "-rss_limit_mb=1024");

  for (const std::string &label : labels) {
    args.push_back(std::string("--label=") + label);
    EXPECT_FALSE(engine_->data_provider().HasLabel(label));
  }
  SetCommandLine(args);

  std::thread t1([this]() { EXPECT_EQ(engine_->Initialize(&argc_, &argv_), ZX_OK); });
  engine_->Start(std::vector<std::string>(options), [](int rc) {});
  RunLoopUntilIdle();
  t1.join();

  int j = args.size() - labels.size();
  EXPECT_EQ(size_t(argc_), j + options.size() + 1);
  for (int i = 0; i < argc_; ++i) {
    if (i < j) {
      EXPECT_STREQ(argv_[i], args[i].c_str());
    } else if (i < argc_ - 1) {
      EXPECT_STREQ(argv_[i], options[i - j].c_str());
    } else {
      EXPECT_STREQ(argv_[i], gAdditionalOption);
    }
  }
  for (const std::string &label : labels) {
    EXPECT_TRUE(engine_->data_provider().HasLabel(label));
  }
}

void EngineTest::PerformFuzzingIteration(const std::vector<uint8_t> &data) {
  // Have the engine run once on a test input. This is a blocking call, so it must happen in another
  // thread for the test to still be able to drive the loop.
  int result = -1;
  sync_completion_t sync;
  std::thread t2([this, &data, &result, &sync]() {
    result = engine_->TestOneInput(&data[0], data.size());
    sync_completion_signal(&sync);
  });

  zx_status_t status;
  while ((status = sync_completion_wait(&sync, ZX_MSEC(10))) != ZX_OK) {
    ASSERT_EQ(status, ZX_ERR_TIMED_OUT);
    RunLoopUntilIdle();
  }
  t2.join();

  EXPECT_EQ(result, 0);
  for (size_t i = 0; i < data.size(); ++i) {
    SCOPED_TRACE(i);
    EXPECT_EQ(FakeInline8BitCounters::At(i), data[i]);
  }
}

TEST_F(EngineTest, TestOneInput) {
  // Calling before |Initialize| fails.
  EXPECT_EQ(engine_->TestOneInput(nullptr, 0), ZX_ERR_BAD_STATE);

  engine_->Start(std::vector<std::string>(), [](int rc) {});
  RunLoopUntilIdle();
  EXPECT_EQ(engine_->Initialize(&argc_, &argv_), ZX_OK);

  PerformFuzzingIteration({0x01, 0x02, 0x03, 0x04});
  PerformFuzzingIteration({});
  PerformFuzzingIteration({0x01, 0x23, 0x45, 0x67, 0x89, 0xab, 0xcd, 0xef});
}

}  // namespace fuzzing
