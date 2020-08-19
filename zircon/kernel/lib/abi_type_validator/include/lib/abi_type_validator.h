// Copyright 2019 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

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

#include <zircon/syscalls/clock.h>
#include <zircon/syscalls/debug.h>
#include <zircon/syscalls/exception.h>
#include <zircon/syscalls/hypervisor.h>
#include <zircon/syscalls/object.h>
#include <zircon/syscalls/pci.h>
#include <zircon/syscalls/policy.h>
#include <zircon/syscalls/port.h>
#include <zircon/syscalls/profile.h>
#include <zircon/syscalls/scheduler.h>
#include <zircon/syscalls/smc.h>
#include <zircon/syscalls/system.h>
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

#if defined(__aarch64__)
VALIDATE_TYPE_SIZE_ALIGNMENT(perfmon::Arm64PmuConfig, 104, 8);
VALIDATE_FIELD_OFFSET_SIZE(perfmon::Arm64PmuConfig, timebase_event, 0, 2);
VALIDATE_FIELD_OFFSET_SIZE(perfmon::Arm64PmuConfig, fixed_events, 2, 2);
VALIDATE_FIELD_OFFSET_SIZE(perfmon::Arm64PmuConfig, programmable_events, 4, 12);
VALIDATE_FIELD_OFFSET_SIZE(perfmon::Arm64PmuConfig, fixed_initial_value, 16, 8);
VALIDATE_FIELD_OFFSET_SIZE(perfmon::Arm64PmuConfig, programmable_initial_value, 24, 24);
VALIDATE_FIELD_OFFSET_SIZE(perfmon::Arm64PmuConfig, fixed_flags, 48, 4);
VALIDATE_FIELD_OFFSET_SIZE(perfmon::Arm64PmuConfig, programmable_flags, 52, 24);
VALIDATE_FIELD_OFFSET_SIZE(perfmon::Arm64PmuConfig, programmable_hw_events, 76, 24);
#elif defined(__x86_64__)
VALIDATE_TYPE_SIZE_ALIGNMENT(perfmon::X86PmuConfig, 344, 8);
VALIDATE_FIELD_OFFSET_SIZE(perfmon::X86PmuConfig, global_ctrl, 0, 8);
VALIDATE_FIELD_OFFSET_SIZE(perfmon::X86PmuConfig, fixed_ctrl, 8, 8);
VALIDATE_FIELD_OFFSET_SIZE(perfmon::X86PmuConfig, debug_ctrl, 16, 8);
VALIDATE_FIELD_OFFSET_SIZE(perfmon::X86PmuConfig, timebase_event, 24, 2);
VALIDATE_FIELD_OFFSET_SIZE(perfmon::X86PmuConfig, fixed_events, 26, 6);
VALIDATE_FIELD_OFFSET_SIZE(perfmon::X86PmuConfig, programmable_events, 32, 16);
VALIDATE_FIELD_OFFSET_SIZE(perfmon::X86PmuConfig, misc_events, 48, 32);
VALIDATE_FIELD_OFFSET_SIZE(perfmon::X86PmuConfig, fixed_initial_value, 80, 24);
VALIDATE_FIELD_OFFSET_SIZE(perfmon::X86PmuConfig, programmable_initial_value, 104, 64);
VALIDATE_FIELD_OFFSET_SIZE(perfmon::X86PmuConfig, fixed_flags, 168, 12);
VALIDATE_FIELD_OFFSET_SIZE(perfmon::X86PmuConfig, programmable_flags, 180, 32);
VALIDATE_FIELD_OFFSET_SIZE(perfmon::X86PmuConfig, misc_flags, 212, 64);
VALIDATE_FIELD_OFFSET_SIZE(perfmon::X86PmuConfig, programmable_hw_events, 280, 64);
#endif

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

VALIDATE_TYPE_SIZE_ALIGNMENT(zx_clock_rate_t, 8, 4);
VALIDATE_FIELD_OFFSET_SIZE(zx_clock_rate_t, synthetic_ticks, 0, 4);
VALIDATE_FIELD_OFFSET_SIZE(zx_clock_rate_t, reference_ticks, 4, 4);

