// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/ot-stack/ot-stack-callback.h>
#include <lib/syslog/cpp/macros.h>

#include <openthread/platform/trel.h>
#include <openthread/platform/udp.h>

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

extern "C" otError otPlatUdpSocket(otUdpSocket* aUdpSocket) { return OT_ERROR_NONE; }
extern "C" otError otPlatUdpClose(otUdpSocket* aUdpSocket) { return OT_ERROR_NONE; }
extern "C" otError otPlatUdpBind(otUdpSocket* aUdpSocket) { return OT_ERROR_NONE; }
extern "C" otError otPlatUdpBindToNetif(otUdpSocket* aUdpSocket,
                                        otNetifIdentifier aNetifIdentifier) {
  return OT_ERROR_NONE;
}
extern "C" otError otPlatUdpConnect(otUdpSocket* aUdpSocket) { return OT_ERROR_NONE; }
extern "C" otError otPlatUdpSend(otUdpSocket* aUdpSocket, otMessage* aMessage,
                                 const otMessageInfo* aMessageInfo) {
  return OT_ERROR_NONE;
}
extern "C" otError otPlatUdpJoinMulticastGroup(otUdpSocket* aUdpSocket,
                                               otNetifIdentifier aNetifIdentifier,
                                               const otIp6Address* aAddress) {
  return OT_ERROR_NONE;
}

extern "C" otError otPlatUdpLeaveMulticastGroup(otUdpSocket* aUdpSocket,
                                                otNetifIdentifier aNetifIdentifier,
                                                const otIp6Address* aAddress) {
  return OT_ERROR_NONE;
}

extern "C" void otPlatTrelEnable(otInstance* aInstance, uint16_t* aUdpPort) {}

extern "C" void otPlatTrelDisable(otInstance* aInstance) {}

extern "C" void otPlatTrelRegisterService(otInstance* aInstance, uint16_t aPort,
                                          const uint8_t* aTxtData, uint8_t aTxtLength) {}

extern "C" void otPlatTrelSend(otInstance* aInstance, const uint8_t* aUdpPayload,
                               uint16_t aUdpPayloadLen, const otSockAddr* aDestSockAddr) {}
