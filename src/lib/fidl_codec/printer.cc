// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/lib/fidl_codec/printer.h"

#include <lib/syslog/cpp/macros.h>

#include "src/lib/fidl_codec/display_handle.h"
#include "src/lib/fidl_codec/status.h"

namespace fidl_codec {

constexpr int kCharactersPerByte = 2;

const Colors WithoutColors("", "", "", "", "", "");
const Colors WithColors(/*new_reset=*/"\u001b[0m", /*new_red=*/"\u001b[31m",
                        /*new_green=*/"\u001b[32m", /*new_blue=*/"\u001b[34m",
                        /*new_white_on_magenta=*/"\u001b[45m\u001b[37m",
                        /*new_yellow_background=*/"\u001b[103m");

PrettyPrinter::PrettyPrinter(std::ostream& os, const Colors& colors, bool pretty_print,
                             std::string_view line_header, int max_line_size,
                             bool header_on_every_line, int tabulations)
    : os_(os),
      colors_(colors),
      pretty_print_(pretty_print),
      line_header_(line_header),
      max_line_size_(max_line_size),
      header_on_every_line_(header_on_every_line),
      tabulations_(tabulations),
      remaining_size_(max_line_size - tabulations * kTabSize) {
  // Computes the displayed size of the header. The header can contain escape sequences (to add some
  // colors) which don't count as displayed characters. Here we count the number of characters in
  // the line header skiping everything between escape ('\u001b') and 'm'.
  size_t i = 0;
  while (i < line_header.size()) {
    if (line_header[i] == '\u001b') {
      i = line_header.find_first_of('m', i + 1);
      if (i == std::string_view::npos) {
        break;
      }
      ++i;
    } else {
      ++i;
      ++line_header_size_;
    }
  }
}

void PrettyPrinter::DisplayHandle(const zx_handle_disposition_t& handle) {
  fidl_codec::DisplayHandle(handle, *this);
}

#define BtiPermNameCase(name)      \
  if ((perm & (name)) == (name)) { \
    *this << separator << #name;   \
    separator = " | ";             \
  }

void PrettyPrinter::DisplayBtiPerm(uint32_t perm) {
  if (perm == 0) {
    *this << Red << "0" << ResetColor;
    return;
  }

  *this << Blue;
  const char* separator = "";
  BtiPermNameCase(ZX_BTI_PERM_READ);
  BtiPermNameCase(ZX_BTI_PERM_WRITE);
  BtiPermNameCase(ZX_BTI_PERM_EXECUTE);
  BtiPermNameCase(ZX_BTI_COMPRESS);
  BtiPermNameCase(ZX_BTI_CONTIGUOUS);
  *this << ResetColor;
}

#define CachePolicyNameCase(name)         \
  case name:                              \
    *this << Blue << #name << ResetColor; \
    return

void PrettyPrinter::DisplayCachePolicy(uint32_t cache_policy) {
  switch (cache_policy) {
    CachePolicyNameCase(ZX_CACHE_POLICY_CACHED);
    CachePolicyNameCase(ZX_CACHE_POLICY_UNCACHED);
    CachePolicyNameCase(ZX_CACHE_POLICY_UNCACHED_DEVICE);
    CachePolicyNameCase(ZX_CACHE_POLICY_WRITE_COMBINING);
    default:
      *this << Red << cache_policy << ResetColor;
      return;
  }
}

#define ClockNameCase(name)               \
  case name:                              \
    *this << Blue << #name << ResetColor; \
    return

void PrettyPrinter::DisplayClock(zx_clock_t clock) {
  switch (clock) {
    ClockNameCase(ZX_CLOCK_MONOTONIC);
    ClockNameCase(ZX_CLOCK_UTC);
    ClockNameCase(ZX_CLOCK_THREAD);
    default:
      *this << Red << clock << ResetColor;
      return;
  }
}

void PrettyPrinter::DisplayDuration(zx_duration_t duration_ns) {
  if (duration_ns == ZX_TIME_INFINITE) {
    *this << Blue << "ZX_TIME_INFINITE" << ResetColor;
    return;
  }
  if (duration_ns == ZX_TIME_INFINITE_PAST) {
    *this << Blue << "ZX_TIME_INFINITE_PAST" << ResetColor;
    return;
  }
  *this << Blue;
  if (duration_ns < 0) {
    *this << '-';
    duration_ns = -duration_ns;
  }
  const char* separator = "";
  int64_t nanoseconds = duration_ns % kOneBillion;
  int64_t seconds = duration_ns / kOneBillion;
  if (seconds != 0) {
    int64_t minutes = seconds / kSecondsPerMinute;
    if (minutes != 0) {
      int64_t hours = minutes / kMinutesPerHour;
      if (hours != 0) {
        int64_t days = hours / kHoursPerDay;
        if (days != 0) {
          *this << days << " days";
          separator = ", ";
        }
        *this << separator << (hours % kHoursPerDay) << " hours";
        separator = ", ";
      }
      *this << separator << (minutes % kMinutesPerHour) << " minutes";
      separator = ", ";
    }
    *this << separator << (seconds % kSecondsPerMinute) << " seconds";
    if (nanoseconds != 0) {
      *this << " and " << nanoseconds << " nano seconds";
    }
  } else if (nanoseconds != 0) {
    *this << nanoseconds << " nano seconds";
  } else {
    *this << "0 seconds";
  }
  *this << ResetColor;
}

#define ExceptionStateNameCase(name) \
  case name:                         \
    *this << #name << ResetColor;    \
    return

void PrettyPrinter::DisplayExceptionState(uint32_t state) {
  *this << Blue;
  switch (state) {
    ExceptionStateNameCase(ZX_EXCEPTION_STATE_TRY_NEXT);
    ExceptionStateNameCase(ZX_EXCEPTION_STATE_HANDLED);
    default:
      *this << static_cast<uint32_t>(state) << ResetColor;
      return;
  }
}

void PrettyPrinter::DisplayHexa8(uint8_t value) {
  std::vector<char> buffer(sizeof(uint8_t) * kCharactersPerByte + 1);
  snprintf(buffer.data(), buffer.size(), "%02" PRIx8, value);
  *this << Blue << buffer.data() << ResetColor;
}

void PrettyPrinter::DisplayHexa16(uint16_t value) {
  std::vector<char> buffer(sizeof(uint16_t) * kCharactersPerByte + 1);
  snprintf(buffer.data(), buffer.size(), "%04" PRIx16, value);
  *this << Blue << buffer.data() << ResetColor;
}

void PrettyPrinter::DisplayHexa32(uint32_t value) {
  std::vector<char> buffer(sizeof(uint32_t) * kCharactersPerByte + 1);
  snprintf(buffer.data(), buffer.size(), "%08" PRIx32, value);
  *this << Blue << buffer.data() << ResetColor;
}

void PrettyPrinter::DisplayHexa64(uint64_t value) {
  std::vector<char> buffer(sizeof(uint64_t) * kCharactersPerByte + 1);
  snprintf(buffer.data(), buffer.size(), "%016" PRIx64, value);
  *this << Blue << buffer.data() << ResetColor;
}

#define TopicNameCase(name)       \
  case name:                      \
    *this << #name << ResetColor; \
    return

void PrettyPrinter::DisplayObjectInfoTopic(uint32_t topic) {
  *this << Blue;
  switch (topic) {
    TopicNameCase(ZX_INFO_NONE);
    TopicNameCase(ZX_INFO_HANDLE_VALID);
    TopicNameCase(ZX_INFO_HANDLE_BASIC);
    TopicNameCase(ZX_INFO_PROCESS);
    TopicNameCase(ZX_INFO_PROCESS_THREADS);
    TopicNameCase(ZX_INFO_VMAR);
    TopicNameCase(ZX_INFO_JOB_CHILDREN);
    TopicNameCase(ZX_INFO_JOB_PROCESSES);
    TopicNameCase(ZX_INFO_THREAD);
    TopicNameCase(ZX_INFO_THREAD_EXCEPTION_REPORT);
    TopicNameCase(ZX_INFO_TASK_STATS);
    TopicNameCase(ZX_INFO_PROCESS_MAPS);
    TopicNameCase(ZX_INFO_PROCESS_VMOS);
    TopicNameCase(ZX_INFO_THREAD_STATS);
    TopicNameCase(ZX_INFO_CPU_STATS);
    TopicNameCase(ZX_INFO_KMEM_STATS);
    TopicNameCase(ZX_INFO_RESOURCE);
    TopicNameCase(ZX_INFO_HANDLE_COUNT);
    TopicNameCase(ZX_INFO_BTI);
    TopicNameCase(ZX_INFO_PROCESS_HANDLE_STATS);
    TopicNameCase(ZX_INFO_SOCKET);
    TopicNameCase(ZX_INFO_VMO);
    TopicNameCase(ZX_INFO_JOB);
    default:
      *this << "topic=" << topic << ResetColor;
      return;
  }
}

void PrettyPrinter::DisplayPaddr(zx_paddr_t addr) {
  std::vector<char> buffer(sizeof(uint64_t) * kCharactersPerByte + 1);
#ifdef __MACH__
  snprintf(buffer.data(), buffer.size(), "%016" PRIxPTR, addr);
#else
  snprintf(buffer.data(), buffer.size(), "%016" PRIx64, addr);
#endif
  *this << Blue << buffer.data() << ResetColor;
}

#define PacketGuestVcpuTypeNameCase(name) \
  case name:                              \
    *this << #name << ResetColor;         \
    return

void PrettyPrinter::DisplayPacketGuestVcpuType(uint8_t type) {
  *this << Blue;
  switch (type) {
    PacketGuestVcpuTypeNameCase(ZX_PKT_GUEST_VCPU_INTERRUPT);
    PacketGuestVcpuTypeNameCase(ZX_PKT_GUEST_VCPU_STARTUP);
    default:
      *this << static_cast<uint32_t>(type) << ResetColor;
      return;
  }
}

#define PacketPageRequestCommandNameCase(name) \
  case name:                                   \
    *this << #name << ResetColor;              \
    return

void PrettyPrinter::DisplayPacketPageRequestCommand(uint16_t command) {
  *this << Blue;
  switch (command) {
    PacketPageRequestCommandNameCase(ZX_PAGER_VMO_READ);
    PacketPageRequestCommandNameCase(ZX_PAGER_VMO_COMPLETE);
    default:
      *this << static_cast<uint32_t>(command) << ResetColor;
      return;
  }
}

#define PciBarTypeNameCase(name)  \
  case name:                      \
    *this << #name << ResetColor; \
    return

void PrettyPrinter::DisplayPciBarType(uint32_t type) {
  *this << Blue;
  switch (type) {
    PciBarTypeNameCase(ZX_PCI_BAR_TYPE_UNUSED);
    PciBarTypeNameCase(ZX_PCI_BAR_TYPE_MMIO);
    PciBarTypeNameCase(ZX_PCI_BAR_TYPE_PIO);
    default:
      *this << static_cast<uint32_t>(type) << ResetColor;
      return;
  }
}

#define ProfileInfoFlagsNameCase(name) \
  if ((flags & (name)) == (name)) {    \
    *this << separator << #name;       \
    separator = " | ";                 \
  }

void PrettyPrinter::DisplayProfileInfoFlags(uint32_t flags) {
  *this << Blue;
  if (flags == 0) {
    *this << "0" << ResetColor;
    return;
  }
  const char* separator = "";
  ProfileInfoFlagsNameCase(ZX_PROFILE_INFO_FLAG_PRIORITY);
  ProfileInfoFlagsNameCase(ZX_PROFILE_INFO_FLAG_CPU_MASK);
  *this << ResetColor;
}

#define PortPacketTypeNameCase(name) \
  case name:                         \
    *this << #name << ResetColor;    \
    return

void PrettyPrinter::DisplayPortPacketType(uint32_t type) {
  *this << Blue;
  switch (type) {
    PortPacketTypeNameCase(ZX_PKT_TYPE_USER);
    PortPacketTypeNameCase(ZX_PKT_TYPE_SIGNAL_ONE);
    PortPacketTypeNameCase(ZX_PKT_TYPE_GUEST_BELL);
    PortPacketTypeNameCase(ZX_PKT_TYPE_GUEST_MEM);
    PortPacketTypeNameCase(ZX_PKT_TYPE_GUEST_IO);
    PortPacketTypeNameCase(ZX_PKT_TYPE_GUEST_VCPU);
    PortPacketTypeNameCase(ZX_PKT_TYPE_INTERRUPT);
    PortPacketTypeNameCase(ZX_PKT_TYPE_PAGE_REQUEST);
    default:
      *this << "port_packet_type=" << type << ResetColor;
      return;
  }
}

// ZX_PROP_REGISTER_GS and ZX_PROP_REGISTER_FS are defined in
// <zircon/system/public/zircon/syscalls/object.h>
// but only available for amd64.
// We need these values in all the environments.
#ifndef ZX_PROP_REGISTER_GS
#define ZX_PROP_REGISTER_GS ((uint32_t)2u)
#endif

#ifndef ZX_PROP_REGISTER_FS
#define ZX_PROP_REGISTER_FS ((uint32_t)4u)
#endif

#define PropTypeNameCase(name) \
  case name:                   \
    *this << #name;            \
    *this << ResetColor;       \
    return

void PrettyPrinter::DisplayPropType(uint32_t type) {
  *this << Blue;
  switch (type) {
    PropTypeNameCase(ZX_PROP_NAME);
    PropTypeNameCase(ZX_PROP_REGISTER_FS);
    PropTypeNameCase(ZX_PROP_REGISTER_GS);
    PropTypeNameCase(ZX_PROP_PROCESS_DEBUG_ADDR);
    PropTypeNameCase(ZX_PROP_PROCESS_VDSO_BASE_ADDRESS);
    PropTypeNameCase(ZX_PROP_SOCKET_RX_THRESHOLD);
    PropTypeNameCase(ZX_PROP_SOCKET_TX_THRESHOLD);
    PropTypeNameCase(ZX_PROP_JOB_KILL_ON_OOM);
    PropTypeNameCase(ZX_PROP_EXCEPTION_STATE);
    default:
      *this << type << ResetColor;
      return;
  }
}

#define RightsNameCase(name)     \
  if ((rights & (name)) != 0) {  \
    *this << separator << #name; \
    separator = " | ";           \
  }

void PrettyPrinter::DisplayRights(uint32_t rights) {
  *this << Blue;
  if (rights == 0) {
    *this << "ZX_RIGHT_NONE" << ResetColor;
    return;
  }
  const char* separator = "";
  RightsNameCase(ZX_RIGHT_DUPLICATE);
  RightsNameCase(ZX_RIGHT_TRANSFER);
  RightsNameCase(ZX_RIGHT_READ);
  RightsNameCase(ZX_RIGHT_WRITE);
  RightsNameCase(ZX_RIGHT_EXECUTE);
  RightsNameCase(ZX_RIGHT_MAP);
  RightsNameCase(ZX_RIGHT_GET_PROPERTY);
  RightsNameCase(ZX_RIGHT_SET_PROPERTY);
  RightsNameCase(ZX_RIGHT_ENUMERATE);
  RightsNameCase(ZX_RIGHT_DESTROY);
  RightsNameCase(ZX_RIGHT_SET_POLICY);
  RightsNameCase(ZX_RIGHT_GET_POLICY);
  RightsNameCase(ZX_RIGHT_SIGNAL);
  RightsNameCase(ZX_RIGHT_SIGNAL_PEER);
  RightsNameCase(ZX_RIGHT_WAIT);
  RightsNameCase(ZX_RIGHT_INSPECT);
  RightsNameCase(ZX_RIGHT_MANAGE_JOB);
  RightsNameCase(ZX_RIGHT_MANAGE_PROCESS);
  RightsNameCase(ZX_RIGHT_MANAGE_THREAD);
  RightsNameCase(ZX_RIGHT_APPLY_PROFILE);
  RightsNameCase(ZX_RIGHT_SAME_RIGHTS);
  *this << ResetColor;
}

#define SignalNameCase(name)          \
  if ((signals & (name)) == (name)) { \
    *this << separator << #name;      \
    separator = " | ";                \
  }

void PrettyPrinter::DisplaySignals(zx_signals_t signals) {
  *this << Blue;
  if (signals == 0) {
    *this << "0" << ResetColor;
    return;
  }
  if (signals == __ZX_OBJECT_SIGNAL_ALL) {
    *this << "__ZX_OBJECT_SIGNAL_ALL" << ResetColor;
    return;
  }
  const char* separator = "";
  SignalNameCase(__ZX_OBJECT_READABLE);
  SignalNameCase(__ZX_OBJECT_WRITABLE);
  SignalNameCase(__ZX_OBJECT_PEER_CLOSED);
  SignalNameCase(__ZX_OBJECT_SIGNALED);
  SignalNameCase(__ZX_OBJECT_SIGNAL_4);
  SignalNameCase(__ZX_OBJECT_SIGNAL_5);
  SignalNameCase(__ZX_OBJECT_SIGNAL_6);
  SignalNameCase(__ZX_OBJECT_SIGNAL_7);
  SignalNameCase(__ZX_OBJECT_SIGNAL_8);
  SignalNameCase(__ZX_OBJECT_SIGNAL_9);
  SignalNameCase(__ZX_OBJECT_SIGNAL_10);
  SignalNameCase(__ZX_OBJECT_SIGNAL_11);
  SignalNameCase(__ZX_OBJECT_SIGNAL_12);
  SignalNameCase(__ZX_OBJECT_SIGNAL_13);
  SignalNameCase(__ZX_OBJECT_SIGNAL_14);
  SignalNameCase(__ZX_OBJECT_SIGNAL_15);
  SignalNameCase(__ZX_OBJECT_SIGNAL_16);
  SignalNameCase(__ZX_OBJECT_SIGNAL_17);
  SignalNameCase(__ZX_OBJECT_SIGNAL_18);
  SignalNameCase(__ZX_OBJECT_SIGNAL_19);
  SignalNameCase(__ZX_OBJECT_SIGNAL_20);
  SignalNameCase(__ZX_OBJECT_SIGNAL_21);
  SignalNameCase(__ZX_OBJECT_SIGNAL_22);
  SignalNameCase(__ZX_OBJECT_HANDLE_CLOSED);
  SignalNameCase(ZX_USER_SIGNAL_0);
  SignalNameCase(ZX_USER_SIGNAL_1);
  SignalNameCase(ZX_USER_SIGNAL_2);
  SignalNameCase(ZX_USER_SIGNAL_3);
  SignalNameCase(ZX_USER_SIGNAL_4);
  SignalNameCase(ZX_USER_SIGNAL_5);
  SignalNameCase(ZX_USER_SIGNAL_6);
  SignalNameCase(ZX_USER_SIGNAL_7);
  *this << ResetColor;
}

void PrettyPrinter::DisplayStatus(zx_status_t status) {
  if (status == ZX_OK) {
    *this << Green;
  } else {
    *this << Red;
  }
  *this << StatusName(status) << ResetColor;
}

void PrettyPrinter::DisplayString(std::string_view string) {
  if (string.data() == nullptr) {
    *this << "nullptr\n";
  } else {
    *this << Red << '"';
    for (char value : string) {
      switch (value) {
        case 0:
          break;
        case '\\':
          *this << "\\\\";
          break;
        case '\n':
          *this << "\\n";
          break;
        default:
          *this << value;
          break;
      }
    }
    *this << '"' << ResetColor;
  }
}

void PrettyPrinter::DisplayTime(zx_time_t time_ns) {
  if (time_ns == ZX_TIME_INFINITE) {
    (*this) << Blue << "ZX_TIME_INFINITE" << ResetColor;
  } else if (time_ns == ZX_TIME_INFINITE_PAST) {
    (*this) << Blue << "ZX_TIME_INFINITE_PAST" << ResetColor;
  } else {
    // Gets the time in seconds.
    time_t value = time_ns / kOneBillion;
    struct tm tm;
    if (localtime_r(&value, &tm) == &tm) {
      char buffer[100];
      strftime(buffer, sizeof(buffer), "%c", &tm);
      // And now, displays the nano seconds.
      (*this) << Blue << buffer << " and ";
      snprintf(buffer, sizeof(buffer), "%09" PRId64, time_ns % kOneBillion);
      (*this) << buffer << " ns" << ResetColor;
    } else {
      (*this) << Red << "unknown time" << ResetColor;
    }
  }
}

#ifdef __MACH__
void PrettyPrinter::DisplayUintptr(uintptr_t ptr) {
  std::vector<char> buffer(sizeof(uint64_t) * kCharactersPerByte + 1);
  snprintf(buffer.data(), buffer.size(), "%016" PRIxPTR, ptr);
  *this << Blue << buffer.data() << ResetColor;
}
#else
void PrettyPrinter::DisplayUintptr(uint64_t ptr) {
  std::vector<char> buffer(sizeof(uint64_t) * kCharactersPerByte + 1);
  snprintf(buffer.data(), buffer.size(), "%016" PRIx64, ptr);
  *this << Blue << buffer.data() << ResetColor;
}
#endif

void PrettyPrinter::DisplayVaddr(zx_vaddr_t addr) {
  std::vector<char> buffer(sizeof(uint64_t) * kCharactersPerByte + 1);
#ifdef __MACH__
  snprintf(buffer.data(), buffer.size(), "%016" PRIxPTR, addr);
#else
  snprintf(buffer.data(), buffer.size(), "%016" PRIx64, addr);
#endif
  *this << Blue << buffer.data() << ResetColor;
}

void PrettyPrinter::DisplayGpAddr(zx_gpaddr_t addr) {
#ifdef __MACH__
  std::vector<char> buffer(sizeof(uintptr_t) * kCharactersPerByte + 1);
  snprintf(buffer.data(), buffer.size(), "%016" PRIxPTR, addr);
#else
  std::vector<char> buffer(sizeof(uint64_t) * kCharactersPerByte + 1);
  snprintf(buffer.data(), buffer.size(), "%016" PRIx64, addr);
#endif
  *this << Blue << buffer.data() << ResetColor;
}

void PrettyPrinter::IncrementTabulations() {
  ++tabulations_;
  if (need_to_print_header_) {
    remaining_size_ -= kTabSize;
  }
}

void PrettyPrinter::DecrementTabulations() {
  --tabulations_;
  if (need_to_print_header_) {
    remaining_size_ += kTabSize;
  }
}

void PrettyPrinter::NeedHeader() {
  remaining_size_ = max_line_size_ - line_header_size_ - tabulations_ * kTabSize;
  need_to_print_header_ = true;
}

void PrettyPrinter::PrintHeader(char first_character) {
  FX_DCHECK(need_to_print_header_);
  need_to_print_header_ = false;
  if (line_header_size_ > 0) {
    os_ << line_header_;
    if (!header_on_every_line_) {
      line_header_size_ = 0;
    }
  }
  if (first_character != '\n') {
    for (int tab = tabulations_ * kTabSize; tab > 0; --tab) {
      os_ << ' ';
    }
  }
}

PrettyPrinter& PrettyPrinter::operator<<(std::string_view data) {
  if (data.empty()) {
    return *this;
  }
  if (need_to_print_header_) {
    PrintHeader(data[0]);
  }
  size_t end_of_line = data.find('\n', 0);
  if (end_of_line == std::string_view::npos) {
    os_ << data;
    remaining_size_ -= data.size();
    return *this;
  }
  size_t current = 0;
  for (;;) {
    std::string_view tmp = data.substr(current, end_of_line - current + 1);
    os_ << tmp;
    NeedHeader();
    current = end_of_line + 1;
    if (current >= data.size()) {
      return *this;
    }
    end_of_line = data.find('\n', current);
    if (end_of_line == std::string_view::npos) {
      os_ << data;
      remaining_size_ -= data.size();
      return *this;
    }
    PrintHeader(data[current]);
  }
}

}  // namespace fidl_codec