VALIDATE_TYPE_SIZE_ALIGNMENT(zx_clock_transformation_t, 24, 8);
VALIDATE_FIELD_OFFSET_SIZE(zx_clock_transformation_t, reference_offset, 0, 8);
VALIDATE_FIELD_OFFSET_SIZE(zx_clock_transformation_t, synthetic_offset, 8, 8);
VALIDATE_FIELD_OFFSET_SIZE(zx_clock_transformation_t, rate, 16, 8);

VALIDATE_TYPE_SIZE_ALIGNMENT(zx_clock_details_v1_t, 112, 8);
VALIDATE_FIELD_OFFSET_SIZE(zx_clock_details_v1_t, options, 0, 8);
VALIDATE_FIELD_OFFSET_SIZE(zx_clock_details_v1_t, backstop_time, 8, 8);
VALIDATE_FIELD_OFFSET_SIZE(zx_clock_details_v1_t, ticks_to_synthetic, 16, 24);
VALIDATE_FIELD_OFFSET_SIZE(zx_clock_details_v1_t, mono_to_synthetic, 40, 24);
VALIDATE_FIELD_OFFSET_SIZE(zx_clock_details_v1_t, error_bound, 64, 8);
VALIDATE_FIELD_OFFSET_SIZE(zx_clock_details_v1_t, query_ticks, 72, 8);
VALIDATE_FIELD_OFFSET_SIZE(zx_clock_details_v1_t, last_value_update_ticks, 80, 8);
VALIDATE_FIELD_OFFSET_SIZE(zx_clock_details_v1_t, last_rate_adjust_update_ticks, 88, 8);
VALIDATE_FIELD_OFFSET_SIZE(zx_clock_details_v1_t, last_error_bounds_update_ticks, 96, 8);
VALIDATE_FIELD_OFFSET_SIZE(zx_clock_details_v1_t, generation_counter, 104, 4);

VALIDATE_TYPE_SIZE_ALIGNMENT(zx_clock_update_args_v1_t, 24, 8);
VALIDATE_FIELD_OFFSET_SIZE(zx_clock_update_args_v1_t, rate_adjust, 0, 4);
VALIDATE_FIELD_OFFSET_SIZE(zx_clock_update_args_v1_t, value, 8, 8);
VALIDATE_FIELD_OFFSET_SIZE(zx_clock_update_args_v1_t, error_bound, 16, 8);

VALIDATE_TYPE_SIZE_ALIGNMENT(zx_exception_info_t, 24, 8);
VALIDATE_FIELD_OFFSET_SIZE(zx_exception_info_t, pid, 0, 8);
VALIDATE_FIELD_OFFSET_SIZE(zx_exception_info_t, tid, 8, 8);
VALIDATE_FIELD_OFFSET_SIZE(zx_exception_info_t, type, 16, 4);

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

VALIDATE_TYPE_SIZE_ALIGNMENT(zx_info_bti_t, 32, 8);
VALIDATE_FIELD_OFFSET_SIZE(zx_info_bti_t, minimum_contiguity, 0, 8);
VALIDATE_FIELD_OFFSET_SIZE(zx_info_bti_t, aspace_size, 8, 8);
VALIDATE_FIELD_OFFSET_SIZE(zx_info_bti_t, pmo_count, 16, 8);
VALIDATE_FIELD_OFFSET_SIZE(zx_info_bti_t, quarantine_count, 24, 8);

VALIDATE_TYPE_SIZE_ALIGNMENT(zx_info_handle_basic_t, 32, 8);
VALIDATE_FIELD_OFFSET_SIZE(zx_info_handle_basic_t, koid, 0, 8);
VALIDATE_FIELD_OFFSET_SIZE(zx_info_handle_basic_t, rights, 8, 4);
VALIDATE_FIELD_OFFSET_SIZE(zx_info_handle_basic_t, type, 12, 4);
VALIDATE_FIELD_OFFSET_SIZE(zx_info_handle_basic_t, related_koid, 16, 8);
VALIDATE_FIELD_OFFSET_SIZE(zx_info_handle_basic_t, reserved, 24, 4);

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

