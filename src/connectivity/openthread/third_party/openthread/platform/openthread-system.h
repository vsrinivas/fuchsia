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
 *   This file defines the platform-specific functions needed by OpenThread's example applications.
 */

#ifndef SRC_CONNECTIVITY_OPENTHREAD_THIRD_PARTY_OPENTHREAD_PLATFORM_OPENTHREAD_SYSTEM_H_
#define SRC_CONNECTIVITY_OPENTHREAD_THIRD_PARTY_OPENTHREAD_PLATFORM_OPENTHREAD_SYSTEM_H_

#include <lib/ot-stack/ot-stack-callback.h>
#include <setjmp.h>
#include <stdbool.h>
#include <stdint.h>
#include <sys/select.h>

#include <openthread/error.h>
#include <openthread/instance.h>

extern "C" {

/**
 * This structure represents platform specific configurations.
 *
 */
typedef struct otPlatformConfig {
  uint32_t m_speed_up_factor;  ///< Speed up factor.
  OtStackCallBack *callback_ptr;
} otPlatformConfig;

/**
 * This function performs all platform-specific initialization of OpenThread's drivers.
 *
 * @note This function is not called by the OpenThread library. Instead, the system/RTOS should call
 * this function when initialization of OpenThread's drivers is most appropriate.
 *
 * @param[in]  a_platform_config  Platform configuration structure.
 *
 * @returns A pointer to the OpenThread instance.
 *
 */
otInstance *otSysInit(otPlatformConfig *a_platform_config);

/**
 * This structure represents a context for a select() based mainloop.
 *
 */
typedef struct otSysMainloopContext {
  struct timeval m_timeout;  ///< The timeout.
} otSysMainloopContext;

typedef struct otRadioSpinelContext {
  uint8_t none;
} otRadioSpinelContext;

}  // end of extern "C"

#endif  // SRC_CONNECTIVITY_OPENTHREAD_THIRD_PARTY_OPENTHREAD_PLATFORM_OPENTHREAD_SYSTEM_H_
