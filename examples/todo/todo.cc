// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/ledger/examples/todo/todo.h"

#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <algorithm>
#include <iostream>

#include "apps/ledger/services/internal/internal.fidl.h"
#include "apps/modular/lib/app/connect.h"
#include "apps/modular/lib/app/service_provider_impl.h"
#include "lib/ftl/functional/make_copyable.h"
#include "lib/ftl/logging.h"
#include "lib/ftl/strings/string_printf.h"
#include "lib/ftl/time/time_delta.h"
#include "lib/mtl/tasks/message_loop.h"

namespace todo {

namespace {

const double kMeanListSize = 7.0;
const double kListSizeStdDev = 2.0;
const int kMinDelaySeconds = 1;
const int kMaxDelaySeconds = 5;

std::string ToString(fidl::Array<uint8_t>& data) {
  std::string ret;
  ret.resize(data.size());
  memcpy(&ret[0], data.data(), data.size());
  return ret;
}

fidl::Array<uint8_t> ToArray(const std::string& val) {
  auto ret = fidl::Array<uint8_t>::New(val.size());
  memcpy(ret.data(), val.data(), val.size());
  return ret;
}

Key MakeKey() {
  return ToArray(ftl::StringPrintf("%120ld-%u", time(0), rand()));
}

std::function<void(ledger::Status)> HandleResponse(std::string description) {
  return [description](ledger::Status status) {
    if (status != ledger::Status::OK) {
      FTL_LOG(ERROR) << description << " failed";
      mtl::MessageLoop::GetCurrent()->PostQuitTask();
    }
  };
}

}  // namespace

TodoApp::TodoApp()
    : rng_(time(0)),
      size_distribution_(kMeanListSize, kListSizeStdDev),
      delay_distribution_(kMinDelaySeconds, kMaxDelaySeconds),
      generator_(&rng_),
      context_(modular::ApplicationContext::CreateFromStartupInfo()),
      module_binding_(this),
      page_watcher_binding_(this) {
  context_->outgoing_services()->AddService<modular::Module>(
      [this](fidl::InterfaceRequest<modular::Module> request) {
        FTL_DCHECK(!module_binding_.is_bound());
        module_binding_.Bind(std::move(request));
      });
}

void TodoApp::Initialize(
    fidl::InterfaceHandle<modular::Story> story,
    fidl::InterfaceHandle<modular::Link> link,
    fidl::InterfaceHandle<modular::ServiceProvider> incoming_services,
    fidl::InterfaceRequest<modular::ServiceProvider> outgoing_services) {
  story_.Bind(std::move(story));
  story_->GetLedger(ledger_.NewRequest(), HandleResponse("GetLedger"));
  ledger_->GetRootPage(page_.NewRequest(), HandleResponse("GetRootPage"));

  page_->Watch(page_watcher_binding_.NewBinding(), HandleResponse("Watch"));

  mtl::MessageLoop::GetCurrent()->task_runner()->PostTask([this] { Act(); });
}

void TodoApp::Stop(const StopCallback& done) {
  done();
}

void TodoApp::OnInitialState(
    ::fidl::InterfaceHandle<ledger::PageSnapshot> snapshot,
    const OnInitialStateCallback& callback) {
  List();
  callback();
}

void TodoApp::OnChange(ledger::PageChangePtr page_change,
                       const OnChangeCallback& callback) {
  List();
  callback(nullptr);
}

void TodoApp::List() {
  ledger::PageSnapshotPtr snapshot;
  page_->GetSnapshot(snapshot.NewRequest(), HandleResponse("GetSnapshot"));

  ledger::PageSnapshot* snapshot_ptr = snapshot.get();
  snapshot_ptr->GetEntries(
      nullptr, nullptr,
      ftl::MakeCopyable([snapshot = std::move(snapshot)](
          ledger::Status status, auto entries, auto next_token) {
        if (status != ledger::Status::OK) {
          FTL_LOG(ERROR) << "GetEntries failed";
          mtl::MessageLoop::GetCurrent()->PostQuitTask();
          return;
        }
        FTL_DCHECK(!next_token);

        std::cout << "--- To Do ---" << std::endl;
        for (auto& entry : entries) {
          std::cout << ToString(entry->value->get_bytes()) << std::endl;
        }
        std::cout << "---" << std::endl;
      }));
}

void TodoApp::GetKeys(std::function<void(fidl::Array<Key>)> callback) {
  ledger::PageSnapshotPtr snapshot;
  page_->GetSnapshot(snapshot.NewRequest(), HandleResponse("GetSnapshot"));

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

int main(int argc, const char** argv) {
  mtl::MessageLoop loop;
  todo::TodoApp app;
  loop.Run();
  return 0;
}
