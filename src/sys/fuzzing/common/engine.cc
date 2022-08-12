// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/sys/fuzzing/common/engine.h"

#include "src/lib/files/directory.h"
#include "src/lib/files/file.h"
#include "src/lib/files/path.h"
#include "src/sys/fuzzing/common/controller-provider.h"

namespace fuzzing {

using ::fuchsia::fuzzer::FUZZ_MODE;

namespace {

// Removes the string at |offset| from |argv|, and updates |argc| and |argv| by decrementing by 1
// and shifting other elements, respectively. Returns the removed string.
std::string ConsumeArg(int* pargc, char*** pargv, char* arg) {
  int argc = *pargc;
  char** argv = *pargv;
  auto i = 0;
  while (i < argc && argv[i] != arg) {
    ++i;
  }
  for (; i != 0; --i) {
    argv[i] = argv[i - 1];
  }
  if (argc > 1) {
    *pargv = &argv[1];
  }
  if (argc > 0) {
    *pargc = argc - 1;
  }
  return std::string(arg);
}

}  // namespace

zx_status_t Engine::Initialize(int* pargc, char*** pargv) {
  int argc = *pargc;
  char** argv = *pargv;
  for (int i = 1; i < argc; ++i) {
    char* arg = argv[i];
    // First, look for the fuzzing indicator. This is typically provided by `fuzz-manager`.
    if (strcmp(arg, FUZZ_MODE) == 0) {
      fuzzing_ = true;
      ConsumeArg(pargc, pargv, arg);
      continue;
    }
    // Skip any remaining flags.
    if (arg[0] == '-') {
      continue;
    }
    // First positional argument is the fuzzer URL.
    if (!url_) {
      url_ = std::make_unique<component::FuchsiaPkgUrl>();
      auto url = ConsumeArg(pargc, pargv, arg);
      if (!url_->Parse(url)) {
        FX_LOGS(WARNING) << "Failed to parse URL: " << url;
        return ZX_ERR_INVALID_ARGS;
      }
      continue;
    }
    // Directory arguments are seed corpora.
    if (files::IsDirectory(arg)) {
      std::vector<std::string> filenames;
      auto dirname = ConsumeArg(pargc, pargv, arg);
      if (!files::ReadDirContents(dirname, &filenames)) {
        FX_LOGS(WARNING) << "Failed to read seed corpus '" << dirname << "': " << strerror(errno);
        return ZX_ERR_INVALID_ARGS;
      }
      for (const auto& filename : filenames) {
        auto pathname = files::JoinPath(dirname, filename);
        std::vector<uint8_t> data;
        if (!files::ReadFileToVector(pathname, &data)) {
          FX_LOGS(WARNING) << "Failed to read test input '" << pathname << "': " << strerror(errno);
          return ZX_ERR_IO;
        }
        inputs_.push_back(Input(data));
      }
    }
  }
  return ZX_OK;
}

zx_status_t Engine::Run(ComponentContextPtr context, RunnerPtr runner) {
  if (fuzzing_) {
    return RunFuzzer(std::move(context), std::move(runner));
  } else {
    return RunTest(std::move(context), std::move(runner));
  }
}

zx_status_t Engine::RunFuzzer(ComponentContextPtr context, RunnerPtr runner) {
  if (!url_) {
    FX_LOGS(WARNING) << "Missing 'url' argument";
    return ZX_ERR_INVALID_ARGS;
  }
  for (auto& input : inputs_) {
    if (auto status = runner->AddToCorpus(CorpusType::SEED, std::move(input)); status != ZX_OK) {
      continue;
    }
  }
  inputs_.clear();
  ControllerProviderImpl provider(context->executor());
  provider.SetRunner(std::move(runner));
  auto task = provider.Serve(url_->ToString(), context->TakeChannel(0));
  context->ScheduleTask(std::move(task));
  return context->Run();
}

zx_status_t Engine::RunTest(ComponentContextPtr context, RunnerPtr runner) {
  auto inputs = std::move(inputs_);
  inputs.emplace_back(Input());
  inputs_.clear();
  FX_LOGS(INFO) << "Testing with " << inputs_.size() << " inputs.";

  auto options = MakeOptions();
  runner->OverrideDefaults(options.get());
  auto task = runner->Configure(options)
                  .and_then([runner, inputs = std::move(inputs), execute = ZxFuture<FuzzResult>()](
                                Context& context) mutable -> ZxResult<FuzzResult> {
                    if (!execute) {
                      execute = runner->Execute(std::move(inputs));
                    }
                    if (!execute(context)) {
                      return fpromise::pending();
                    }
                    return execute.take_result();
                  })
                  .then([](ZxResult<FuzzResult>& result) {
                    if (result.is_error()) {
                      exit(result.error());
                    }
                    auto fuzz_result = result.take_value();
                    if (fuzz_result != FuzzResult::NO_ERRORS) {
                      exit(fuzz_result);
                    }
                    exit(0);
                  });
  context->ScheduleTask(std::move(task));
  return context->Run();
}

}  // namespace fuzzing
