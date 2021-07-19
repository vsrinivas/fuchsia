// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "tools/fidlcat/lib/type_decoder.h"

#include <zircon/features.h>
#include <zircon/rights.h>
#include <zircon/syscalls/exception.h>
#include <zircon/syscalls/iommu.h>
#include <zircon/syscalls/object.h>
#include <zircon/syscalls/pci.h>
#include <zircon/syscalls/policy.h>
#include <zircon/syscalls/port.h>
#include <zircon/syscalls/profile.h>
#include <zircon/syscalls/system.h>
#include <zircon/types.h>

#include <cstdint>
#include <iomanip>
#include <ostream>
#include <sstream>

#include "src/lib/fidl_codec/status.h"

namespace fidlcat {

std::string_view TypeName(SyscallType type) {
  switch (type) {
    case SyscallType::kBool:
      return "bool";
    case SyscallType::kChar:
      return "char";
    case SyscallType::kInt32:
      return "int32";
    case SyscallType::kInt64:
      return "int64";
    case SyscallType::kUint8:
    case SyscallType::kUint8Hexa:
      return "uint8";
    case SyscallType::kUint16:
    case SyscallType::kUint16Hexa:
      return "uint16";
    case SyscallType::kUint32:
    case SyscallType::kUint32Hexa:
      return "uint32";
    case SyscallType::kUint64:
    case SyscallType::kUint64Hexa:
      return "uint64";
    case SyscallType::kUint128Hexa:
      return "uint128";
    case SyscallType::kBtiPerm:
      return "zx_bti_perm_t";
    case SyscallType::kCachePolicy:
      return "zx_cache_policy_t";
    case SyscallType::kChannelOption:
      return "uint32";
    case SyscallType::kClock:
      return "clock";
    case SyscallType::kDuration:
      return "duration";
    case SyscallType::kExceptionChannelType:
      return "zx_info_thread_t::wait_exception_channel_type";
    case SyscallType::kExceptionState:
      return "zx_exception_state_t";
    case SyscallType::kFeatureKind:
      return "zx_feature_kind_t";
    case SyscallType::kFutex:
      return "zx_futex_t";
    case SyscallType::kGpAddr:
      return "zx_gpaddr_t";
    case SyscallType::kGuestTrap:
      return "zx_guest_trap_t";
    case SyscallType::kHandle:
      return "handle";
    case SyscallType::kInfoMapsType:
      return "zx_info_maps_type_t";
    case SyscallType::kInterruptFlags:
      return "zx_interrupt_flags_t";
    case SyscallType::kIommuType:
      return "zx_iommu_type_t";
    case SyscallType::kKoid:
      return "zx_koid_t";
    case SyscallType::kKtraceControlAction:
      return "zx_ktrace_control_action_t";
    case SyscallType::kMonotonicTime:
      return "zx_time_t";
    case SyscallType::kObjectInfoTopic:
      return "zx_object_info_topic_t";
    case SyscallType::kObjType:
      return "zx_obj_type_t";
    case SyscallType::kPacketGuestVcpuType:
      return "zx_packet_guest_vcpu_t::type";
    case SyscallType::kPacketPageRequestCommand:
      return "zx_packet_page_request_t::command";
    case SyscallType::kPaddr:
      return "zx_paddr_t";
    case SyscallType::kPciBarType:
      return "zx_pci_bar_type_t";
    case SyscallType::kPolicyAction:
      return "zx_policy_action_t";
    case SyscallType::kPolicyCondition:
      return "zx_policy_condition_t";
    case SyscallType::kPolicyTopic:
      return "zx_policy_topic_t";
    case SyscallType::kPortPacketType:
      return "zx_port_packet_t::type";
    case SyscallType::kProfileInfoFlags:
      return "zx_profile_info_flags_t";
    case SyscallType::kPropType:
      return "zx_prop_type_t";
    case SyscallType::kRights:
      return "zx_rights_t";
    case SyscallType::kRsrcKind:
      return "zx_rsrc_kind_t";
    case SyscallType::kSignals:
      return "signals";
    case SyscallType::kSize:
      return "size_t";
    case SyscallType::kSocketCreateOptions:
      return "zx_socket_create_options_t";
    case SyscallType::kSocketReadOptions:
      return "zx_socket_read_options_t";
    case SyscallType::kSocketShutdownOptions:
      return "zx_socket_shutdown_options_t";
    case SyscallType::kSocketDisposition:
      return "zx_socket_disposition_t";
    case SyscallType::kStatus:
      return "status_t";
    case SyscallType::kSystemEventType:
      return "zx_system_event_type_t";
    case SyscallType::kSystemPowerctl:
      return "zx_system_powerctl_t";
    case SyscallType::kThreadState:
      return "zx_info_thread_t::state";
    case SyscallType::kThreadStateTopic:
      return "zx_thread_state_topic_t";
    case SyscallType::kTime:
      return "time";
    case SyscallType::kTimerOption:
      return "zx_timer_option_t";
    case SyscallType::kUintptr:
      return "uintptr_t";
    case SyscallType::kVaddr:
      return "zx_vaddr_t";
    case SyscallType::kVcpu:
      return "zx_vcpu_t";
    case SyscallType::kVmOption:
      return "zx_vm_option_t";
    case SyscallType::kVmoCreationOption:
      return "zx_vmo_creation_option_t";
    case SyscallType::kVmoOp:
      return "zx_vmo_op_t";
    case SyscallType::kVmoOption:
      return "zx_vmo_option_t";
    case SyscallType::kVmoType:
      return "zx_info_vmo_type_t";
    case SyscallType::kStruct:
      return "";
  }
}

void DisplayType(SyscallType type, fidl_codec::PrettyPrinter& printer) {
  printer << ": " << fidl_codec::Green << TypeName(type) << fidl_codec::ResetColor << " = ";
}

}  // namespace fidlcat
