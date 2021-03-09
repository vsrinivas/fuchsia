/*
 * Copyright (c) 2021 The Fuchsia Authors
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY
 * SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION
 * OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN
 * CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */
#ifndef SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_BROADCOM_BRCMFMAC_ARP_LOGGER_H_
#define SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_BROADCOM_BRCMFMAC_ARP_LOGGER_H_

#include <zircon/status.h>

#include <mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>

namespace wlan::brcmfmac {

class ArpLogger {
 public:
  // The operations in ArpLogger when an ARP frame is sent out by brcmfmac driver.
  zx_status_t ArpRequestOut(const uint32_t& dest_ip_addr);

  // The operations in ArpLogger when an ARP frame is received by brcmfmac driver.
  zx_status_t ArpReplyIn(const uint32_t& src_ip_addr);

  // Adding an ARP request frame to the set, return true if the frame doesn't exist in the set.
  bool AddArpRequestFrame(const std::string& frame_in);

  /* The following functions are used for testing purpose.*/
  // Get the value of a table entry from the key.
  zx_status_t GetArpCount(const uint32_t& addr, uint16_t* count_out);

  // Get the size of unique ARP Request frame set.
  size_t GetArpReqFrameSize();

  // When the unreplied arp frame number exceed this value, a log line will be emitted.
  constexpr static uint16_t kArpLogThreshold = 10;

 private:
  // Decode the uint32_t IP address to byte array.
  std::string IpToStr(const uint32_t& ip_addr);
  // This table records the number of consecutive unreplied ARP requests for all target IPv4
  // addresses. The key is target IP address and the value is the count.
  std::unordered_map<uint32_t, uint16_t> unreplied_arp_requests_;

  // The set storing each unique ARP request frame.
  std::unordered_set<std::string> unique_arp_req_frames_;

  // The mutex protects unreplied_arp_requests_ table modifications.
  std::mutex table_lock_;
};

}  // namespace wlan::brcmfmac

#endif  // SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_BROADCOM_BRCMFMAC_ARP_LOGGER_H_
