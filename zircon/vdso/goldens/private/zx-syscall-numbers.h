// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// WARNING: THIS FILE IS MACHINE GENERATED BY //tools/kazoo. DO NOT EDIT.

#define ZX_SYS_bti_create 0
#define ZX_SYS_bti_pin 1
#define ZX_SYS_bti_release_quarantine 2
#define ZX_SYS_channel_call_etc_finish 3
#define ZX_SYS_channel_call_etc_noretry 4
#define ZX_SYS_channel_call_finish 5
#define ZX_SYS_channel_call_noretry 6
#define ZX_SYS_channel_create 7
#define ZX_SYS_channel_read 8
#define ZX_SYS_channel_read_etc 9
#define ZX_SYS_channel_write 10
#define ZX_SYS_channel_write_etc 11
#define ZX_SYS_clock_create 12
#define ZX_SYS_clock_get_details 13
#define ZX_SYS_clock_get_monotonic_via_kernel 14
#define ZX_SYS_clock_read 15
#define ZX_SYS_clock_update 16
#define ZX_SYS_cprng_add_entropy 17
#define ZX_SYS_cprng_draw_once 18
#define ZX_SYS_debug_read 19
#define ZX_SYS_debug_send_command 20
#define ZX_SYS_debug_write 21
#define ZX_SYS_debuglog_create 22
#define ZX_SYS_debuglog_read 23
#define ZX_SYS_debuglog_write 24
#define ZX_SYS_event_create 25
#define ZX_SYS_eventpair_create 26
#define ZX_SYS_exception_get_process 27
#define ZX_SYS_exception_get_thread 28
#define ZX_SYS_fifo_create 29
#define ZX_SYS_fifo_read 30
#define ZX_SYS_fifo_write 31
#define ZX_SYS_framebuffer_get_info 32
#define ZX_SYS_framebuffer_set_range 33
#define ZX_SYS_futex_get_owner 34
#define ZX_SYS_futex_requeue 35
#define ZX_SYS_futex_requeue_single_owner 36
#define ZX_SYS_futex_wait 37
#define ZX_SYS_futex_wake 38
#define ZX_SYS_futex_wake_single_owner 39
#define ZX_SYS_guest_create 40
#define ZX_SYS_guest_set_trap 41
#define ZX_SYS_handle_close 42
#define ZX_SYS_handle_close_many 43
#define ZX_SYS_handle_duplicate 44
#define ZX_SYS_handle_replace 45
#define ZX_SYS_interrupt_ack 46
#define ZX_SYS_interrupt_bind 47
#define ZX_SYS_interrupt_create 48
#define ZX_SYS_interrupt_destroy 49
#define ZX_SYS_interrupt_trigger 50
#define ZX_SYS_interrupt_wait 51
#define ZX_SYS_iommu_create 52
#define ZX_SYS_ioports_release 53
#define ZX_SYS_ioports_request 54
#define ZX_SYS_job_create 55
#define ZX_SYS_job_set_critical 56
#define ZX_SYS_job_set_policy 57
#define ZX_SYS_ktrace_control 58
#define ZX_SYS_ktrace_read 59
#define ZX_SYS_ktrace_write 60
#define ZX_SYS_msi_allocate 61
#define ZX_SYS_msi_create 62
#define ZX_SYS_mtrace_control 63
#define ZX_SYS_nanosleep 64
#define ZX_SYS_object_get_child 65
#define ZX_SYS_object_get_info 66
#define ZX_SYS_object_get_property 67
#define ZX_SYS_object_set_profile 68
#define ZX_SYS_object_set_property 69
#define ZX_SYS_object_signal 70
#define ZX_SYS_object_signal_peer 71
#define ZX_SYS_object_wait_async 72
#define ZX_SYS_object_wait_many 73
#define ZX_SYS_object_wait_one 74
#define ZX_SYS_pager_create 75
#define ZX_SYS_pager_create_vmo 76
#define ZX_SYS_pager_detach_vmo 77
#define ZX_SYS_pager_op_range 78
#define ZX_SYS_pager_query_dirty_ranges 79
#define ZX_SYS_pager_query_vmo_stats 80
#define ZX_SYS_pager_supply_pages 81
#define ZX_SYS_pc_firmware_tables 82
#define ZX_SYS_pci_add_subtract_io_range 83
#define ZX_SYS_pci_cfg_pio_rw 84
#define ZX_SYS_pci_config_read 85
#define ZX_SYS_pci_config_write 86
#define ZX_SYS_pci_enable_bus_master 87
#define ZX_SYS_pci_get_bar 88
#define ZX_SYS_pci_get_nth_device 89
#define ZX_SYS_pci_init 90
#define ZX_SYS_pci_map_interrupt 91
#define ZX_SYS_pci_query_irq_mode 92
#define ZX_SYS_pci_reset_device 93
#define ZX_SYS_pci_set_irq_mode 94
#define ZX_SYS_pmt_unpin 95
#define ZX_SYS_port_cancel 96
#define ZX_SYS_port_create 97
#define ZX_SYS_port_queue 98
#define ZX_SYS_port_wait 99
#define ZX_SYS_process_create 100
#define ZX_SYS_process_create_shared 101
#define ZX_SYS_process_exit 102
#define ZX_SYS_process_read_memory 103
#define ZX_SYS_process_start 104
#define ZX_SYS_process_write_memory 105
#define ZX_SYS_profile_create 106
#define ZX_SYS_resource_create 107
#define ZX_SYS_restricted_enter 108
#define ZX_SYS_restricted_read_state 109
#define ZX_SYS_restricted_write_state 110
#define ZX_SYS_smc_call 111
#define ZX_SYS_socket_create 112
#define ZX_SYS_socket_read 113
#define ZX_SYS_socket_set_disposition 114
#define ZX_SYS_socket_write 115
#define ZX_SYS_stream_create 116
#define ZX_SYS_stream_readv 117
#define ZX_SYS_stream_readv_at 118
#define ZX_SYS_stream_seek 119
#define ZX_SYS_stream_writev 120
#define ZX_SYS_stream_writev_at 121
#define ZX_SYS_syscall_next_1 122
#define ZX_SYS_syscall_test_0 123
#define ZX_SYS_syscall_test_1 124
#define ZX_SYS_syscall_test_2 125
#define ZX_SYS_syscall_test_3 126
#define ZX_SYS_syscall_test_4 127
#define ZX_SYS_syscall_test_5 128
#define ZX_SYS_syscall_test_6 129
#define ZX_SYS_syscall_test_7 130
#define ZX_SYS_syscall_test_8 131
#define ZX_SYS_syscall_test_handle_create 132
#define ZX_SYS_syscall_test_widening_signed_narrow 133
#define ZX_SYS_syscall_test_widening_signed_wide 134
#define ZX_SYS_syscall_test_widening_unsigned_narrow 135
#define ZX_SYS_syscall_test_widening_unsigned_wide 136
#define ZX_SYS_syscall_test_wrapper 137
#define ZX_SYS_system_get_event 138
#define ZX_SYS_system_get_performance_info 139
#define ZX_SYS_system_mexec 140
#define ZX_SYS_system_mexec_payload_get 141
#define ZX_SYS_system_powerctl 142
#define ZX_SYS_system_set_performance_info 143
#define ZX_SYS_task_create_exception_channel 144
#define ZX_SYS_task_kill 145
#define ZX_SYS_task_suspend 146
#define ZX_SYS_task_suspend_token 147
#define ZX_SYS_thread_create 148
#define ZX_SYS_thread_exit 149
#define ZX_SYS_thread_legacy_yield 150
#define ZX_SYS_thread_read_state 151
#define ZX_SYS_thread_start 152
#define ZX_SYS_thread_write_state 153
#define ZX_SYS_ticks_get_via_kernel 154
#define ZX_SYS_timer_cancel 155
#define ZX_SYS_timer_create 156
#define ZX_SYS_timer_set 157
#define ZX_SYS_vcpu_create 158
#define ZX_SYS_vcpu_enter 159
#define ZX_SYS_vcpu_interrupt 160
#define ZX_SYS_vcpu_kick 161
#define ZX_SYS_vcpu_read_state 162
#define ZX_SYS_vcpu_write_state 163
#define ZX_SYS_vmar_allocate 164
#define ZX_SYS_vmar_destroy 165
#define ZX_SYS_vmar_map 166
#define ZX_SYS_vmar_op_range 167
#define ZX_SYS_vmar_protect 168
#define ZX_SYS_vmar_unmap 169
#define ZX_SYS_vmo_create 170
#define ZX_SYS_vmo_create_child 171
#define ZX_SYS_vmo_create_contiguous 172
#define ZX_SYS_vmo_create_physical 173
#define ZX_SYS_vmo_get_size 174
#define ZX_SYS_vmo_op_range 175
#define ZX_SYS_vmo_read 176
#define ZX_SYS_vmo_replace_as_executable 177
#define ZX_SYS_vmo_set_cache_policy 178
#define ZX_SYS_vmo_set_size 179
#define ZX_SYS_vmo_write 180
#define ZX_SYS_COUNT 181
