// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/lib/fidl_codec/printer.h"

#include <lib/syslog/cpp/macros.h>
#include <sys/stat.h>
#include <zircon/syscalls/exception.h>
#include <zircon/syscalls/iommu.h>
#include <zircon/syscalls/object.h>
#include <zircon/syscalls/pci.h>
#include <zircon/syscalls/policy.h>
#include <zircon/syscalls/port.h>
#include <zircon/syscalls/profile.h>
#include <zircon/syscalls/system.h>

#include "src/lib/fidl_codec/display_handle.h"
#include "src/lib/fidl_codec/status.h"

namespace fidl_codec {

// Imported from sdk/fidl/fuchsia.io/io.fidl.
constexpr uint32_t OPEN_RIGHT_READABLE = 0x1;
constexpr uint32_t OPEN_RIGHT_WRITEABLE = 0x2;
constexpr uint32_t OPEN_RIGHT_ADMIN = 0x4;
constexpr uint32_t OPEN_RIGHT_EXECUTABLE = 0x8;
constexpr uint32_t OPEN_FLAG_CREATE = 0x10000;
constexpr uint32_t OPEN_FLAG_CREATE_IF_ABSENT = 0x20000;
constexpr uint32_t OPEN_FLAG_TRUNCATE = 0x40000;
constexpr uint32_t OPEN_FLAG_DIRECTORY = 0x80000;
constexpr uint32_t OPEN_FLAG_APPEND = 0x100000;
constexpr uint32_t OPEN_FLAG_NO_REMOTE = 0x200000;
constexpr uint32_t OPEN_FLAG_NODE_REFERENCE = 0x400000;
constexpr uint32_t OPEN_FLAG_DESCRIBE = 0x800000;
constexpr uint32_t OPEN_FLAG_POSIX = 0x1000000;
constexpr uint32_t OPEN_FLAG_POSIX_WRITABLE = 0x8000000;
constexpr uint32_t OPEN_FLAG_POSIX_EXECUTABLE = 0x10000000;
constexpr uint32_t OPEN_FLAG_NOT_DIRECTORY = 0x20000000;
constexpr uint32_t CLONE_FLAGS_SAME_RIGHTS = 0x4000000;

constexpr uint32_t MODE_TYPE_DIRECTORY = 0x4000;
constexpr uint32_t MODE_TYPE_BLOCK_DEVICE = 0x6000;
constexpr uint32_t MODE_TYPE_FILE = 0x8000;
constexpr uint32_t MODE_TYPE_SOCKET = 0xC000;
constexpr uint32_t MODE_TYPE_SERVICE = 0x10000;

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

#define BtiPermCase(name)          \
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
  BtiPermCase(ZX_BTI_PERM_READ);
  BtiPermCase(ZX_BTI_PERM_WRITE);
  BtiPermCase(ZX_BTI_PERM_EXECUTE);
  BtiPermCase(ZX_BTI_COMPRESS);
  BtiPermCase(ZX_BTI_CONTIGUOUS);
  *this << ResetColor;
}

#define CachePolicyCase(name)             \
  case name:                              \
    *this << Blue << #name << ResetColor; \
    return

void PrettyPrinter::DisplayCachePolicy(uint32_t cache_policy) {
  switch (cache_policy) {
    CachePolicyCase(ZX_CACHE_POLICY_CACHED);
    CachePolicyCase(ZX_CACHE_POLICY_UNCACHED);
    CachePolicyCase(ZX_CACHE_POLICY_UNCACHED_DEVICE);
    CachePolicyCase(ZX_CACHE_POLICY_WRITE_COMBINING);
    default:
      *this << Red << cache_policy << ResetColor;
      return;
  }
}

#define ChannelOptionCase(name)       \
  if ((options & (name)) == (name)) { \
    *this << separator << #name;      \
    separator = " | ";                \
  }

void PrettyPrinter::DisplayChannelOption(uint32_t options) {
  if (options == 0) {
    *this << Blue << "0" << ResetColor;
    return;
  }

  *this << Blue;
  const char* separator = "";
  ChannelOptionCase(ZX_CHANNEL_READ_MAY_DISCARD);
  ChannelOptionCase(ZX_CHANNEL_WRITE_USE_IOVEC);
  *this << ResetColor;
}

#define ClockCase(name)                   \
  case name:                              \
    *this << Blue << #name << ResetColor; \
    return

void PrettyPrinter::DisplayClock(zx_clock_t clock) {
  switch (clock) {
    ClockCase(ZX_CLOCK_MONOTONIC);
    ClockCase(ZX_CLOCK_UTC);
    ClockCase(ZX_CLOCK_THREAD);
    default:
      *this << Red << clock << ResetColor;
      return;
  }
}

#define DirectoryOpenCase(name)     \
  if ((value & (name)) == (name)) { \
    value &= ~(name);               \
    *this << separator << #name;    \
    separator = " | ";              \
  }

