// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CONNECTIVITY_OPENTHREAD_LIB_OT_STACK_INCLUDE_LIB_OT_STACK_OT_STACK_CALLBACK_H_
#define SRC_CONNECTIVITY_OPENTHREAD_LIB_OT_STACK_INCLUDE_LIB_OT_STACK_OT_STACK_CALLBACK_H_
#include <fuchsia/lowpan/spinel/llcpp/fidl.h>

#include <vector>

/**
 * @file
 * This file contains pure virtual functions to be implemented by ot-stack
 *
 */

class OtStackCallBack {
 public:
  OtStackCallBack() = default;
  virtual ~OtStackCallBack() = default;
  virtual void SendOneFrameToRadio(uint8_t* buffer, uint32_t size) = 0;
  virtual std::vector<uint8_t> WaitForFrameFromRadio(uint64_t timeout_us) = 0;
  virtual std::vector<uint8_t> Process() = 0;
  virtual void SendOneFrameToClient(uint8_t* buffer, uint32_t size) = 0;
  virtual void PostNcpFidlInboundTask() = 0;
  virtual void PostOtLibTaskletProcessTask() = 0;
  virtual void PostDelayedAlarmTask(zx::duration delay) = 0;
};

#endif  // SRC_CONNECTIVITY_OPENTHREAD_LIB_OT_STACK_INCLUDE_LIB_OT_STACK_OT_STACK_CALLBACK_H_
