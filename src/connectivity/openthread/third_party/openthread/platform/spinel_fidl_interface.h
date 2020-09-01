// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CONNECTIVITY_OPENTHREAD_THIRD_PARTY_OPENTHREAD_PLATFORM_SPINEL_FIDL_INTERFACE_H_
#define SRC_CONNECTIVITY_OPENTHREAD_THIRD_PARTY_OPENTHREAD_PLATFORM_SPINEL_FIDL_INTERFACE_H_

#include <stdint.h>
#include <string.h>

#include <openthread/error.h>

#include "openthread-system.h"

#include <spinel_interface.hpp>
namespace ot {
namespace Fuchsia {
/**
 * This class defines an spinel interface to the Radio Co-processor (RCP)
 *
 */
class SpinelFidlInterface {
 public:
  /**
   * Constructor
   *
   */
  SpinelFidlInterface(Spinel::SpinelInterface::ReceiveFrameCallback aCallback,
                      void* aCallbackContext, Spinel::SpinelInterface::RxFrameBuffer& aFrameBuffer);
  /**
   * Initializes the Spinel Fidl Interface with a valid OtStackCallBack pointer in the config
   *
   */
  otError Init(const otPlatformConfig* a_platform_config);
  /**
   * Deinitialized the instance
   *
   */
  void Deinit(void);
  /**
   * Send the frame from ot-lib to ot-radio driver
   *
   */
  otError SendFrame(uint8_t* aFrame, uint16_t aLength);
  /**
   * Used for waiting for a spinel frame response with a timeout
   *
   */
  otError WaitForFrame(uint64_t aTimeoutUs);
  /**
   * Used for process a spinel frame event
   *
   */
  void Process(const otRadioSpinelContext& aContext);

 private:
  /**
   * Write received inbound frame to the buffer where can be processed by ot-lib
   *
   */
  void WriteToRxFrameBuffer(std::vector<uint8_t> vec);

  Spinel::SpinelInterface::ReceiveFrameCallback mReceiveFrameCallback;
  void* mReceiveFrameContext;
  Spinel::SpinelInterface::RxFrameBuffer& mReceiveFrameBuffer;

  std::optional<OtStackCallBack*> ot_stack_callback_ptr_;
};  // SpinelFidlInterface
};  // namespace Fuchsia
};  // namespace ot

#endif  // SRC_CONNECTIVITY_OPENTHREAD_THIRD_PARTY_OPENTHREAD_PLATFORM_SPINEL_FIDL_INTERFACE_H_
