// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/ledger/examples/todo/todo.h"

#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <algorithm>
#include <iostream>

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

TodoApp::TodoApp(ftl::CommandLine command_line)
    : rng_(time(0)),
      size_distribution_(kMeanListSize, kListSizeStdDev),
      delay_distribution_(kMinDelaySeconds, kMaxDelaySeconds),
      command_line_(std::move(command_line)),
      generator_(&rng_, command_line_.positional_args()),
      context_(modular::ApplicationContext::CreateFromStartupInfo()),
      page_watcher_binding_(this) {
  ledger::LedgerPtr ledger = GetLedger();
  ledger->GetRootPage(page_.NewRequest(), HandleResponse("GetRootPage"));

  ledger::PageWatcherPtr page_watcher;
  page_watcher_binding_.Bind(page_watcher.NewRequest());
  page_->Watch(std::move(page_watcher), HandleResponse("Watch"));

  mtl::MessageLoop::GetCurrent()->task_runner()->PostTask([this] { Act(); });
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
  callback();
}

ledger::LedgerPtr TodoApp::GetLedger() {
  // TODO(ppi): this whole function should be just
  // a call to context_->ConnectToEnvironmentService<ledger::Ledger>();
  // once we can run the todo app in modular.

  ledger::LedgerRepositoryFactoryPtr repository_factory;
  modular::ServiceProviderPtr child_services;
  auto launch_info = modular::ApplicationLaunchInfo::New();
  launch_info->url = "file:///system/apps/ledger";
  launch_info->services = child_services.NewRequest();
  context_->launcher()->CreateApplication(std::move(launch_info),
                                          ledger_controller_.NewRequest());
  modular::ConnectToService(child_services.get(),
                            repository_factory.NewRequest());

  ledger::LedgerRepositoryPtr repository;
  repository_factory->GetRepository("/data/ledger/todo_user",
                                    repository.NewRequest(),
                                    HandleResponse("GetRepository"));

  ledger::LedgerPtr ledger;
  repository->GetLedger(ToArray("todo"), ledger.NewRequest(),
                        HandleResponse("GetLedger"));
  return ledger;
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
          std::cout << ToString(entry->value) << std::endl;
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
  ftl::CommandLine command_line = ftl::CommandLineFromArgcArgv(argc, argv);
  mtl::MessageLoop loop;
  todo::TodoApp app(std::move(command_line));
  loop.Run();
  return 0;
}
