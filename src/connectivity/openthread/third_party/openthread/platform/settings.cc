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
 *   This file implements the OpenThread platform abstraction for non-volatile storage of settings.
 *
 */

#include <openthread/platform/misc.h>
#include <openthread/platform/settings.h>

static otError platformSettingsDelete(otInstance *a_instance, uint16_t a_key, int a_index,
                                      int *a_swap_fd);

void otPlatSettingsInit(otInstance *a_instance) {}

void otPlatSettingsDeinit(otInstance *a_instance) {}

otError otPlatSettingsGet(otInstance *a_instance, uint16_t a_key, int a_index, uint8_t *a_value,
                          uint16_t *a_value_length) {
  return OT_ERROR_NONE;
}

otError otPlatSettingsSet(otInstance *a_instance, uint16_t a_key, const uint8_t *a_value,
                          uint16_t a_value_length) {
  return OT_ERROR_NONE;
}

otError otPlatSettingsAdd(otInstance *a_instance, uint16_t a_key, const uint8_t *a_value,
                          uint16_t a_value_length) {
  return OT_ERROR_NONE;
}

otError otPlatSettingsDelete(otInstance *a_instance, uint16_t a_key, int a_index) {
  return platformSettingsDelete(a_instance, a_key, a_index, NULL);
}

static otError platformSettingsDelete(otInstance *a_instance, uint16_t a_key, int a_index,
                                      int *a_swap_fd) {
  return OT_ERROR_NONE;
}

void otPlatSettingsWipe(otInstance *a_instance) {}