VALIDATE_TYPE_SIZE_ALIGNMENT(zx_info_task_runtime_t, 16, 8);
VALIDATE_FIELD_OFFSET_SIZE(zx_info_task_runtime_t, cpu_time, 0, 8);
VALIDATE_FIELD_OFFSET_SIZE(zx_info_task_runtime_t, queue_time, 8, 8);

VALIDATE_TYPE_SIZE_ALIGNMENT(zx_info_timer_t, 24, 8);
VALIDATE_FIELD_OFFSET_SIZE(zx_info_timer_t, options, 0, 4);
VALIDATE_FIELD_OFFSET_SIZE(zx_info_timer_t, deadline, 8, 8);
VALIDATE_FIELD_OFFSET_SIZE(zx_info_timer_t, slack, 16, 8);

VALIDATE_TYPE_SIZE_ALIGNMENT(zx_info_vmo_t, 120, 8);
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
VALIDATE_FIELD_OFFSET_SIZE(zx_info_vmo_t, metadata_bytes, 104, 8);
VALIDATE_FIELD_OFFSET_SIZE(zx_info_vmo_t, committed_change_events, 112, 8);

VALIDATE_TYPE_SIZE_ALIGNMENT(zx_info_vmo_v1_t, 104, 8);
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

VALIDATE_TYPE_SIZE_ALIGNMENT(zx_pci_init_arg_t, 5896, 8);
VALIDATE_FIELD_OFFSET_SIZE(zx_pci_init_arg_t, dev_pin_to_global_irq, 0, 4096);
VALIDATE_FIELD_OFFSET_SIZE(zx_pci_init_arg_t, num_irqs, 4096, 4);
VALIDATE_FIELD_OFFSET_SIZE(zx_pci_init_arg_t, irqs, 4100, 1792);
VALIDATE_FIELD_OFFSET_SIZE(zx_pci_init_arg_t, addr_window_count, 5892, 4);
VALIDATE_FIELD_OFFSET_SIZE(zx_pci_init_arg_t, addr_windows[0], 5896, 24);
VALIDATE_FIELD_OFFSET_SIZE(zx_pci_init_arg_t, addr_windows[0].base, 5896, 8);
VALIDATE_FIELD_OFFSET_SIZE(zx_pci_init_arg_t, addr_windows[0].size, 5904, 8);
VALIDATE_FIELD_OFFSET_SIZE(zx_pci_init_arg_t, addr_windows[0].bus_start, 5912, 1);
VALIDATE_FIELD_OFFSET_SIZE(zx_pci_init_arg_t, addr_windows[0].bus_end, 5913, 1);
VALIDATE_FIELD_OFFSET_SIZE(zx_pci_init_arg_t, addr_windows[0].cfg_space_type, 5914, 1);
VALIDATE_FIELD_OFFSET_SIZE(zx_pci_init_arg_t, addr_windows[0].has_ecam, 5915, 1);

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

VALIDATE_TYPE_SIZE_ALIGNMENT(zx_policy_timer_slack_t, 16, 8);
VALIDATE_FIELD_OFFSET_SIZE(zx_policy_timer_slack_t, min_slack, 0, 8);
VALIDATE_FIELD_OFFSET_SIZE(zx_policy_timer_slack_t, default_mode, 8, 4);

VALIDATE_TYPE_SIZE_ALIGNMENT(zx_port_packet_t, 48, 8);
VALIDATE_FIELD_OFFSET_SIZE(zx_port_packet_t, key, 0, 8);
VALIDATE_FIELD_OFFSET_SIZE(zx_port_packet_t, type, 8, 4);
VALIDATE_FIELD_OFFSET_SIZE(zx_port_packet_t, status, 12, 4);
VALIDATE_FIELD_OFFSET_SIZE(zx_port_packet_t, user, 16, 32);
VALIDATE_FIELD_OFFSET_SIZE(zx_port_packet_t, signal, 16, 32);
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

