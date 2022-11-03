/*
 * Copyright (c) 2010 Broadcom Corporation
 *
 * Permission to use, copy, modify, and/or distribute this software for any purpose with or without
 * fee is hereby granted, provided that the above copyright notice and this permission notice appear
 * in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES WITH REGARD TO THIS
 * SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE
 * AUTHOR BE LIABLE FOR ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT,
 * NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR PERFORMANCE
 * OF THIS SOFTWARE.
 */

#ifndef SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_BROADCOM_BRCMFMAC_DEBUG_H_
#define SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_BROADCOM_BRCMFMAC_DEBUG_H_

#include <lib/ddk/debug.h>
#include <stdint.h>
#include <zircon/types.h>

#include <algorithm>
#include <cinttypes>
#include <cstring>
#include <utility>

#include <wlan/drivers/log.h>

// Some convenience macros for error and debug printing.
#define BRCMF_ERR(fmt...) lerror(fmt)

#define BRCMF_WARN(fmt...) lwarn(fmt)

#define BRCMF_INFO(fmt...) linfo(fmt)

#define BRCMF_DBG_UNFILTERED BRCMF_INFO

#define BRCMF_DBG(filter, fmt, ...)             \
  do {                                          \
    if (BRCMF_IS_ON(filter)) {                  \
      BRCMF_DBG_UNFILTERED(fmt, ##__VA_ARGS__); \
    }                                           \
  } while (0)

#define BRCMF_DBG_EVENT(ifp, event_msg, REASON_FMT, reason_formatter) \
  BRCMF_DBG_LOG_EVENT(EVENT, ifp, event_msg, REASON_FMT, reason_formatter)

#define BRCMF_DBG_LOG_EVENT(FILTER, ifp, event_msg, REASON_FMT, reason_formatter)                 \
  {                                                                                               \
    if (ifp == nullptr || event_msg == nullptr) {                                                 \
      BRCMF_DBG(FILTER, "Unable to log event %p for ifp %p", event_msg, ifp);                     \
    } else {                                                                                      \
      BRCMF_DBG(FILTER, "IF: %d event %s (%u)", ifp == nullptr ? -1 : ifp->ifidx,                 \
                brcmf_fweh_event_name(static_cast<brcmf_fweh_event_code>(event_msg->event_code)), \
                event_msg->event_code);                                                           \
      BRCMF_DBG(FILTER, "  status %s", brcmf_fweh_get_event_status_str(event_msg->status));       \
      BRCMF_DBG(FILTER, "  reason " REASON_FMT, reason_formatter(event_msg->reason));             \
      BRCMF_DBG(FILTER, "    auth %s", brcmf_fweh_get_auth_type_str(event_msg->auth_type));       \
      BRCMF_DBG(FILTER, "   flags 0x%x", event_msg->flags);                                       \
    }                                                                                             \
  }

// TODO(fxb/61311): Remove once this verbose logging is no longer needed in
// brcmf_indicate_client_disconnect().
#define BRCMF_INFO_EVENT(ifp, event_msg, REASON_FMT, reason_formatter)                             \
  {                                                                                                \
    if (ifp == nullptr || event_msg == nullptr) {                                                  \
      BRCMF_INFO("Unable to log event %p for ifp %p", event_msg, ifp);                             \
    } else {                                                                                       \
      BRCMF_INFO("IF: %d event %s (%u)", ifp == nullptr ? -1 : ifp->ifidx,                         \
                 brcmf_fweh_event_name(static_cast<brcmf_fweh_event_code>(event_msg->event_code)), \
                 event_msg->event_code);                                                           \
      BRCMF_INFO("  status %s", brcmf_fweh_get_event_status_str(event_msg->status));               \
      BRCMF_INFO("  reason " REASON_FMT, reason_formatter(event_msg->reason));                     \
      BRCMF_INFO("    auth %s", brcmf_fweh_get_auth_type_str(event_msg->auth_type));               \
      BRCMF_INFO("   flags 0x%x", event_msg->flags);                                               \
    }                                                                                              \
  }

#define BRCMF_IFDBG(FILTER, ndev, fmt, ...)                                                      \
  BRCMF_DBG(FILTER, "%s(%d): " fmt, brcmf_cfg80211_get_iface_str(ndev), ndev_to_if(ndev)->ifidx, \
            ##__VA_ARGS__);

#define BRCMF_DBG_HEX_DUMP(condition, data, length, fmt, ...) \
  do {                                                        \
    if (condition) {                                          \
      linfo(fmt, ##__VA_ARGS__);                              \
      lhexdump_info(data, length);                            \
    }                                                         \
  } while (0)

#define BRCMF_IS_ON(filter) \
  ::wlan::brcmfmac::Debug::IsFilterOn(::wlan::brcmfmac::Debug::Filter::k##filter)

// Enable an event if |condition| is true and if it is enabled throttle it so that it is only called
// at a rate of at most |events_per_second|. If the condition is not true this will efficiently
// avoid any throttling checks but note that this also means that any calls to this macro when
// condition is false will not count towards throttling.
#define BRCMF_THROTTLE_IF(events_per_second, condition, event) \
  lthrottle_log_if(events_per_second, condition, event)

// Convenience macros for logging with certain log levels and a default number of events per second
// allowed. In order to log with a custom number of events per second just do something like:
// BRCMF_THROTTLE_MSG(42, BRCMF_ERR, "error occurred: %s", error_str);
// which would allow 42 errors per second at most. Replace BRCMF_ERR with the log function you want.
#define BRCMF_ERR_THROTTLE(fmt...) lthrottle_error(fmt)
#define BRCMF_WARN_THROTTLE(fmt...) lthrottle_warn(fmt)
#define BRCMF_INFO_THROTTLE(fmt...) lthrottle_info(fmt)
#define BRCMF_DBG_THROTTLE(filter, fmt...) \
  do {                                     \
    if (BRCMF_IS_ON(filter)) {             \
      BRCMF_INFO_THROTTLE(fmt);            \
    }                                      \
  } while (0)

namespace wlan {
namespace brcmfmac {

// This class implements debugging functionality for the brcmfmac driver.
class Debug {
 public:
  enum class Filter : uint32_t {
    kTEMP = 1 << 0,
    kTRACE = 1 << 1,
    kDATA = 1 << 2,
    kCTL = 1 << 3,
    kTIMER = 1 << 4,
    kHDRS = 1 << 5,
    kBYTES = 1 << 6,
    kINTR = 1 << 7,
    kGLOM = 1 << 8,
    kEVENT = 1 << 9,
    kBTA = 1 << 10,
    kFIL = 1 << 11,
    kUSB = 1 << 12,
    kSCAN = 1 << 13,
    kCONN = 1 << 14,
    kBCDC = 1 << 15,
    kSDIO = 1 << 16,
    kPCIE = 1 << 17,
    kFWCON = 1 << 18,
    kSIM = 1 << 19,
    kWLANIF = 1 << 20,
    kSIMERRINJ = 1 << 21,
    kWLANPHY = 1 << 22,
    kBTCOEX = 1 << 23,
    kQUERY = 1 << 24,
    kFEAT = 1 << 25,
    kALL = ~0u,
  };

  // Enabled debug log categories. Include WLAN_FULLMAC messages in the log output (at level INFO)
  // to aid in recognizing important events.
  //
  // All changes made to this filter must be approved by privacy to ensure sensitive data is not
  // unintentionally printed in logs.
  //
  // http://fxbug.dev/29792 - Remove WLAN_FULLMAC once things have stabilized.
  static constexpr uint32_t kBrcmfMsgFilter = static_cast<uint32_t>(Filter::kWLANIF) |
                                              static_cast<uint32_t>(Filter::kWLANPHY) |
                                              static_cast<uint32_t>(Filter::kSIM);

  // Check if a given debugging filter class is turned on.
  static constexpr bool IsFilterOn(Filter filter) {
    return (static_cast<uint32_t>(filter) & kBrcmfMsgFilter) != 0;
  }

  // Create a memory dump.
  static void CreateMemoryDump(const void* data, size_t length);
};

}  // namespace brcmfmac
}  // namespace wlan

#endif  // SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_BROADCOM_BRCMFMAC_DEBUG_H_
