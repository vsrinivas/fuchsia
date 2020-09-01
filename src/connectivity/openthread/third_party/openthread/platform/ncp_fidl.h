// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CONNECTIVITY_OPENTHREAD_THIRD_PARTY_OPENTHREAD_PLATFORM_NCP_FIDL_H_
#define SRC_CONNECTIVITY_OPENTHREAD_THIRD_PARTY_OPENTHREAD_PLATFORM_NCP_FIDL_H_

#include <ncp/ncp_config.h>

#include "openthread-system.h"

#include <lib/hdlc/hdlc.hpp>
#include <ncp/ncp_base.hpp>

namespace ot {
namespace Ncp {

class NcpFidl : public NcpBase {
 public:
  /**
   * Constructor, requires an initialized ot::Instance.
   *
   */
  explicit NcpFidl(Instance* a_instance);
  /**
   * Initialization, requires an initialized OtStackCallBack instance.
   *
   */
  void Init(OtStackCallBack* callback_ptr);
  /**
   * This method is called when Ncp Fidl inbound tramsmission is finished.
   * It prepares and sends the next data chunk (if any) to Fidl.
   *
   */
  void HandleFidlSendDone(void);
  /**
   * This method is called when Ncp Fidl outbound buffer is ready.
   * It post task to send frame back to the Ncp client.
   *
   */
  static void HandleFrameAddedToNcpBuffer(void* aContext, Spinel::Buffer::FrameTag aTag,
                                          Spinel::Buffer::Priority aPriority,
                                          Spinel::Buffer* aBuffer);
  /**
   * This method is called when Ncp Fidl inbound buffer is ready.
   * It will get context and call HandleFrameAddedToNcpBuffer().
   *
   */
  void HandleFrameAddedToNcpBuffer(void);
  /**
   * This method is called when Fidl received a data buffer.
   *
   */
  void HandleFidlReceiveDone(const uint8_t* a_buf, uint16_t a_buf_length);
  /**
   * Get the callback pointer of ot-stack.
   *
   */
  OtStackCallBack* OtStackCallbackPtr();

 private:
  std::optional<OtStackCallBack*> ot_stack_callback_ptr_{std::nullopt};
};

/**
 * This is called when initialize the singleton NcpFidl object.
 *
 */
void otNcpInit(otInstance* a_instance);
/**
 * This is called to get the NcpFidl instance.
 *
 */
NcpFidl* otNcpGetInstance();

};  // namespace Ncp
};  // namespace ot

#endif  // SRC_CONNECTIVITY_OPENTHREAD_THIRD_PARTY_OPENTHREAD_PLATFORM_NCP_FIDL_H_
