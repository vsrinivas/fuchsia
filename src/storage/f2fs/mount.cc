// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifdef __Fuchsia__
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/async/dispatcher.h>
#include <lib/syslog/cpp/macros.h>
#include <lib/trace-provider/provider.h>
#endif  // __Fuchsia__

#include "src/storage/f2fs/f2fs.h"

namespace f2fs {

// TODO: set .configurable to true when the feature is supported.
const MountOpt default_option[] = {
    {"background_gc_off", 1, false},
    {"disable_roll_forward", 0, true},
    {"discard", 1, true},
    {"no_heap", 1, false},
    {"nouser_xattr", 1, false},
    {"noacl", 1, false},
    {"disable_ext_identify", 0, true},
    {"inline_xattr", 0, false},
    {"inline_data", 1, true},
    {"inline_dentry", 1, true},
    {"mode", static_cast<uint32_t>(ModeType::kModeAdaptive), true},
    {"active_logs", 6, true},
};

zx_status_t Mount(const MountOptions &options, std::unique_ptr<f2fs::Bcache> bc) {
#ifdef __Fuchsia__
  zx::channel outgoing_server = zx::channel(zx_take_startup_handle(PA_DIRECTORY_REQUEST));
  if (!outgoing_server.is_valid()) {
    FX_LOGS(ERROR) << "[f2fs] Could not get startup handle to serve on";
    return ZX_ERR_BAD_STATE;
  }

  auto export_root = fidl::ServerEnd<fuchsia_io::Directory>(std::move(outgoing_server));

  async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);
  trace::TraceProviderWithFdio trace_provider(loop.dispatcher());

  auto on_unmount = [&loop]() {
    loop.Quit();
    FX_LOGS(INFO) << "[f2fs] Unmounted successfully";
  };

  auto runner_or = Runner::Create(loop.dispatcher(), std::move(bc), options);
  if (runner_or.is_error()) {
    FX_LOGS(ERROR) << "[f2fs] Failed to create runner object " << runner_or.error_value();
    return runner_or.error_value();
  }

  if (auto status = (*runner_or)->ServeRoot(std::move(export_root)); status.is_error()) {
    return status.error_value();
  }

  (*runner_or)->SetUnmountCallback(std::move(on_unmount));

  FX_LOGS(INFO) << "[f2fs] Mounted successfully";

  ZX_ASSERT(loop.Run() == ZX_ERR_CANCELED);

#else   // __Fuchsia__
  auto runner_or = Runner::Create(nullptr, std::move(bc), options);
  if (runner_or.is_error()) {
    FX_LOGS(ERROR) << "[f2fs] Failed to create filesystem object";
    return runner_or.error_value();
  }
#endif  // __Fuchsia__

  return ZX_OK;
}

MountOptions::MountOptions() {
  for (uint32_t i = 0; i < kOptMaxNum; ++i) {
    opt_[i] = default_option[i];
  }
}

zx_status_t MountOptions::GetValue(const uint32_t opt_id, uint32_t *out) {
  if (opt_id >= kOptMaxNum)
    return ZX_ERR_INVALID_ARGS;
  *out = opt_[opt_id].value;
  return ZX_OK;
}

uint32_t MountOptions::GetOptionID(std::string_view opt) {
  for (uint32_t i = 0; i < kOptMaxNum; ++i) {
    if (opt_[i].name.compare(opt) == 0) {
      return i;
    }
  }
  return kOptMaxNum;
}

zx_status_t MountOptions::SetValue(std::string_view opt, const uint32_t value) {
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
      case kOptForceLfs:
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