void PrettyPrinter::DisplayDirectoryOpenFlags(uint32_t value) {
  if (value == 0) {
    *this << Blue << "0" << ResetColor;
    return;
  }

  *this << Blue;
  const char* separator = "";
  DirectoryOpenCase(OPEN_RIGHT_READABLE);
  DirectoryOpenCase(OPEN_RIGHT_WRITEABLE);
  DirectoryOpenCase(OPEN_RIGHT_ADMIN);
  DirectoryOpenCase(OPEN_RIGHT_EXECUTABLE);
  DirectoryOpenCase(OPEN_FLAG_CREATE);
  DirectoryOpenCase(OPEN_FLAG_CREATE_IF_ABSENT);
  DirectoryOpenCase(OPEN_FLAG_TRUNCATE);
  DirectoryOpenCase(OPEN_FLAG_DIRECTORY);
  DirectoryOpenCase(OPEN_FLAG_APPEND);
  DirectoryOpenCase(OPEN_FLAG_NO_REMOTE);
  DirectoryOpenCase(OPEN_FLAG_NODE_REFERENCE);
  DirectoryOpenCase(OPEN_FLAG_DESCRIBE);
  DirectoryOpenCase(OPEN_FLAG_POSIX);
  DirectoryOpenCase(OPEN_FLAG_POSIX_WRITABLE);
  DirectoryOpenCase(OPEN_FLAG_POSIX_EXECUTABLE);
  DirectoryOpenCase(OPEN_FLAG_NOT_DIRECTORY);
  DirectoryOpenCase(CLONE_FLAGS_SAME_RIGHTS);
  if (value != 0) {
    *this << separator << "0x" << std::hex << value << std::dec;
  }
  *this << ResetColor;
}

#define OpenModeCase(name) \
  case name:               \
    *this << #name;        \
    separator = " | ";     \
    break

