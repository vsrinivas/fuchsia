// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/process_explorer/process_explorer.h"

#include <fuchsia/kernel/cpp/fidl.h>
#include <lib/fdio/directory.h>
#include <lib/syslog/cpp/macros.h>
#include <zircon/status.h>

#include <task-utils/walker.h>

#include "src/developer/process_explorer/utils.h"
#include "src/lib/fsl/socket/strings.h"

namespace process_explorer {
namespace {

class ProcessWalker : public TaskEnumerator {
 public:
  ProcessWalker() = default;
  ~ProcessWalker() = default;

  zx_status_t WalkProcessTree(std::vector<Process>* processes) {
    FX_CHECK(processes_.empty());

    if (auto status = WalkRootJobTree(); status != ZX_OK) {
      FX_LOGS(ERROR) << "Unable to walk job tree: " << zx_status_get_string(status);
      return status;
    }

    *processes = std::move(processes_);
    processes_.clear();
    return ZX_OK;
  }

  zx_status_t OnProcess(int depth, zx_handle_t process_handle, zx_koid_t koid,
                        zx_koid_t parent_koid) override {
    zx::unowned_process process(process_handle);
    char name[ZX_MAX_NAME_LEN];

    if (auto status = process->get_property(ZX_PROP_NAME, &name, sizeof(name)); status != ZX_OK) {
      FX_LOGS(ERROR) << "Unable to get process name: " << zx_status_get_string(status);
      return status;
    }

    std::vector<zx_info_handle_extended_t> handles;
    if (auto status = GetHandles(std::move(process), &handles); status != ZX_OK) {
      FX_LOGS(ERROR) << "Unable to get associated handles for process: "
                     << zx_status_get_string(status);
      return status;
    }

    std::vector<KernelObject> process_objects;
    for (auto const& handle : handles) {
      process_objects.push_back(
          {handle.type, handle.koid, handle.related_koid, handle.peer_owner_koid});
    }

    std::string process_name(name);
    processes_.push_back({koid, process_name, process_objects});

    return ZX_OK;
  }

 protected:
  bool has_on_process() const override { return true; }

 private:
  std::vector<Process> processes_;
};

zx_status_t GetProcessesData(std::vector<Process>* processes_data) {
  ProcessWalker process_walker;
  if (auto status = process_walker.WalkProcessTree(processes_data); status != ZX_OK) {
    FX_LOGS(ERROR) << "Unable to get processes data: " << zx_status_get_string(status);
    return status;
  }

  // TODO(fxbug.dev/60170): Remove call to FillPeerOwnerKoid (and remove
  // FillPeerOwnerKoid itself) after peer owner koids become populated by
  // the kernel.
  FillPeerOwnerKoid(*processes_data);

  return ZX_OK;
}

}  // namespace

Explorer::Explorer(std::unique_ptr<sys::ComponentContext> context)
    : component_context_(std::move(context)) {
  component_context_->outgoing()->AddPublicService(bindings_.GetHandler(this));
}

Explorer::~Explorer() = default;

void Explorer::WriteJsonProcessesData(zx::socket socket) {
  std::vector<Process> processes_data;
  if (auto status = GetProcessesData(&processes_data); status != ZX_OK) {
    // Returning immediately. Nothing will have been written on the socket which will let the client
    // know an error has occurred.
    return;
  }

  const std::string json_string = WriteProcessesDataAsJson(std::move(processes_data));

  // TODO(fxbug.dev/108528): change to asynchronous
  fsl::BlockingCopyFromString(json_string, socket);
}

}  // namespace process_explorer
