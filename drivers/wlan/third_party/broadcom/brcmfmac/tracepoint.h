/*
 * Copyright (c) 2013 Broadcom Corporation
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
#if !defined(GARNET_DRIVERS_WLAN_THIRD_PARTY_BROADCOM_BRCMFMAC_TRACEPOINT_H_) || \
    defined(TRACE_HEADER_MULTI_READ)
#define GARNET_DRIVERS_WLAN_THIRD_PARTY_BROADCOM_BRCMFMAC_TRACEPOINT_H_

#include "linuxisms.h"

#if 0
#ifndef CONFIG_BRCM_TRACING

#undef TRACE_EVENT
#define TRACE_EVENT(name, proto, ...) \
    static inline void trace_##name(proto) {}

#undef DECLARE_EVENT_CLASS
#define DECLARE_EVENT_CLASS(...)

#undef DEFINE_EVENT
#define DEFINE_EVENT(evt_class, name, proto, ...) \
    static inline void trace_##name(proto) {}

#endif /* CONFIG_BRCM_TRACING */

#undef TRACE_SYSTEM
#define TRACE_SYSTEM brcmfmac

#define MAX_MSG_LEN 100

TRACE_EVENT(brcmf_err, TP_PROTO(const char* func, struct va_format* vaf), TP_ARGS(func, vaf),
            TP_STRUCT__entry(__string(func, func) __dynamic_array(char, msg, MAX_MSG_LEN)),
            TP_fast_assign(__assign_str(func, func);
                           WARN_ON_ONCE(vsnprintf(__get_dynamic_array(msg), MAX_MSG_LEN, vaf->fmt,
                                                  *vaf->va) >= MAX_MSG_LEN);),
            TP_printk("%s: %s", __get_str(func), __get_str(msg)));

TRACE_EVENT(brcmf_dbg, TP_PROTO(uint32_t level, const char* func, struct va_format* vaf),
            TP_ARGS(level, func, vaf),
            TP_STRUCT__entry(__field(uint32_t, level) __string(func, func)
                                 __dynamic_array(char, msg, MAX_MSG_LEN)),
            TP_fast_assign(__entry->level = level; __assign_str(func, func);
                           WARN_ON_ONCE(vsnprintf(__get_dynamic_array(msg), MAX_MSG_LEN, vaf->fmt,
                                                  *vaf->va) >= MAX_MSG_LEN);),
            TP_printk("%s: %s", __get_str(func), __get_str(msg)));

TRACE_EVENT(brcmf_hexdump, TP_PROTO(void* data, size_t len), TP_ARGS(data, len),
            TP_STRUCT__entry(__field(unsigned long, len) __field(unsigned long, addr)
                                 __dynamic_array(uint8_t, hdata, len)),
            TP_fast_assign(__entry->len = len; __entry->addr = (unsigned long)data;
                           memcpy(__get_dynamic_array(hdata), data, len);),
            TP_printk("hexdump [addr=%lx, length=%lu]", __entry->addr, __entry->len));

TRACE_EVENT(brcmf_bcdchdr, TP_PROTO(void* data), TP_ARGS(data),
            TP_STRUCT__entry(__field(uint8_t, flags) __field(uint8_t, prio) __field(uint8_t, flags2)
                                 __field(uint32_t, siglen)
                                     __dynamic_array(uint8_t, signal, *((uint8_t*)data + 3) * 4)),
            TP_fast_assign(__entry->flags = *(uint8_t*)data; __entry->prio = *((uint8_t*)data + 1);
                           __entry->flags2 = *((uint8_t*)data + 2);
                           __entry->siglen = *((uint8_t*)data + 3) * 4;
                           memcpy(__get_dynamic_array(signal), (uint8_t*)data + 4,
                                  __entry->siglen);),
            TP_printk("bcdc: prio=%d siglen=%d", __entry->prio, __entry->siglen));
#endif  // LINUX cphoenix

#ifndef SDPCM_RX
#define SDPCM_RX 0
#endif
#ifndef SDPCM_TX
#define SDPCM_TX 1
#endif
#ifndef SDPCM_GLOM
#define SDPCM_GLOM 2
#endif

#if 0
TRACE_EVENT(brcmf_sdpcm_hdr, TP_PROTO(uint8_t dir, void* data), TP_ARGS(dir, data),
            TP_STRUCT__entry(__field(uint8_t, dir) __field(uint16_t, len)
                                 __dynamic_array(uint8_t, hdr, dir == SDPCM_GLOM ? 20 : 12)),
            TP_fast_assign(memcpy(__get_dynamic_array(hdr), data, dir == SDPCM_GLOM ? 20 : 12);
                           __entry->len = *(uint8_t*)data | (*((uint8_t*)data + 1) << 8);
                           __entry->dir = dir;),
            TP_printk("sdpcm: %s len %u, seq %d", __entry->dir == SDPCM_RX ? "RX" : "TX",
                      __entry->len, ((uint8_t*)__get_dynamic_array(hdr))[4]));

#ifdef CONFIG_BRCM_TRACING

#undef TRACE_INCLUDE_PATH
#define TRACE_INCLUDE_PATH .
#undef TRACE_INCLUDE_FILE
#define TRACE_INCLUDE_FILE tracepoint

#endif  // LINUX cphoenix

#endif /* CONFIG_BRCM_TRACING */

#endif /* GARNET_DRIVERS_WLAN_THIRD_PARTY_BROADCOM_BRCMFMAC_TRACEPOINT_H_ */
