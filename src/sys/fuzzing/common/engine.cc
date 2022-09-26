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
  *pargv = argc > 1 ? &argv[1] : nullptr;
  *pargc = argc > 0 ? (argc - 1) : 0;
  return std::string(arg);
}

}  // namespace

Engine::Engine() : Engine("/pkg") {}

Engine::Engine(const std::string& pkg_dir) : pkg_dir_(pkg_dir) {}

zx_status_t Engine::Initialize(int* pargc, char*** pargv) {
  url_.reset();
  fuzzing_ = false;
  corpus_.clear();
  dictionary_ = Input();

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
    // Escape hatch.
    if (strcmp(arg, "--") == 0) {
      ConsumeArg(pargc, pargv, arg);
      break;
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

    // Ignore remaining arguments except data files that need to be imported.
    if (strncmp(arg, "data", 4) != 0) {
      continue;
    }
    auto pathname = files::JoinPath(pkg_dir_, ConsumeArg(pargc, pargv, arg));

    // A file argument is a dictionary.
    if (files::IsFile(pathname)) {
      if (dictionary_.size() != 0) {
        FX_LOGS(WARNING) << "Multiple dictionaries found: " << arg;
        return ZX_ERR_INVALID_ARGS;
      }
      std::vector<uint8_t> data;
      if (!files::ReadFileToVector(pathname, &data)) {
        FX_LOGS(WARNING) << "Failed to read dictionary '" << pathname << "': " << strerror(errno);
        return ZX_ERR_IO;
      }
      dictionary_ = Input(data);
      continue;
    }

    // Directory arguments are seed corpora.
    if (files::IsDirectory(pathname)) {
      std::vector<std::string> filenames;
      if (!files::ReadDirContents(pathname, &filenames)) {
        FX_LOGS(WARNING) << "Failed to read seed corpus '" << pathname << "': " << strerror(errno);
        return ZX_ERR_IO;
      }
      for (const auto& filename : filenames) {
        auto input_file = files::JoinPath(pathname, filename);
        if (!files::IsFile(input_file)) {
          continue;
        }
        std::vector<uint8_t> data;
        if (!files::ReadFileToVector(input_file, &data)) {
          FX_LOGS(WARNING) << "Failed to read input '" << input_file << "': " << strerror(errno);
          return ZX_ERR_IO;
        }
        corpus_.push_back(Input(data));
      }
      continue;
    }

    // No other positional arguments are supported.
    FX_LOGS(WARNING) << "Invalid package path: " << pathname;
    return ZX_ERR_NOT_FOUND;
  }
  if (!url_) {
    FX_LOGS(WARNING) << "Missing required URL.";
    return ZX_ERR_INVALID_ARGS;
  }
  return ZX_OK;
}

zx_status_t Engine::Run(ComponentContextPtr context, RunnerPtr runner) {
  if (!url_) {
    FX_LOGS(WARNING) << "Not initialized.";
    return ZX_ERR_BAD_STATE;
  }
  auto url = url_->ToString();
  url_.reset();

  if (dictionary_.size() != 0) {
    if (auto status = runner->ParseDictionary(dictionary_); status != ZX_OK) {
      return status;
    }
  }

  if (fuzzing_) {
    return RunFuzzer(std::move(context), std::move(runner), url);
  } else {
    return RunTest(std::move(context), std::move(runner));
  }
}

zx_status_t Engine::RunFuzzer(ComponentContextPtr context, RunnerPtr runner,
                              const std::string& url) {
  for (auto& input : corpus_) {
    if (auto status = runner->AddToCorpus(CorpusType::SEED, std::move(input)); status != ZX_OK) {
      return status;
    }
  }
  ControllerProviderImpl provider(context->executor());
  provider.SetRunner(std::move(runner));
  auto task = provider.Serve(url, context->TakeChannel(0));
  context->ScheduleTask(std::move(task));
  return context->Run();
}

zx_status_t Engine::RunTest(ComponentContextPtr context, RunnerPtr runner) {
  corpus_.emplace_back(Input());
  FX_LOGS(INFO) << "Testing with " << corpus_.size() << " inputs.";

  auto options = MakeOptions();
  runner->OverrideDefaults(options.get());

  // TODO(fxbug.dev/109100): Rarely, spawned process output may be truncated. `LibFuzzerRunner`
  // needs to return `ZX_ERR_IO_INVALID` in this case. By retying several times, the probability of
  // the underlying flake failing a test drops to almost zero.
  static constexpr const size_t kFuzzerTestRetries = 10;

  // In order to make this more testable, the following task does not exit directly. Instead, it
  // repeatedly calls |RunUntilIdle| until it has set an exit code. This allows this method to be
  // called as part of a gTest as well as by the elf_test_runner.
  zx_status_t exitcode = ZX_ERR_NEXT;
  auto task = runner->Configure(options)
                  .and_then([runner, corpus = std::move(corpus_), execute = ZxFuture<FuzzResult>(),
                             attempts = 0U](Context& context) mutable -> ZxResult<FuzzResult> {
                    while (attempts < kFuzzerTestRetries) {
                      if (!execute) {
                        execute = runner->Execute(std::move(corpus));
                      }
                      if (!execute(context)) {
                        return fpromise::pending();
                      }
                      if (execute.is_ok()) {
                        return fpromise::ok(execute.take_value());
                      }
                      if (auto status = execute.take_error(); status != ZX_ERR_IO_INVALID) {
                        return fpromise::error(status);
                      }
                      attempts++;
                    }
                    return fpromise::error(ZX_ERR_IO_INVALID);
                  })
                  .then([&exitcode](ZxResult<FuzzResult>& result) {
                    if (result.is_error()) {
                      exitcode = result.error();
                      return;
                    }
                    auto fuzz_result = result.take_value();
                    exitcode = (fuzz_result == FuzzResult::NO_ERRORS) ? 0 : fuzz_result;
                  });
  context->ScheduleTask(std::move(task));
  while (exitcode == ZX_ERR_NEXT) {
    if (auto status = context->RunUntilIdle(); status != ZX_OK) {
      return status;
    }
  }
  return exitcode;
}

}  // namespace fuzzing
