// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_SYS_FUZZING_COMMON_ENGINE_H_
#define SRC_SYS_FUZZING_COMMON_ENGINE_H_

#include <string>
#include <vector>

#include "src/lib/pkg_url/fuchsia_pkg_url.h"
#include "src/sys/fuzzing/common/component-context.h"
#include "src/sys/fuzzing/common/runner.h"

namespace fuzzing {

// The |Engine| represents a generic fuzzing engine. Specific engines with specific runners should
// call |RunEngine| with command line arguments and a |Runner| factory method.
class Engine {
 public:
  Engine();
  explicit Engine(const std::string& pkg_dir);
  ~Engine() = default;

  // Accessors and mutators for testing.
  std::string url() const { return url_ ? url_->ToString() : std::string(); }
  bool fuzzing() const { return fuzzing_; }
  const std::vector<Input>& corpus() { return corpus_; }
  Input dictionary() const { return dictionary_.Duplicate(); }
  void set_pkg_dir(const std::string& pkg_dir) { pkg_dir_ = pkg_dir; }

  // Parses the command line and extracts recognized arguments from it.
  __WARN_UNUSED_RESULT zx_status_t Initialize(int* pargc, char*** pargv);

  // Runs the engine.
  __WARN_UNUSED_RESULT zx_status_t Run(ComponentContextPtr context, RunnerPtr runner);

 private:
  // Runs the engine in "fuzzing" mode: the engine will serve `fuchsia.fuzzer.ControllerProvider`
  // and fulfill `fuchsia.fuzzer.Controller` requests.
  __WARN_UNUSED_RESULT zx_status_t RunFuzzer(ComponentContextPtr context, RunnerPtr runner,
                                             const std::string& url);

  // Runs the engine in "test" mode: the engine will execute the fuzzer with the set of inputs
  // given by seed corpora listed in the fuzzer's command line arguments.
  __WARN_UNUSED_RESULT zx_status_t RunTest(ComponentContextPtr context, RunnerPtr runner);

  std::string pkg_dir_;
  std::unique_ptr<component::FuchsiaPkgUrl> url_;
  bool fuzzing_ = false;
  std::vector<Input> corpus_;
  Input dictionary_;
};

// Starts the engine with runner provided by |MakeRunnerPtr|, which should have the signature:
// `ZxResult<RunnerPtr>(int, char**, ComponentContext&)`. This should be called from `main`, and
// the first two parameters should be |argc| and |argv|, respectively.
template <typename RunnerPtrMaker>
zx_status_t RunEngine(int argc, char** argv, RunnerPtrMaker MakeRunnerPtr) {
  Engine engine;
  if (auto status = engine.Initialize(&argc, &argv); status != ZX_OK) {
    return status;
  }
  auto context = ComponentContext::Create();
  auto result = MakeRunnerPtr(argc, argv, *context);
  if (result.is_error()) {
    return result.error();
  }
  return engine.Run(std::move(context), result.take_value());
}

}  // namespace fuzzing

#endif  // SRC_SYS_FUZZING_COMMON_ENGINE_H_
