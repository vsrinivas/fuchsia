// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "llvm-fuzzer.h"

#include <lib/syslog/cpp/macros.h>
#include <string.h>
#include <zircon/errors.h>
#include <zircon/status.h>

#include "libfuzzer.h"

namespace fuzzing {
namespace {

// Dummy argv[0] used when invoking |LLVMFuzzerInitialize|. Use it to warn the user-provided that
// argv[0] cannot be modified, or libFuzzer will encounter a fatal error.
const char *kArgv0 = "argv0-cannot-be-modified";

}  // namespace

// Public methods

LlvmFuzzerImpl::LlvmFuzzerImpl() : binding_(this) {}

LlvmFuzzerImpl::~LlvmFuzzerImpl() {}

fidl::InterfaceRequestHandler<LlvmFuzzer> LlvmFuzzerImpl::GetHandler(
    async_dispatcher_t *dispatcher) {
  return [this, dispatcher](fidl::InterfaceRequest<LlvmFuzzer> request) {
    if (!binding_.is_bound()) {
      binding_.Bind(std::move(request), dispatcher);
    }
  };
}

void LlvmFuzzerImpl::Initialize(zx::vmo vmo, std::vector<std::string> options,
                                InitializeCallback callback) {
  zx_status_t status = ZX_OK;
  if (input_.is_mapped()) {
    FX_LOGS(ERROR) << "Already initialized.";
    status = ZX_ERR_BAD_STATE;

  } else if ((status = input_.Link(std::move(vmo))) != ZX_OK) {
    FX_LOGS(ERROR) << "Failed to link shared test input memory: " << zx_status_get_string(status);

  } else if (LLVMFuzzerInitialize != nullptr) {
    int argc = options.size() + 1;
    std::vector<char *> c_strs;
    c_strs.reserve(argc);
    c_strs.push_back(const_cast<char *>(kArgv0));
    std::transform(options.begin(), options.end(), std::back_inserter(c_strs),
                   [](const std::string &s) { return const_cast<char *>(s.c_str()); });
    char **argv = &c_strs[0];

    // |LLVMFuzzerInitialize| MUST NOT deallocate any memory passed to it, and is responsible for
    // any memory it allocates. This matches the "normal" behavior in a single-process fuzzer, where
    // argv would normally refer to stack locations.
    status = LLVMFuzzerInitialize(&argc, &argv);

    std::vector<std::string> mods;
    mods.reserve(argc);
    for (int i = 1; i < argc; ++i) {
      mods.push_back(std::string(argv[i]));
    }
    options = std::move(mods);
  }
  callback(status, std::move(options));
}

void LlvmFuzzerImpl::TestOneInput(TestOneInputCallback callback) {
  callback(LLVMFuzzerTestOneInput(input_.data(), input_.size()));
}

void LlvmFuzzerImpl::Reset() { input_.Reset(); }

}  // namespace fuzzing