VALIDATE_TYPE_SIZE_ALIGNMENT(zx_sched_deadline_params_t, 24, 8);
VALIDATE_FIELD_OFFSET_SIZE(zx_sched_deadline_params_t, capacity, 0, 8);
VALIDATE_FIELD_OFFSET_SIZE(zx_sched_deadline_params_t, relative_deadline, 8, 8);
VALIDATE_FIELD_OFFSET_SIZE(zx_sched_deadline_params_t, period, 16, 8);

VALIDATE_TYPE_SIZE_ALIGNMENT(zx_cpu_set_t, 64, 8);
VALIDATE_FIELD_OFFSET_SIZE(zx_cpu_set_t, mask, 0, 64);

VALIDATE_TYPE_SIZE_ALIGNMENT(zx_profile_info_t, 96, 8);
VALIDATE_FIELD_OFFSET_SIZE(zx_profile_info_t, flags, 0, 4);
VALIDATE_FIELD_OFFSET_SIZE(zx_profile_info_t, priority, 8, 4);
VALIDATE_FIELD_OFFSET_SIZE(zx_profile_info_t, deadline_params, 8, 24);
VALIDATE_FIELD_OFFSET_SIZE(zx_profile_info_t, cpu_affinity_mask, 32, 64);

VALIDATE_TYPE_SIZE_ALIGNMENT(zx_smc_parameters_t, 64, 8);
VALIDATE_FIELD_OFFSET_SIZE(zx_smc_parameters_t, func_id, 0, 4);
VALIDATE_FIELD_OFFSET_SIZE(zx_smc_parameters_t, arg1, 8, 8);
VALIDATE_FIELD_OFFSET_SIZE(zx_smc_parameters_t, arg2, 16, 8);
VALIDATE_FIELD_OFFSET_SIZE(zx_smc_parameters_t, arg3, 24, 8);
VALIDATE_FIELD_OFFSET_SIZE(zx_smc_parameters_t, arg4, 32, 8);
VALIDATE_FIELD_OFFSET_SIZE(zx_smc_parameters_t, arg5, 40, 8);
VALIDATE_FIELD_OFFSET_SIZE(zx_smc_parameters_t, arg6, 48, 8);
VALIDATE_FIELD_OFFSET_SIZE(zx_smc_parameters_t, client_id, 56, 2);
VALIDATE_FIELD_OFFSET_SIZE(zx_smc_parameters_t, secure_os_id, 58, 2);

VALIDATE_TYPE_SIZE_ALIGNMENT(zx_system_powerctl_arg_t, 12, 4);
VALIDATE_FIELD_OFFSET_SIZE(zx_system_powerctl_arg_t, acpi_transition_s_state, 0, 3);
VALIDATE_FIELD_OFFSET_SIZE(zx_system_powerctl_arg_t, acpi_transition_s_state.target_s_state, 0, 1);
VALIDATE_FIELD_OFFSET_SIZE(zx_system_powerctl_arg_t, acpi_transition_s_state.sleep_type_a, 1, 1);
VALIDATE_FIELD_OFFSET_SIZE(zx_system_powerctl_arg_t, acpi_transition_s_state.sleep_type_b, 2, 1);
VALIDATE_FIELD_OFFSET_SIZE(zx_system_powerctl_arg_t, x86_power_limit, 0, 12);
VALIDATE_FIELD_OFFSET_SIZE(zx_system_powerctl_arg_t, x86_power_limit.power_limit, 0, 4);
VALIDATE_FIELD_OFFSET_SIZE(zx_system_powerctl_arg_t, x86_power_limit.time_window, 4, 4);
VALIDATE_FIELD_OFFSET_SIZE(zx_system_powerctl_arg_t, x86_power_limit.clamp, 8, 1);
VALIDATE_FIELD_OFFSET_SIZE(zx_system_powerctl_arg_t, x86_power_limit.enable, 9, 1);

