// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/forensics/utils/component/component.h"

#include <lib/async-loop/default.h>
#include <lib/syslog/cpp/macros.h>

#include <string>

#include "src/lib/files/directory.h"
#include "src/lib/files/file.h"

namespace forensics {
namespace component {

constexpr char kComponentDirectory[] = "/tmp/component";
constexpr char kInstanceIndexPath[] = "/tmp/component/instance_index.txt";

Component::Component()
    : loop_(&kAsyncLoopConfigAttachToCurrentThread),
      context_(sys::ComponentContext::CreateAndServeOutgoingDirectory()),
      inspector_(context_.get()),
      instance_index_(1u /*default if no file present*/) {
  if (!files::IsDirectory(kComponentDirectory) && !files::CreateDirectory(kComponentDirectory)) {
    FX_LOGS(INFO) << "Unable to create " << kComponentDirectory
                  << ", assuming first instance of component";
    return;
  }

  std::string starts_str;
  if (files::ReadFileToString(kInstanceIndexPath, &starts_str)) {
    instance_index_ = std::stoull(starts_str) + 1;
  }

  files::WriteFile(kInstanceIndexPath, std::to_string(instance_index_));
}

async_dispatcher_t* Component::Dispatcher() { return loop_.dispatcher(); }

std::shared_ptr<sys::ServiceDirectory> Component::Services() { return context_->svc(); }

inspect::Node* Component::InspectRoot() { return &(inspector_.root()); }

bool Component::IsFirstInstance() const { return instance_index_ == 1; }

zx_status_t Component::RunLoop() { return loop_.Run(); }

}  // namespace component
}  // namespace forensics
