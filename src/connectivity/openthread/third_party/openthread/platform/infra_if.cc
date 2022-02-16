// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "infra_if.h"
#ifdef OPENTHREAD_CONFIG_BORDER_ROUTING_ENABLE
#include <openthread/platform/infra_if.h>

extern "C" bool otPlatInfraIfHasAddress(uint32_t aInfraIfIndex, const otIp6Address* aAddress) {
  return false;
}

extern "C" otError otPlatInfraIfSendIcmp6Nd(uint32_t aInfraIfIndex,
                                            const otIp6Address* aDestAddress,
                                            const uint8_t* aBuffer, uint16_t aBufferLength) {
  return OT_ERROR_NONE;
}
#endif  // OPENTHREAD_CONFIG_BORDER_ROUTING_ENABLE
