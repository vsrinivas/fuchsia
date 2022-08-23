// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/sys/fuzzing/common/engine.h"

#include <lib/syslog/cpp/macros.h>

#include <limits>

#include <gtest/gtest.h>

#include "src/lib/files/directory.h"
#include "src/lib/files/file.h"
#include "src/lib/files/path.h"
#include "src/sys/fuzzing/common/async-types.h"
#include "src/sys/fuzzing/common/testing/component-context.h"
#include "src/sys/fuzzing/common/testing/registrar.h"
#include "src/sys/fuzzing/common/testing/runner.h"
#include "testing/fidl/async_loop_for_test.h"

namespace fuzzing {

// Test fixtures

// |FakeCmdline| constructs |argc|/|argv| with vaguely RAII semantics. These fields should only be
// used with functions that normally handle arguments from |main|. There are plenty of ways to abuse
// |argv| and corrupt memory. Don't do that.
struct FakeCmdline final {
  int argc = 0;
  char** argv = nullptr;

  bool SetArgs(const std::initializer_list<const char*>& cmdline_args) {
    if (cmdline_args.size() >= std::numeric_limits<int>::max()) {
      return false;
    }
    argc = static_cast<int>(cmdline_args.size() + 1);
    args.clear();
    args.reserve(argc);
    owned.clear();
    owned.reserve(argc);
    Add("/test/engine");
    for (const char* arg : cmdline_args) {
      Add(arg);
    }
    argv = &args[0];
    return true;
  }

  std::string GetArgs() const {
    if (argc < 2) {
      return std::string();
    }
    std::ostringstream oss;
    for (int i = 1; i < argc; ++i) {
      oss << " " << argv[i];
    }
    return oss.str().substr(1);
  }

 private:
  void Add(const char* s) {
    std::string arg(s);
    owned.emplace_back(std::move(arg));
    args.push_back(const_cast<char*>(owned.back().c_str()));
  }

