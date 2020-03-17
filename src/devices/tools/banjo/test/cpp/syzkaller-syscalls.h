// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// WARNING: THIS FILE IS MACHINE GENERATED. DO NOT EDIT.
// Generated from the banjo.examples.syzkaller.syscalls banjo file

#pragma once

#include <banjo/examples/syzkaller/syscalls.h>
#include <ddk/driver.h>
#include <ddktl/device-internal.h>
#include <lib/zx/bti.h>
#include <lib/zx/channel.h>
#include <lib/zx/debuglog.h>
#include <lib/zx/event.h>
#include <lib/zx/eventpair.h>
#include <lib/zx/fifo.h>
#include <lib/zx/guest.h>
#include <lib/zx/handle.h>
#include <lib/zx/interrupt.h>
#include <lib/zx/iommu.h>
#include <lib/zx/job.h>
#include <lib/zx/pager.h>
#include <lib/zx/pmt.h>
#include <lib/zx/port.h>
#include <lib/zx/process.h>
#include <lib/zx/profile.h>
#include <lib/zx/resource.h>
#include <lib/zx/socket.h>
#include <lib/zx/thread.h>
#include <lib/zx/timer.h>
#include <lib/zx/vcpu.h>
#include <lib/zx/vmar.h>
#include <lib/zx/vmo.h>
#include <zircon/assert.h>
#include <zircon/compiler.h>
#include <zircon/types.h>

#include "syscalls-internal.h"

