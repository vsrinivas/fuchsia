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

#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/arp_logger.h"

#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/debug.h"

namespace wlan::brcmfmac {

zx_status_t ArpLogger::ArpRequestOut(const uint32_t& dest_ip_addr) {
  std::lock_guard<std::mutex> lock(table_lock_);

  // std::unordered_map will create a new entry if the key doesn't exist.
  uint16_t& count = unreplied_arp_requests_[dest_ip_addr];
  ++count;

  BRCMF_DBG(INFO,
            "Increased the count for <%s> to %u, The there are %lu different addresses "
            "recorded in the "
            "table in total.",
            IpToStr(dest_ip_addr).c_str(), count, unreplied_arp_requests_.size());

  // Clear the count when it reaches the threshold and emit a log line.
  if (count >= kArpLogThreshold) {
    BRCMF_ERR(
        "Too many consecutive ARP requests sent with no ARP response. Target IP address: "
        "<%s>",
        IpToStr(dest_ip_addr).c_str());
    count = 0;
  }

  return ZX_OK;
}

zx_status_t ArpLogger::ArpReplyIn(const uint32_t& src_ip_addr) {
  std::lock_guard<std::mutex> lock(table_lock_);

  BRCMF_DBG(INFO, "Receiving an ARP response from <%s>", IpToStr(src_ip_addr).c_str());
  auto value = unreplied_arp_requests_.find(src_ip_addr);
  if (value == unreplied_arp_requests_.end()) {
    BRCMF_INFO("Received ARP Reply frame from <%s> with no request seen.",
               IpToStr(src_ip_addr).c_str());
    return ZX_ERR_NOT_FOUND;
  }

  // Clean the count value in corresponding table entry.
  value->second = 0;

  return ZX_OK;
}

zx_status_t ArpLogger::GetArpCount(const uint32_t& ip_addr, uint16_t* count_out) {
  std::lock_guard<std::mutex> lock(table_lock_);

  if (count_out == nullptr) {
    BRCMF_ERR("The out pointer is empty.");
    return ZX_ERR_INVALID_ARGS;
  }

  auto value = unreplied_arp_requests_.find(ip_addr);
  if (value == unreplied_arp_requests_.end()) {
    BRCMF_ERR("Table entry for this Ip address is not found.");
    return ZX_ERR_NOT_FOUND;
  }

  *count_out = value->second;
  return ZX_OK;
}

std::string ArpLogger::IpToStr(const uint32_t& ip_addr) {
  uint32_t temp_ip = ip_addr;
  uint8_t ip_addr_bytes[4];
  ip_addr_bytes[0] = temp_ip >> 24;
  ip_addr_bytes[1] = temp_ip >> 16;
  ip_addr_bytes[2] = temp_ip >> 8;
  ip_addr_bytes[3] = temp_ip;

  return std::to_string(ip_addr_bytes[0]) + '.' + std::to_string(ip_addr_bytes[1]) + '.' +
         std::to_string(ip_addr_bytes[2]) + '.' + std::to_string(ip_addr_bytes[3]);
}

}  // namespace wlan::brcmfmac
