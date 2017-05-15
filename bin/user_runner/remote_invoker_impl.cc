// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/modular/src/user_runner/remote_invoker_impl.h"

#include <chrono>

#include "apps/ledger/services/public/ledger.fidl.h"
#include "apps/modular/lib/fidl/array_to_string.h"
#include "apps/modular/lib/fidl/json_xdr.h"
#include "apps/modular/lib/ledger/storage.h"
#include "lib/fidl/cpp/bindings/array.h"

namespace modular {

namespace {
struct StoryEntry {
  std::string story_id;
  std::string timestamp;
};

void XdrStoryData(XdrContext* const xdr, StoryEntry* const data) {
  xdr->Field("story_id", &data->story_id);
  xdr->Field("timestamp", &data->timestamp);
}
}  // namespace

// Asynchronous operations of this service.

class RemoteInvokerImpl::StartOnDeviceCall : Operation<fidl::String> {
 public:
  StartOnDeviceCall(OperationContainer* const container,
                    ledger::Ledger* const ledger,
                    const fidl::String& device_id,
                    const fidl::String& story_id,
                    ResultCall result_call)
      : Operation(container, std::move(result_call)),
        ledger_(ledger),
        device_id_(device_id),
        story_id_(story_id),
        timestamp_(std::to_string(
            std::chrono::system_clock::now().time_since_epoch().count())) {
    Ready();
  }

 private:
  void Run() override {
    // TODO(planders) Use Zac's function to generate page id (once it's ready)
    ledger_->GetPage(to_array(device_id_), device_page_.NewRequest(),
                     [this](ledger::Status status) {
                       if (status != ledger::Status::OK) {
                         FTL_LOG(ERROR)
                             << "Ledger GetPage returned status: " << status;
                         Finish();
                       } else {
                         Cont1();
                       }
                     });
  }

  void Cont1() {
    device_page_->StartTransaction([this](ledger::Status status) {
      if (status != ledger::Status::OK) {
        FTL_LOG(ERROR) << "Ledger StartTransaction returned status: " << status;
        Finish();
      } else {
        Cont2();
      }
    });
  }

  void Cont2() {
    std::string json;
    StoryEntry story;
    story.story_id = story_id_;
    story.timestamp = timestamp_;
    XdrWrite(&json, &story, XdrStoryData);

    // TODO(planders) use random key
    device_page_->PutWithPriority(
        to_array(timestamp_), to_array(json), ledger::Priority::EAGER,
        [this](ledger::Status status) {
          if (status != ledger::Status::OK) {
            FTL_LOG(ERROR) << "Ledger PutWithPriority returned status: "
                           << status;
            Finish();
          } else {
            Cont3();
          }
        });
  }

  void Cont3() {
    device_page_->Commit([this](ledger::Status status) {
      if (status != ledger::Status::OK) {
        FTL_LOG(ERROR) << "Ledger Commit returned status: " << status;
        Finish();
      } else {
        Cont4();
      }
    });
  }

  void Cont4() {
    device_page_->GetId([this](fidl::Array<uint8_t> page_id) {
      FTL_LOG(INFO) << "Retrieved page " << to_string(page_id);
      page_id_ = to_string(page_id);
      Finish();
    });
  }

  void Finish() { Done(std::move(page_id_)); }

  ledger::Ledger* const ledger_;  // not owned
  const fidl::String device_id_;
  const fidl::String story_id_;
  const fidl::String timestamp_;
  ledger::PagePtr device_page_;
  fidl::String page_id_;

  FTL_DISALLOW_COPY_AND_ASSIGN(StartOnDeviceCall);
};

RemoteInvokerImpl::RemoteInvokerImpl(ledger::Ledger* const ledger)
    : ledger_(ledger) {}

// | RemoteService |
void RemoteInvokerImpl::StartOnDevice(const fidl::String& device_id,
                                      const fidl::String& story_id,
                                      const StartOnDeviceCallback& callback) {
  FTL_LOG(INFO) << "Starting rehydrate call for story " << story_id
                << " on device " << device_id;
  new StartOnDeviceCall(&operation_queue_, ledger_, device_id, story_id,
                        callback);
}

RemoteInvokerImpl::~RemoteInvokerImpl() = default;

void RemoteInvokerImpl::Connect(fidl::InterfaceRequest<RemoteInvoker> request) {
  bindings_.AddBinding(this, std::move(request));
}

}  // namespace modular