// DDK syscalls-protocol support
//
// :: Proxies ::
//
// ddk::ApiProtocolClient is a simple wrapper around
// api_protocol_t. It does not own the pointers passed to it.
//
// :: Mixins ::
//
// ddk::ApiProtocol is a mixin class that simplifies writing DDK drivers
// that implement the api protocol. It doesn't set the base protocol.
//
// :: Examples ::
//
// // A driver that implements a ZX_PROTOCOL_API device.
// class ApiDevice;
// using ApiDeviceType = ddk::Device<ApiDevice, /* ddk mixins */>;
//
// class ApiDevice : public ApiDeviceType,
//                      public ddk::ApiProtocol<ApiDevice> {
//   public:
//     ApiDevice(zx_device_t* parent)
//         : ApiDeviceType(parent) {}
//
//     zx_status_t Apiclock_get(zx_clock_t clock_id, zx_time_t* out_out);
//
//     zx_status_t Apiclock_get_new(zx_clock_t clock_id, zx_time_t* out_out);
//
//     zx_time_t Apiclock_get_monotonic();
//
//     zx_status_t Apinanosleep(zx_time_t deadline);
//
//     zx_ticks_t Apiticks_get();
//
//     zx_ticks_t Apiticks_per_second();
//
//     zx_time_t Apideadline_after(zx_duration_t nanoseconds);
//
//     zx_status_t Apiclock_adjust(zx::resource handle, zx_clock_t clock_id, int64_t offset);
//
//     uint32_t Apisystem_get_dcache_line_size();
//
//     uint32_t Apisystem_get_num_cpus();
//
//     zx_status_t Apisystem_get_version(const char* version, size_t version_size);
//
//     uint64_t Apisystem_get_physmem();
//
//     zx_status_t Apisystem_get_features(uint32_t kind, uint32_t* out_features);
//
//     zx_status_t Apisystem_get_event(zx::job root_job, uint32_t kind, zx::event* out_event);
//
//     zx_status_t Apicache_flush(const void addr[size], size_t size, uint32_t options);
//
//     zx_status_t Apihandle_close(zx::handle handle);
//
//     zx_status_t Apihandle_close_many(const zx::handle handles[num_handles], size_t num_handles);
//
//     zx_status_t Apihandle_duplicate(zx::handle handle, zx_rights_t rights, zx::handle* out_out);
//
//     zx_status_t Apihandle_replace(zx::handle handle, zx_rights_t rights, zx::handle* out_out);
//
//     zx_status_t Apiobject_wait_one(zx::handle handle, zx_signals_t signals, zx_time_t deadline, zx_signals_t* out_observed);
//
//     zx_status_t Apiobject_wait_many(const zx_wait_item_t items[count], size_t count, zx_time_t deadline);
//
//     zx_status_t Apiobject_wait_async(zx::handle handle, zx::port port, uint64_t key, zx_signals_t signals, uint32_t options);
//
//     zx_status_t Apiobject_signal(zx::handle handle, uint32_t clear_mask, uint32_t set_mask);
//
//     zx_status_t Apiobject_signal_peer(zx::handle handle, uint32_t clear_mask, uint32_t set_mask);
//
//     zx_status_t Apiobject_get_property(zx::handle handle, uint32_t property, const void value[value_size], size_t value_size);
//
//     zx_status_t Apiobject_set_property(zx::handle handle, uint32_t property, const void value[value_size], size_t value_size);
//
//     zx_status_t Apiobject_get_info(zx::handle handle, uint32_t topic, const void buffer[buffer_size], size_t buffer_size, size_t* out_actual, size_t* out_avail);
//
//     zx_status_t Apiobject_get_child(zx::handle handle, zx_koid_t koid, zx_rights_t rights, zx::handle* out_out);
//
//     zx_status_t Apiobject_set_profile(zx::thread handle, zx::profile profile, uint32_t options);
//
//     zx_status_t Apisocket_create(uint32_t options, zx::socket* out_out0, zx::socket* out_out1);
//
//     zx_status_t Apisocket_write(zx::socket handle, uint32_t options, const void buffer[buffer_size], size_t buffer_size, size_t* out_actual);
//
//     zx_status_t Apisocket_read(zx::socket handle, uint32_t options, const void buffer[buffer_size], size_t buffer_size, size_t* out_actual);
//
//     zx_status_t Apisocket_share(zx::socket handle, zx::socket socket_to_share);
//
//     zx_status_t Apisocket_accept(zx::socket handle, zx::socket* out_out_socket);
//
//     zx_status_t Apisocket_shutdown(zx::socket handle, uint32_t options);
//
//     void Apithread_exit();
//
//     zx_status_t Apithread_create(zx::process process, const char* name, size_t name_size, uint32_t options, zx::thread* out_out);
//
//     zx_status_t Apithread_start(zx::thread handle, zx_vaddr_t thread_entry, zx_vaddr_t stack, size_t arg1, size_t arg2);
//
//     zx_status_t Apithread_read_state(zx::thread handle, uint32_t kind, const void buffer[buffer_size], size_t buffer_size);
//
//     zx_status_t Apithread_write_state(zx::thread handle, uint32_t kind, const void buffer[buffer_size], size_t buffer_size);
//
//     void Apiprocess_exit(int64_t retcode);
//
//     zx_status_t Apiprocess_create(zx::job job, const char* name, size_t name_size, uint32_t options, zx::process* out_proc_handle, zx::vmar* out_vmar_handle);
//
//     zx_status_t Apiprocess_start(zx::process handle, zx::thread thread, zx_vaddr_t entry, zx_vaddr_t stack, zx::handle arg1, size_t arg2);
//
//     zx_status_t Apiprocess_read_memory(zx::process handle, zx_vaddr_t vaddr, const void buffer[buffer_size], size_t buffer_size, size_t* out_actual);
//
//     zx_status_t Apiprocess_write_memory(zx::process handle, zx_vaddr_t vaddr, const void buffer[buffer_size], size_t buffer_size, size_t* out_actual);
//
//     zx_status_t Apijob_create(zx::job parent_job, uint32_t options, zx::job* out_out);
//
//     zx_status_t Apijob_set_policy(zx::job handle, uint32_t options, uint32_t topic, const void policy[count], uint32_t count);
//
//     zx_status_t Apitask_suspend(zx::handle handle, zx::handle* out_token);
//
//     zx_status_t Apitask_suspend_token(zx::handle handle, zx::handle* out_token);
//
//     zx_status_t Apitask_create_exception_channel(zx::handle handle, uint32_t options, zx::channel* out_out);
//
//     zx_status_t Apitask_kill(zx::handle handle);
//
//     zx_status_t Apiexception_get_thread(zx::handle handle, zx::thread* out_out);
//
//     zx_status_t Apiexception_get_process(zx::handle handle, zx::process* out_out);
//
//     zx_status_t Apievent_create(uint32_t options, zx::event* out_out);
//
//     zx_status_t Apieventpair_create(uint32_t options, zx::eventpair* out_out0, zx::eventpair* out_out1);
//
//     zx_status_t Apifutex_wait(const int32_t value_ptr[1], int32_t current_value, zx::handle new_futex_owner, zx_time_t deadline);
//
//     zx_status_t Apifutex_wake(const int32_t value_ptr[1], uint32_t wake_count);
//
//     zx_status_t Apifutex_requeue(const int32_t value_ptr[1], uint32_t wake_count, int32_t current_value, const int32_t requeue_ptr[1], uint32_t requeue_count, zx::handle new_requeue_owner);
//
//     zx_status_t Apifutex_wake_single_owner(const int32_t value_ptr[1]);
//
//     zx_status_t Apifutex_requeue_single_owner(const int32_t value_ptr[1], int32_t current_value, const int32_t requeue_ptr[1], uint32_t requeue_count, zx::handle new_requeue_owner);
//
//     zx_status_t Apifutex_get_owner(const int32_t value_ptr[1], const zx_koid_t koid[1]);
//
//     zx_status_t Apiport_create(uint32_t options, zx::port* out_out);
//
//     zx_status_t Apiport_queue(zx::port handle, const zx_port_packet_t packet[1]);
//
//     zx_status_t Apiport_wait(zx::port handle, zx_time_t deadline, const zx_port_packet_t packet[1]);
//
//     zx_status_t Apiport_cancel(zx::port handle, zx::handle source, uint64_t key);
//
//     zx_status_t Apitimer_create(uint32_t options, zx_clock_t clock_id, zx::timer* out_out);
//
//     zx_status_t Apitimer_set(zx::timer handle, zx_time_t deadline, zx_duration_t slack);
//
//     zx_status_t Apitimer_cancel(zx::timer handle);
//
//     zx_status_t Apivmo_create(uint64_t size, uint32_t options, zx::vmo* out_out);
//
//     zx_status_t Apivmo_read(zx::vmo handle, const void buffer[buffer_size], uint64_t offset, size_t buffer_size);
//
//     zx_status_t Apivmo_write(zx::vmo handle, const void buffer[buffer_size], uint64_t offset, size_t buffer_size);
//
//     zx_status_t Apivmo_get_size(zx::vmo handle, uint64_t* out_size);
//
//     zx_status_t Apivmo_set_size(zx::vmo handle, uint64_t size);
//
//     zx_status_t Apivmo_op_range(zx::vmo handle, uint32_t op, uint64_t offset, uint64_t size, const void buffer[buffer_size], size_t buffer_size);
//
//     zx_status_t Apivmo_create_child(zx::vmo handle, uint32_t options, uint64_t offset, uint64_t size, zx::vmo* out_out);
//
//     zx_status_t Apivmo_set_cache_policy(zx::vmo handle, uint32_t cache_policy);
//
//     zx_status_t Apivmo_replace_as_executable(zx::vmo handle, zx::resource vmex, zx::vmo* out_out);
//
//     zx_status_t Apivmar_allocate(zx::vmar parent_vmar, zx_vm_option_t options, uint64_t offset, uint64_t size, zx::vmar* out_child_vmar, zx_vaddr_t* out_child_addr);
//
//     zx_status_t Apivmar_destroy(zx::vmar handle);
//
//     zx_status_t Apivmar_map(zx::vmar handle, zx_vm_option_t options, uint64_t vmar_offset, zx::vmo vmo, uint64_t vmo_offset, uint64_t len, zx_vaddr_t* out_mapped_addr);
//
//     zx_status_t Apivmar_unmap(zx::vmo handle, zx_vaddr_t addr, uint64_t len);
//
//     zx_status_t Apivmar_protect(zx::vmo handle, zx_vm_option_t options, zx_vaddr_t addr, uint64_t len);
//
//     zx_status_t Apicprng_draw_once(const void buffer[buffer_size], size_t buffer_size);
//
//     void Apicprng_draw(const void buffer[buffer_size], size_t buffer_size);
//
//     zx_status_t Apicprng_add_entropy(const void buffer[buffer_size], size_t buffer_size);
//
//     zx_status_t Apififo_create(size_t elem_count, size_t elem_size, uint32_t options, zx::fifo* out_out0, zx::fifo* out_out1);
//
//     zx_status_t Apififo_read(zx::fifo handle, size_t elem_size, const void data[N], size_t count, size_t* out_actual_count);
//
//     zx_status_t Apififo_write(zx::fifo handle, size_t elem_size, const void data[N], size_t count, size_t* out_actual_count);
//
//     zx_status_t Apiprofile_create(zx::job root_job, uint32_t options, const zx_profile_info_t profile[1], zx::profile* out_out);
//
//     zx_status_t Apivmar_unmap_handle_close_thread_exit(zx::vmar vmar_handle, zx_vaddr_t addr, size_t size, zx::handle close_handle);
//
//     void Apifutex_wake_handle_close_thread_exit(const int32_t value_ptr[1], uint32_t wake_count, int32_t new_value, zx::handle close_handle);
//
//     zx_status_t Apidebuglog_create(zx::resource resource, uint32_t options, zx::debuglog* out_out);
//
//     zx_status_t Apidebuglog_write(zx::debuglog handle, uint32_t options, const void buffer[buffer_size], size_t buffer_size);
//
//     zx_status_t Apidebuglog_read(zx::debuglog handle, uint32_t options, const void buffer[buffer_size], size_t buffer_size);
//
//     zx_status_t Apiktrace_read(zx::resource handle, const void data[data_size], uint32_t offset, size_t data_size, size_t* out_actual);
//
//     zx_status_t Apiktrace_control(zx::resource handle, uint32_t action, uint32_t options, const void ptr[action]);
//
//     zx_status_t Apiktrace_write(zx::resource handle, uint32_t id, uint32_t arg0, uint32_t arg1);
//
//     zx_status_t Apimtrace_control(zx::resource handle, uint32_t kind, uint32_t action, uint32_t options, const void ptr[ptr_size], size_t ptr_size);
//
//     zx_status_t Apidebug_read(zx::resource handle, const char* buffer, const size_t buffer_size[1]);
//
//     zx_status_t Apidebug_write(const char* buffer, size_t buffer_size);
//
//     zx_status_t Apidebug_send_command(zx::resource resource, const char* buffer, size_t buffer_size);
//
//     zx_status_t Apiinterrupt_create(zx::resource src_obj, uint32_t src_num, uint32_t options, zx::interrupt* out_out_handle);
//
//     zx_status_t Apiinterrupt_bind(zx::interrupt handle, zx::port port_handle, uint64_t key, uint32_t options);
//
//     zx_status_t Apiinterrupt_wait(zx::interrupt handle, zx_time_t* out_out_timestamp);
//
//     zx_status_t Apiinterrupt_destroy(zx::interrupt handle);
//
//     zx_status_t Apiinterrupt_ack(zx::interrupt handle);
//
//     zx_status_t Apiinterrupt_trigger(zx::interrupt handle, uint32_t options, zx_time_t timestamp);
//
//     zx_status_t Apiinterrupt_bind_vcpu(zx::interrupt handle, zx::vcpu vcpu, uint32_t options);
//
//     zx_status_t Apiioports_request(zx::resource resource, uint16_t io_addr, uint32_t len);
//
//     zx_status_t Apivmo_create_contiguous(zx::bti bti, size_t size, uint32_t alignment_log2, zx::vmo* out_out);
//
//     zx_status_t Apivmo_create_physical(zx::vmo resource, zx_paddr_t paddr, size_t size, zx::vmo* out_out);
//
//     zx_status_t Apiiommu_create(zx::resource resource, uint32_t type, const void desc[desc_size], size_t desc_size, zx::iommu* out_out);
//
//     zx_status_t Apibti_create(zx::iommu iommu, uint32_t options, uint64_t bti_id, zx::bti* out_out);
//
//     zx_status_t Apibti_pin(zx::bti handle, uint32_t options, zx::vmo vmo, uint64_t offset, uint64_t size, const zx_paddr_t addrs[addrs_count], size_t addrs_count, zx::pmt* out_pmt);
//
//     zx_status_t Apibti_release_quarantine(zx::bti handle);
//
//     zx_status_t Apipmt_unpin(zx::pmt handle);
//
//     zx_status_t Apiframebuffer_get_info(zx::resource resource, uint32_t* out_format, uint32_t* out_width, uint32_t* out_height, uint32_t* out_stride);
//
//     zx_status_t Apiframebuffer_set_range(zx::resource resource, zx::vmo vmo, uint32_t len, uint32_t format, uint32_t width, uint32_t height, uint32_t stride);
//
//     zx_status_t Apipci_get_nth_device(zx::resource handle, uint32_t index, zx_pcie_device_info_t* out_out_info, zx::handle* out_out_handle);
//
//     zx_status_t Apipci_enable_bus_master(zx::handle handle, bool enable);
//
//     zx_status_t Apipci_reset_device(zx::handle handle);
//
//     zx_status_t Apipci_config_read(zx::handle handle, uint16_t offset, size_t width, const uint32_t out_val[1]);
//
//     zx_status_t Apipci_config_write(zx::handle handle, uint16_t offset, size_t width, uint32_t val);
//
//     zx_status_t Apipci_cfg_pio_rw(zx::resource handle, uint8_t bus, uint8_t dev, uint8_t func, uint8_t offset, const uint32_t val[1], size_t width, bool write);
//
//     zx_status_t Apipci_get_bar(zx::handle handle, uint32_t bar_num, const zx_pci_bar_t out_bar[1], zx::handle* out_out_handle);
//
//     zx_status_t Apipci_map_interrupt(zx::handle handle, int32_t which_irq, zx::handle* out_out_handle);
//
//     zx_status_t Apipci_query_irq_mode(zx::handle handle, uint32_t mode, uint32_t* out_out_max_irqs);
//
//     zx_status_t Apipci_set_irq_mode(zx::handle handle, uint32_t mode, uint32_t requested_irq_count);
//
//     zx_status_t Apipci_init(zx::resource handle, const zx_pci_init_arg_t init_buf[len], uint32_t len);
//
//     zx_status_t Apipci_add_subtract_io_range(zx::resource handle, bool mmio, uint64_t base, uint64_t len, bool add);
//
//     zx_status_t Apipc_firmware_tables(zx::resource handle, zx_paddr_t* out_acpi_rsdp, zx_paddr_t* out_smbios);
//
//     zx_status_t Apismc_call(zx::handle handle, const zx_smc_parameters_t parameters[1], zx_smc_result_t* out_out_smc_result);
//
//     zx_status_t Apiresource_create(zx::resource parent_rsrc, uint32_t options, uint64_t base, size_t size, const char* name, size_t name_size, zx::resource* out_resource_out);
//
//     zx_status_t Apiguest_create(zx::resource resource, uint32_t options, zx::guest* out_guest_handle, zx::vmar* out_vmar_handle);
//
//     zx_status_t Apiguest_set_trap(zx::guest handle, uint32_t kind, zx_vaddr_t addr, size_t size, zx::port port_handle, uint64_t key);
//
//     zx_status_t Apivcpu_create(zx::guest guest, uint32_t options, zx_vaddr_t entry, zx::vcpu* out_out);
//
//     zx_status_t Apivcpu_resume(zx::vcpu handle, zx_port_packet_t* out_packet);
//
//     zx_status_t Apivcpu_interrupt(zx::vcpu handle, uint32_t vector);
//
//     zx_status_t Apivcpu_read_state(zx::handle handle, uint32_t kind, const void buffer[buffer_size], size_t buffer_size);
//
//     zx_status_t Apivcpu_write_state(zx::handle handle, uint32_t kind, const void buffer[buffer_size], size_t buffer_size);
//
//     zx_status_t Apisystem_mexec(zx::resource resource, zx::vmo kernel_vmo, zx::vmo bootimage_vmo);
//
//     zx_status_t Apisystem_mexec_payload_get(zx::resource resource, const void buffer[buffer_size], size_t buffer_size);
//
//     zx_status_t Apisystem_powerctl(zx::resource resource, uint32_t cmd, const zx_system_powerctl_arg_t arg[1]);
//
//     zx_status_t Apipager_create(uint32_t options, zx::pager* out_out);
//
//     zx_status_t Apipager_create_vmo(zx::pager pager, uint32_t options, zx::port port, uint64_t key, uint64_t size, zx::vmo* out_out);
//
//     zx_status_t Apipager_detach_vmo(zx::pager pager, zx::vmo vmo);
//
//     zx_status_t Apipager_supply_pages(zx::pager pager, zx::vmo pager_vmo, uint64_t offset, uint64_t length, zx::vmo aux_vmo, uint64_t aux_offset);
//
//     ...
// };

namespace ddk {
