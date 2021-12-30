// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_SYS_FUZZING_LIBFUZZER_TESTING_RELAY_H_
#define SRC_SYS_FUZZING_LIBFUZZER_TESTING_RELAY_H_

#include <lib/fidl/cpp/binding_set.h>
#include <lib/fidl/cpp/interface_request.h>
#include <lib/sync/completion.h>

#include <vector>

#include <test/fuzzer/cpp/fidl.h>

#include "src/lib/fxl/macros.h"

namespace fuzzing {

using ::test::fuzzer::Relay;
using ::test::fuzzer::SignaledBuffer;
using ::test::fuzzer::SignaledBufferPtr;

class RelayImpl : public Relay {
 public:
  RelayImpl() = default;
  ~RelayImpl() override = default;

  // FIDL methods.
  fidl::InterfaceRequestHandler<Relay> GetHandler(async_dispatcher_t* dispatcher);
  void SetTestData(SignaledBuffer data) override;
  void WatchTestData(WatchTestDataCallback callback) override;

 private:
  void MaybeCallback();

  fidl::BindingSet<Relay> bindings_;
  WatchTestDataCallback callback_;
  SignaledBufferPtr data_;

  FXL_DISALLOW_COPY_ASSIGN_AND_MOVE(RelayImpl);
};

}  // namespace fuzzing

#endif  // SRC_SYS_FUZZING_LIBFUZZER_TESTING_RELAY_H_
