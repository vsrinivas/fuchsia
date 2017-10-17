// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/lib/module_manifest_repository/module_manifest_repository.h"

#include <errno.h>
#include <fcntl.h>
#include <fdio/watcher.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <thread>

#include "lib/fsl/tasks/message_loop.h"
#include "lib/fxl/files/file.h"
#include "lib/fxl/logging.h"
#include "lib/fxl/tasks/task_runner.h"
#include "peridot/lib/fidl/json_xdr.h"
#include "third_party/rapidjson/rapidjson/document.h"

namespace modular {

namespace {

struct WatcherState {
  // WatcherHandler() uses this to terminate watching the directory when the
  // ModuleManifestRepository instance is destroyed.
  fxl::WeakPtr<ModuleManifestRepository> weak_repository;

  // Called with the filename of the new file every time one is found.
  std::function<void(std::string)> on_new;

  // We use this to post tasks to the TaskRunner of the thread that created
  // the ModuleManifestRepository.
  fxl::RefPtr<fxl::TaskRunner> task_runner;

  // The directory file handle for our repository directory.
  int dirfd;
};

bool IsRegularFile(const char* const path) {
  struct stat path_stat;
  if (stat(path, &path_stat) < 0) {
    FXL_LOG(INFO) << "Couldn't stat file: " << strerror(errno);
    return false;
  }
  return S_ISREG(path_stat.st_mode);
}

zx_status_t WatcherHandler(const int dirfd,
                           const int event,
                           const char* const name,
                           void* const cookie) {
  auto state = static_cast<WatcherState*>(cookie);
  if (!state->weak_repository) {
    close(state->dirfd);
    delete state;
    // Returning ZX_ERR_STOP prevents future notifications from happening.
    return ZX_ERR_STOP;
  }

  const std::string name_str{name};
  switch (event) {
    case WATCH_EVENT_ADD_FILE:
      state->task_runner->PostTask(
          [weak_repository = state->weak_repository, state, name_str = std::move(name_str)] {
            // If the repository has gone out of scope, |state| either already
            // did, or it will soon.
            if (!weak_repository)
              return;
            state->on_new(name_str);
          });
      break;
    case WATCH_EVENT_REMOVE_FILE:
      // TODO(thatguy)
      break;
    case WATCH_EVENT_IDLE:
      // This means we've seen all the existing files and we're now waiting for
      // new ones. Just return as normal.
      break;
  }

  return ZX_OK;
}

void XdrNounConstraint(
    modular::XdrContext* const xdr,
    ModuleManifestRepository::Entry::NounConstraint* const data) {
  xdr->Field("name", &data->name);
  xdr->Field("types", &data->types);
}

void XdrEntry(modular::XdrContext* const xdr,
              ModuleManifestRepository::Entry* const data) {
  xdr->Field("binary", &data->binary);
  xdr->Field("local_name", &data->local_name);
  xdr->Field("verb", &data->verb);
  xdr->Field("noun_constraints", &data->noun_constraints, XdrNounConstraint);
}

}  // namespace

ModuleManifestRepository::ModuleManifestRepository(
    std::string repository_dir,
    NewEntryFn fn)
    : repository_dir_(repository_dir), new_entry_fn_(fn), weak_factory_(this) {
  // fdio_watch_directory() is blocking so we start the watching process in its
  // own thread. We give that thread a pointer to our TaskRunner and a WeakPtr
  // to us in case we are destroyed.
  auto task_runner = fsl::MessageLoop::GetCurrent()->task_runner();
  auto thread = std::thread([weak_this = weak_factory_.GetWeakPtr(),
                             task_runner, repository_dir = std::move(repository_dir)] {
    // In the unlikely event our owner is destroyed before we get here.
    if (!weak_this)
      return;

    // Set up the fdio waiter.
    auto dirfd = open(repository_dir.c_str(), O_DIRECTORY | O_RDONLY);
    FXL_CHECK(dirfd >= 0) << "Could not open " << repository_dir << ": "
                          << strerror(errno);

    auto state = new WatcherState();  // Managed by WatcherHandler() above.
    state->weak_repository = weak_this;
    state->task_runner = task_runner;

    state->on_new = [weak_this](const std::string name) {
      if (!weak_this)
        return;
      weak_this->OnNewFile(name);
    };
    fdio_watch_directory(dirfd, WatcherHandler, ZX_TIME_INFINITE,
                         static_cast<void*>(state));
  });

  // We rely on the fact that the thread will kill itself eventually if either:
  // a) The owning process is killed.
  // b) |this| is destroyed, and fdio_watch_directory() returns because its
  //    callback (WatcherHandler()) checks its WeakPtr to |this|. This would
  //    only happen when the contents of the watched directory change.
  //
  // We assume that instances of ModuleManifestRepository will stay alive
  // approximately equivalently to the lifetime of the owning process.
  thread.detach();
}

ModuleManifestRepository::~ModuleManifestRepository() {}

void ModuleManifestRepository::OnNewFile(const std::string& name) {
  std::string data;
  const std::string path = repository_dir_ + '/' + name;
  if (!IsRegularFile(path.c_str())) {
    return;
  }
  FXL_CHECK(files::ReadFileToString(path, &data)) << path;

  rapidjson::Document doc;
  doc.Parse(data.c_str());

  // Our tooling validates |doc|'s JSON schema so that we don't have to here.
  // It may be good to do this, though.
  // TODO(thatguy): Do this if it becomes a problem.

  std::vector<Entry> entries;
  if (!modular::XdrRead(&doc, &entries, XdrEntry)) {
    FXL_LOG(WARNING) << "Could not parse Module manifest from: " << path;
    return;
  }

  for (const auto& entry : entries) {
    new_entry_fn_(entry);
  }
}

}  // namespace modular