  std::vector<char*> args;
  std::vector<std::string> owned;
};

// Writes |contents| to a file at |pathname|, creating any intermediary directories in the
// process.
void WriteInput(const std::string& pathname, Input contents) {
  auto dirname = files::GetDirectoryName(pathname);
  ASSERT_TRUE(files::CreateDirectory(dirname));
  const auto* data = reinterpret_cast<const char*>(contents.data());
  ASSERT_TRUE(files::WriteFile(pathname, data, contents.size()));
}

// For each input in |inputs|, creates a file under |dirname| with name and contents matching that
// input, and adds a corresponding |Input| to the sorted set returned via |out|.
void MakeCorpus(const char* dirname, const std::initializer_list<const char*>& inputs,
                std::vector<Input>* out) {
  ASSERT_TRUE(files::CreateDirectory(dirname));
  out->reserve(out->size() + inputs.size());
  for (const auto* input : inputs) {
    auto pathname = files::JoinPath(dirname, input);
    ASSERT_TRUE(files::WriteFile(pathname, input));
    out->emplace_back(input);
  }
  std::sort(out->begin(), out->end());
}

// Returns a sorted copy of the given |inputs|, minus any empty inputs.
std::vector<Input> SortInputs(const std::vector<Input>& inputs) {
  std::vector<Input> sorted;
  sorted.reserve(inputs.size());
  for (const auto& input : inputs) {
    if (input.size() != 0) {
      sorted.emplace_back(input.Duplicate());
    }
  }
  std::sort(sorted.begin(), sorted.end());
  return sorted;
}

// Unit tests

TEST(EngineTest, InitializeUrl) {
  Engine engine;
  FakeCmdline cmdline;

  // URL is required.
  ASSERT_TRUE(cmdline.SetArgs({}));
  EXPECT_EQ(engine.Initialize(&cmdline.argc, &cmdline.argv), ZX_ERR_INVALID_ARGS);

  // Other arguments are optional.
  ASSERT_TRUE(cmdline.SetArgs({kFakeFuzzerUrl}));
  EXPECT_EQ(engine.Initialize(&cmdline.argc, &cmdline.argv), ZX_OK);
  EXPECT_EQ(engine.url(), kFakeFuzzerUrl);
  EXPECT_FALSE(engine.fuzzing());
  EXPECT_TRUE(engine.corpus().empty());
  EXPECT_EQ(engine.dictionary().size(), 0U);
  EXPECT_EQ(cmdline.GetArgs(), "");
}

TEST(EngineTest, InitializeFlags) {
  Engine engine;
  FakeCmdline cmdline;

  // `fuchsia.fuzzer.FUZZ_MODE` flag.
  ASSERT_TRUE(cmdline.SetArgs({kFakeFuzzerUrl, "--fuzz"}));
  EXPECT_EQ(engine.Initialize(&cmdline.argc, &cmdline.argv), ZX_OK);
  EXPECT_TRUE(engine.fuzzing());
  EXPECT_EQ(cmdline.GetArgs(), "");

  // Other flags are passed through.
  ASSERT_TRUE(cmdline.SetArgs({"-libfuzzer=flag", kFakeFuzzerUrl, "--other"}));
  EXPECT_EQ(engine.Initialize(&cmdline.argc, &cmdline.argv), ZX_OK);
  EXPECT_FALSE(engine.fuzzing());
  EXPECT_EQ(cmdline.GetArgs(), "-libfuzzer=flag --other");

  // Order is flexible.
  ASSERT_TRUE(cmdline.SetArgs({"--fuzz", kFakeFuzzerUrl, "-libfuzzer=flag"}));
  EXPECT_EQ(engine.Initialize(&cmdline.argc, &cmdline.argv), ZX_OK);
  EXPECT_TRUE(engine.fuzzing());
  EXPECT_EQ(cmdline.GetArgs(), "-libfuzzer=flag");

  // '--' preserves following arguments.
  ASSERT_TRUE(cmdline.SetArgs({kFakeFuzzerUrl, "--", "-libfuzzer=flag", "--", "--fuzz"}));
  EXPECT_EQ(engine.Initialize(&cmdline.argc, &cmdline.argv), ZX_OK);
  EXPECT_FALSE(engine.fuzzing());
  EXPECT_EQ(cmdline.GetArgs(), "-libfuzzer=flag -- --fuzz");
}

TEST(EngineTest, InitializeCorpus) {
  Engine engine("/tmp/corpus");
  FakeCmdline cmdline;
  std::vector<Input> corpus;

  // Only "data/..." directories are considered corpora.
  ASSERT_TRUE(files::CreateDirectory("/tmp/corpus/non-data/corpus"));
  ASSERT_TRUE(cmdline.SetArgs({kFakeFuzzerUrl, "non-data/corpus"}));
  EXPECT_EQ(engine.Initialize(&cmdline.argc, &cmdline.argv), ZX_OK);
  EXPECT_TRUE(engine.corpus().empty());
  EXPECT_EQ(cmdline.GetArgs(), "non-data/corpus");

  // Directory must exist.
  ASSERT_TRUE(cmdline.SetArgs({kFakeFuzzerUrl, "data/invalid-corpus"}));
  EXPECT_EQ(engine.Initialize(&cmdline.argc, &cmdline.argv), ZX_ERR_NOT_FOUND);

  // Directory contents are added.
  ASSERT_NO_FATAL_FAILURE(MakeCorpus("/tmp/corpus/data/corpus1", {"foo", "bar"}, &corpus));
  ASSERT_TRUE(cmdline.SetArgs({kFakeFuzzerUrl, "data/corpus1"}));
  EXPECT_EQ(engine.Initialize(&cmdline.argc, &cmdline.argv), ZX_OK);
  EXPECT_EQ(SortInputs(engine.corpus()), corpus);
  EXPECT_EQ(cmdline.GetArgs(), "");

  // Multiple corpora can be added.
  ASSERT_NO_FATAL_FAILURE(MakeCorpus("/tmp/corpus/data/corpus2", {"baz", "qux", "quux"}, &corpus));
  ASSERT_TRUE(cmdline.SetArgs({kFakeFuzzerUrl, "data/corpus1", "data/corpus2"}));
  EXPECT_EQ(engine.Initialize(&cmdline.argc, &cmdline.argv), ZX_OK);
  EXPECT_EQ(SortInputs(engine.corpus()), corpus);
  EXPECT_EQ(cmdline.GetArgs(), "");
}

TEST(EngineTest, InitializeDictionary) {
  Engine engine("/tmp/dictionary");
  FakeCmdline cmdline;

  // Only "data/..." files are considered corpora.
  ASSERT_NO_FATAL_FAILURE(
      WriteInput("/tmp/dictionary/non-data/some-file", FakeRunner::valid_dictionary()));
  ASSERT_TRUE(cmdline.SetArgs({kFakeFuzzerUrl, "non-data/some-file"}));
  EXPECT_EQ(engine.Initialize(&cmdline.argc, &cmdline.argv), ZX_OK);
  EXPECT_EQ(engine.dictionary(), Input());
  EXPECT_EQ(cmdline.GetArgs(), "non-data/some-file");

  // File must exist.
  ASSERT_TRUE(cmdline.SetArgs({kFakeFuzzerUrl, "data/invalid-dictionary"}));
  EXPECT_EQ(engine.Initialize(&cmdline.argc, &cmdline.argv), ZX_ERR_NOT_FOUND);

  // Valid.
  ASSERT_NO_FATAL_FAILURE(
      WriteInput("/tmp/dictionary/data/dictionary1", FakeRunner::valid_dictionary()));
  ASSERT_TRUE(cmdline.SetArgs({kFakeFuzzerUrl, "data/dictionary1"}));
  EXPECT_EQ(engine.Initialize(&cmdline.argc, &cmdline.argv), ZX_OK);
  EXPECT_EQ(engine.dictionary(), FakeRunner::valid_dictionary());
  EXPECT_EQ(cmdline.GetArgs(), "");

  // At most one dictionary is supported.
  ASSERT_NO_FATAL_FAILURE(
      WriteInput("/tmp/dictionary/data/dictionary2", FakeRunner::valid_dictionary()));
  ASSERT_TRUE(cmdline.SetArgs({kFakeFuzzerUrl, "data/dictionary1", "data/dictionary2"}));
  EXPECT_EQ(engine.Initialize(&cmdline.argc, &cmdline.argv), ZX_ERR_INVALID_ARGS);
}

TEST(EngineTest, RunUninitialized) {
  Engine engine;
  FakeCmdline cmdline;
  auto context = ComponentContextForTest::Create();
  auto runner = FakeRunner::MakePtr(context->executor());

  // Initialize must be called first.
  EXPECT_EQ(engine.Run(std::move(context), runner), ZX_ERR_BAD_STATE);
}

TEST(EngineTest, RunInvalidDictionary) {
  Engine engine("/tmp/invalid");
  FakeCmdline cmdline;
  auto context = ComponentContextForTest::Create();
  auto runner = FakeRunner::MakePtr(context->executor());

  // Dictionary is parsed when running.
  ASSERT_NO_FATAL_FAILURE(
      WriteInput("/tmp/invalid/data/dictionary", FakeRunner::invalid_dictionary()));
  ASSERT_TRUE(cmdline.SetArgs({kFakeFuzzerUrl, "data/dictionary"}));
  EXPECT_EQ(engine.Initialize(&cmdline.argc, &cmdline.argv), ZX_OK);
  EXPECT_EQ(engine.Run(std::move(context), runner), ZX_ERR_INVALID_ARGS);
}

TEST(EngineTest, RunFuzzer) {
  Engine engine("/tmp/fuzzer");
  FakeCmdline cmdline;
  std::vector<Input> corpus;

  auto context = ComponentContextForTest::Create();
  FakeRegistrar registrar(context->executor());
  auto* context_for_test = static_cast<ComponentContextForTest*>(context.get());
  context_for_test->PutChannel(0, registrar.NewBinding().TakeChannel());
  auto runner = FakeRunner::MakePtr(context->executor());
  auto fake_runner = std::static_pointer_cast<FakeRunner>(runner);

  ASSERT_NO_FATAL_FAILURE(MakeCorpus("/tmp/fuzzer/data/corpus1", {"foo", "bar"}, &corpus));
  ASSERT_NO_FATAL_FAILURE(MakeCorpus("/tmp/fuzzer/data/corpus2", {"baz", "qux", "quux"}, &corpus));
  ASSERT_NO_FATAL_FAILURE(
      WriteInput("/tmp/fuzzer/data/dictionary", FakeRunner::valid_dictionary()));
  ASSERT_TRUE(
      cmdline.SetArgs({kFakeFuzzerUrl, "--fuzz", "-libfuzzer=flag", "data/corpus1", "data/corpus2",
                       "data/dictionary", "--", "data/invalid-dictionary"}));
  EXPECT_EQ(engine.Initialize(&cmdline.argc, &cmdline.argv), ZX_OK);
  EXPECT_EQ(engine.Run(std::move(context), runner), ZX_OK);
  EXPECT_EQ(cmdline.GetArgs(), "-libfuzzer=flag data/invalid-dictionary");
  EXPECT_EQ(SortInputs(runner->GetCorpus(CorpusType::SEED)), corpus);
}

TEST(EngineTest, RunTest) {
  Engine engine("/tmp/test");
  FakeCmdline cmdline;
  std::vector<Input> corpus;

  auto context = ComponentContextForTest::Create();
  auto runner = FakeRunner::MakePtr(context->executor());
  auto fake_runner = std::static_pointer_cast<FakeRunner>(runner);

  ASSERT_NO_FATAL_FAILURE(MakeCorpus("/tmp/test/data/corpus1", {"foo", "bar"}, &corpus));
  ASSERT_NO_FATAL_FAILURE(MakeCorpus("/tmp/test/data/corpus2", {"baz", "qux", "quux"}, &corpus));
  ASSERT_NO_FATAL_FAILURE(WriteInput("/tmp/test/data/dictionary", FakeRunner::valid_dictionary()));
  ASSERT_TRUE(cmdline.SetArgs({kFakeFuzzerUrl, "-libfuzzer=flag", "data/corpus1", "data/corpus2",
                               "data/dictionary", "--", "data/invalid-dictionary"}));
  EXPECT_EQ(engine.Initialize(&cmdline.argc, &cmdline.argv), ZX_OK);
  EXPECT_EQ(engine.Run(std::move(context), runner), ZX_OK);
  EXPECT_EQ(cmdline.GetArgs(), "-libfuzzer=flag data/invalid-dictionary");
  EXPECT_EQ(SortInputs(fake_runner->get_inputs()), corpus);
}

}  // namespace fuzzing
