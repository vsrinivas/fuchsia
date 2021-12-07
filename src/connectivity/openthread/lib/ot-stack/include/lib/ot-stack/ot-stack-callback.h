// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CONNECTIVITY_OPENTHREAD_LIB_OT_STACK_INCLUDE_LIB_OT_STACK_OT_STACK_CALLBACK_H_
#define SRC_CONNECTIVITY_OPENTHREAD_LIB_OT_STACK_INCLUDE_LIB_OT_STACK_OT_STACK_CALLBACK_H_
#include <fidl/fuchsia.lowpan.spinel/cpp/wire.h>

#include <openthread/instance.h>
#include <openthread/platform/logging.h>

/**
 * @file
 * This file contains interfaces to be implemented by ot-stack and/or lowpan-driver
 *
 */

extern "C" void platformCallbackSendOneFrameToRadio(otInstance* a_instance, uint8_t* buffer,
                                                    size_t size);
extern "C" size_t platformCallbackWaitForFrameFromRadio(otInstance* a_instance, uint8_t* buffer,
                                                        size_t buffer_len_max, uint64_t timeout_us);
extern "C" size_t platformCallbackFetchQueuedFrameFromRadio(otInstance* a_instance, uint8_t* buffer,
                                                            size_t buffer_len_max);
extern "C" void platformCallbackSendOneFrameToClient(otInstance* a_instance, uint8_t* buffer,
                                                     size_t size);
extern "C" void platformCallbackPostNcpFidlInboundTask(otInstance* a_instance);
extern "C" void platformCallbackPostDelayedAlarmTask(otInstance* a_instance, zx_duration_t delay);

extern "C" void otPlatLogLine(otLogLevel aLogLevel, otLogRegion aLogRegion, const char* aLine);
extern "C" void platformLog(otInstance* a_instance, zx_duration_t delay);

#endif  // SRC_CONNECTIVITY_OPENTHREAD_LIB_OT_STACK_INCLUDE_LIB_OT_STACK_OT_STACK_CALLBACK_H_