#if defined(__aarch64__)
VALIDATE_TYPE_SIZE_ALIGNMENT(zx_thread_state_debug_regs_t, 528, 8);
VALIDATE_FIELD_OFFSET_SIZE(zx_thread_state_debug_regs_t, hw_bps, 0, 256);
VALIDATE_FIELD_OFFSET_SIZE(zx_thread_state_debug_regs_t, hw_bps[0].dbgbcr, 0, 4);
VALIDATE_FIELD_OFFSET_SIZE(zx_thread_state_debug_regs_t, hw_bps[0].dbgbvr, 8, 8);
VALIDATE_FIELD_OFFSET_SIZE(zx_thread_state_debug_regs_t, hw_wps, 256, 256);
VALIDATE_FIELD_OFFSET_SIZE(zx_thread_state_debug_regs_t, far, 512, 8);
VALIDATE_FIELD_OFFSET_SIZE(zx_thread_state_debug_regs_t, esr, 520, 4);
VALIDATE_FIELD_OFFSET_SIZE(zx_thread_state_debug_regs_t, hw_bps_count, 524, 1);
VALIDATE_FIELD_OFFSET_SIZE(zx_thread_state_debug_regs_t, hw_wps_count, 525, 1);
#elif defined(__x86_64__)
VALIDATE_TYPE_SIZE_ALIGNMENT(zx_thread_state_debug_regs_t, 48, 8);
VALIDATE_FIELD_OFFSET_SIZE(zx_thread_state_debug_regs_t, dr, 0, 32);
VALIDATE_FIELD_OFFSET_SIZE(zx_thread_state_debug_regs_t, dr6, 32, 8);
VALIDATE_FIELD_OFFSET_SIZE(zx_thread_state_debug_regs_t, dr7, 40, 8);
#endif

#if defined(__aarch64__)
VALIDATE_TYPE_SIZE_ALIGNMENT(zx_thread_state_fp_regs_t, 4, 4);
VALIDATE_FIELD_OFFSET_SIZE(zx_thread_state_fp_regs_t, unused, 0, 4);
#elif defined(__x86_64__)
VALIDATE_TYPE_SIZE_ALIGNMENT(zx_thread_state_fp_regs_t, 160, 16);
VALIDATE_FIELD_OFFSET_SIZE(zx_thread_state_fp_regs_t, fcw, 0, 2);
VALIDATE_FIELD_OFFSET_SIZE(zx_thread_state_fp_regs_t, fsw, 2, 2);
VALIDATE_FIELD_OFFSET_SIZE(zx_thread_state_fp_regs_t, ftw, 4, 1);
VALIDATE_FIELD_OFFSET_SIZE(zx_thread_state_fp_regs_t, reserved, 5, 1);
VALIDATE_FIELD_OFFSET_SIZE(zx_thread_state_fp_regs_t, fop, 6, 2);
VALIDATE_FIELD_OFFSET_SIZE(zx_thread_state_fp_regs_t, fip, 8, 8);
VALIDATE_FIELD_OFFSET_SIZE(zx_thread_state_fp_regs_t, fdp, 16, 8);
VALIDATE_FIELD_OFFSET_SIZE(zx_thread_state_fp_regs_t, st, 32, 128);
VALIDATE_FIELD_OFFSET_SIZE(zx_thread_state_fp_regs_t, st[0].low, 32, 8);
VALIDATE_FIELD_OFFSET_SIZE(zx_thread_state_fp_regs_t, st[0].high, 40, 8);
#endif

#if defined(__aarch64__)
VALIDATE_TYPE_SIZE_ALIGNMENT(zx_thread_state_vector_regs_t, 520, 8);
VALIDATE_FIELD_OFFSET_SIZE(zx_thread_state_vector_regs_t, fpcr, 0, 4);
VALIDATE_FIELD_OFFSET_SIZE(zx_thread_state_vector_regs_t, fpsr, 4, 4);
VALIDATE_FIELD_OFFSET_SIZE(zx_thread_state_vector_regs_t, v, 8, 512);
VALIDATE_FIELD_OFFSET_SIZE(zx_thread_state_vector_regs_t, v[0].low, 8, 8);
VALIDATE_FIELD_OFFSET_SIZE(zx_thread_state_vector_regs_t, v[0].high, 16, 8);
#elif defined(__x86_64__)
VALIDATE_TYPE_SIZE_ALIGNMENT(zx_thread_state_vector_regs_t, 2120, 8);
VALIDATE_FIELD_OFFSET_SIZE(zx_thread_state_vector_regs_t, zmm, 0, 2048);
VALIDATE_FIELD_OFFSET_SIZE(zx_thread_state_vector_regs_t, zmm[0].v, 0, 64);
VALIDATE_FIELD_OFFSET_SIZE(zx_thread_state_vector_regs_t, opmask, 2048, 64);
VALIDATE_FIELD_OFFSET_SIZE(zx_thread_state_vector_regs_t, mxcsr, 2112, 4);
#endif

