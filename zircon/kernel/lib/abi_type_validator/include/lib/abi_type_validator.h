// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ZIRCON_KERNEL_LIB_ABI_TYPE_VALIDATOR_INCLUDE_LIB_ABI_TYPE_VALIDATOR_H_
#define ZIRCON_KERNEL_LIB_ABI_TYPE_VALIDATOR_INCLUDE_LIB_ABI_TYPE_VALIDATOR_H_

// This C++ header is used to validate kernel ABI types.
//
// It can and should be included by both kernel and usermode code to statically assert that both
// kernel and usermode agree on the size, alignment and field offsets of various types used in the
// ABI.
//
// If one of these static_asserts fire, it means that a kernel type has changed in a backwards
// incompatible way. In other words, the change breaks ABI compatibility.

#ifndef __cplusplus
#error "Can only be included in C++."
#endif  // __cplusplus

#if defined(__aarch64__)
#include <lib/zircon-internal/device/cpu-trace/arm64-pm.h>
#elif defined(__x86_64__)
#include <lib/zircon-internal/device/cpu-trace/intel-pm.h>
#endif

#include <zircon/syscalls/exception.h>
#include <zircon/syscalls/object.h>
#include <zircon/syscalls/pci.h>
#include <zircon/syscalls/port.h>
#include <zircon/types.h>

#include <cstddef>

