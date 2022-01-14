// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "spinel_fidl_interface.h"

#include <sstream>

namespace ot {
namespace Fuchsia {

static otInstance *sOtInstancePtr = nullptr;

void spinelInterfaceInit(otInstance *a_instance) { sOtInstancePtr = a_instance; }

SpinelFidlInterface::SpinelFidlInterface(Spinel::SpinelInterface::ReceiveFrameCallback aCallback,
                                         void *aCallbackContext,
                                         Spinel::SpinelInterface::RxFrameBuffer &aFrameBuffer)
    : mReceiveFrameCallback(aCallback),
      mReceiveFrameContext(aCallbackContext),
      mReceiveFrameBuffer(aFrameBuffer) {}

otError SpinelFidlInterface::Init() { return OT_ERROR_NONE; }

void SpinelFidlInterface::Deinit(void) { sOtInstancePtr = nullptr; }

otError SpinelFidlInterface::SendFrame(uint8_t *aFrame, uint16_t aLength) {
  OT_UNUSED_VARIABLE(mReceiveFrameCallback);
  OT_UNUSED_VARIABLE(mReceiveFrameContext);
  OT_UNUSED_VARIABLE(mReceiveFrameBuffer);
  otError ret_val = OT_ERROR_NONE;
  platformCallbackSendOneFrameToRadio(sOtInstancePtr, aFrame, aLength);
  return ret_val;
}

void SpinelFidlInterface::WriteToRxFrameBuffer(std::vector<uint8_t> vec) {
  // Discard any previous frame that wasn't saved.
  mReceiveFrameBuffer.DiscardFrame();
  if (mReceiveFrameBuffer.CanWrite(vec.size())) {
    for (uint32_t i = 0; i < vec.size(); i++) {
      // This will not fail, since the available buffer size has been checked
      (void)mReceiveFrameBuffer.WriteByte(vec[i]);
    }
    mReceiveFrameCallback(mReceiveFrameContext);
  } else {
    // Frame too large. Assert here for easier debugging
    otPlatLog(OT_LOG_LEVEL_CRIT, OT_LOG_REGION_PLATFORM,
              "spinel-fidl: rx buffer full, requesting %u bytes", vec.size());
    OT_ASSERT(0);
  }
}

otError SpinelFidlInterface::WaitForFrame(uint64_t aTimeoutUs) {
  otError ret_val = OT_ERROR_NONE;
  uint8_t buffer[SPINEL_FRAME_MAX_SIZE];
  size_t size = platformCallbackWaitForFrameFromRadio(sOtInstancePtr, buffer, SPINEL_FRAME_MAX_SIZE,
                                                      aTimeoutUs);
  std::vector<uint8_t> vec;
  vec.assign(buffer, buffer + size);
  if (vec.size() == 0) {
    // This is okay, since ot-lib request with a relatively short timeout.
    otPlatLog(OT_LOG_LEVEL_DEBG, OT_LOG_REGION_PLATFORM, "spinel-fidl: failed to receive frame");
    return OT_ERROR_RESPONSE_TIMEOUT;
  }
  WriteToRxFrameBuffer(vec);
  return ret_val;
}

void SpinelFidlInterface::Process(const otRadioSpinelContext &aContext) {
  OT_UNUSED_VARIABLE(aContext);
  uint8_t buffer[SPINEL_FRAME_MAX_SIZE];
  for (;;) {
    size_t size =
        platformCallbackFetchQueuedFrameFromRadio(sOtInstancePtr, buffer, SPINEL_FRAME_MAX_SIZE);
    if (size == 0) {
      return;
    }
    std::vector<uint8_t> vec;
    vec.assign(buffer, buffer + size);
    WriteToRxFrameBuffer(vec);
  }
}

void SpinelFidlInterface::OnRcpReset(void) {
  otPlatLog(OT_LOG_LEVEL_WARN, OT_LOG_REGION_PLATFORM, "SpinelFidlInterface::OnRcpReset()");
}

otError SpinelFidlInterface::ResetConnection(void) {
  otPlatLog(OT_LOG_LEVEL_NOTE, OT_LOG_REGION_PLATFORM, "SpinelFidlInterface::ResetConnection()");
  return OT_ERROR_NONE;
}

}  // namespace Fuchsia
}  // namespace ot
