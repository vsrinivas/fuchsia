// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#ifndef SRC_CONNECTIVITY_OPENTHREAD_THIRD_PARTY_OPENTHREAD_PLATFORM_INFRA_IF_H_
#define SRC_CONNECTIVITY_OPENTHREAD_THIRD_PARTY_OPENTHREAD_PLATFORM_INFRA_IF_H_
/**
 * @file
 *   This file implements the infrastructure interface for fuchsia.
 */

#include <openthread-config-fuchsia.h>
#if OPENTHREAD_CONFIG_BORDER_ROUTING_ENABLE
#include <assert.h>
#include <net/if.h>

#include <openthread/ip6.h>
#include <openthread/platform/logging.h>
#include <platform/exit_code.h>

#include "openthread-system.h"

namespace ot {
namespace Fuchsia {

extern "C" void platformInfraIfOnReceiveIcmp6Msg(otInstance* a_instance);
extern "C" int platformInfraIfInit(int infra_if_idx);
extern "C" void platformInfraIfOnStateChanged(otInstance* a_instance);

#define VERIFY_OR_ASSERT(a_condition, a_message)                                              \
  do {                                                                                        \
    if (!(a_condition)) {                                                                     \
      otPlatLog(OT_LOG_LEVEL_CRIT, OT_LOG_REGION_PLATFORM, "FAILED %s:%d - %s", __FUNCTION__, \
                __LINE__, a_message);                                                         \
      assert(false);                                                                          \
    }                                                                                         \
  } while (false)

enum SocketBlockOption {
  kSocketBlock,
  kSocketNonBlock,
};

class InfraNetif {
 public:
  /**
   * This is called during the init of lowpan-ot-driver after the OtInstance is created.
   */
  void Init(uint32_t infra_if_idx);

  /**
   * This is called when lowpan-ot-driver is deinitialized.
   *
   * @note This method is called after OpenThread instance is destructed.
   *
   */
  void Deinit(void);

  /**
   * This method checks whether the infrastructure network interface is running.
   *
   */
  bool IsRunning(void) const;

  /**
   * This method sends an ICMPv6 Neighbor Discovery message on given infrastructure interface.
   *
   * See RFC 4861: https://tools.ietf.org/html/rfc4861.
   *
   * @param[in]  a_infra_if_index  The index of the infrastructure interface this message is sent
   * to.
   * @param[in]  a_dest_address   The destination address this message is sent to.
   * @param[in]  a_buffer        The ICMPv6 message buffer. The ICMPv6 checksum is left zero and the
   *                            platform should do the checksum calculate.
   * @param[in]  a_buffer_length  The length of the message buffer.
   *
   * @note  Per RFC 4861, the implementation should send the message with IPv6 link-local source
   * address of interface @p a_infra_if_index and IP Hop Limit 255.
   *
   * @retval OT_ERROR_NONE    Successfully sent the ICMPv6 message.
   * @retval OT_ERROR_FAILED  Failed to send the ICMPv6 message.
   *
   */
  otError SendIcmp6Nd(uint32_t a_infra_if_index, const otIp6Address& a_dest_address,
                      const uint8_t* a_buffer, uint16_t a_buffer_length);
  /**
   * This function gets the infrastructure network interface singleton.
   *
   * @returns The singleton object.
   */
  static InfraNetif& Get(void);

  void ReceiveIcmp6Message(otInstance* a_instance);

  int GetIcmpSocket(void) { return infra_if_icmp6_socket_; }

  void OnStateChanged(otInstance* a_instance);

 private:
  char infra_if_name_[IF_NAMESIZE];
  uint32_t infra_if_name_len_ = 0;
  uint32_t infra_if_idx_ = 0;
  int infra_if_icmp6_socket_ = -1;
  bool HasLinkLocalAddress(void) const;
};

}  // namespace Fuchsia
}  // namespace ot
#endif  // OPENTHREAD_CONFIG_BORDER_ROUTING_ENABLE
#endif  // SRC_CONNECTIVITY_OPENTHREAD_THIRD_PARTY_OPENTHREAD_PLATFORM_INFRA_IF_H_