// Statically asserts that type |name| has sizeof(|size|) and alignof(|alignment|).
#define VALIDATE_TYPE_SIZE_ALIGNMENT(name, size, alignment)                                   \
  static_assert(sizeof(name) == (size), "size change to " #name " breaks ABI compatibility"); \
  static_assert(alignof(name) == (alignment),                                                 \
                "alignment change to " #name " breaks ABI compatibility")

// Statically asserts that type |name| has field |field| at offset |offset| with size |size|.
#define VALIDATE_FIELD_OFFSET_SIZE(name, field, offset, size)                       \
  static_assert(offsetof(name, field) == (offset),                                  \
                "offset change to " #name "::" #field " breaks ABI compatibility"); \
  static_assert(sizeof(name::field) == (size),                                      \
                "size change to " #name "::" #field " breaks ABI compatibility")

// TODO(maniscalco): Expand the set of types validated. Validate all the ABI types, not just those
// with implicit padding that are copied out to usermode.

VALIDATE_TYPE_SIZE_ALIGNMENT(perfmon::PmuCommonProperties, 14, 2);
VALIDATE_FIELD_OFFSET_SIZE(perfmon::PmuCommonProperties, pm_version, 0, 2);
VALIDATE_FIELD_OFFSET_SIZE(perfmon::PmuCommonProperties, pm_version, 0, 2);
VALIDATE_FIELD_OFFSET_SIZE(perfmon::PmuCommonProperties, max_num_fixed_events, 2, 2);
VALIDATE_FIELD_OFFSET_SIZE(perfmon::PmuCommonProperties, max_fixed_counter_width, 4, 2);
VALIDATE_FIELD_OFFSET_SIZE(perfmon::PmuCommonProperties, max_num_programmable_events, 6, 2);
VALIDATE_FIELD_OFFSET_SIZE(perfmon::PmuCommonProperties, max_programmable_counter_width, 8, 2);
VALIDATE_FIELD_OFFSET_SIZE(perfmon::PmuCommonProperties, max_num_misc_events, 10, 2);
VALIDATE_FIELD_OFFSET_SIZE(perfmon::PmuCommonProperties, max_misc_counter_width, 12, 2);

#if defined(__aarch64__)
VALIDATE_TYPE_SIZE_ALIGNMENT(perfmon::Arm64PmuProperties, 14, 2);
VALIDATE_FIELD_OFFSET_SIZE(perfmon::Arm64PmuProperties, common, 0, 14);
#elif defined(__x86_64__)
VALIDATE_TYPE_SIZE_ALIGNMENT(perfmon::X86PmuProperties, 32, 8);
VALIDATE_FIELD_OFFSET_SIZE(perfmon::X86PmuProperties, common, 0, 14);
VALIDATE_FIELD_OFFSET_SIZE(perfmon::X86PmuProperties, perf_capabilities, 16, 8);
VALIDATE_FIELD_OFFSET_SIZE(perfmon::X86PmuProperties, lbr_stack_size, 24, 4);
#endif

VALIDATE_TYPE_SIZE_ALIGNMENT(zx_exception_report_t, 32, 8);
VALIDATE_FIELD_OFFSET_SIZE(zx_exception_report_t, header, 0, 8);
VALIDATE_FIELD_OFFSET_SIZE(zx_exception_report_t, context, 8, 24);

VALIDATE_TYPE_SIZE_ALIGNMENT(zx_exception_header_t, 8, 4);
VALIDATE_FIELD_OFFSET_SIZE(zx_exception_header_t, size, 0, 4);
VALIDATE_FIELD_OFFSET_SIZE(zx_exception_header_t, type, 4, 4);

VALIDATE_TYPE_SIZE_ALIGNMENT(zx_exception_context_t, 24, 8);
VALIDATE_FIELD_OFFSET_SIZE(zx_exception_context_t, arch, 0, 24);
VALIDATE_FIELD_OFFSET_SIZE(zx_exception_context_t, arch.u, 0, 24);
VALIDATE_FIELD_OFFSET_SIZE(zx_exception_context_t, arch.u.x86_64, 0, 24);
VALIDATE_FIELD_OFFSET_SIZE(zx_exception_context_t, arch.u.arm_64, 0, 16);

VALIDATE_TYPE_SIZE_ALIGNMENT(zx_x86_64_exc_data_t, 24, 8);
VALIDATE_FIELD_OFFSET_SIZE(zx_x86_64_exc_data_t, vector, 0, 8);
VALIDATE_FIELD_OFFSET_SIZE(zx_x86_64_exc_data_t, err_code, 8, 8);
VALIDATE_FIELD_OFFSET_SIZE(zx_x86_64_exc_data_t, cr2, 16, 8);

VALIDATE_TYPE_SIZE_ALIGNMENT(zx_arm64_exc_data_t, 16, 8);
VALIDATE_FIELD_OFFSET_SIZE(zx_arm64_exc_data_t, esr, 0, 4);
VALIDATE_FIELD_OFFSET_SIZE(zx_arm64_exc_data_t, far, 8, 8);

VALIDATE_TYPE_SIZE_ALIGNMENT(zx_info_bti_t, 16, 8);
VALIDATE_FIELD_OFFSET_SIZE(zx_info_bti_t, minimum_contiguity, 0, 8);
VALIDATE_FIELD_OFFSET_SIZE(zx_info_bti_t, aspace_size, 8, 8);

VALIDATE_TYPE_SIZE_ALIGNMENT(zx_info_handle_basic_t, 32, 8);
VALIDATE_FIELD_OFFSET_SIZE(zx_info_handle_basic_t, koid, 0, 8);
VALIDATE_FIELD_OFFSET_SIZE(zx_info_handle_basic_t, rights, 8, 4);
VALIDATE_FIELD_OFFSET_SIZE(zx_info_handle_basic_t, type, 12, 4);
VALIDATE_FIELD_OFFSET_SIZE(zx_info_handle_basic_t, related_koid, 16, 8);
VALIDATE_FIELD_OFFSET_SIZE(zx_info_handle_basic_t, props, 24, 4);

VALIDATE_TYPE_SIZE_ALIGNMENT(zx_info_job_t, 16, 8);
VALIDATE_FIELD_OFFSET_SIZE(zx_info_job_t, return_code, 0, 8);
VALIDATE_FIELD_OFFSET_SIZE(zx_info_job_t, exited, 8, 1);
VALIDATE_FIELD_OFFSET_SIZE(zx_info_job_t, kill_on_oom, 9, 1);
VALIDATE_FIELD_OFFSET_SIZE(zx_info_job_t, debugger_attached, 10, 1);

VALIDATE_TYPE_SIZE_ALIGNMENT(zx_info_maps_mapping_t, 32, 8);
VALIDATE_FIELD_OFFSET_SIZE(zx_info_maps_mapping_t, mmu_flags, 0, 4);
VALIDATE_FIELD_OFFSET_SIZE(zx_info_maps_mapping_t, vmo_koid, 8, 8);
VALIDATE_FIELD_OFFSET_SIZE(zx_info_maps_mapping_t, vmo_offset, 16, 8);
VALIDATE_FIELD_OFFSET_SIZE(zx_info_maps_mapping_t, committed_pages, 24, 8);

VALIDATE_TYPE_SIZE_ALIGNMENT(zx_info_maps_t, 96, 8);
VALIDATE_FIELD_OFFSET_SIZE(zx_info_maps_t, name, 0, 32);
VALIDATE_FIELD_OFFSET_SIZE(zx_info_maps_t, base, 32, 8);
VALIDATE_FIELD_OFFSET_SIZE(zx_info_maps_t, size, 40, 8);
VALIDATE_FIELD_OFFSET_SIZE(zx_info_maps_t, depth, 48, 8);
VALIDATE_FIELD_OFFSET_SIZE(zx_info_maps_t, type, 56, 4);
VALIDATE_FIELD_OFFSET_SIZE(zx_info_maps_t, u, 64, 32);
VALIDATE_FIELD_OFFSET_SIZE(zx_info_maps_t, u.mapping, 64, 32);

VALIDATE_TYPE_SIZE_ALIGNMENT(zx_info_process_t, 16, 8);
VALIDATE_FIELD_OFFSET_SIZE(zx_info_process_t, return_code, 0, 8);
VALIDATE_FIELD_OFFSET_SIZE(zx_info_process_t, started, 8, 1);
VALIDATE_FIELD_OFFSET_SIZE(zx_info_process_t, exited, 9, 1);
VALIDATE_FIELD_OFFSET_SIZE(zx_info_process_t, debugger_attached, 10, 1);

VALIDATE_TYPE_SIZE_ALIGNMENT(zx_info_socket_t, 48, 8);
VALIDATE_FIELD_OFFSET_SIZE(zx_info_socket_t, options, 0, 4);
VALIDATE_FIELD_OFFSET_SIZE(zx_info_socket_t, rx_buf_max, 8, 8);
VALIDATE_FIELD_OFFSET_SIZE(zx_info_socket_t, rx_buf_size, 16, 8);
VALIDATE_FIELD_OFFSET_SIZE(zx_info_socket_t, rx_buf_available, 24, 8);
VALIDATE_FIELD_OFFSET_SIZE(zx_info_socket_t, tx_buf_max, 32, 8);
VALIDATE_FIELD_OFFSET_SIZE(zx_info_socket_t, tx_buf_size, 40, 8);

VALIDATE_TYPE_SIZE_ALIGNMENT(zx_info_thread_stats_t, 16, 8);
VALIDATE_FIELD_OFFSET_SIZE(zx_info_thread_stats_t, total_runtime, 0, 8);
VALIDATE_FIELD_OFFSET_SIZE(zx_info_thread_stats_t, last_scheduled_cpu, 8, 4);

VALIDATE_TYPE_SIZE_ALIGNMENT(zx_info_timer_t, 24, 8);
VALIDATE_FIELD_OFFSET_SIZE(zx_info_timer_t, options, 0, 4);
VALIDATE_FIELD_OFFSET_SIZE(zx_info_timer_t, deadline, 8, 8);
VALIDATE_FIELD_OFFSET_SIZE(zx_info_timer_t, slack, 16, 8);

VALIDATE_TYPE_SIZE_ALIGNMENT(zx_info_vmo_t, 104, 8);
VALIDATE_FIELD_OFFSET_SIZE(zx_info_vmo_t, koid, 0, 8);
VALIDATE_FIELD_OFFSET_SIZE(zx_info_vmo_t, name, 8, 32);
VALIDATE_FIELD_OFFSET_SIZE(zx_info_vmo_t, size_bytes, 40, 8);
VALIDATE_FIELD_OFFSET_SIZE(zx_info_vmo_t, parent_koid, 48, 8);
VALIDATE_FIELD_OFFSET_SIZE(zx_info_vmo_t, num_children, 56, 8);
VALIDATE_FIELD_OFFSET_SIZE(zx_info_vmo_t, num_mappings, 64, 8);
VALIDATE_FIELD_OFFSET_SIZE(zx_info_vmo_t, share_count, 72, 8);
VALIDATE_FIELD_OFFSET_SIZE(zx_info_vmo_t, flags, 80, 4);
VALIDATE_FIELD_OFFSET_SIZE(zx_info_vmo_t, committed_bytes, 88, 8);
VALIDATE_FIELD_OFFSET_SIZE(zx_info_vmo_t, handle_rights, 96, 4);
VALIDATE_FIELD_OFFSET_SIZE(zx_info_vmo_t, cache_policy, 100, 4);

VALIDATE_TYPE_SIZE_ALIGNMENT(zx_pci_bar_t, 24, 8);
VALIDATE_FIELD_OFFSET_SIZE(zx_pci_bar_t, id, 0, 4);
VALIDATE_FIELD_OFFSET_SIZE(zx_pci_bar_t, type, 4, 4);
VALIDATE_FIELD_OFFSET_SIZE(zx_pci_bar_t, size, 8, 8);
VALIDATE_FIELD_OFFSET_SIZE(zx_pci_bar_t, addr, 16, 8);
VALIDATE_FIELD_OFFSET_SIZE(zx_pci_bar_t, handle, 16, 4);

VALIDATE_TYPE_SIZE_ALIGNMENT(zx_pcie_device_info_t, 12, 2);
VALIDATE_FIELD_OFFSET_SIZE(zx_pcie_device_info_t, vendor_id, 0, 2);
VALIDATE_FIELD_OFFSET_SIZE(zx_pcie_device_info_t, device_id, 2, 2);
VALIDATE_FIELD_OFFSET_SIZE(zx_pcie_device_info_t, base_class, 4, 1);
VALIDATE_FIELD_OFFSET_SIZE(zx_pcie_device_info_t, sub_class, 5, 1);
VALIDATE_FIELD_OFFSET_SIZE(zx_pcie_device_info_t, program_interface, 6, 1);
VALIDATE_FIELD_OFFSET_SIZE(zx_pcie_device_info_t, revision_id, 7, 1);
VALIDATE_FIELD_OFFSET_SIZE(zx_pcie_device_info_t, bus_id, 8, 1);
VALIDATE_FIELD_OFFSET_SIZE(zx_pcie_device_info_t, dev_id, 9, 1);
VALIDATE_FIELD_OFFSET_SIZE(zx_pcie_device_info_t, func_id, 10, 1);

VALIDATE_TYPE_SIZE_ALIGNMENT(zx_port_packet_t, 48, 8);
VALIDATE_FIELD_OFFSET_SIZE(zx_port_packet_t, key, 0, 8);
VALIDATE_FIELD_OFFSET_SIZE(zx_port_packet_t, type, 8, 4);
VALIDATE_FIELD_OFFSET_SIZE(zx_port_packet_t, status, 12, 4);
VALIDATE_FIELD_OFFSET_SIZE(zx_port_packet_t, user, 16, 32);
VALIDATE_FIELD_OFFSET_SIZE(zx_port_packet_t, signal, 16, 32);
VALIDATE_FIELD_OFFSET_SIZE(zx_port_packet_t, exception, 16, 32);
VALIDATE_FIELD_OFFSET_SIZE(zx_port_packet_t, guest_bell, 16, 32);
VALIDATE_FIELD_OFFSET_SIZE(zx_port_packet_t, guest_mem, 16, 32);
VALIDATE_FIELD_OFFSET_SIZE(zx_port_packet_t, guest_io, 16, 32);
VALIDATE_FIELD_OFFSET_SIZE(zx_port_packet_t, guest_vcpu, 16, 32);
VALIDATE_FIELD_OFFSET_SIZE(zx_port_packet_t, interrupt, 16, 32);
VALIDATE_FIELD_OFFSET_SIZE(zx_port_packet_t, page_request, 16, 32);

VALIDATE_TYPE_SIZE_ALIGNMENT(zx_packet_user_t, 32, 8);
VALIDATE_FIELD_OFFSET_SIZE(zx_packet_user_t, u64, 0, 32);
VALIDATE_FIELD_OFFSET_SIZE(zx_packet_user_t, u32, 0, 32);
VALIDATE_FIELD_OFFSET_SIZE(zx_packet_user_t, u16, 0, 32);
VALIDATE_FIELD_OFFSET_SIZE(zx_packet_user_t, c8, 0, 32);

VALIDATE_TYPE_SIZE_ALIGNMENT(zx_packet_signal_t, 32, 8);
VALIDATE_FIELD_OFFSET_SIZE(zx_packet_signal_t, trigger, 0, 4);
VALIDATE_FIELD_OFFSET_SIZE(zx_packet_signal_t, observed, 4, 4);
VALIDATE_FIELD_OFFSET_SIZE(zx_packet_signal_t, count, 8, 8);
VALIDATE_FIELD_OFFSET_SIZE(zx_packet_signal_t, timestamp, 16, 8);
VALIDATE_FIELD_OFFSET_SIZE(zx_packet_signal_t, reserved1, 24, 8);

VALIDATE_TYPE_SIZE_ALIGNMENT(zx_packet_exception_t, 32, 8);
VALIDATE_FIELD_OFFSET_SIZE(zx_packet_exception_t, pid, 0, 8);
VALIDATE_FIELD_OFFSET_SIZE(zx_packet_exception_t, tid, 8, 8);
VALIDATE_FIELD_OFFSET_SIZE(zx_packet_exception_t, reserved0, 16, 8);
VALIDATE_FIELD_OFFSET_SIZE(zx_packet_exception_t, reserved1, 24, 8);

VALIDATE_TYPE_SIZE_ALIGNMENT(zx_packet_guest_bell_t, 32, 8);
VALIDATE_FIELD_OFFSET_SIZE(zx_packet_guest_bell_t, addr, 0, 8);
VALIDATE_FIELD_OFFSET_SIZE(zx_packet_guest_bell_t, reserved0, 8, 8);
VALIDATE_FIELD_OFFSET_SIZE(zx_packet_guest_bell_t, reserved1, 16, 8);
VALIDATE_FIELD_OFFSET_SIZE(zx_packet_guest_bell_t, reserved2, 24, 8);

VALIDATE_TYPE_SIZE_ALIGNMENT(zx_packet_guest_mem_t, 32, 8);
VALIDATE_FIELD_OFFSET_SIZE(zx_packet_guest_mem_t, addr, 0, 8);
#if defined(__aarch64__)
VALIDATE_FIELD_OFFSET_SIZE(zx_packet_guest_mem_t, access_size, 8, 1);
VALIDATE_FIELD_OFFSET_SIZE(zx_packet_guest_mem_t, sign_extend, 9, 1);
VALIDATE_FIELD_OFFSET_SIZE(zx_packet_guest_mem_t, xt, 10, 1);
VALIDATE_FIELD_OFFSET_SIZE(zx_packet_guest_mem_t, read, 11, 1);
VALIDATE_FIELD_OFFSET_SIZE(zx_packet_guest_mem_t, data, 16, 8);
VALIDATE_FIELD_OFFSET_SIZE(zx_packet_guest_mem_t, reserved, 24, 8);
#elif defined(__x86_64__)
VALIDATE_FIELD_OFFSET_SIZE(zx_packet_guest_mem_t, inst_len, 8, 1);
VALIDATE_FIELD_OFFSET_SIZE(zx_packet_guest_mem_t, inst_buf, 9, 15);
VALIDATE_FIELD_OFFSET_SIZE(zx_packet_guest_mem_t, default_operand_size, 24, 1);
VALIDATE_FIELD_OFFSET_SIZE(zx_packet_guest_mem_t, reserved, 25, 7);
#endif

VALIDATE_TYPE_SIZE_ALIGNMENT(zx_packet_guest_io_t, 32, 8);
VALIDATE_FIELD_OFFSET_SIZE(zx_packet_guest_io_t, port, 0, 2);
VALIDATE_FIELD_OFFSET_SIZE(zx_packet_guest_io_t, access_size, 2, 1);
VALIDATE_FIELD_OFFSET_SIZE(zx_packet_guest_io_t, input, 3, 1);
VALIDATE_FIELD_OFFSET_SIZE(zx_packet_guest_io_t, u8, 4, 1);
VALIDATE_FIELD_OFFSET_SIZE(zx_packet_guest_io_t, u16, 4, 2);
VALIDATE_FIELD_OFFSET_SIZE(zx_packet_guest_io_t, u32, 4, 4);
VALIDATE_FIELD_OFFSET_SIZE(zx_packet_guest_io_t, data, 4, 4);
VALIDATE_FIELD_OFFSET_SIZE(zx_packet_guest_io_t, reserved0, 8, 8);
VALIDATE_FIELD_OFFSET_SIZE(zx_packet_guest_io_t, reserved1, 16, 8);
VALIDATE_FIELD_OFFSET_SIZE(zx_packet_guest_io_t, reserved2, 24, 8);

VALIDATE_TYPE_SIZE_ALIGNMENT(zx_packet_guest_vcpu_t, 32, 8);
VALIDATE_FIELD_OFFSET_SIZE(zx_packet_guest_vcpu_t, interrupt, 0, 16);
VALIDATE_FIELD_OFFSET_SIZE(zx_packet_guest_vcpu_t, interrupt.mask, 0, 8);
VALIDATE_FIELD_OFFSET_SIZE(zx_packet_guest_vcpu_t, interrupt.vector, 8, 1);
VALIDATE_FIELD_OFFSET_SIZE(zx_packet_guest_vcpu_t, startup, 0, 16);
VALIDATE_FIELD_OFFSET_SIZE(zx_packet_guest_vcpu_t, startup.id, 0, 8);
VALIDATE_FIELD_OFFSET_SIZE(zx_packet_guest_vcpu_t, startup.entry, 8, 8);
VALIDATE_FIELD_OFFSET_SIZE(zx_packet_guest_vcpu_t, type, 16, 1);
VALIDATE_FIELD_OFFSET_SIZE(zx_packet_guest_vcpu_t, reserved, 24, 8);

VALIDATE_TYPE_SIZE_ALIGNMENT(zx_packet_interrupt_t, 32, 8);
VALIDATE_FIELD_OFFSET_SIZE(zx_packet_interrupt_t, timestamp, 0, 8);
VALIDATE_FIELD_OFFSET_SIZE(zx_packet_interrupt_t, reserved0, 8, 8);
VALIDATE_FIELD_OFFSET_SIZE(zx_packet_interrupt_t, reserved1, 16, 8);
VALIDATE_FIELD_OFFSET_SIZE(zx_packet_interrupt_t, reserved2, 24, 8);

VALIDATE_TYPE_SIZE_ALIGNMENT(zx_packet_page_request_t, 32, 8);
VALIDATE_FIELD_OFFSET_SIZE(zx_packet_page_request_t, command, 0, 2);
VALIDATE_FIELD_OFFSET_SIZE(zx_packet_page_request_t, flags, 2, 2);
VALIDATE_FIELD_OFFSET_SIZE(zx_packet_page_request_t, reserved0, 4, 4);
VALIDATE_FIELD_OFFSET_SIZE(zx_packet_page_request_t, offset, 8, 8);
VALIDATE_FIELD_OFFSET_SIZE(zx_packet_page_request_t, length, 16, 8);
VALIDATE_FIELD_OFFSET_SIZE(zx_packet_page_request_t, reserved1, 24, 8);

#undef VALIDATE_TYPE_SIZE_ALIGNMENT
#undef VALIDATE_FIELD_OFFSET_SIZE

#endif  // ZIRCON_KERNEL_LIB_ABI_TYPE_VALIDATOR_INCLUDE_LIB_ABI_TYPE_VALIDATOR_H_
