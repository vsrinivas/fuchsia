// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/async/dispatcher.h>
#include <lib/syslog/cpp/macros.h>
#include <lib/trace-provider/provider.h>

#include "src/storage/f2fs/f2fs.h"

namespace f2fs {

// TODO: change .configurable to true once the feature is supported
// TODO: set default on for inline_dentry after inline dentry check on fsck is available
const MountOpt default_option[] = {
    {"background_gc_off", 1, false},
#ifdef F2FS_ROLL_FORWARD
    {"disable_roll_forward", 0, false},
#else
    {"disable_roll_forward", 1, false},
#endif
    {"discard", 1, true},
    {"no_heap", 1, false},
    {"nouser_xattr", 1, false},
    {"noacl", 1, false},
    {"disable_ext_identify", 0, true},
    {"inline_xattr", 0, false},
    {"inline_data", 0, false},
    {"inline_dentry", 0, true},
    {"active_logs", 6, true},
};

zx_status_t Mount(const MountOptions &options, std::unique_ptr<f2fs::Bcache> bc) {
  zx::channel outgoing_server = zx::channel(zx_take_startup_handle(PA_DIRECTORY_REQUEST));
  zx::channel root_server = zx::channel(zx_take_startup_handle(FS_HANDLE_ROOT_ID));

  if (outgoing_server.is_valid() && root_server.is_valid()) {
    FX_LOGS(ERROR) << "both PA_DIRECTORY_REQUEST and FS_HANDLE_ROOT_ID provided - need one or the "
                      "other.";
    return ZX_ERR_BAD_STATE;
  }

  fidl::ServerEnd<fuchsia_io::Directory> export_root;
  f2fs::ServeLayout serve_layout;
  if (outgoing_server.is_valid()) {
    export_root = fidl::ServerEnd<fuchsia_io::Directory>(std::move(outgoing_server));
    serve_layout = f2fs::ServeLayout::kExportDirectory;
  } else if (root_server.is_valid()) {
    export_root = fidl::ServerEnd<fuchsia_io::Directory>(std::move(root_server));
    serve_layout = f2fs::ServeLayout::kDataRootOnly;
  } else {
    FX_LOGS(ERROR) << "could not get startup handle to serve on";
    return ZX_ERR_BAD_STATE;
  }

  async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);
  trace::TraceProviderWithFdio trace_provider(loop.dispatcher());

  auto on_unmount = [&loop]() {
    loop.Quit();
    FX_LOGS(WARNING) << "Unmounted";
  };

  auto fs_or = CreateFsAndRoot(options, loop.dispatcher(), std::move(bc), std::move(export_root),
                               std::move(on_unmount), serve_layout);
  if (fs_or.is_error()) {
    FX_LOGS(ERROR) << "failed to create filesystem object " << fs_or.status_string();
    return EXIT_FAILURE;
  }

  FX_LOGS(INFO) << "Mounted successfully";

  ZX_ASSERT(loop.Run() == ZX_ERR_CANCELED);

  return ZX_OK;
}

MountOptions::MountOptions() {
  for (uint32_t i = 0; i < kOptMaxNum; i++) {
    opt_[i] = default_option[i];
  }
}

zx_status_t MountOptions::GetValue(const uint32_t opt_id, uint32_t *out) {
  if (opt_id >= kOptMaxNum)
    return ZX_ERR_INVALID_ARGS;
  *out = opt_[opt_id].value;
  return ZX_OK;
}

uint32_t MountOptions::GetOptionID(const std::string_view &opt) {
  uint32_t i;
  for (i = 0; i < kOptMaxNum; i++) {
    if (opt_[i].name.compare(opt) == 0) {
      break;
    }
  }
  return i;
}

zx_status_t MountOptions::SetValue(const std::string_view &opt, const uint32_t value) {
  zx_status_t ret = ZX_ERR_INVALID_ARGS;
  uint32_t id = GetOptionID(opt);
  if (id < kOptMaxNum && !opt_[id].configurable) {
    FX_LOGS(WARNING) << opt << " is not configurable.";
  } else {
    switch (id) {
      case kOptActiveLogs:
        if (value != 2 && value != 4 && value != 6) {
          FX_LOGS(WARNING) << opt << " can be set only to 2, 4, or 6.";
        } else {
          opt_[id].value = value;
          ret = ZX_OK;
        }
        break;
      case kOptDiscard:
      case kOptBgGcOff:
      case kOptNoHeap:
      case kOptDisableExtIdentify:
      case kOptNoUserXAttr:
      case kOptNoAcl:
      case kOptDisableRollForward:
      case kOptInlineXattr:
      case kOptInlineData:
      case kOptInlineDentry:
        opt_[id].value = value;
        ret = ZX_OK;
        break;
      default:
        FX_LOGS(WARNING) << opt << " is not supported.";
        break;
    };
  }
  return ret;
}

}  // namespace f2fs
