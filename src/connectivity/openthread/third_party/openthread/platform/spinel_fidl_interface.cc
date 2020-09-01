// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "spinel_fidl_interface.h"

#include <sstream>

namespace ot {
namespace Fuchsia {

SpinelFidlInterface::SpinelFidlInterface(Spinel::SpinelInterface::ReceiveFrameCallback aCallback,
                                         void *aCallbackContext,
                                         Spinel::SpinelInterface::RxFrameBuffer &aFrameBuffer)
    : mReceiveFrameCallback(aCallback),
      mReceiveFrameContext(aCallbackContext),
      mReceiveFrameBuffer(aFrameBuffer) {}

otError SpinelFidlInterface::Init(const otPlatformConfig *a_platform_config) {
  if (ot_stack_callback_ptr_.has_value()) {
    otPlatLog(OT_LOG_LEVEL_CRIT, OT_LOG_REGION_PLATFORM,
              "spinelfidl: interface already initialized");
    return OT_ERROR_ALREADY;
  }
  ot_stack_callback_ptr_ = a_platform_config->callback_ptr;
  return OT_ERROR_NONE;
}

void SpinelFidlInterface::Deinit(void) {
  if (ot_stack_callback_ptr_.has_value()) {
    otPlatLog(OT_LOG_LEVEL_CRIT, OT_LOG_REGION_PLATFORM, "spinelfidl: calling deinit before init");
  }
  ot_stack_callback_ptr_.reset();
}

// TODO (jiamingw): remove after ot-lib integration is stabilized
static void printHexArr(const uint8_t *buf, uint16_t buf_len, const char *str) {
#if 0
  if (buf_len) {
    printf("ot-plat: spinel-fidl: %s [", str);
    uint32_t i = 0;
    for (; i < buf_len - 1; i++) {
      printf("0x%x, ", buf[i]);
    }
    printf("0x%x]\n", buf[i]);
  }
#endif
}

otError SpinelFidlInterface::SendFrame(uint8_t *aFrame, uint16_t aLength) {
  OT_UNUSED_VARIABLE(mReceiveFrameCallback);
  OT_UNUSED_VARIABLE(mReceiveFrameContext);
  OT_UNUSED_VARIABLE(mReceiveFrameBuffer);
  otError ret_val = OT_ERROR_NONE;
  if (ot_stack_callback_ptr_.has_value()) {
    ot_stack_callback_ptr_.value()->SendOneFrameToRadio(aFrame, aLength);
    otPlatLog(OT_LOG_LEVEL_INFO, OT_LOG_REGION_PLATFORM,
              "spinel-fidl: sending outbound frame to radio, data:");
    printHexArr(aFrame, aLength, "tx to radio");
  } else {
    otPlatLog(OT_LOG_LEVEL_INFO, OT_LOG_REGION_PLATFORM,
              "spinel-fidl: sending frame before init, data:");
    ret_val = OT_ERROR_INVALID_STATE;
  }
  return ret_val;
}

void SpinelFidlInterface::WriteToRxFrameBuffer(std::vector<uint8_t> vec) {
  mReceiveFrameBuffer.Clear();
  if (mReceiveFrameBuffer.CanWrite(vec.size())) {
    for (uint32_t i = 0; i < vec.size(); i++) {
      // This will not fail, since the available buffer size has been checked
      (void)mReceiveFrameBuffer.WriteByte(vec[i]);
    }
    mReceiveFrameCallback(mReceiveFrameContext);
  } else {
    // Frame too large. Assert here for easier debugging
    otPlatLog(OT_LOG_LEVEL_CRIT, OT_LOG_REGION_PLATFORM, "spinel-fidl: received frame too large");
    OT_ASSERT(0);
  }
}

otError SpinelFidlInterface::WaitForFrame(uint64_t aTimeoutUs) {
  otError ret_val = OT_ERROR_NONE;
  if (ot_stack_callback_ptr_.has_value()) {
    std::vector<uint8_t> vec = ot_stack_callback_ptr_.value()->WaitForFrameFromRadio(aTimeoutUs);
    if (vec.size() == 0) {
      // This is okay, since ot-lib request with a relatively short timeout.
      otPlatLog(OT_LOG_LEVEL_INFO, OT_LOG_REGION_PLATFORM, "spinel-fidl: failed to receive frame");
      return OT_ERROR_RESPONSE_TIMEOUT;
    }
    otPlatLog(OT_LOG_LEVEL_INFO, OT_LOG_REGION_PLATFORM,
              "spinel-fidl: WaitForFrame() received inbound frame from radio, data:");
    printHexArr(vec.data(), vec.size(), "rx from radio");
    WriteToRxFrameBuffer(vec);
  } else {
    otPlatLog(OT_LOG_LEVEL_CRIT, OT_LOG_REGION_PLATFORM,
              "spinel-fidl: waiting for frame before init");
    ret_val = OT_ERROR_INVALID_STATE;
    OT_ASSERT(0);
  }
  return ret_val;
}

void SpinelFidlInterface::Process(const otRadioSpinelContext &aContext) {
  OT_UNUSED_VARIABLE(aContext);
  if (ot_stack_callback_ptr_.has_value()) {
    std::vector<uint8_t> vec = ot_stack_callback_ptr_.value()->Process();
    if (vec.size() == 0) {
      otPlatLog(OT_LOG_LEVEL_INFO, OT_LOG_REGION_PLATFORM, "spinel-fidl: no new frame");
      return;
    }
    otPlatLog(OT_LOG_LEVEL_INFO, OT_LOG_REGION_PLATFORM,
              "spinel-fidl: Process() received inbound frame from radio, data:");
    printHexArr(vec.data(), vec.size(), "rx from radio (event)");
    WriteToRxFrameBuffer(vec);
  } else {
    otPlatLog(OT_LOG_LEVEL_CRIT, OT_LOG_REGION_PLATFORM,
              "spinel-fidl: waiting for frame before init");
    OT_ASSERT(0);
  }
}

}  // namespace Fuchsia
}  // namespace ot