VALIDATE_TYPE_SIZE_ALIGNMENT(zx_vcpu_io_t, 8, 4);
VALIDATE_FIELD_OFFSET_SIZE(zx_vcpu_io_t, access_size, 0, 1);
VALIDATE_FIELD_OFFSET_SIZE(zx_vcpu_io_t, u8, 4, 1);
VALIDATE_FIELD_OFFSET_SIZE(zx_vcpu_io_t, u16, 4, 2);
VALIDATE_FIELD_OFFSET_SIZE(zx_vcpu_io_t, u32, 4, 4);
VALIDATE_FIELD_OFFSET_SIZE(zx_vcpu_io_t, data, 4, 4);

#if defined(__aarch64__)
VALIDATE_TYPE_SIZE_ALIGNMENT(zx_vcpu_state_t, 264, 8);
VALIDATE_FIELD_OFFSET_SIZE(zx_vcpu_state_t, x, 0, 248);
VALIDATE_FIELD_OFFSET_SIZE(zx_vcpu_state_t, sp, 248, 8);
VALIDATE_FIELD_OFFSET_SIZE(zx_vcpu_state_t, cpsr, 256, 4);
#elif defined(__x86_64__)
VALIDATE_TYPE_SIZE_ALIGNMENT(zx_vcpu_state_t, 136, 8);
VALIDATE_FIELD_OFFSET_SIZE(zx_vcpu_state_t, rax, 0, 8);
VALIDATE_FIELD_OFFSET_SIZE(zx_vcpu_state_t, rcx, 8, 8);
VALIDATE_FIELD_OFFSET_SIZE(zx_vcpu_state_t, rdx, 16, 8);
VALIDATE_FIELD_OFFSET_SIZE(zx_vcpu_state_t, rbx, 24, 8);
VALIDATE_FIELD_OFFSET_SIZE(zx_vcpu_state_t, rsp, 32, 8);
VALIDATE_FIELD_OFFSET_SIZE(zx_vcpu_state_t, rbp, 40, 8);
VALIDATE_FIELD_OFFSET_SIZE(zx_vcpu_state_t, rsi, 48, 8);
VALIDATE_FIELD_OFFSET_SIZE(zx_vcpu_state_t, rdi, 56, 8);
VALIDATE_FIELD_OFFSET_SIZE(zx_vcpu_state_t, r8, 64, 8);
VALIDATE_FIELD_OFFSET_SIZE(zx_vcpu_state_t, r9, 72, 8);
VALIDATE_FIELD_OFFSET_SIZE(zx_vcpu_state_t, r10, 80, 8);
VALIDATE_FIELD_OFFSET_SIZE(zx_vcpu_state_t, r11, 88, 8);
VALIDATE_FIELD_OFFSET_SIZE(zx_vcpu_state_t, r12, 96, 8);
VALIDATE_FIELD_OFFSET_SIZE(zx_vcpu_state_t, r13, 104, 8);
VALIDATE_FIELD_OFFSET_SIZE(zx_vcpu_state_t, r14, 112, 8);
VALIDATE_FIELD_OFFSET_SIZE(zx_vcpu_state_t, r15, 120, 8);
VALIDATE_FIELD_OFFSET_SIZE(zx_vcpu_state_t, rflags, 128, 8);
#endif

#undef VALIDATE_TYPE_SIZE_ALIGNMENT
#undef VALIDATE_FIELD_OFFSET_SIZE

#endif  // ZIRCON_KERNEL_LIB_ABI_TYPE_VALIDATOR_INCLUDE_LIB_ABI_TYPE_VALIDATOR_H_
