// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/sys/fuzzing/common/controller.h"

#include <lib/syslog/cpp/macros.h>
#include <zircon/sanitizer.h>
#include <zircon/status.h>

#include "src/sys/fuzzing/common/async-types.h"
#include "src/sys/fuzzing/common/corpus-reader-client.h"
#include "src/sys/fuzzing/common/options.h"

namespace fuzzing {

ControllerImpl::ControllerImpl()
    : binding_(this),
      close_([this]() { CloseImpl(); }),
      interrupt_([this]() { InterruptImpl(); }),
      join_([this]() { JoinImpl(); }) {
  dispatcher_ = binding_.dispatcher();
  executor_ = std::make_unique<async::Executor>(dispatcher_->get());
  options_ = std::make_shared<Options>();
  transceiver_ = std::make_shared<Transceiver>();
}

ControllerImpl::~ControllerImpl() {
  Close();
  Interrupt();
  Join();
}

void ControllerImpl::Bind(fidl::InterfaceRequest<Controller> request) {
  FX_DCHECK(runner_);
  binding_.Bind(std::move(request));
}

void ControllerImpl::SetRunner(std::unique_ptr<Runner> runner) {
  runner_ = std::move(runner);
  AddDefaults();
  runner_->Configure(options_);
}

void ControllerImpl::AddDefaults() {
  if (!options_->has_seed()) {
    options_->set_seed(static_cast<uint32_t>(zx::ticks::now().get()));
  }
  runner_->AddDefaults(options_.get());
}

void ControllerImpl::ReceiveAndThen(FidlInput fidl_input, Response response,
                                    fit::function<void(Input, Response)> callback) {
  transceiver_->Receive(std::move(fidl_input),
                        [response = std::move(response), callback = std::move(callback)](
                            zx_status_t status, Input received) mutable {
                          if (status != ZX_OK) {
                            response.Send(status);
                          } else {
                            callback(std::move(received), std::move(response));
                          }
                        });
}

///////////////////////////////////////////////////////////////
// FIDL methods.

void ControllerImpl::Configure(Options options, ConfigureCallback callback) {
  *options_ = std::move(options);
  AddDefaults();
  callback(runner_->Configure(options_));
}

void ControllerImpl::GetOptions(GetOptionsCallback callback) {
  auto options = CopyOptions(*options_);
  callback(std::move(options));
}

void ControllerImpl::AddToCorpus(CorpusType corpus_type, FidlInput fidl_input,
                                 AddToCorpusCallback callback) {
  ReceiveAndThen(std::move(fidl_input), NewResponse(std::move(callback)),
                 [this, corpus_type](Input received, Response response) {
                   response.Send(runner_->AddToCorpus(corpus_type, std::move(received)));
                 });
}

void ControllerImpl::ReadCorpus(CorpusType corpus_type, fidl::InterfaceHandle<CorpusReader> reader,
                                ReadCorpusCallback callback) {
  std::vector<Input> inputs;
  for (size_t offset = 1;; ++offset) {
    auto input = runner_->ReadFromCorpus(corpus_type, offset);
    if (input.size() == 0) {
      break;
    }
    inputs.emplace_back(std::move(input));
  }
  auto client = std::make_unique<CorpusReaderClient>(executor_);
  client->Bind(std::move(reader));

  // Prevent the client from going out of scope before the promise completes.
  auto task = fpromise::make_promise(
      [inputs = std::move(inputs), client = std::move(client), callback = std::move(callback),
       sending = ZxFuture<>()](Context& context) mutable -> Result<> {
        if (!sending) {
          sending = client->Send(std::move(inputs));
        }
        if (!sending(context)) {
          return fpromise::pending();
        }
        callback();
        return fpromise::ok();
      });
  executor_->schedule_task(std::move(task));
}

void ControllerImpl::WriteDictionary(FidlInput dictionary, WriteDictionaryCallback callback) {
  ReceiveAndThen(std::move(dictionary), NewResponse(std::move(callback)),
                 [this](Input received, Response response) {
                   response.Send(runner_->ParseDictionary(received));
                 });
}

void ControllerImpl::ReadDictionary(ReadDictionaryCallback callback) {
  auto response = NewResponse(std::move(callback));
  response.Send(ZX_OK, FuzzResult::NO_ERRORS, runner_->GetDictionaryAsInput());
}

void ControllerImpl::GetStatus(GetStatusCallback callback) { callback(runner_->CollectStatus()); }

void ControllerImpl::AddMonitor(fidl::InterfaceHandle<Monitor> monitor,
                                AddMonitorCallback callback) {
  FX_DCHECK(runner_);
  runner_->AddMonitor(std::move(monitor));
  callback();
}

void ControllerImpl::GetResults(GetResultsCallback callback) {
  auto response = NewResponse(std::move(callback));
  response.Send(ZX_OK, runner_->result(), runner_->result_input());
}

void ControllerImpl::Execute(FidlInput fidl_input, ExecuteCallback callback) {
  ReceiveAndThen(std::move(fidl_input), NewResponse(std::move(callback)),
                 [this](Input received, Response response) {
                   runner_->Execute(std::move(received), [this, response = std::move(response)](
                                                             zx_status_t status) mutable {
                     response.Send(status, runner_->result(), runner_->result_input());
                   });
                 });
}

void ControllerImpl::Minimize(FidlInput fidl_input, MinimizeCallback callback) {
  ReceiveAndThen(std::move(fidl_input), NewResponse(std::move(callback)),
                 [this](Input received, Response response) {
                   runner_->Minimize(std::move(received), [this, response = std::move(response)](
                                                              zx_status_t status) mutable {
                     response.Send(status, runner_->result(), runner_->result_input());
                   });
                 });
}

void ControllerImpl::Cleanse(FidlInput fidl_input, CleanseCallback callback) {
  ReceiveAndThen(std::move(fidl_input), NewResponse(std::move(callback)),
                 [this](Input received, Response response) {
                   runner_->Cleanse(std::move(received), [this, response = std::move(response)](
                                                             zx_status_t status) mutable {
                     response.Send(status, runner_->result(), runner_->result_input());
                   });
                 });
}

void ControllerImpl::Fuzz(FuzzCallback callback) {
  runner_->Fuzz([this, response = NewResponse(std::move(callback))](zx_status_t status) mutable {
    response.Send(status, runner_->result(), runner_->result_input());
  });
}

void ControllerImpl::Merge(MergeCallback callback) {
  runner_->Merge([response = NewResponse(std::move(callback))](zx_status_t status) mutable {
    response.Send(status);
  });
}

void ControllerImpl::CloseImpl() {
  binding_.Unbind();
  if (runner_) {
    runner_->Close();
  }
  transceiver_->Close();
}

void ControllerImpl::InterruptImpl() {
  if (runner_) {
    runner_->Interrupt();
  }
}

void ControllerImpl::JoinImpl() {
  if (runner_) {
    runner_->Join();
  }
  transceiver_->Join();
}

}  // namespace fuzzing