void PrettyPrinter::DisplayDirectoryOpenMode(uint32_t value) {
  if (value == 0) {
    *this << Blue << "0" << ResetColor;
    return;
  }

  *this << Blue;
  const char* separator = "";

  // Type.
  switch (value & 0xff000) {
    OpenModeCase(MODE_TYPE_SERVICE);
    OpenModeCase(MODE_TYPE_SOCKET);
    OpenModeCase(MODE_TYPE_FILE);
    OpenModeCase(MODE_TYPE_BLOCK_DEVICE);
    OpenModeCase(MODE_TYPE_DIRECTORY);
  }

  // Remaining flags.
  value &= 0xfff;
  DirectoryOpenCase(S_ISUID);
  DirectoryOpenCase(S_ISGID);
  DirectoryOpenCase(S_IRWXU);
  DirectoryOpenCase(S_IRUSR);
  DirectoryOpenCase(S_IWUSR);
  DirectoryOpenCase(S_IXUSR);
  DirectoryOpenCase(S_IRWXG);
  DirectoryOpenCase(S_IRGRP);
  DirectoryOpenCase(S_IWGRP);
  DirectoryOpenCase(S_IXGRP);
  DirectoryOpenCase(S_IRWXO);
  DirectoryOpenCase(S_IROTH);
  DirectoryOpenCase(S_IWOTH);
  DirectoryOpenCase(S_IXOTH);
  if (value != 0) {
    *this << separator << "0x" << std::hex << value << std::dec;
  }
  *this << ResetColor;
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

#define ExceptionChannelTypeCase(name) \
  case name:                           \
    *this << #name;                    \
    break

void PrettyPrinter::DisplayExceptionChannelType(uint32_t type) {
  *this << Blue;
  switch (type) {
    ExceptionChannelTypeCase(ZX_EXCEPTION_CHANNEL_TYPE_NONE);
    ExceptionChannelTypeCase(ZX_EXCEPTION_CHANNEL_TYPE_DEBUGGER);
    ExceptionChannelTypeCase(ZX_EXCEPTION_CHANNEL_TYPE_THREAD);
    ExceptionChannelTypeCase(ZX_EXCEPTION_CHANNEL_TYPE_PROCESS);
    ExceptionChannelTypeCase(ZX_EXCEPTION_CHANNEL_TYPE_JOB);
    ExceptionChannelTypeCase(ZX_EXCEPTION_CHANNEL_TYPE_JOB_DEBUGGER);
    default:
      *this << static_cast<uint32_t>(type);
      break;
  }
  *this << ResetColor;
}

#define ExceptionStateCase(name) \
  case name:                     \
    *this << #name;              \
    break

void PrettyPrinter::DisplayExceptionState(uint32_t state) {
  *this << Blue;
  switch (state) {
    ExceptionStateCase(ZX_EXCEPTION_STATE_TRY_NEXT);
    ExceptionStateCase(ZX_EXCEPTION_STATE_HANDLED);
    default:
      *this << static_cast<uint32_t>(state);
      break;
  }
  *this << ResetColor;
}

#define FeatureKindCase(name) \
  case name:                  \
    *this << #name;           \
    break

void PrettyPrinter::DisplayFeatureKind(uint32_t kind) {
  *this << Red;
  switch (kind) {
    FeatureKindCase(ZX_FEATURE_KIND_CPU);
    FeatureKindCase(ZX_FEATURE_KIND_HW_BREAKPOINT_COUNT);
    FeatureKindCase(ZX_FEATURE_KIND_HW_WATCHPOINT_COUNT);
    default:
      *this << static_cast<uint32_t>(kind);
      break;
  }
  *this << ResetColor;
}

#define GuestTrapCase(name) \
  case name:                \
    *this << #name;         \
    break

void PrettyPrinter::DisplayGuestTrap(uint32_t trap_id) {
  *this << Red;
  switch (trap_id) {
    GuestTrapCase(ZX_GUEST_TRAP_BELL);
    GuestTrapCase(ZX_GUEST_TRAP_IO);
    GuestTrapCase(ZX_GUEST_TRAP_MEM);
    default:
      *this << static_cast<uint32_t>(trap_id);
      break;
  }
  *this << ResetColor;
}

#define KoidCase(name)                                             \
  case name:                                                       \
    *this << #name << " (" << static_cast<uint64_t>(state) << ")"; \
    break

void PrettyPrinter::DisplayKoid(uint64_t state) {
  *this << Red;
  switch (state) {
    KoidCase(ZX_KOID_INVALID);
    KoidCase(ZX_KOID_KERNEL);
    default:
      *this << static_cast<uint64_t>(state);
      break;
  }
  *this << ResetColor;
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

#define InfoMapsTypeCase(name)    \
  case name:                      \
    *this << #name << ResetColor; \
    return

void PrettyPrinter::DisplayInfoMapsType(zx_info_maps_type_t type) {
  *this << Red;
  switch (type) {
    InfoMapsTypeCase(ZX_INFO_MAPS_TYPE_NONE);
    InfoMapsTypeCase(ZX_INFO_MAPS_TYPE_ASPACE);
    InfoMapsTypeCase(ZX_INFO_MAPS_TYPE_VMAR);
    InfoMapsTypeCase(ZX_INFO_MAPS_TYPE_MAPPING);
    default:
      *this << type << ResetColor;
      return;
  }
}

#define InterruptFlagsCase(name) \
  case name:                     \
    *this << #name;              \
    break

#define InterruptFlagsFlag(name)    \
  if ((flags & (name)) == (name)) { \
    *this << " | " << #name;        \
  }

void PrettyPrinter::DisplayInterruptFlags(uint32_t flags) {
  *this << Red;
  switch (flags & ZX_INTERRUPT_MODE_MASK) {
    InterruptFlagsCase(ZX_INTERRUPT_MODE_DEFAULT);
    InterruptFlagsCase(ZX_INTERRUPT_MODE_EDGE_LOW);
    InterruptFlagsCase(ZX_INTERRUPT_MODE_EDGE_HIGH);
    InterruptFlagsCase(ZX_INTERRUPT_MODE_LEVEL_LOW);
    InterruptFlagsCase(ZX_INTERRUPT_MODE_LEVEL_HIGH);
    InterruptFlagsCase(ZX_INTERRUPT_MODE_EDGE_BOTH);
    default:
      *this << (flags & ZX_INTERRUPT_MODE_MASK);
      break;
  }
  InterruptFlagsFlag(ZX_INTERRUPT_REMAP_IRQ);
  InterruptFlagsFlag(ZX_INTERRUPT_VIRTUAL);
  *this << ResetColor;
}

#define IommuTypeCase(name)       \
  case name:                      \
    *this << #name << ResetColor; \
    return

void PrettyPrinter::DisplayIommuType(uint32_t type) {
  *this << Red;
  switch (type) {
    IommuTypeCase(ZX_IOMMU_TYPE_DUMMY);
    IommuTypeCase(ZX_IOMMU_TYPE_INTEL);
    default:
      *this << type << ResetColor;
      return;
  }
}

#define KtraceControlActionCase(name) \
  case name:                          \
    *this << #name << ResetColor;     \
    return

void PrettyPrinter::DisplayKtraceControlAction(uint32_t action) {
  constexpr uint32_t KTRACE_ACTION_START = 1;
  constexpr uint32_t KTRACE_ACTION_STOP = 2;
  constexpr uint32_t KTRACE_ACTION_REWIND = 3;
  constexpr uint32_t KTRACE_ACTION_NEW_PROBE = 4;
  *this << Blue;
  switch (action) {
    KtraceControlActionCase(KTRACE_ACTION_START);
    KtraceControlActionCase(KTRACE_ACTION_STOP);
    KtraceControlActionCase(KTRACE_ACTION_REWIND);
    KtraceControlActionCase(KTRACE_ACTION_NEW_PROBE);
    default:
      *this << action << ResetColor;
      return;
  }
}

#define TopicCase(name)           \
  case name:                      \
    *this << #name << ResetColor; \
    return

void PrettyPrinter::DisplayObjectInfoTopic(uint32_t topic) {
  *this << Blue;
  switch (topic) {
    TopicCase(ZX_INFO_NONE);
    TopicCase(ZX_INFO_HANDLE_VALID);
    TopicCase(ZX_INFO_HANDLE_BASIC);
    TopicCase(ZX_INFO_PROCESS_V1);
    TopicCase(ZX_INFO_PROCESS_V2);
    TopicCase(ZX_INFO_PROCESS_THREADS);
    TopicCase(ZX_INFO_VMAR);
    TopicCase(ZX_INFO_JOB_CHILDREN);
    TopicCase(ZX_INFO_JOB_PROCESSES);
    TopicCase(ZX_INFO_THREAD);
    TopicCase(ZX_INFO_THREAD_EXCEPTION_REPORT);
    TopicCase(ZX_INFO_TASK_STATS);
    TopicCase(ZX_INFO_PROCESS_MAPS);
    TopicCase(ZX_INFO_PROCESS_VMOS);
    TopicCase(ZX_INFO_THREAD_STATS);
    TopicCase(ZX_INFO_CPU_STATS);
    TopicCase(ZX_INFO_KMEM_STATS);
    TopicCase(ZX_INFO_RESOURCE);
    TopicCase(ZX_INFO_HANDLE_COUNT);
    TopicCase(ZX_INFO_BTI);
    TopicCase(ZX_INFO_PROCESS_HANDLE_STATS);
    TopicCase(ZX_INFO_SOCKET);
    TopicCase(ZX_INFO_VMO);
    TopicCase(ZX_INFO_JOB);
    default:
      *this << "topic=" << topic << ResetColor;
      return;
  }
}

#define ObjTypeCase(name)         \
  case name:                      \
    *this << #name << ResetColor; \
    return

void PrettyPrinter::DisplayObjType(zx_obj_type_t obj_type) {
  *this << Blue;
  switch (obj_type) {
    ObjTypeCase(ZX_OBJ_TYPE_NONE);
    ObjTypeCase(ZX_OBJ_TYPE_PROCESS);
    ObjTypeCase(ZX_OBJ_TYPE_THREAD);
    ObjTypeCase(ZX_OBJ_TYPE_VMO);
    ObjTypeCase(ZX_OBJ_TYPE_CHANNEL);
    ObjTypeCase(ZX_OBJ_TYPE_EVENT);
    ObjTypeCase(ZX_OBJ_TYPE_PORT);
    ObjTypeCase(ZX_OBJ_TYPE_INTERRUPT);
    ObjTypeCase(ZX_OBJ_TYPE_PCI_DEVICE);
    ObjTypeCase(ZX_OBJ_TYPE_LOG);
    ObjTypeCase(ZX_OBJ_TYPE_SOCKET);
    ObjTypeCase(ZX_OBJ_TYPE_RESOURCE);
    ObjTypeCase(ZX_OBJ_TYPE_EVENTPAIR);
    ObjTypeCase(ZX_OBJ_TYPE_JOB);
    ObjTypeCase(ZX_OBJ_TYPE_VMAR);
    ObjTypeCase(ZX_OBJ_TYPE_FIFO);
    ObjTypeCase(ZX_OBJ_TYPE_GUEST);
    ObjTypeCase(ZX_OBJ_TYPE_VCPU);
    ObjTypeCase(ZX_OBJ_TYPE_TIMER);
    ObjTypeCase(ZX_OBJ_TYPE_IOMMU);
    ObjTypeCase(ZX_OBJ_TYPE_BTI);
    ObjTypeCase(ZX_OBJ_TYPE_PROFILE);
    ObjTypeCase(ZX_OBJ_TYPE_PMT);
    ObjTypeCase(ZX_OBJ_TYPE_SUSPEND_TOKEN);
    ObjTypeCase(ZX_OBJ_TYPE_PAGER);
    ObjTypeCase(ZX_OBJ_TYPE_EXCEPTION);
    ObjTypeCase(ZX_OBJ_TYPE_CLOCK);
    ObjTypeCase(ZX_OBJ_TYPE_STREAM);
    ObjTypeCase(ZX_OBJ_TYPE_MSI_ALLOCATION);
    ObjTypeCase(ZX_OBJ_TYPE_MSI_INTERRUPT);
    default:
      *this << obj_type << ResetColor;
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

#define PacketGuestVcpuTypeCase(name) \
  case name:                          \
    *this << #name << ResetColor;     \
    return

void PrettyPrinter::DisplayPacketGuestVcpuType(uint8_t type) {
  *this << Blue;
  switch (type) {
    PacketGuestVcpuTypeCase(ZX_PKT_GUEST_VCPU_INTERRUPT);
    PacketGuestVcpuTypeCase(ZX_PKT_GUEST_VCPU_STARTUP);
    default:
      *this << static_cast<uint32_t>(type) << ResetColor;
      return;
  }
}

#define PacketPageRequestCommandCase(name) \
  case name:                               \
    *this << #name << ResetColor;          \
    return

void PrettyPrinter::DisplayPacketPageRequestCommand(uint16_t command) {
  *this << Blue;
  switch (command) {
    PacketPageRequestCommandCase(ZX_PAGER_VMO_READ);
    PacketPageRequestCommandCase(ZX_PAGER_VMO_COMPLETE);
    default:
      *this << static_cast<uint32_t>(command) << ResetColor;
      return;
  }
}

#define PciBarTypeCase(name)      \
  case name:                      \
    *this << #name << ResetColor; \
    return

void PrettyPrinter::DisplayPciBarType(uint32_t type) {
  *this << Blue;
  switch (type) {
    PciBarTypeCase(ZX_PCI_BAR_TYPE_UNUSED);
    PciBarTypeCase(ZX_PCI_BAR_TYPE_MMIO);
    PciBarTypeCase(ZX_PCI_BAR_TYPE_PIO);
    default:
      *this << static_cast<uint32_t>(type) << ResetColor;
      return;
  }
}

#define PolicyCase(name)          \
  case name:                      \
    *this << #name << ResetColor; \
    return

void PrettyPrinter::DisplayPolicyAction(uint32_t action) {
  *this << Blue;
  switch (action) {
    PolicyCase(ZX_POL_ACTION_ALLOW);
    PolicyCase(ZX_POL_ACTION_DENY);
    PolicyCase(ZX_POL_ACTION_ALLOW_EXCEPTION);
    PolicyCase(ZX_POL_ACTION_DENY_EXCEPTION);
    PolicyCase(ZX_POL_ACTION_KILL);
    default:
      *this << action << ResetColor;
      return;
  }
}

void PrettyPrinter::DisplayPolicyCondition(uint32_t condition) {
  *this << Blue;
  switch (condition) {
    PolicyCase(ZX_POL_BAD_HANDLE);
    PolicyCase(ZX_POL_WRONG_OBJECT);
    PolicyCase(ZX_POL_VMAR_WX);
    PolicyCase(ZX_POL_NEW_ANY);
    PolicyCase(ZX_POL_NEW_VMO);
    PolicyCase(ZX_POL_NEW_CHANNEL);
    PolicyCase(ZX_POL_NEW_EVENT);
    PolicyCase(ZX_POL_NEW_EVENTPAIR);
    PolicyCase(ZX_POL_NEW_PORT);
    PolicyCase(ZX_POL_NEW_SOCKET);
    PolicyCase(ZX_POL_NEW_FIFO);
    PolicyCase(ZX_POL_NEW_TIMER);
    PolicyCase(ZX_POL_NEW_PROCESS);
    PolicyCase(ZX_POL_NEW_PROFILE);
    PolicyCase(ZX_POL_AMBIENT_MARK_VMO_EXEC);
    default:
      *this << condition << ResetColor;
      return;
  }
}

void PrettyPrinter::DisplayPolicyTopic(uint32_t topic) {
  *this << Blue;
  switch (topic) {
    PolicyCase(ZX_JOB_POL_BASIC);
    PolicyCase(ZX_JOB_POL_TIMER_SLACK);
    default:
      *this << topic << ResetColor;
      return;
  }
}

#define ProfileInfoFlagsCase(name)  \
  if ((flags & (name)) == (name)) { \
    *this << separator << #name;    \
    separator = " | ";              \
  }

void PrettyPrinter::DisplayProfileInfoFlags(uint32_t flags) {
  *this << Blue;
  if (flags == 0) {
    *this << "0" << ResetColor;
    return;
  }
  const char* separator = "";
  ProfileInfoFlagsCase(ZX_PROFILE_INFO_FLAG_PRIORITY);
  ProfileInfoFlagsCase(ZX_PROFILE_INFO_FLAG_CPU_MASK);
  *this << ResetColor;
}

#define PortPacketTypeCase(name)  \
  case name:                      \
    *this << #name << ResetColor; \
    return

void PrettyPrinter::DisplayPortPacketType(uint32_t type) {
  *this << Blue;
  switch (type) {
    PortPacketTypeCase(ZX_PKT_TYPE_USER);
    PortPacketTypeCase(ZX_PKT_TYPE_SIGNAL_ONE);
    PortPacketTypeCase(ZX_PKT_TYPE_GUEST_BELL);
    PortPacketTypeCase(ZX_PKT_TYPE_GUEST_MEM);
    PortPacketTypeCase(ZX_PKT_TYPE_GUEST_IO);
    PortPacketTypeCase(ZX_PKT_TYPE_GUEST_VCPU);
    PortPacketTypeCase(ZX_PKT_TYPE_INTERRUPT);
    PortPacketTypeCase(ZX_PKT_TYPE_PAGE_REQUEST);
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

#define PropTypeCase(name) \
  case name:               \
    *this << #name;        \
    *this << ResetColor;   \
    return

void PrettyPrinter::DisplayPropType(uint32_t type) {
  *this << Blue;
  switch (type) {
    PropTypeCase(ZX_PROP_NAME);
    PropTypeCase(ZX_PROP_REGISTER_FS);
    PropTypeCase(ZX_PROP_REGISTER_GS);
    PropTypeCase(ZX_PROP_PROCESS_DEBUG_ADDR);
    PropTypeCase(ZX_PROP_PROCESS_VDSO_BASE_ADDRESS);
    PropTypeCase(ZX_PROP_SOCKET_RX_THRESHOLD);
    PropTypeCase(ZX_PROP_SOCKET_TX_THRESHOLD);
    PropTypeCase(ZX_PROP_JOB_KILL_ON_OOM);
    PropTypeCase(ZX_PROP_EXCEPTION_STATE);
    default:
      *this << type << ResetColor;
      return;
  }
}

#define RightsCase(name)         \
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
  RightsCase(ZX_RIGHT_DUPLICATE);
  RightsCase(ZX_RIGHT_TRANSFER);
  RightsCase(ZX_RIGHT_READ);
  RightsCase(ZX_RIGHT_WRITE);
  RightsCase(ZX_RIGHT_EXECUTE);
  RightsCase(ZX_RIGHT_MAP);
  RightsCase(ZX_RIGHT_GET_PROPERTY);
  RightsCase(ZX_RIGHT_SET_PROPERTY);
  RightsCase(ZX_RIGHT_ENUMERATE);
  RightsCase(ZX_RIGHT_DESTROY);
  RightsCase(ZX_RIGHT_SET_POLICY);
  RightsCase(ZX_RIGHT_GET_POLICY);
  RightsCase(ZX_RIGHT_SIGNAL);
  RightsCase(ZX_RIGHT_SIGNAL_PEER);
  RightsCase(ZX_RIGHT_WAIT);
  RightsCase(ZX_RIGHT_INSPECT);
  RightsCase(ZX_RIGHT_MANAGE_JOB);
  RightsCase(ZX_RIGHT_MANAGE_PROCESS);
  RightsCase(ZX_RIGHT_MANAGE_THREAD);
  RightsCase(ZX_RIGHT_APPLY_PROFILE);
  RightsCase(ZX_RIGHT_MANAGE_SOCKET);
  RightsCase(ZX_RIGHT_SAME_RIGHTS);
  *this << ResetColor;
}

#define RsrcKindCase(name)        \
  case name:                      \
    *this << #name << ResetColor; \
    return

void PrettyPrinter::DisplayRsrcKind(zx_rsrc_kind_t kind) {
  *this << Blue;
  switch (kind) {
    RsrcKindCase(ZX_RSRC_KIND_MMIO);
    RsrcKindCase(ZX_RSRC_KIND_IRQ);
    RsrcKindCase(ZX_RSRC_KIND_IOPORT);
    RsrcKindCase(ZX_RSRC_KIND_ROOT);
    RsrcKindCase(ZX_RSRC_KIND_SMC);
    RsrcKindCase(ZX_RSRC_KIND_SYSTEM);
    RsrcKindCase(ZX_RSRC_KIND_COUNT);
    default:
      *this << kind << ResetColor;
      return;
  }
}

#define SignalCase(name)              \
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
  SignalCase(__ZX_OBJECT_READABLE);
  SignalCase(__ZX_OBJECT_WRITABLE);
  SignalCase(__ZX_OBJECT_PEER_CLOSED);
  SignalCase(__ZX_OBJECT_SIGNALED);
  SignalCase(__ZX_OBJECT_SIGNAL_4);
  SignalCase(__ZX_OBJECT_SIGNAL_5);
  SignalCase(__ZX_OBJECT_SIGNAL_6);
  SignalCase(__ZX_OBJECT_SIGNAL_7);
  SignalCase(__ZX_OBJECT_SIGNAL_8);
  SignalCase(__ZX_OBJECT_SIGNAL_9);
  SignalCase(__ZX_OBJECT_SIGNAL_10);
  SignalCase(__ZX_OBJECT_SIGNAL_11);
  SignalCase(__ZX_OBJECT_SIGNAL_12);
  SignalCase(__ZX_OBJECT_SIGNAL_13);
  SignalCase(__ZX_OBJECT_SIGNAL_14);
  SignalCase(__ZX_OBJECT_SIGNAL_15);
  SignalCase(__ZX_OBJECT_SIGNAL_16);
  SignalCase(__ZX_OBJECT_SIGNAL_17);
  SignalCase(__ZX_OBJECT_SIGNAL_18);
  SignalCase(__ZX_OBJECT_SIGNAL_19);
  SignalCase(__ZX_OBJECT_SIGNAL_20);
  SignalCase(__ZX_OBJECT_SIGNAL_21);
  SignalCase(__ZX_OBJECT_SIGNAL_22);
  SignalCase(__ZX_OBJECT_HANDLE_CLOSED);
  SignalCase(ZX_USER_SIGNAL_0);
  SignalCase(ZX_USER_SIGNAL_1);
  SignalCase(ZX_USER_SIGNAL_2);
  SignalCase(ZX_USER_SIGNAL_3);
  SignalCase(ZX_USER_SIGNAL_4);
  SignalCase(ZX_USER_SIGNAL_5);
  SignalCase(ZX_USER_SIGNAL_6);
  SignalCase(ZX_USER_SIGNAL_7);
  *this << ResetColor;
}

#define SocketCreateOptionsCase(name) \
  case name:                          \
    *this << #name << ResetColor;     \
    return

void PrettyPrinter::DisplaySocketCreateOptions(uint32_t options) {
  *this << Blue;
  switch (options) {
    SocketCreateOptionsCase(ZX_SOCKET_STREAM);
    SocketCreateOptionsCase(ZX_SOCKET_DATAGRAM);
    default:
      *this << static_cast<uint32_t>(options) << ResetColor;
      return;
  }
}

#define SocketReadOptionsCase(name) \
  case name:                        \
    *this << #name << ResetColor;   \
    return

void PrettyPrinter::DisplaySocketReadOptions(uint32_t options) {
  *this << Blue;
  switch (options) {
    SocketReadOptionsCase(ZX_SOCKET_PEEK);
    default:
      *this << static_cast<uint32_t>(options) << ResetColor;
      return;
  }
}

#define SocketShutdownOptionsCase(name) \
  if ((options & (name)) == (name)) {   \
    *this << separator << #name;        \
    separator = " | ";                  \
  }

void PrettyPrinter::DisplaySocketShutdownOptions(uint32_t options) {
  *this << Blue;
  if (options == 0) {
    *this << "0" << ResetColor;
    return;
  }
  const char* separator = "";
  SocketShutdownOptionsCase(ZX_SOCKET_SHUTDOWN_WRITE);
  SocketShutdownOptionsCase(ZX_SOCKET_SHUTDOWN_READ);
  *this << ResetColor;
}

#define SocketDispositionCase(name)       \
  if ((disposition & (name)) == (name)) { \
    disposition ^= name;                  \
    *this << separator << #name;          \
    separator = " | ";                    \
  }

void PrettyPrinter::DisplaySocketDisposition(uint32_t disposition) {
  *this << Blue;
  if (disposition == 0) {
    *this << "0" << ResetColor;
    return;
  }
  const char* separator = "";
  SocketDispositionCase(ZX_SOCKET_DISPOSITION_WRITE_DISABLED);
  SocketDispositionCase(ZX_SOCKET_DISPOSITION_WRITE_ENABLED);
  if (disposition) {
    *this << separator << disposition;
  }
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

#define SystemEventTypeCase(name) \
  case name:                      \
    *this << #name << ResetColor; \
    return

void PrettyPrinter::DisplaySystemEventType(zx_system_event_type_t type) {
  *this << Blue;
  switch (type) {
    SystemEventTypeCase(ZX_SYSTEM_EVENT_OUT_OF_MEMORY);
    default:
      *this << type << ResetColor;
      return;
  }
}

#define SystemPowerctlCase(name)  \
  case name:                      \
    *this << #name << ResetColor; \
    return

void PrettyPrinter::DisplaySystemPowerctl(uint32_t powerctl) {
  *this << Blue;
  switch (powerctl) {
    SystemPowerctlCase(ZX_SYSTEM_POWERCTL_ENABLE_ALL_CPUS);
    SystemPowerctlCase(ZX_SYSTEM_POWERCTL_DISABLE_ALL_CPUS_BUT_PRIMARY);
    SystemPowerctlCase(ZX_SYSTEM_POWERCTL_ACPI_TRANSITION_S_STATE);
    SystemPowerctlCase(ZX_SYSTEM_POWERCTL_X86_SET_PKG_PL1);
    SystemPowerctlCase(ZX_SYSTEM_POWERCTL_REBOOT);
    SystemPowerctlCase(ZX_SYSTEM_POWERCTL_REBOOT_BOOTLOADER);
    SystemPowerctlCase(ZX_SYSTEM_POWERCTL_REBOOT_RECOVERY);
    SystemPowerctlCase(ZX_SYSTEM_POWERCTL_SHUTDOWN);
    default:
      *this << powerctl << ResetColor;
      return;
  }
}

#define ThreadStateCase(name)     \
  case name:                      \
    *this << #name << ResetColor; \
    return

void PrettyPrinter::DisplayThreadState(uint32_t state) {
  *this << Blue;
  switch (state) {
    ThreadStateCase(ZX_THREAD_STATE_NEW);
    ThreadStateCase(ZX_THREAD_STATE_RUNNING);
    ThreadStateCase(ZX_THREAD_STATE_SUSPENDED);
    ThreadStateCase(ZX_THREAD_STATE_BLOCKED);
    ThreadStateCase(ZX_THREAD_STATE_DYING);
    ThreadStateCase(ZX_THREAD_STATE_DEAD);
    ThreadStateCase(ZX_THREAD_STATE_BLOCKED_EXCEPTION);
    ThreadStateCase(ZX_THREAD_STATE_BLOCKED_SLEEPING);
    ThreadStateCase(ZX_THREAD_STATE_BLOCKED_FUTEX);
    ThreadStateCase(ZX_THREAD_STATE_BLOCKED_PORT);
    ThreadStateCase(ZX_THREAD_STATE_BLOCKED_CHANNEL);
    ThreadStateCase(ZX_THREAD_STATE_BLOCKED_WAIT_ONE);
    ThreadStateCase(ZX_THREAD_STATE_BLOCKED_WAIT_MANY);
    ThreadStateCase(ZX_THREAD_STATE_BLOCKED_INTERRUPT);
    ThreadStateCase(ZX_THREAD_STATE_BLOCKED_PAGER);
    default:
      *this << static_cast<uint32_t>(state) << ResetColor;
      return;
  }
}

#define ThreadStateTopicCase(name) \
  case name:                       \
    *this << #name << ResetColor;  \
    return

void PrettyPrinter::DisplayThreadStateTopic(zx_thread_state_topic_t topic) {
  *this << Blue;
  switch (topic) {
    ThreadStateTopicCase(ZX_THREAD_STATE_GENERAL_REGS);
    ThreadStateTopicCase(ZX_THREAD_STATE_FP_REGS);
    ThreadStateTopicCase(ZX_THREAD_STATE_VECTOR_REGS);
    ThreadStateTopicCase(ZX_THREAD_STATE_DEBUG_REGS);
    ThreadStateTopicCase(ZX_THREAD_STATE_SINGLE_STEP);
    default:
      *this << static_cast<uint32_t>(topic) << ResetColor;
      return;
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

#define TimerOptionCase(name)     \
  case name:                      \
    *this << #name << ResetColor; \
    return

void PrettyPrinter::DisplayTimerOption(uint32_t option) {
  *this << Blue;
  switch (option) {
    TimerOptionCase(ZX_TIMER_SLACK_CENTER);
    TimerOptionCase(ZX_TIMER_SLACK_EARLY);
    TimerOptionCase(ZX_TIMER_SLACK_LATE);
    default:
      *this << option << ResetColor;
      return;
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

#define VcpuCase(name)            \
  case name:                      \
    *this << #name << ResetColor; \
    return

void PrettyPrinter::DisplayVcpu(uint32_t type) {
  *this << Red;
  switch (type) {
    VcpuCase(ZX_VCPU_STATE);
    VcpuCase(ZX_VCPU_IO);
    default:
      *this << type << ResetColor;
      return;
  }
}

#define VmOptionAlign(name) \
  case name:                \
    *this << #name;         \
    separator = " | ";      \
    break;

#define VmOptionCase(name)           \
  if ((option & (name)) == (name)) { \
    *this << separator << #name;     \
    separator = " | ";               \
  }

void PrettyPrinter::DisplayVmOption(zx_vm_option_t option) {
  *this << Red;
  if (option == 0) {
    *this << "0" << ResetColor;
    return;
  }
  const char* separator = "";
  switch (option & ~((1 << ZX_VM_ALIGN_BASE) - 1)) {
    VmOptionAlign(ZX_VM_ALIGN_1KB);
    VmOptionAlign(ZX_VM_ALIGN_2KB);
    VmOptionAlign(ZX_VM_ALIGN_4KB);
    VmOptionAlign(ZX_VM_ALIGN_8KB);
    VmOptionAlign(ZX_VM_ALIGN_16KB);
    VmOptionAlign(ZX_VM_ALIGN_32KB);
    VmOptionAlign(ZX_VM_ALIGN_64KB);
    VmOptionAlign(ZX_VM_ALIGN_128KB);
    VmOptionAlign(ZX_VM_ALIGN_256KB);
    VmOptionAlign(ZX_VM_ALIGN_512KB);
    VmOptionAlign(ZX_VM_ALIGN_1MB);
    VmOptionAlign(ZX_VM_ALIGN_2MB);
    VmOptionAlign(ZX_VM_ALIGN_4MB);
    VmOptionAlign(ZX_VM_ALIGN_8MB);
    VmOptionAlign(ZX_VM_ALIGN_16MB);
    VmOptionAlign(ZX_VM_ALIGN_32MB);
    VmOptionAlign(ZX_VM_ALIGN_64MB);
    VmOptionAlign(ZX_VM_ALIGN_128MB);
    VmOptionAlign(ZX_VM_ALIGN_256MB);
    VmOptionAlign(ZX_VM_ALIGN_512MB);
    VmOptionAlign(ZX_VM_ALIGN_1GB);
    VmOptionAlign(ZX_VM_ALIGN_2GB);
    VmOptionAlign(ZX_VM_ALIGN_4GB);
    default:
      if ((option >> ZX_VM_ALIGN_BASE) != 0) {
        *this << (option >> ZX_VM_ALIGN_BASE);
      }
      break;
  }
  VmOptionCase(ZX_VM_PERM_READ);
  VmOptionCase(ZX_VM_PERM_WRITE);
  VmOptionCase(ZX_VM_PERM_EXECUTE);
  VmOptionCase(ZX_VM_COMPACT);
  VmOptionCase(ZX_VM_SPECIFIC);
  VmOptionCase(ZX_VM_SPECIFIC_OVERWRITE);
  VmOptionCase(ZX_VM_CAN_MAP_SPECIFIC);
  VmOptionCase(ZX_VM_CAN_MAP_READ);
  VmOptionCase(ZX_VM_CAN_MAP_WRITE);
  VmOptionCase(ZX_VM_CAN_MAP_EXECUTE);
  VmOptionCase(ZX_VM_MAP_RANGE);
  VmOptionCase(ZX_VM_REQUIRE_NON_RESIZABLE);
  VmOptionCase(ZX_VM_ALLOW_FAULTS);
  *this << ResetColor;
}

#define VmoCreationOptionCase(name)   \
  if ((options & (name)) == (name)) { \
    *this << separator << #name;      \
    separator = " | ";                \
  }

void PrettyPrinter::DisplayVmoCreationOption(uint32_t options) {
  *this << Blue;
  if (options == 0) {
    *this << "0" << ResetColor;
    return;
  }
  const char* separator = "";
  VmoCreationOptionCase(ZX_VMO_RESIZABLE);
  *this << ResetColor;
}

#define VmoOpCase(name)           \
  case name:                      \
    *this << #name << ResetColor; \
    return

void PrettyPrinter::DisplayVmoOp(uint32_t op) {
  *this << Blue;
  switch (op) {
    VmoOpCase(ZX_VMO_OP_COMMIT);
    VmoOpCase(ZX_VMO_OP_DECOMMIT);
    VmoOpCase(ZX_VMO_OP_LOCK);
    VmoOpCase(ZX_VMO_OP_UNLOCK);
    VmoOpCase(ZX_VMO_OP_CACHE_SYNC);
    VmoOpCase(ZX_VMO_OP_CACHE_INVALIDATE);
    VmoOpCase(ZX_VMO_OP_CACHE_CLEAN);
    VmoOpCase(ZX_VMO_OP_CACHE_CLEAN_INVALIDATE);
    default:
      *this << op << ResetColor;
      return;
  }
}

#define VmoOptionCase(name)           \
  if ((options & (name)) == (name)) { \
    *this << separator << #name;      \
    separator = " | ";                \
  }

void PrettyPrinter::DisplayVmoOption(uint32_t options) {
  *this << Blue;
  if (options == 0) {
    *this << "0" << ResetColor;
    return;
  }
  const char* separator = "";
  VmoOptionCase(ZX_VMO_CHILD_SNAPSHOT);
  VmoOptionCase(ZX_VMO_CHILD_RESIZABLE);
  VmoOptionCase(ZX_VMO_CHILD_SLICE);
  VmoOptionCase(ZX_VMO_CHILD_SNAPSHOT_AT_LEAST_ON_WRITE);
  *this << ResetColor;
}

#define VmoTypeCase(name)          \
  if ((type & (name)) == (name)) { \
    *this << " | " << #name;       \
  }

void PrettyPrinter::DisplayVmoType(uint32_t type) {
  *this << Blue;
  if ((type & 1) == ZX_INFO_VMO_TYPE_PHYSICAL) {
    *this << "ZX_INFO_VMO_TYPE_PHYSICAL";
  } else {
    *this << "ZX_INFO_VMO_TYPE_PAGED";
  }
  VmoTypeCase(ZX_INFO_VMO_RESIZABLE);
  VmoTypeCase(ZX_INFO_VMO_IS_COW_CLONE);
  VmoTypeCase(ZX_INFO_VMO_VIA_HANDLE);
  VmoTypeCase(ZX_INFO_VMO_VIA_MAPPING);
  VmoTypeCase(ZX_INFO_VMO_PAGER_BACKED);
  VmoTypeCase(ZX_INFO_VMO_CONTIGUOUS);
  *this << ResetColor;
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
