/*
 *  Copyright (c) 2018, The OpenThread Authors.
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
 *   This file implements the spinel based radio transceiver.
 */

#include <openthread/platform/radio.h>

#include "openthread-system.h"

extern "C" void otPlatRadioGetIeeeEui64(otInstance *a_instance, uint8_t *a_ieee_eui64) {}

extern "C" void otPlatRadioSetPanId(otInstance *a_instance, uint16_t panid) {}

extern "C" void otPlatRadioSetExtendedAddress(otInstance *a_instance,
                                              const otExtAddress *a_address) {}

extern "C" void otPlatRadioSetShortAddress(otInstance *a_instance, uint16_t a_address) {}

extern "C" void otPlatRadioSetPromiscuous(otInstance *a_instance, bool a_enable) {}

extern "C" void platformRadioInit(const otPlatformConfig *a_platform_config) {}

extern "C" otError otPlatRadioEnable(otInstance *a_instance) { return OT_ERROR_NONE; }

extern "C" otError otPlatRadioDisable(otInstance *a_instance) { return OT_ERROR_NONE; }

extern "C" otError otPlatRadioSleep(otInstance *a_instance) { return OT_ERROR_NONE; }

extern "C" otError otPlatRadioReceive(otInstance *a_instance, uint8_t a_channel) {
  return OT_ERROR_NONE;
}

extern "C" otError otPlatRadioTransmit(otInstance *a_instance, otRadioFrame *a_frame) {
  return OT_ERROR_NONE;
}

extern "C" otRadioFrame *otPlatRadioGetTransmitBuffer(otInstance *a_instance) { return NULL; }

extern "C" int8_t otPlatRadioGetRssi(otInstance *a_instance) { return 0; }

extern "C" otRadioCaps otPlatRadioGetCaps(otInstance *a_instance) { return 0; }

extern "C" bool otPlatRadioGetPromiscuous(otInstance *a_instance) { return false; }

extern "C" void otPlatRadioEnableSrcMatch(otInstance *a_instance, bool a_enable) {}

extern "C" otError otPlatRadioAddSrcMatchShortEntry(otInstance *a_instance,
                                                    uint16_t a_short_address) {
  return OT_ERROR_NONE;
}

extern "C" otError otPlatRadioAddSrcMatchExtEntry(otInstance *a_instance,
                                                  const otExtAddress *a_ext_address) {
  return OT_ERROR_NONE;
}
extern "C" otError otPlatRadioClearSrcMatchShortEntry(otInstance *a_instance,
                                                      uint16_t a_short_address) {
  return OT_ERROR_NONE;
}

extern "C" otError otPlatRadioClearSrcMatchExtEntry(otInstance *a_instance,
                                                    const otExtAddress *a_ext_address) {
  return OT_ERROR_NONE;
}

extern "C" void otPlatRadioClearSrcMatchShortEntries(otInstance *a_instance) {}

extern "C" void otPlatRadioClearSrcMatchExtEntries(otInstance *a_instance) {}

extern "C" otError otPlatRadioEnergyScan(otInstance *a_instance, uint8_t a_scan_channel,
                                         uint16_t a_scan_duration) {
  return OT_ERROR_NONE;
}

extern "C" int8_t otPlatRadioGetReceiveSensitivity(otInstance *a_instance) { return 0; }

extern "C" otError otPlatRadioGetCcaEnergyDetectThreshold(otInstance *a_instance,
                                                          int8_t *a_threshold) {
  OT_UNUSED_VARIABLE(a_instance);
  OT_UNUSED_VARIABLE(a_threshold);
  return OT_ERROR_NOT_IMPLEMENTED;
}

extern "C" otError otPlatRadioSetCcaEnergyDetectThreshold(otInstance *a_instance,
                                                          int8_t a_threshold) {
  OT_UNUSED_VARIABLE(a_instance);
  OT_UNUSED_VARIABLE(a_threshold);
  return OT_ERROR_NOT_IMPLEMENTED;
}

extern "C" otError otPlatRadioGetTransmitPower(otInstance *a_instance, int8_t *a_power) {
  OT_UNUSED_VARIABLE(a_instance);
  OT_UNUSED_VARIABLE(a_power);
  return OT_ERROR_NOT_IMPLEMENTED;
}

extern "C" otError otPlatRadioSetTransmitPower(otInstance *a_instance, int8_t a_power) {
  OT_UNUSED_VARIABLE(a_power);
  OT_UNUSED_VARIABLE(a_instance);
  return OT_ERROR_NOT_IMPLEMENTED;
}
