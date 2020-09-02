/*
 *  Copyright (c) 2016, The OpenThread Authors.
 *  All rights reserved.
 *
 *  Redistribution and use in source and binary forms, with or without
 *  modification, are permitted provided that the following conditions are met:
 *  1. Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 *  2. Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in the
 *     documentation and/or other materials provided with the distribution.
 *  3. Neither the name of the copyright holder nor the
 *     names of its contributors may be used to endorse or promote products
 *     derived from this software without specific prior written permission.
 *
 *  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 *  AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 *  IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 *  ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 *  LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 *  CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 *  SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 *  INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 *  CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 *  ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 *  POSSIBILITY OF SUCH DAMAGE.
 */

/**
 * @file
 * @brief
 *   This file includes the platform-specific initializers.
 */

#include <openthread/tasklet.h>

#include "alarm.h"
#include "openthread-system.h"

void platformSimInit(void);
extern "C" void platformRadioInit(const otPlatformConfig *a_platform_config);
void platformRandomInit(void);
void platformAlarmInit(uint32_t a_speed_up_factor);
static OtStackCallBack *ot_stack_callback_ptr = nullptr;

otInstance *otSysInit(otPlatformConfig *a_platform_config) {
  otInstance *instance = NULL;
  platformAlarmInit(a_platform_config->m_speed_up_factor);
  platformAlarmSetCallbackPtr(a_platform_config->callback_ptr);
  platformRadioInit(a_platform_config);
  platformRandomInit();
  ot_stack_callback_ptr = a_platform_config->callback_ptr;

  instance = otInstanceInitSingle();
  return instance;
}

extern void otTaskletsSignalPending(otInstance *aInstance) {
  OT_UNUSED_VARIABLE(aInstance);
  if (ot_stack_callback_ptr != nullptr) {
    ot_stack_callback_ptr->PostOtLibTaskletProcessTask();
  }
}
