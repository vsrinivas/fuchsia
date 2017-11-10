// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/lib/module_manifest_source/directory_source.h"

#include <errno.h>
#include <fcntl.h>
#include <fdio/watcher.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <thread>

#include "lib/fxl/files/directory.h"
#include "lib/fxl/files/file.h"
#include "lib/fxl/logging.h"
#include "lib/fxl/memory/weak_ptr.h"
#include "peridot/lib/fidl/json_xdr.h"
#include "third_party/rapidjson/rapidjson/document.h"

namespace modular {

namespace {

// We ignore files with this suffix, as they are in the process of being
// written. Copy processes will rename them atomically when copying is
// complete.
constexpr char kIncomingSuffix[] = ".incoming";
constexpr size_t kIncomingSuffixLen =
    sizeof(kIncomingSuffix) / sizeof(kIncomingSuffix[0]) - 1;

struct WatcherState {
  // WatcherHandler() uses this to terminate watching the directory when the
  // DirectoryModuleManifestSource instance is destroyed.
  fxl::WeakPtr<DirectoryModuleManifestSource> source;

  std::function<void()> on_idle;

  // Called with the filename of the new file every time one is found.
  std::function<void(std::string)> on_new;

  // Called with the filename of removed files.
  std::function<void(std::string)> on_remove;

  fxl::RefPtr<fxl::TaskRunner> task_runner;
};

bool ShouldIgnoreFile(const std::string& name) {
  if (name.size() >= kIncomingSuffixLen &&
      name.compare(name.size() - kIncomingSuffixLen, kIncomingSuffixLen,
                   kIncomingSuffix) == 0) {
    return true;
  }
  return false;
}

zx_status_t WatcherHandler(const int dirfd,
                           const int event,
                           const char* const name,
                           void* const cookie) {
  auto state = static_cast<WatcherState*>(cookie);
  if (!state->source) {
    delete state;
    // Returning ZX_ERR_STOP prevents future notifications from happening.
    return ZX_ERR_STOP;
  }

  const std::string name_str{name};
  switch (event) {
    case WATCH_EVENT_ADD_FILE:
      state->task_runner->PostTask(
          [source = state->source, state, name_str = std::move(name_str)] {
            // If the repository has gone out of scope, |state| either already
            // did, or it will soon.
            if (!source)
              return;
            state->on_new(name_str);
          });
      break;
    case WATCH_EVENT_REMOVE_FILE:
      state->task_runner->PostTask(
          [source = state->source, state, name_str = std::move(name_str)] {
            // If the repository has gone out of scope, |state| either already
            // did, or it will soon.
            if (!source)
              return;
            state->on_remove(name_str);
          });
      break;
    case WATCH_EVENT_IDLE:
      // This means we've seen all the existing files and we're now waiting for
      // new ones.
      state->task_runner->PostTask([source = state->source, state] {
        if (!source)
          return;
        state->on_idle();
      });
      break;
  }

  return ZX_OK;
}

void XdrNounConstraint(
    modular::XdrContext* const xdr,
    ModuleManifestSource::Entry::NounConstraint* const data) {
  xdr->Field("name", &data->name);
  xdr->Field("types", &data->types);
}

void XdrEntry(modular::XdrContext* const xdr,
              ModuleManifestSource::Entry* const data) {
  xdr->Field("binary", &data->binary);
  xdr->Field("local_name", &data->local_name);
  xdr->Field("verb", &data->verb);
  xdr->Field("noun_constraints", &data->noun_constraints, XdrNounConstraint);
}

}  // namespace

DirectoryModuleManifestSource::DirectoryModuleManifestSource(std::string dir,
                                                             const bool create)
    : dir_(dir), weak_factory_(this) {
  if (!files::IsDirectory(dir)) {
    if (create) {
      auto create_result = files::CreateDirectory(dir);
      FXL_CHECK(create_result) << "Could not create " << dir;
    }
  }
}

DirectoryModuleManifestSource::~DirectoryModuleManifestSource() {}

void DirectoryModuleManifestSource::Watch(
    fxl::RefPtr<fxl::TaskRunner> task_runner,
    IdleFn idle_fn,
    NewEntryFn new_fn,
    RemovedEntryFn removed_fn) {
  // fdio_watch_directory() is blocking so we start the watching process in its
  // own thread. We give that thread a pointer to the TaskRunner and a WeakPtr
  // to us in case we are destroyed.
  auto thread = std::thread([weak_this = weak_factory_.GetWeakPtr(),
                             task_runner, idle_fn, new_fn, removed_fn] {
    // In the unlikely event our owner is destroyed before we get here.
    if (!weak_this)
      return;

    // Set up the fdio waiter.
    auto dirfd = open(weak_this->dir_.c_str(), O_DIRECTORY | O_RDONLY);
    if (dirfd < 0) {
      FXL_LOG(ERROR) << "Could not open " << weak_this->dir_ << ": "
                     << strerror(errno) << std::endl;
      return;
    }

    auto state = new WatcherState();  // Managed by WatcherHandler() above.
    state->source = weak_this;
    state->task_runner = task_runner;

    state->on_idle = idle_fn;
    state->on_new = [weak_this, new_fn](const std::string name) {
      if (!weak_this)
        return;
      weak_this->OnNewFile(name, new_fn);
    };
    state->on_remove = [weak_this, removed_fn](const std::string name) {
      if (!weak_this)
        return;
      weak_this->OnRemoveFile(name, removed_fn);
    };
    fdio_watch_directory(dirfd, WatcherHandler, ZX_TIME_INFINITE,
                         static_cast<void*>(state));
    close(dirfd);
  });

  // We rely on the fact that the thread will kill itself eventually if either:
  // a) The owning process is killed.
  // b) |this| is destroyed, and fdio_watch_directory() returns because its
  //    callback (WatcherHandler()) checks its WeakPtr to |this|. This would
  //    only happen when the contents of the watched directory change.
  //
  // We assume that instances of DirectoryModuleManifestSource will stay alive
  // approximately equivalently to the lifetime of the owning process.
  thread.detach();
}

void DirectoryModuleManifestSource::OnNewFile(const std::string& name,
                                              NewEntryFn fn) {
  if (ShouldIgnoreFile(name)) {
    return;
  }

  const std::string path = dir_ + '/' + name;
  if (!files::IsFile(path.c_str())) {
    return;
  }
  std::string data;
  bool read_result = files::ReadFileToString(path, &data);
  FXL_CHECK(read_result) << "Couldn't read file: " << path;

  rapidjson::Document doc;
  // Schema validation of the JSON is happening at publish time. By the time we
  // get here, we assume it's valid manifest JSON.
  doc.Parse(data.c_str());

  // Handle bad manifests, including older files expressed as an array.
  // Any mismatch causes XdrRead to DCHECK.
  if (!doc.IsObject()) {
    FXL_LOG(WARNING) << "Ignored invalid manifest: " << path;
    return;
  }

  // Our tooling validates |doc|'s JSON schema so that we don't have to here.
  // It may be good to do this, though.
  // TODO(thatguy): Do this if it becomes a problem.

  Entry entry;
  if (!modular::XdrRead(&doc, &entry, XdrEntry)) {
    FXL_LOG(WARNING) << "Could not parse Module manifest from: " << path;
    return;
  }

  fn(name, std::move(entry));
}

void DirectoryModuleManifestSource::OnRemoveFile(const std::string& name,
                                                 RemovedEntryFn fn) {
  fn(name);
}

}  // namespace modular
