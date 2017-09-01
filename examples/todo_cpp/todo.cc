// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/modular/examples/todo_cpp/todo.h"

#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <algorithm>
#include <iostream>

#include "application/lib/app/connect.h"
#include "apps/ledger/services/internal/internal.fidl.h"
#include "lib/ftl/functional/make_copyable.h"
#include "lib/ftl/logging.h"
#include "lib/ftl/strings/string_printf.h"
#include "lib/ftl/time/time_delta.h"
#include "lib/mtl/tasks/message_loop.h"
#include "lib/mtl/vmo/strings.h"

namespace todo {

namespace {

const double kMeanListSize = 7.0;
const double kListSizeStdDev = 2.0;
const int kMinDelaySeconds = 1;
const int kMaxDelaySeconds = 5;

std::string ToString(const mx::vmo& vmo) {
  std::string ret;
  if (!mtl::StringFromVmo(vmo, &ret)) {
    FTL_DCHECK(false);
  }
  return ret;
}

fidl::Array<uint8_t> ToArray(const std::string& val) {
  auto ret = fidl::Array<uint8_t>::New(val.size());
  memcpy(ret.data(), val.data(), val.size());
  return ret;
}

Key MakeKey() {
  return ToArray(ftl::StringPrintf("%120ld-%u", time(nullptr), rand()));
}

std::function<void(ledger::Status)> HandleResponse(std::string description) {
  return [description](ledger::Status status) {
    if (status != ledger::Status::OK) {
      FTL_LOG(ERROR) << description << " failed";
      mtl::MessageLoop::GetCurrent()->PostQuitTask();
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
      nullptr, std::move(token), ftl::MakeCopyable([
        snapshot = std::move(snapshot), entries = std::move(entries),
        callback = std::move(callback)
      ](ledger::Status status, auto new_entries, auto next_token) mutable {
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
        FTL_DCHECK(!module_binding_.is_bound());
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

  mtl::MessageLoop::GetCurrent()->task_runner()->PostTask([this] { Act(); });
}

void TodoApp::Terminate() {
  mtl::MessageLoop::GetCurrent()->QuitNow();
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
      FTL_LOG(ERROR) << "GetEntries failed";
      mtl::MessageLoop::GetCurrent()->PostQuitTask();
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
  snapshot_ptr->GetKeys(nullptr, nullptr, ftl::MakeCopyable([
                          snapshot = std::move(snapshot), callback
                        ](ledger::Status status, auto keys, auto next_token) {
                          callback(std::move(keys));
                        }));
}

void TodoApp::AddNew() {
  page_->Put(MakeKey(), ToArray(generator_.Generate()), HandleResponse("Put"));
}

void TodoApp::DeleteOne(fidl::Array<Key> keys) {
  FTL_DCHECK(keys.size());
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
  ftl::TimeDelta delay = ftl::TimeDelta::FromSeconds(delay_distribution_(rng_));
  mtl::MessageLoop::GetCurrent()->task_runner()->PostDelayedTask(
      [this] { Act(); }, delay);
}

}  // namespace todo

int main(int /*argc*/, const char** /*argv*/) {
  mtl::MessageLoop loop;
  todo::TodoApp app;
  loop.Run();
  return 0;
}
