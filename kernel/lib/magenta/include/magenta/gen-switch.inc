// Copyright 2017 The Fuchsia Authors. All rights reserved.
// This is a GENERATED file. The license governing this file can be found in the LICENSE file.

    case 0: ret = static_cast<uint64_t>(sys_time_get(
        static_cast<uint32_t>(arg1)));
        break;
    case 1: ret = static_cast<uint64_t>(sys_nanosleep(
        static_cast<mx_time_t>(arg1)));
        break;
    case 2: ret = static_cast<uint64_t>(sys_handle_close(
        static_cast<mx_handle_t>(arg1)));
        break;
    case 3: ret = static_cast<uint64_t>(sys_handle_duplicate(
        static_cast<mx_handle_t>(arg1),
        static_cast<mx_rights_t>(arg2),
        reinterpret_cast<mx_handle_t*>(arg3)));
        break;
    case 4: ret = static_cast<uint64_t>(sys_handle_replace(
        static_cast<mx_handle_t>(arg1),
        static_cast<mx_rights_t>(arg2),
        reinterpret_cast<mx_handle_t*>(arg3)));
        break;
    case 5: ret = static_cast<uint64_t>(sys_object_wait_one(
        static_cast<mx_handle_t>(arg1),
        static_cast<mx_signals_t>(arg2),
        static_cast<mx_time_t>(arg3),
        reinterpret_cast<mx_signals_t*>(arg4)));
        break;
    case 6: ret = static_cast<uint64_t>(sys_object_wait_many(
        reinterpret_cast<mx_wait_item_t*>(arg1),
        static_cast<uint32_t>(arg2),
        static_cast<mx_time_t>(arg3)));
        break;
    case 7: ret = static_cast<uint64_t>(sys_object_wait_async(
        static_cast<mx_handle_t>(arg1),
        static_cast<mx_handle_t>(arg2),
        static_cast<uint64_t>(arg3),
        static_cast<mx_signals_t>(arg4),
        static_cast<uint32_t>(arg5)));
        break;
    case 8: ret = static_cast<uint64_t>(sys_object_signal(
        static_cast<mx_handle_t>(arg1),
        static_cast<uint32_t>(arg2),
        static_cast<uint32_t>(arg3)));
        break;
    case 9: ret = static_cast<uint64_t>(sys_object_signal_peer(
        static_cast<mx_handle_t>(arg1),
        static_cast<uint32_t>(arg2),
        static_cast<uint32_t>(arg3)));
        break;
    case 10: ret = static_cast<uint64_t>(sys_object_get_property(
        static_cast<mx_handle_t>(arg1),
        static_cast<uint32_t>(arg2),
        reinterpret_cast<void*>(arg3),
        static_cast<size_t>(arg4)));
        break;
    case 11: ret = static_cast<uint64_t>(sys_object_set_property(
        static_cast<mx_handle_t>(arg1),
        static_cast<uint32_t>(arg2),
        reinterpret_cast<const void*>(arg3),
        static_cast<size_t>(arg4)));
        break;
    case 12: ret = static_cast<uint64_t>(sys_object_get_info(
        static_cast<mx_handle_t>(arg1),
        static_cast<uint32_t>(arg2),
        reinterpret_cast<void*>(arg3),
        static_cast<size_t>(arg4),
        reinterpret_cast<size_t*>(arg5),
        reinterpret_cast<size_t*>(arg6)));
        break;
    case 13: ret = static_cast<uint64_t>(sys_object_get_child(
        static_cast<mx_handle_t>(arg1),
        static_cast<uint64_t>(arg2),
        static_cast<mx_rights_t>(arg3),
        reinterpret_cast<mx_handle_t*>(arg4)));
        break;
    case 14: ret = static_cast<uint64_t>(sys_channel_create(
        static_cast<uint32_t>(arg1),
        reinterpret_cast<mx_handle_t*>(arg2),
        reinterpret_cast<mx_handle_t*>(arg3)));
        break;
    case 15: ret = static_cast<uint64_t>(sys_channel_read(
        static_cast<mx_handle_t>(arg1),
        static_cast<uint32_t>(arg2),
        reinterpret_cast<void*>(arg3),
        static_cast<uint32_t>(arg4),
        reinterpret_cast<uint32_t*>(arg5),
        reinterpret_cast<mx_handle_t*>(arg6),
        static_cast<uint32_t>(arg7),
        reinterpret_cast<uint32_t*>(arg8)));
        break;
    case 16: ret = static_cast<uint64_t>(sys_channel_write(
        static_cast<mx_handle_t>(arg1),
        static_cast<uint32_t>(arg2),
        reinterpret_cast<const void*>(arg3),
        static_cast<uint32_t>(arg4),
        reinterpret_cast<const mx_handle_t*>(arg5),
        static_cast<uint32_t>(arg6)));
        break;
    case 17: ret = static_cast<uint64_t>(sys_channel_call(
        static_cast<mx_handle_t>(arg1),
        static_cast<uint32_t>(arg2),
        static_cast<mx_time_t>(arg3),
        reinterpret_cast<const mx_channel_call_args_t*>(arg4),
        reinterpret_cast<uint32_t*>(arg5),
        reinterpret_cast<uint32_t*>(arg6),
        reinterpret_cast<mx_status_t*>(arg7)));
        break;
    case 18: ret = static_cast<uint64_t>(sys_socket_create(
        static_cast<uint32_t>(arg1),
        reinterpret_cast<mx_handle_t*>(arg2),
        reinterpret_cast<mx_handle_t*>(arg3)));
        break;
    case 19: ret = static_cast<uint64_t>(sys_socket_write(
        static_cast<mx_handle_t>(arg1),
        static_cast<uint32_t>(arg2),
        reinterpret_cast<const void*>(arg3),
        static_cast<size_t>(arg4),
        reinterpret_cast<size_t*>(arg5)));
        break;
    case 20: ret = static_cast<uint64_t>(sys_socket_read(
        static_cast<mx_handle_t>(arg1),
        static_cast<uint32_t>(arg2),
        reinterpret_cast<void*>(arg3),
        static_cast<size_t>(arg4),
        reinterpret_cast<size_t*>(arg5)));
        break;
    case 21: sys_thread_exit(); // __noreturn__
    case 22: ret = static_cast<uint64_t>(sys_thread_create(
        static_cast<mx_handle_t>(arg1),
        reinterpret_cast<const char*>(arg2),
        static_cast<uint32_t>(arg3),
        static_cast<uint32_t>(arg4),
        reinterpret_cast<mx_handle_t*>(arg5)));
        break;
    case 23: ret = static_cast<uint64_t>(sys_thread_start(
        static_cast<mx_handle_t>(arg1),
        static_cast<uintptr_t>(arg2),
        static_cast<uintptr_t>(arg3),
        static_cast<uintptr_t>(arg4),
        static_cast<uintptr_t>(arg5)));
        break;
    case 24: ret = static_cast<uint64_t>(sys_thread_read_state(
        static_cast<mx_handle_t>(arg1),
        static_cast<uint32_t>(arg2),
        reinterpret_cast<void*>(arg3),
        static_cast<uint32_t>(arg4),
        reinterpret_cast<uint32_t*>(arg5)));
        break;
    case 25: ret = static_cast<uint64_t>(sys_thread_write_state(
        static_cast<mx_handle_t>(arg1),
        static_cast<uint32_t>(arg2),
        reinterpret_cast<const void*>(arg3),
        static_cast<uint32_t>(arg4)));
        break;
    case 26: sys_process_exit(
        static_cast<int>(arg1)); // __noreturn__
    case 27: ret = static_cast<uint64_t>(sys_process_create(
        static_cast<mx_handle_t>(arg1),
        reinterpret_cast<const char*>(arg2),
        static_cast<uint32_t>(arg3),
        static_cast<uint32_t>(arg4),
        reinterpret_cast<mx_handle_t*>(arg5),
        reinterpret_cast<mx_handle_t*>(arg6)));
        break;
    case 28: ret = static_cast<uint64_t>(sys_process_start(
        static_cast<mx_handle_t>(arg1),
        static_cast<mx_handle_t>(arg2),
        static_cast<uintptr_t>(arg3),
        static_cast<uintptr_t>(arg4),
        static_cast<mx_handle_t>(arg5),
        static_cast<uintptr_t>(arg6)));
        break;
    case 29: ret = static_cast<uint64_t>(sys_process_read_memory(
        static_cast<mx_handle_t>(arg1),
        static_cast<uintptr_t>(arg2),
        reinterpret_cast<void*>(arg3),
        static_cast<size_t>(arg4),
        reinterpret_cast<size_t*>(arg5)));
        break;
    case 30: ret = static_cast<uint64_t>(sys_process_write_memory(
        static_cast<mx_handle_t>(arg1),
        static_cast<uintptr_t>(arg2),
        reinterpret_cast<const void*>(arg3),
        static_cast<size_t>(arg4),
        reinterpret_cast<size_t*>(arg5)));
        break;
    case 31: ret = static_cast<uint64_t>(sys_job_create(
        static_cast<mx_handle_t>(arg1),
        static_cast<uint32_t>(arg2),
        reinterpret_cast<mx_handle_t*>(arg3)));
        break;
    case 32: ret = static_cast<uint64_t>(sys_task_bind_exception_port(
        static_cast<mx_handle_t>(arg1),
        static_cast<mx_handle_t>(arg2),
        static_cast<uint64_t>(arg3),
        static_cast<uint32_t>(arg4)));
        break;
    case 33: ret = static_cast<uint64_t>(sys_task_resume(
        static_cast<mx_handle_t>(arg1),
        static_cast<uint32_t>(arg2)));
        break;
    case 34: ret = static_cast<uint64_t>(sys_task_kill(
        static_cast<mx_handle_t>(arg1)));
        break;
    case 35: ret = static_cast<uint64_t>(sys_event_create(
        static_cast<uint32_t>(arg1),
        reinterpret_cast<mx_handle_t*>(arg2)));
        break;
    case 36: ret = static_cast<uint64_t>(sys_eventpair_create(
        static_cast<uint32_t>(arg1),
        reinterpret_cast<mx_handle_t*>(arg2),
        reinterpret_cast<mx_handle_t*>(arg3)));
        break;
    case 37: ret = static_cast<uint64_t>(sys_futex_wait(
        reinterpret_cast<mx_futex_t*>(arg1),
        static_cast<int>(arg2),
        static_cast<mx_time_t>(arg3)));
        break;
    case 38: ret = static_cast<uint64_t>(sys_futex_wake(
        reinterpret_cast<mx_futex_t*>(arg1),
        static_cast<uint32_t>(arg2)));
        break;
    case 39: ret = static_cast<uint64_t>(sys_futex_requeue(
        reinterpret_cast<mx_futex_t*>(arg1),
        static_cast<uint32_t>(arg2),
        static_cast<int>(arg3),
        reinterpret_cast<mx_futex_t*>(arg4),
        static_cast<uint32_t>(arg5)));
        break;
    case 40: ret = static_cast<uint64_t>(sys_waitset_create(
        static_cast<uint32_t>(arg1),
        reinterpret_cast<mx_handle_t*>(arg2)));
        break;
    case 41: ret = static_cast<uint64_t>(sys_waitset_add(
        static_cast<mx_handle_t>(arg1),
        static_cast<uint64_t>(arg2),
        static_cast<mx_handle_t>(arg3),
        static_cast<mx_signals_t>(arg4)));
        break;
    case 42: ret = static_cast<uint64_t>(sys_waitset_remove(
        static_cast<mx_handle_t>(arg1),
        static_cast<uint64_t>(arg2)));
        break;
    case 43: ret = static_cast<uint64_t>(sys_waitset_wait(
        static_cast<mx_handle_t>(arg1),
        static_cast<mx_time_t>(arg2),
        reinterpret_cast<mx_waitset_result_t*>(arg3),
        reinterpret_cast<uint32_t*>(arg4)));
        break;
    case 44: ret = static_cast<uint64_t>(sys_port_create(
        static_cast<uint32_t>(arg1),
        reinterpret_cast<mx_handle_t*>(arg2)));
        break;
    case 45: ret = static_cast<uint64_t>(sys_port_queue(
        static_cast<mx_handle_t>(arg1),
        reinterpret_cast<const void*>(arg2),
        static_cast<size_t>(arg3)));
        break;
    case 46: ret = static_cast<uint64_t>(sys_port_wait(
        static_cast<mx_handle_t>(arg1),
        static_cast<mx_time_t>(arg2),
        reinterpret_cast<void*>(arg3),
        static_cast<size_t>(arg4)));
        break;
    case 47: ret = static_cast<uint64_t>(sys_port_bind(
        static_cast<mx_handle_t>(arg1),
        static_cast<uint64_t>(arg2),
        static_cast<mx_handle_t>(arg3),
        static_cast<mx_signals_t>(arg4)));
        break;
    case 48: ret = static_cast<uint64_t>(sys_vmo_create(
        static_cast<uint64_t>(arg1),
        static_cast<uint32_t>(arg2),
        reinterpret_cast<mx_handle_t*>(arg3)));
        break;
    case 49: ret = static_cast<uint64_t>(sys_vmo_read(
        static_cast<mx_handle_t>(arg1),
        reinterpret_cast<void*>(arg2),
        static_cast<uint64_t>(arg3),
        static_cast<size_t>(arg4),
        reinterpret_cast<size_t*>(arg5)));
        break;
    case 50: ret = static_cast<uint64_t>(sys_vmo_write(
        static_cast<mx_handle_t>(arg1),
        reinterpret_cast<const void*>(arg2),
        static_cast<uint64_t>(arg3),
        static_cast<size_t>(arg4),
        reinterpret_cast<size_t*>(arg5)));
        break;
    case 51: ret = static_cast<uint64_t>(sys_vmo_get_size(
        static_cast<mx_handle_t>(arg1),
        reinterpret_cast<uint64_t*>(arg2)));
        break;
    case 52: ret = static_cast<uint64_t>(sys_vmo_set_size(
        static_cast<mx_handle_t>(arg1),
        static_cast<uint64_t>(arg2)));
        break;
    case 53: ret = static_cast<uint64_t>(sys_vmo_op_range(
        static_cast<mx_handle_t>(arg1),
        static_cast<uint32_t>(arg2),
        static_cast<uint64_t>(arg3),
        static_cast<uint64_t>(arg4),
        reinterpret_cast<void*>(arg5),
        static_cast<size_t>(arg6)));
        break;
    case 54: ret = static_cast<uint64_t>(sys_cprng_draw(
        reinterpret_cast<void*>(arg1),
        static_cast<size_t>(arg2),
        reinterpret_cast<size_t*>(arg3)));
        break;
    case 55: ret = static_cast<uint64_t>(sys_cprng_add_entropy(
        reinterpret_cast<const void*>(arg1),
        static_cast<size_t>(arg2)));
        break;
    case 56: ret = static_cast<uint64_t>(sys_fifo_create(
        static_cast<uint32_t>(arg1),
        static_cast<uint32_t>(arg2),
        static_cast<uint32_t>(arg3),
        reinterpret_cast<mx_handle_t*>(arg4),
        reinterpret_cast<mx_handle_t*>(arg5)));
        break;
    case 57: ret = static_cast<uint64_t>(sys_fifo_read(
        static_cast<mx_handle_t>(arg1),
        reinterpret_cast<void*>(arg2),
        static_cast<size_t>(arg3),
        reinterpret_cast<uint32_t*>(arg4)));
        break;
    case 58: ret = static_cast<uint64_t>(sys_fifo_write(
        static_cast<mx_handle_t>(arg1),
        reinterpret_cast<const void*>(arg2),
        static_cast<size_t>(arg3),
        reinterpret_cast<uint32_t*>(arg4)));
        break;
    case 59: ret = static_cast<uint64_t>(sys_log_create(
        static_cast<uint32_t>(arg1),
        reinterpret_cast<mx_handle_t*>(arg2)));
        break;
    case 60: ret = static_cast<uint64_t>(sys_log_write(
        static_cast<mx_handle_t>(arg1),
        static_cast<uint32_t>(arg2),
        reinterpret_cast<const void*>(arg3),
        static_cast<uint32_t>(arg4)));
        break;
    case 61: ret = static_cast<uint64_t>(sys_log_read(
        static_cast<mx_handle_t>(arg1),
        static_cast<uint32_t>(arg2),
        reinterpret_cast<void*>(arg3),
        static_cast<uint32_t>(arg4)));
        break;
    case 62: ret = static_cast<uint64_t>(sys_ktrace_read(
        static_cast<mx_handle_t>(arg1),
        reinterpret_cast<void*>(arg2),
        static_cast<uint32_t>(arg3),
        static_cast<uint32_t>(arg4),
        reinterpret_cast<uint32_t*>(arg5)));
        break;
    case 63: ret = static_cast<uint64_t>(sys_ktrace_control(
        static_cast<mx_handle_t>(arg1),
        static_cast<uint32_t>(arg2),
        static_cast<uint32_t>(arg3),
        reinterpret_cast<void*>(arg4)));
        break;
    case 64: ret = static_cast<uint64_t>(sys_ktrace_write(
        static_cast<mx_handle_t>(arg1),
        static_cast<uint32_t>(arg2),
        static_cast<uint32_t>(arg3),
        static_cast<uint32_t>(arg4)));
        break;
    case 65: ret = static_cast<uint64_t>(sys_mtrace_control(
        static_cast<mx_handle_t>(arg1),
        static_cast<uint32_t>(arg2),
        static_cast<uint32_t>(arg3),
        static_cast<uint32_t>(arg4),
        reinterpret_cast<void*>(arg5),
        static_cast<uint32_t>(arg6)));
        break;
    case 66: ret = static_cast<uint64_t>(sys_debug_transfer_handle(
        static_cast<mx_handle_t>(arg1),
        static_cast<mx_handle_t>(arg2)));
        break;
    case 67: ret = static_cast<uint64_t>(sys_debug_read(
        static_cast<mx_handle_t>(arg1),
        reinterpret_cast<void*>(arg2),
        static_cast<uint32_t>(arg3)));
        break;
    case 68: ret = static_cast<uint64_t>(sys_debug_write(
        reinterpret_cast<const void*>(arg1),
        static_cast<uint32_t>(arg2)));
        break;
    case 69: ret = static_cast<uint64_t>(sys_debug_send_command(
        static_cast<mx_handle_t>(arg1),
        reinterpret_cast<const void*>(arg2),
        static_cast<uint32_t>(arg3)));
        break;
    case 70: ret = static_cast<uint64_t>(sys_interrupt_create(
        static_cast<mx_handle_t>(arg1),
        static_cast<uint32_t>(arg2),
        static_cast<uint32_t>(arg3)));
        break;
    case 71: ret = static_cast<uint64_t>(sys_interrupt_complete(
        static_cast<mx_handle_t>(arg1)));
        break;
    case 72: ret = static_cast<uint64_t>(sys_interrupt_wait(
        static_cast<mx_handle_t>(arg1)));
        break;
    case 73: ret = static_cast<uint64_t>(sys_interrupt_signal(
        static_cast<mx_handle_t>(arg1)));
        break;
    case 74: ret = static_cast<uint64_t>(sys_mmap_device_io(
        static_cast<mx_handle_t>(arg1),
        static_cast<uint32_t>(arg2),
        static_cast<uint32_t>(arg3)));
        break;
    case 75: ret = static_cast<uint64_t>(sys_mmap_device_memory(
        static_cast<mx_handle_t>(arg1),
        static_cast<mx_paddr_t>(arg2),
        static_cast<uint32_t>(arg3),
        static_cast<mx_cache_policy_t>(arg4),
        reinterpret_cast<uintptr_t*>(arg5)));
        break;
    case 76: ret = static_cast<uint64_t>(sys_io_mapping_get_info(
        static_cast<mx_handle_t>(arg1),
        reinterpret_cast<uintptr_t*>(arg2),
        reinterpret_cast<uint64_t*>(arg3)));
        break;
    case 77: ret = static_cast<uint64_t>(sys_vmo_create_contiguous(
        static_cast<mx_handle_t>(arg1),
        static_cast<size_t>(arg2),
        static_cast<uint32_t>(arg3),
        reinterpret_cast<mx_handle_t*>(arg4)));
        break;
    case 78: ret = static_cast<uint64_t>(sys_vmar_allocate(
        static_cast<mx_handle_t>(arg1),
        static_cast<size_t>(arg2),
        static_cast<size_t>(arg3),
        static_cast<uint32_t>(arg4),
        reinterpret_cast<mx_handle_t*>(arg5),
        reinterpret_cast<uintptr_t*>(arg6)));
        break;
    case 79: ret = static_cast<uint64_t>(sys_vmar_destroy(
        static_cast<mx_handle_t>(arg1)));
        break;
    case 80: ret = static_cast<uint64_t>(sys_vmar_map(
        static_cast<mx_handle_t>(arg1),
        static_cast<size_t>(arg2),
        static_cast<mx_handle_t>(arg3),
        static_cast<uint64_t>(arg4),
        static_cast<size_t>(arg5),
        static_cast<uint32_t>(arg6),
        reinterpret_cast<uintptr_t*>(arg7)));
        break;
    case 81: ret = static_cast<uint64_t>(sys_vmar_unmap(
        static_cast<mx_handle_t>(arg1),
        static_cast<uintptr_t>(arg2),
        static_cast<size_t>(arg3)));
        break;
    case 82: ret = static_cast<uint64_t>(sys_vmar_protect(
        static_cast<mx_handle_t>(arg1),
        static_cast<uintptr_t>(arg2),
        static_cast<size_t>(arg3),
        static_cast<uint32_t>(arg4)));
        break;
    case 83: ret = static_cast<uint64_t>(sys_bootloader_fb_get_info(
        reinterpret_cast<uint32_t*>(arg1),
        reinterpret_cast<uint32_t*>(arg2),
        reinterpret_cast<uint32_t*>(arg3),
        reinterpret_cast<uint32_t*>(arg4)));
        break;
    case 84: ret = static_cast<uint64_t>(sys_set_framebuffer(
        static_cast<mx_handle_t>(arg1),
        reinterpret_cast<void*>(arg2),
        static_cast<uint32_t>(arg3),
        static_cast<uint32_t>(arg4),
        static_cast<uint32_t>(arg5),
        static_cast<uint32_t>(arg6),
        static_cast<uint32_t>(arg7)));
        break;
    case 85: ret = static_cast<uint64_t>(sys_clock_adjust(
        static_cast<mx_handle_t>(arg1),
        static_cast<uint32_t>(arg2),
        static_cast<int64_t>(arg3)));
        break;
    case 86: ret = static_cast<uint64_t>(sys_pci_get_nth_device(
        static_cast<mx_handle_t>(arg1),
        static_cast<uint32_t>(arg2),
        reinterpret_cast<mx_pcie_get_nth_info_t*>(arg3)));
        break;
    case 87: ret = static_cast<uint64_t>(sys_pci_claim_device(
        static_cast<mx_handle_t>(arg1)));
        break;
    case 88: ret = static_cast<uint64_t>(sys_pci_enable_bus_master(
        static_cast<mx_handle_t>(arg1),
        static_cast<bool>(arg2)));
        break;
    case 89: ret = static_cast<uint64_t>(sys_pci_enable_pio(
        static_cast<mx_handle_t>(arg1),
        static_cast<bool>(arg2)));
        break;
    case 90: ret = static_cast<uint64_t>(sys_pci_reset_device(
        static_cast<mx_handle_t>(arg1)));
        break;
    case 91: ret = static_cast<uint64_t>(sys_pci_map_mmio(
        static_cast<mx_handle_t>(arg1),
        static_cast<uint32_t>(arg2),
        static_cast<mx_cache_policy_t>(arg3),
        reinterpret_cast<mx_handle_t*>(arg4)));
        break;
    case 92: ret = static_cast<uint64_t>(sys_pci_io_write(
        static_cast<mx_handle_t>(arg1),
        static_cast<uint32_t>(arg2),
        static_cast<uint32_t>(arg3),
        static_cast<uint32_t>(arg4),
        static_cast<uint32_t>(arg5)));
        break;
    case 93: ret = static_cast<uint64_t>(sys_pci_io_read(
        static_cast<mx_handle_t>(arg1),
        static_cast<uint32_t>(arg2),
        static_cast<uint32_t>(arg3),
        static_cast<uint32_t>(arg4),
        reinterpret_cast<uint32_t*>(arg5)));
        break;
    case 94: ret = static_cast<uint64_t>(sys_pci_map_interrupt(
        static_cast<mx_handle_t>(arg1),
        static_cast<int32_t>(arg2),
        reinterpret_cast<mx_handle_t*>(arg3)));
        break;
    case 95: ret = static_cast<uint64_t>(sys_pci_map_config(
        static_cast<mx_handle_t>(arg1),
        reinterpret_cast<mx_handle_t*>(arg2)));
        break;
    case 96: ret = static_cast<uint64_t>(sys_pci_query_irq_mode_caps(
        static_cast<mx_handle_t>(arg1),
        static_cast<uint32_t>(arg2),
        reinterpret_cast<uint32_t*>(arg3)));
        break;
    case 97: ret = static_cast<uint64_t>(sys_pci_set_irq_mode(
        static_cast<mx_handle_t>(arg1),
        static_cast<uint32_t>(arg2),
        static_cast<uint32_t>(arg3)));
        break;
    case 98: ret = static_cast<uint64_t>(sys_pci_init(
        static_cast<mx_handle_t>(arg1),
        reinterpret_cast<const mx_pci_init_arg_t*>(arg2),
        static_cast<uint32_t>(arg3)));
        break;
    case 99: ret = static_cast<uint64_t>(sys_pci_add_subtract_io_range(
        static_cast<mx_handle_t>(arg1),
        static_cast<bool>(arg2),
        static_cast<uint64_t>(arg3),
        static_cast<uint64_t>(arg4),
        static_cast<bool>(arg5)));
        break;
    case 100: ret = static_cast<uint64_t>(sys_acpi_uefi_rsdp(
        static_cast<mx_handle_t>(arg1)));
        break;
    case 101: ret = static_cast<uint64_t>(sys_acpi_cache_flush(
        static_cast<mx_handle_t>(arg1)));
        break;
    case 102: ret = static_cast<uint64_t>(sys_resource_create(
        static_cast<mx_handle_t>(arg1),
        reinterpret_cast<const mx_rrec_t*>(arg2),
        static_cast<uint32_t>(arg3),
        reinterpret_cast<mx_handle_t*>(arg4)));
        break;
    case 103: ret = static_cast<uint64_t>(sys_resource_get_handle(
        static_cast<mx_handle_t>(arg1),
        static_cast<uint32_t>(arg2),
        static_cast<uint32_t>(arg3),
        reinterpret_cast<mx_handle_t*>(arg4)));
        break;
    case 104: ret = static_cast<uint64_t>(sys_resource_do_action(
        static_cast<mx_handle_t>(arg1),
        static_cast<uint32_t>(arg2),
        static_cast<uint32_t>(arg3),
        static_cast<uint32_t>(arg4),
        static_cast<uint32_t>(arg5)));
        break;
    case 105: ret = static_cast<uint64_t>(sys_resource_connect(
        static_cast<mx_handle_t>(arg1),
        static_cast<mx_handle_t>(arg2)));
        break;
    case 106: ret = static_cast<uint64_t>(sys_resource_accept(
        static_cast<mx_handle_t>(arg1),
        reinterpret_cast<mx_handle_t*>(arg2)));
        break;
    case 107: ret = static_cast<uint64_t>(sys_hypervisor_create(
        static_cast<mx_handle_t>(arg1),
        static_cast<uint32_t>(arg2),
        reinterpret_cast<mx_handle_t*>(arg3)));
        break;
    case 108: ret = static_cast<uint64_t>(sys_hypervisor_op(
        static_cast<mx_handle_t>(arg1),
        static_cast<uint32_t>(arg2),
        reinterpret_cast<const void*>(arg3),
        static_cast<uint32_t>(arg4),
        reinterpret_cast<void*>(arg5),
        static_cast<uint32_t>(arg6)));
        break;
    case 109: ret = static_cast<uint64_t>(sys_syscall_test_0());
        break;
    case 110: ret = static_cast<uint64_t>(sys_syscall_test_1(
        static_cast<int>(arg1)));
        break;
    case 111: ret = static_cast<uint64_t>(sys_syscall_test_2(
        static_cast<int>(arg1),
        static_cast<int>(arg2)));
        break;
    case 112: ret = static_cast<uint64_t>(sys_syscall_test_3(
        static_cast<int>(arg1),
        static_cast<int>(arg2),
        static_cast<int>(arg3)));
        break;
    case 113: ret = static_cast<uint64_t>(sys_syscall_test_4(
        static_cast<int>(arg1),
        static_cast<int>(arg2),
        static_cast<int>(arg3),
        static_cast<int>(arg4)));
        break;
    case 114: ret = static_cast<uint64_t>(sys_syscall_test_5(
        static_cast<int>(arg1),
        static_cast<int>(arg2),
        static_cast<int>(arg3),
        static_cast<int>(arg4),
        static_cast<int>(arg5)));
        break;
    case 115: ret = static_cast<uint64_t>(sys_syscall_test_6(
        static_cast<int>(arg1),
        static_cast<int>(arg2),
        static_cast<int>(arg3),
        static_cast<int>(arg4),
        static_cast<int>(arg5),
        static_cast<int>(arg6)));
        break;
    case 116: ret = static_cast<uint64_t>(sys_syscall_test_7(
        static_cast<int>(arg1),
        static_cast<int>(arg2),
        static_cast<int>(arg3),
        static_cast<int>(arg4),
        static_cast<int>(arg5),
        static_cast<int>(arg6),
        static_cast<int>(arg7)));
        break;
    case 117: ret = static_cast<uint64_t>(sys_syscall_test_8(
        static_cast<int>(arg1),
        static_cast<int>(arg2),
        static_cast<int>(arg3),
        static_cast<int>(arg4),
        static_cast<int>(arg5),
        static_cast<int>(arg6),
        static_cast<int>(arg7),
        static_cast<int>(arg8)));
        break;

