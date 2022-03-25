// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_SYS_FUZZING_COMMON_TESTING_REGISTRAR_H_
#define SRC_SYS_FUZZING_COMMON_TESTING_REGISTRAR_H_

#include <fuchsia/fuzzer/cpp/fidl.h>
#include <lib/fidl/cpp/binding.h>
#include <lib/fidl/cpp/interface_handle.h>
#include <lib/fidl/cpp/interface_request.h>
#include <stdint.h>

#include <string>

#include "src/lib/fxl/macros.h"
#include "src/sys/fuzzing/common/async-types.h"

namespace fuzzing {

using ::fuchsia::fuzzer::Controller;
using ::fuchsia::fuzzer::ControllerProvider;
using ::fuchsia::fuzzer::Registrar;

class FakeRegistrar final : public Registrar {
 public:
  explicit FakeRegistrar(ExecutorPtr executor);
  ~FakeRegistrar() override = default;

  // Returns a channel to this object's implementation of |fuchsia.fuzzer.Registrar|.
  zx::channel Bind();

  // FIDL methods.
  void Register(fidl::InterfaceHandle<ControllerProvider> provider,
                RegisterCallback callback) override;

  fidl::InterfaceHandle<ControllerProvider> TakeProvider();

 private:
  fidl::Binding<Registrar> binding_;
  ExecutorPtr executor_;
  fidl::InterfaceHandle<ControllerProvider> provider_;

  FXL_DISALLOW_COPY_ASSIGN_AND_MOVE(FakeRegistrar);
};

}  // namespace fuzzing

#endif  // SRC_SYS_FUZZING_COMMON_TESTING_REGISTRAR_H_
