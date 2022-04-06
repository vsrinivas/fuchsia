// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_SYS_FUZZING_LIBFUZZER_TESTING_RELAY_H_
#define SRC_SYS_FUZZING_LIBFUZZER_TESTING_RELAY_H_

#include <lib/fidl/cpp/binding_set.h>
#include <lib/fidl/cpp/interface_request.h>

#include <test/fuzzer/cpp/fidl.h>

#include "src/lib/fxl/macros.h"
#include "src/sys/fuzzing/common/async-types.h"

namespace fuzzing {

using ::test::fuzzer::Relay;
using ::test::fuzzer::SignaledBuffer;

class RelayImpl : public Relay {
 public:
  explicit RelayImpl(ExecutorPtr executor);
  ~RelayImpl() override = default;

  // FIDL methods.
  fidl::InterfaceRequestHandler<Relay> GetHandler();
  void SetTestData(SignaledBuffer data, SetTestDataCallback callback) override;
  void WatchTestData(WatchTestDataCallback callback) override;
  void Finish() override;

 private:
  fidl::BindingSet<Relay> bindings_;
  ExecutorPtr executor_;
  Completer<SignaledBuffer> completer_;
  Consumer<SignaledBuffer> consumer_;
  Completer<> finish_;

  FXL_DISALLOW_COPY_ASSIGN_AND_MOVE(RelayImpl);
};

}  // namespace fuzzing

#endif  // SRC_SYS_FUZZING_LIBFUZZER_TESTING_RELAY_H_
