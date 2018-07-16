// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/examples/todo_cpp/todo.h"

#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <algorithm>
#include <iostream>

#include <fuchsia/ledger/internal/cpp/fidl.h>
#include <lib/component/cpp/connect.h>
#include <lib/async/cpp/task.h>
#include <lib/fit/function.h>
#include <lib/fsl/vmo/strings.h>
#include <lib/fxl/functional/make_copyable.h>
#include <lib/fxl/logging.h>
#include <lib/fxl/strings/string_printf.h>
#include <lib/fxl/time/time_delta.h>

namespace todo {

namespace {

const double kMeanListSize = 7.0;
const double kListSizeStdDev = 2.0;
const int kMinDelaySeconds = 1;
const int kMaxDelaySeconds = 5;

std::string ToString(const fuchsia::mem::Buffer& vmo) {
  std::string ret;
  if (!fsl::StringFromVmo(vmo, &ret)) {
    FXL_DCHECK(false);
  }
  return ret;
}

fidl::VectorPtr<uint8_t> ToArray(const std::string& val) {
  auto ret = fidl::VectorPtr<uint8_t>::New(val.size());
  memcpy(ret->data(), val.data(), val.size());
  return ret;
}

Key MakeKey() {
  return ToArray(fxl::StringPrintf("%120ld-%u", time(nullptr), rand()));
}

std::function<void(fuchsia::ledger::Status)> HandleResponse(
    fit::closure quit_callback, std::string description) {
  return [description, &quit_callback](fuchsia::ledger::Status status) {
    if (status != fuchsia::ledger::Status::OK) {
      FXL_LOG(ERROR) << description << " failed";
      quit_callback();
    }
  };
}

void GetEntries(fuchsia::ledger::PageSnapshotPtr snapshot,
                std::vector<fuchsia::ledger::Entry> entries,
                std::unique_ptr<fuchsia::ledger::Token> token,
                std::function<void(fuchsia::ledger::Status,
                                   std::vector<fuchsia::ledger::Entry>)>
                    callback) {
  fuchsia::ledger::PageSnapshot* snapshot_ptr = snapshot.get();
  snapshot_ptr->GetEntries(
      fidl::VectorPtr<uint8_t>::New(0), std::move(token),
      fxl::MakeCopyable(
          [snapshot = std::move(snapshot), entries = std::move(entries),
           callback = std::move(callback)](fuchsia::ledger::Status status,
                                           auto new_entries,
                                           auto next_token) mutable {
            if (status != fuchsia::ledger::Status::OK &&
                status != fuchsia::ledger::Status::PARTIAL_RESULT) {
              callback(status, {});
              return;
            }
            for (size_t i = 0; i < new_entries->size(); ++i) {
              entries.push_back(std::move(new_entries->at(i)));
            }
            if (status == fuchsia::ledger::Status::OK) {
              callback(fuchsia::ledger::Status::OK, std::move(entries));
              return;
            }
            GetEntries(std::move(snapshot), std::move(entries),
                       std::move(next_token), std::move(callback));
          }));
}

void GetEntries(fuchsia::ledger::PageSnapshotPtr snapshot,
                std::function<void(fuchsia::ledger::Status,
                                   std::vector<fuchsia::ledger::Entry>)>
                    callback) {
  GetEntries(std::move(snapshot), {}, nullptr, std::move(callback));
}

}  // namespace

TodoApp::TodoApp(async::Loop* loop)
    : loop_(loop),
      rng_(time(nullptr)),
      size_distribution_(kMeanListSize, kListSizeStdDev),
      delay_distribution_(kMinDelaySeconds, kMaxDelaySeconds),
      generator_(&rng_),
      context_(component::StartupContext::CreateFromStartupInfo()),
      page_watcher_binding_(this) {
  context_->ConnectToEnvironmentService(module_context_.NewRequest());
  module_context_->GetComponentContext(component_context_.NewRequest());
  component_context_->GetLedger(
      ledger_.NewRequest(),
      HandleResponse([this] { loop_->Quit(); }, "GetLedger"));
  ledger_->GetRootPage(
      page_.NewRequest(),
      HandleResponse([this] { loop_->Quit(); }, "GetRootPage"));

  fuchsia::ledger::PageSnapshotPtr snapshot;
  page_->GetSnapshot(snapshot.NewRequest(), fidl::VectorPtr<uint8_t>::New(0),
                     page_watcher_binding_.NewBinding(),
                     HandleResponse([this] { loop_->Quit(); }, "Watch"));
  List(std::move(snapshot));

  async::PostTask(loop_->dispatcher(), [this] { Act(); });
}

void TodoApp::Terminate() { loop_->Quit(); }

void TodoApp::OnChange(fuchsia::ledger::PageChange /*page_change*/,
                       fuchsia::ledger::ResultState result_state,
                       OnChangeCallback callback) {
  if (result_state != fuchsia::ledger::ResultState::PARTIAL_STARTED &&
      result_state != fuchsia::ledger::ResultState::COMPLETED) {
    // Only request the entries list once, on the first OnChange call.
    callback(nullptr);
    return;
  }

  fuchsia::ledger::PageSnapshotPtr snapshot;
  callback(snapshot.NewRequest());
  List(std::move(snapshot));
}

void TodoApp::List(fuchsia::ledger::PageSnapshotPtr snapshot) {
  GetEntries(std::move(snapshot),
             [this](fuchsia::ledger::Status status, auto entries) {
               if (status != fuchsia::ledger::Status::OK) {
                 FXL_LOG(ERROR) << "GetEntries failed";
                 loop_->Quit();
                 return;
               }

               std::cout << "--- To Do ---" << std::endl;
               for (auto& entry : entries) {
                 std::cout << (entry.value ? ToString(*entry.value) : "<empty>")
                           << std::endl;
               }
               std::cout << "---" << std::endl;
             });
}

void TodoApp::GetKeys(std::function<void(fidl::VectorPtr<Key>)> callback) {
  fuchsia::ledger::PageSnapshotPtr snapshot;
  page_->GetSnapshot(snapshot.NewRequest(), nullptr, nullptr,
                     HandleResponse([this] { loop_->Quit(); }, "GetSnapshot"));

  fuchsia::ledger::PageSnapshot* snapshot_ptr = snapshot.get();
  snapshot_ptr->GetKeys(
      fidl::VectorPtr<uint8_t>::New(0), nullptr,
      fxl::MakeCopyable([snapshot = std::move(snapshot), callback](
                            fuchsia::ledger::Status status, auto keys,
                            auto next_token) { callback(std::move(keys)); }));
}

void TodoApp::AddNew() {
  page_->Put(MakeKey(), ToArray(generator_.Generate()),
             HandleResponse([this] { loop_->Quit(); }, "Put"));
}

void TodoApp::DeleteOne(fidl::VectorPtr<Key> keys) {
  FXL_DCHECK(keys->size());
  std::uniform_int_distribution<> distribution(0, keys->size() - 1);
  page_->Delete(std::move(keys->at(distribution(rng_))),
                HandleResponse([this] { loop_->Quit(); }, "Delete"));
}

void TodoApp::Act() {
  GetKeys([this](fidl::VectorPtr<Key> keys) {
    size_t target_size = std::round(size_distribution_(rng_));
    if (keys->size() > std::max(static_cast<size_t>(0), target_size)) {
      DeleteOne(std::move(keys));
    } else {
      AddNew();
    }
  });
  zx::duration delay = zx::sec(delay_distribution_(rng_));
  async::PostDelayedTask(loop_->dispatcher(), [this] { Act(); }, delay);
}

}  // namespace todo

int main(int /*argc*/, const char** /*argv*/) {
  async::Loop loop(&kAsyncLoopConfigMakeDefault);
  todo::TodoApp app(&loop);
  loop.Run();
  return 0;
}
