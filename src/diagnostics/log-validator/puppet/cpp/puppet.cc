// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/diagnostics/stream/cpp/fidl.h>
#include <fuchsia/validate/logs/cpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/fidl/cpp/binding_set.h>
#include <lib/sys/cpp/component_context.h>

#include <string>

#include <sdk/lib/syslog/streams/cpp/encode.h>

#include "src/lib/fsl/vmo/vector.h"

class Puppet : public fuchsia::validate::logs::Validate {
 public:
  explicit Puppet(std::unique_ptr<sys::ComponentContext> context) : context_(std::move(context)) {
    context_->outgoing()->AddPublicService(bindings_.GetHandler(this));
  }

  void Log(fuchsia::diagnostics::stream::Record record, LogCallback callback) override {
    std::vector<uint8_t> buffer;
    streams::log_record(record, &buffer);
    fuchsia::mem::Buffer read_buffer;
    fsl::VmoFromVector(buffer, &read_buffer);
    fuchsia::validate::logs::Validate_Log_Result result;
    fuchsia::validate::logs::Validate_Log_Response response{std::move(read_buffer)};
    result.set_response(std::move(response));
    callback(std::move(result));
  }

 private:
  std::unique_ptr<sys::ComponentContext> context_;
  fidl::BindingSet<fuchsia::validate::logs::Validate> bindings_;
};

int main(int argc, const char** argv) {
  async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);
  Puppet puppet(sys::ComponentContext::CreateAndServeOutgoingDirectory());
  loop.Run();
}
