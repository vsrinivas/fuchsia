// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/ot-stack/ot-stack-callback.h>
#include <lib/syslog/cpp/macros.h>

extern "C" void platformCallbackSendOneFrameToRadio(otInstance* a_instance, uint8_t* buffer,
                                                    size_t size) {}
extern "C" size_t platformCallbackWaitForFrameFromRadio(otInstance* a_instance, uint8_t* buffer,
                                                        size_t buffer_len_max,
                                                        uint64_t timeout_us) {
  return 0;
}
extern "C" size_t platformCallbackFetchQueuedFrameFromRadio(otInstance* a_instance, uint8_t* buffer,
                                                            size_t buffer_len_max) {
  return 0;
}
extern "C" void platformCallbackSendOneFrameToClient(otInstance* a_instance, uint8_t* buffer,
                                                     size_t size) {}
extern "C" void platformCallbackPostNcpFidlInboundTask(otInstance* a_instance) {}
extern "C" void platformCallbackPostDelayedAlarmTask(otInstance* a_instance, zx_duration_t delay) {}
extern "C" void otPlatReset(otInstance* a_instance) {}

extern "C" void otPlatLogLine(otLogLevel log_level, otLogRegion log_region, const char* line) {
  // Print the string to appropriate FX_LOG
  switch (log_level) {
    default:
    case OT_LOG_LEVEL_NONE:
      FX_LOGS(FATAL) << line << std::endl;
      break;

    case OT_LOG_LEVEL_CRIT:
      FX_LOGS(ERROR) << line << std::endl;
      break;

    case OT_LOG_LEVEL_WARN:
      FX_LOGS(WARNING) << line << std::endl;
      break;

    case OT_LOG_LEVEL_NOTE:
    case OT_LOG_LEVEL_INFO:
      FX_LOGS(INFO) << line << std::endl;
      break;

    case OT_LOG_LEVEL_DEBG:
      FX_LOGS(DEBUG) << line << std::endl;
      break;
  }
}
