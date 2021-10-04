// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/sys/fuzzing/common/controller.h"

#include <lib/syslog/cpp/macros.h>
#include <zircon/sanitizer.h>
#include <zircon/status.h>

#include "src/sys/fuzzing/common/options.h"

namespace fuzzing {

ControllerImpl::ControllerImpl() : binding_(this, std::make_shared<Dispatcher>()) {
  dispatcher_ = binding_.dispatcher();
  options_ = std::make_shared<Options>();
  transceiver_ = std::make_shared<Transceiver>();
  reader_ = std::thread([this]() { ReadCorpusImpl(); });
}

ControllerImpl::~ControllerImpl() {
  // No new requests.
  dispatcher_->Shutdown();
  // Finish sending the corpus.
  {
    std::lock_guard<std::mutex> lock(mutex_);
    reading_ = false;
    sync_completion_signal(&pending_readers_);
  }
  reader_.join();
  // Finish up data transfers.
  transceiver_->Shutdown();
}

void ControllerImpl::SetRunner(const std::shared_ptr<Runner>& runner) {
  runner_ = runner;
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

void ControllerImpl::Bind(fidl::InterfaceRequest<Controller> request) {
  FX_DCHECK(runner_);
  binding_.Bind(std::move(request));
}

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
  CorpusReaderPtr ptr;
  auto status = ptr.Bind(std::move(reader), dispatcher_->get());
  if (status != ZX_OK) {
    FX_LOGS(WARNING) << "Failed to bind to CorpusReader: " << zx_status_get_string(status);
    return;
  }
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (reading_) {
      readers_.emplace_back(std::make_pair(corpus_type, std::move(ptr)));
      sync_completion_signal(&pending_readers_);
    }
  }
  // If |!reading|, the |ptr| will be dropped, signalling no further inputs to the |CorpusReader|.
  callback();
}

void ControllerImpl::ReadCorpusImpl() {
  while (true) {
    // |pending_readers_| will be signalled on object destruction.
    sync_completion_wait(&pending_readers_, ZX_TIME_INFINITE);
    CorpusReaderRequest request;
    {
      std::lock_guard<std::mutex> lock(mutex_);
      if (readers_.empty()) {
        return;
      }
      request = std::move(readers_.front());
      readers_.pop_front();
      if (reading_ && readers_.empty()) {
        sync_completion_reset(&pending_readers_);
      }
    }
    CorpusType corpus_type = request.first;
    CorpusReaderPtr reader = std::move(request.second);
    sync_completion_t sync;
    size_t offset = 1;
    for (bool has_more = true; has_more;) {
      auto input = runner_->ReadFromCorpus(corpus_type, offset++);
      zx_status_t result;
      auto status = transceiver_->Transmit(input, [&reader, &result, &sync](FidlInput fidl_input) {
        reader->Next(std::move(fidl_input), [&result, &sync](zx_status_t tx_result) {
          result = tx_result;
          sync_completion_signal(&sync);
        });
      });
      if (status == ZX_OK) {
        sync_completion_wait(&sync, ZX_TIME_INFINITE);
        sync_completion_reset(&sync);
        status = result;
      }
      if (status != ZX_OK) {
        FX_LOGS(WARNING) << "Failed to read from corpus: " << zx_status_get_string(status);
        break;
      }
      has_more = input.size() != 0;
    }
  }
}

void ControllerImpl::WriteDictionary(FidlInput dictionary, WriteDictionaryCallback callback) {
  ReceiveAndThen(std::move(dictionary), NewResponse(std::move(callback)),
                 [this](Input received, Response response) {
                   response.Send(runner_->ParseDictionary(received));
                 });
}

void ControllerImpl::ReadDictionary(ReadDictionaryCallback callback) {
  auto response = NewResponse(std::move(callback));
  response.Send(ZX_OK, Result::NO_ERRORS, runner_->GetDictionaryAsInput());
}

void ControllerImpl::GetStatus(GetStatusCallback callback) { callback(runner_->CollectStatus()); }

void ControllerImpl::AddMonitor(fidl::InterfaceHandle<Monitor> monitor,
                                AddMonitorCallback callback) {
  MonitorPtr ptr;
  ptr.Bind(std::move(monitor), dispatcher_->get());
  runner_->AddMonitor(std::move(ptr));
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

}  // namespace fuzzing
