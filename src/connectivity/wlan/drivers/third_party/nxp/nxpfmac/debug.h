// Copyright (c) 2022 The Fuchsia Authors
//
// Permission to use, copy, modify, and/or distribute this software for any purpose with or without
// fee is hereby granted, provided that the above copyright notice and this permission notice
// appear in all copies.
//
// THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES WITH REGARD TO THIS
// SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE
// AUTHOR BE LIABLE FOR ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
// WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT,
// NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR PERFORMANCE
// OF THIS SOFTWARE.

#ifndef SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_NXP_NXPFMAC_DEBUG_H_
#define SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_NXP_NXPFMAC_DEBUG_H_

#include <wlan/drivers/log.h>

#define NXPF_ERR(fmt, ...) lerror(fmt, ##__VA_ARGS__)
#define NXPF_WARN(fmt, ...) lwarn(fmt, ##__VA_ARGS__)
#define NXPF_INFO(fmt, ...) linfo(fmt, ##__VA_ARGS__)
#define NXPF_DEBUG(filter, tag, fmt, ...) ldebug(filter, tag, fmt, ##__VA_ARGS__)
#define NXPF_TRACE(filter, tag, fmt, ...) ltrace(filter, tag, fmt, ##__VA_ARGS__)

#define NXPF_THROTTLE_ERR(fmt, ...) lthrottle_error(fmt, ##__VA_ARGS__)
#define NXPF_THROTTLE_WARN(fmt, ...) lthrottle_warn(fmt, ##__VA_ARGS__)
#define NXPF_THROTTLE_INFO(fmt, ...) lthrottle_info(fmt, ##__VA_ARGS__)
#define NXPF_THROTTLE_DEBUG(filter, tag, fmt, ...) lthrottle_debug(filter, tag, fmt, ##__VA_ARGS__)
#define NXPF_THROTTLE_TRACE(filter, tag, fmt, ...) lthrottle_trace(filter, tag, fmt, ##__VA_ARGS__)

#define NXPF_THROTTLE_IF(log_per_sec, condition, log) lthrottle_log_if(log_per_sec, condition, log)

#define NXPF_HEXDUMP_ERR(data, length) lhexdump_error(data, length)
#define NXPF_HEXDUMP_WARN(data, length) lhexdump_warn(data, length)
#define NXPF_HEXDUMP_INFO(data, length) lhexdump_info(data, length)
#define NXPF_HEXDUMP_DEBUG(filter, tag, data, length) lhexdump_debug(filter, tag, data, length)
#define NXPF_HEXDUMP_TRACE(filter, tag, data, length) lhexdump_trace(filter, tag, data, length)

#endif  // SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_NXP_NXPFMAC_DEBUG_H_
