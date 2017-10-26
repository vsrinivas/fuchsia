// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/examples/todo_cpp/todo.h"

#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <algorithm>
#include <iostream>

#include "lib/app/cpp/connect.h"
#include "lib/fsl/tasks/message_loop.h"
#include "lib/fsl/vmo/strings.h"
#include "lib/fxl/functional/make_copyable.h"
#include "lib/fxl/logging.h"
#include "lib/fxl/strings/string_printf.h"
#include "lib/fxl/time/time_delta.h"
#include "peridot/bin/ledger/fidl/internal.fidl.h"

namespace todo {

namespace {

const double kMeanListSize = 7.0;
const double kListSizeStdDev = 2.0;
const int kMinDelaySeconds = 1;
const int kMaxDelaySeconds = 5;

std::string ToString(const zx::vmo& vmo) {
  std::string ret;
  if (!fsl::StringFromVmo(vmo, &ret)) {
    FXL_DCHECK(false);
  }
  return ret;
}

fidl::Array<uint8_t> ToArray(const std::string& val) {
  auto ret = fidl::Array<uint8_t>::New(val.size());
  memcpy(ret.data(), val.data(), val.size());
  return ret;
}

Key MakeKey() {
  return ToArray(fxl::StringPrintf("%120ld-%u", time(nullptr), rand()));
}

std::function<void(ledger::Status)> HandleResponse(std::string description) {
  return [description](ledger::Status status) {
    if (status != ledger::Status::OK) {
      FXL_LOG(ERROR) << description << " failed";
      fsl::MessageLoop::GetCurrent()->PostQuitTask();
    }
  };
}

void GetEntries(ledger::PageSnapshotPtr snapshot,
                std::vector<ledger::EntryPtr> entries,
                fidl::Array<uint8_t> token,
                std::function<void(ledger::Status,
                                   std::vector<ledger::EntryPtr>)> callback) {
  ledger::PageSnapshot* snapshot_ptr = snapshot.get();
  snapshot_ptr->GetEntries(
      nullptr, std::move(token),
      fxl::MakeCopyable(
          [snapshot = std::move(snapshot), entries = std::move(entries),
           callback = std::move(callback)](ledger::Status status,
                                           auto new_entries,
                                           auto next_token) mutable {
            if (status != ledger::Status::OK &&
                status != ledger::Status::PARTIAL_RESULT) {
              callback(status, {});
              return;
            }
            for (auto& entry : new_entries) {
              entries.push_back(std::move(entry));
            }
            if (status == ledger::Status::OK) {
              callback(ledger::Status::OK, std::move(entries));
              return;
            }
            GetEntries(std::move(snapshot), std::move(entries),
                       std::move(next_token), std::move(callback));
          }));
}

void GetEntries(ledger::PageSnapshotPtr snapshot,
                std::function<void(ledger::Status,
                                   std::vector<ledger::EntryPtr>)> callback) {
  GetEntries(std::move(snapshot), {}, nullptr, std::move(callback));
}

}  // namespace

TodoApp::TodoApp()
    : rng_(time(nullptr)),
      size_distribution_(kMeanListSize, kListSizeStdDev),
      delay_distribution_(kMinDelaySeconds, kMaxDelaySeconds),
      generator_(&rng_),
      context_(app::ApplicationContext::CreateFromStartupInfo()),
      module_binding_(this),
      page_watcher_binding_(this) {
  context_->outgoing_services()->AddService<modular::Module>(
      [this](fidl::InterfaceRequest<modular::Module> request) {
        FXL_DCHECK(!module_binding_.is_bound());
        module_binding_.Bind(std::move(request));
      });
}

void TodoApp::Initialize(
    fidl::InterfaceHandle<modular::ModuleContext> module_context,
    fidl::InterfaceHandle<app::ServiceProvider> /*incoming_services*/,
    fidl::InterfaceRequest<app::ServiceProvider> /*outgoing_services*/) {
  module_context_.Bind(std::move(module_context));
  module_context_->GetComponentContext(component_context_.NewRequest());
  component_context_->GetLedger(ledger_.NewRequest(),
                                HandleResponse("GetLedger"));
  ledger_->GetRootPage(page_.NewRequest(), HandleResponse("GetRootPage"));

  ledger::PageSnapshotPtr snapshot;
  page_->GetSnapshot(snapshot.NewRequest(), nullptr,
                     page_watcher_binding_.NewBinding(),
                     HandleResponse("Watch"));
  List(std::move(snapshot));

  fsl::MessageLoop::GetCurrent()->task_runner()->PostTask([this] { Act(); });
}

void TodoApp::Terminate() {
  fsl::MessageLoop::GetCurrent()->QuitNow();
}

void TodoApp::OnChange(ledger::PageChangePtr /*page_change*/,
                       ledger::ResultState result_state,
                       const OnChangeCallback& callback) {
  if (result_state != ledger::ResultState::PARTIAL_STARTED &&
      result_state != ledger::ResultState::COMPLETED) {
    // Only request the entries list once, on the first OnChange call.
    callback(nullptr);
    return;
  }

  ledger::PageSnapshotPtr snapshot;
  callback(snapshot.NewRequest());
  List(std::move(snapshot));
}

void TodoApp::List(ledger::PageSnapshotPtr snapshot) {
  GetEntries(std::move(snapshot), [](ledger::Status status, auto entries) {
    if (status != ledger::Status::OK) {
      FXL_LOG(ERROR) << "GetEntries failed";
      fsl::MessageLoop::GetCurrent()->PostQuitTask();
      return;
    }

    std::cout << "--- To Do ---" << std::endl;
    for (auto& entry : entries) {
      std::cout << ToString(entry->value) << std::endl;
    }
    std::cout << "---" << std::endl;
  });
}

void TodoApp::GetKeys(std::function<void(fidl::Array<Key>)> callback) {
  ledger::PageSnapshotPtr snapshot;
  page_->GetSnapshot(snapshot.NewRequest(), nullptr, nullptr,
                     HandleResponse("GetSnapshot"));

  ledger::PageSnapshot* snapshot_ptr = snapshot.get();
  snapshot_ptr->GetKeys(
      nullptr, nullptr,
      fxl::MakeCopyable([snapshot = std::move(snapshot), callback](
                            ledger::Status status, auto keys, auto next_token) {
        callback(std::move(keys));
      }));
}

void TodoApp::AddNew() {
  page_->Put(MakeKey(), ToArray(generator_.Generate()), HandleResponse("Put"));
}

void TodoApp::DeleteOne(fidl::Array<Key> keys) {
  FXL_DCHECK(keys.size());
  std::uniform_int_distribution<> distribution(0, keys.size() - 1);
  page_->Delete(std::move(keys[distribution(rng_)]), HandleResponse("Delete"));
}

void TodoApp::Act() {
  GetKeys([this](fidl::Array<Key> keys) {
    size_t target_size = std::round(size_distribution_(rng_));
    if (keys.size() > std::max(static_cast<size_t>(0), target_size)) {
      DeleteOne(std::move(keys));
    } else {
      AddNew();
    }
  });
  fxl::TimeDelta delay = fxl::TimeDelta::FromSeconds(delay_distribution_(rng_));
  fsl::MessageLoop::GetCurrent()->task_runner()->PostDelayedTask(
      [this] { Act(); }, delay);
}

}  // namespace todo

int main(int /*argc*/, const char** /*argv*/) {
  fsl::MessageLoop loop;
  todo::TodoApp app;
  loop.Run();
  return 0;
}
