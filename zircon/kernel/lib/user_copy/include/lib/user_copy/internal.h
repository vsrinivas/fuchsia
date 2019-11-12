// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ZIRCON_KERNEL_LIB_USER_COPY_INCLUDE_LIB_USER_COPY_INTERNAL_H_
#define ZIRCON_KERNEL_LIB_USER_COPY_INCLUDE_LIB_USER_COPY_INTERNAL_H_

#include <stddef.h>
#include <zircon/syscalls/clock.h>
#include <zircon/syscalls/debug.h>
#include <zircon/syscalls/exception.h>
#include <zircon/syscalls/hypervisor.h>
#include <zircon/syscalls/object.h>
#include <zircon/syscalls/pci.h>
#include <zircon/syscalls/policy.h>
#include <zircon/syscalls/port.h>
#include <zircon/syscalls/smc.h>
#include <zircon/syscalls/types.h>
#include <zircon/types.h>

#include <arch/arch_perfmon.h>
#include <ktl/type_traits.h>

namespace internal {

typedef ktl::remove_reference_t<decltype(zx_pci_init_arg_t::addr_windows[0])> zx_pci_addr_window_t;

// Generates a type whose ::value is true if |T| is on the user_ptr exception list.
//
// The user_ptr exception list is a list of kernel ABI types that have implicit padding, are not
// trivial (C++'s TrivialType), or don't have a standard-layout (C++'s StandardLayoutType), but are
// allowed to be copied to/from usermode.
//
// The purpose of this list is to prevent the use of new types that are not ABI-safe while
// continuing to allow existing code to function.
//
// Eventually, this exception list should be empty.
template <typename T>
struct is_on_user_ptr_exception_list
    : ktl::is_one_of<
          ktl::remove_const_t<T>, ArchPmuConfig, ArchPmuProperties, zx_clock_details_v1_t,
          zx_clock_update_args_v1_t, zx_exception_info_t, zx_exception_report_t, zx_info_bti_t,
          zx_info_handle_basic_t, zx_info_job_t, zx_info_maps_mapping_t, zx_info_maps_t,
          zx_info_process_t, zx_info_socket_t, zx_info_thread_stats_t, zx_info_timer_t,
          zx_info_vmo_t, zx_pci_bar_t, zx_pci_init_arg_t, zx_pci_addr_window_t,
          zx_pcie_device_info_t, zx_policy_timer_slack_t, zx_port_packet_t, zx_profile_info_t,
          zx_smc_parameters_t, zx_system_powerctl_arg_t, zx_thread_state_debug_regs_t,
          zx_thread_state_fp_regs_t, zx_thread_state_vector_regs_t, zx_vcpu_io_t, zx_vcpu_state_t> {
};

// Generates a type whose ::value is true if |T| is allowed to be copied to/from usermode.
//
// The purpose of this type trait is to ensure a stable ABI and prevent bugs by restricting the
// types that may be copied to/from usermode. These types must:
//
//   * Be trivial and can be trivially copied.
//
//   * Have a standard-layout, which ensures their layout won't change from compiler to compiler.
//
//   * Have unique object representations, which ensures they do not contain implicit
//   padding. Copying types with implicit padding can lead information disclosure bugs because the
//   padding may or may not contain uninitialized data.
//
// Exception: We make an exception for existing ABI types that either are not PODs or have implicit
// padding. See |is_on_user_ptr_exception_list|.
template <typename T>
struct is_copy_allowed
    : ktl::disjunction<ktl::conjunction<ktl::is_trivial<T>, ktl::is_standard_layout<T>,
                                        ktl::has_unique_object_representations<T>>,
                       is_on_user_ptr_exception_list<T>> {};

}  // namespace internal

#endif  // ZIRCON_KERNEL_LIB_USER_COPY_INCLUDE_LIB_USER_COPY_INTERNAL_H_
