/*
 * Copyright 2018 The Fuchsia Authors.
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

// This file contains what's needed to make Linux code compile (but not run) on Fuchsia.
// As the driver is ported, symbols will be removed from this file. When the driver is
// fully ported, this file will be empty and can be deleted.
// The symbols were defined by hand, based only on information from compiler errors and
// code in this driver. Do not expect defines/enums to have correct values, or struct fields to have
// correct types. Function prototypes are even less accurate.

#ifndef GARNET_DRIVERS_WLAN_THIRD_PARTY_BROADCOM_INCLUDE_LINUXISMS_H_
#define GARNET_DRIVERS_WLAN_THIRD_PARTY_BROADCOM_INCLUDE_LINUXISMS_H_

#include <ddk/debug.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <sys/types.h>
#include <zircon/assert.h>

typedef uint16_t __be16;
typedef uint32_t __be32;
typedef uint64_t __be64;
typedef struct {
    int counter;
} atomic_t;

// FROM Josh's linuxisms.h

#define ARRAY_SIZE(arr) (sizeof(arr) / sizeof(arr[0]))

#define BIT(pos) (1UL << (pos))

#define DIV_ROUND_UP(n, m) (((n) + ((m)-1)) / (m))

#define ETHTOOL_FWVERS_LEN 32

#define GENMASK1(val) ((1UL << (val)) - 1)
#define GENMASK(start, end) ((GENMASK1((start) + 1) & ~GENMASK1(end)))

#define LOCK_ASSERT_HELD(lock)                                                \
    do {                                                                      \
        int res = mtx_trylock(lock);                                          \
        ZX_ASSERT(res != 0);                                                  \
        if (res == 0) {                                                       \
            printf("broadcom: lock not held at %s:%d\n", __FILE__, __LINE__); \
            mtx_unlock(lock);                                                 \
        }                                                                     \
    } while (0)

#define WARN(cond, msg) \
    printf("broadcom: unexpected condition %s warns %s at %s:%d\n", #cond, msg, __FILE__, __LINE__)

#define WARN_ON(cond)                          \
    ({                                         \
        if (cond) { WARN(#cond, "it's bad"); } \
        cond;                                  \
    })

#define WARN_ON_ONCE(cond)                               \
    ({                                                   \
        static bool warn_next = true;                    \
        if (cond && warn_next) {                         \
            WARN(#cond, "(future warnings suppressed)"); \
            warn_next = false;                           \
        }                                                \
        cond;                                            \
    })

#define ilog2(val) \
    (((val) == 0) ? 0 : (((sizeof(unsigned long long) * 8) - 1) - __builtin_clzll(val)))

#define iowrite32(value, addr)                              \
    do {                                                    \
        (*(volatile uint32_t*)(uintptr_t)(addr)) = (value); \
    } while (0)

#define ioread32(addr) (*(volatile uint32_t*)(uintptr_t)(addr))

#define lockdep_assert_held(mtx) ZX_ASSERT(mtx_trylock(mtx) != thrd_success)

#define mdelay(msecs)                                                                \
    do {                                                                             \
        zx_time_t busy_loop_end = zx_clock_get(ZX_CLOCK_MONOTONIC) + ZX_MSEC(msecs); \
        while (zx_clock_get(ZX_CLOCK_MONOTONIC) < busy_loop_end) {}                  \
    } while (0)

#define min(a, b) (((a) < (b)) ? (a) : (b))
#define min_t(t, a, b) (((t)(a) < (t)(b)) ? (t)(a) : (t)(b))

#define __packed __attribute__((packed))
#define __aligned(n) __attribute__((aligned(n)))

#define rounddown(n, m) ((n) - ((n) % (m)))
#define roundup(n, m) (((n) % (m) == 0) ? (n) : (n) + ((m) - ((n) % (m))))

#define roundup_pow_of_two(val)        \
    ((unsigned long)(val) == 0 ? (val) \
                               : 1UL << ((sizeof(unsigned long) * 8) - __builtin_clzl((val)-1)))

/* Not actually a linuxism, but closely related to the previous definition */
#define roundup_log2(val) \
    ((unsigned long)(val) == 0 ? (val) : ((sizeof(unsigned long) * 8) - __builtin_clzl((val)-1)))

typedef int32_t spinlock_t;
typedef uint32_t gfp_t;

#define LINUX_FUNC(name, paramtype, rettype)                                                   \
    static inline rettype name(paramtype foo, ...) {                                           \
        zxlogf(ERROR, "cphoenix: You called linux function %s at line %d of file %s\n", #name, \
               __LINE__, __FILE__);                                                            \
        return (rettype)0;                                                                     \
    }
#define LINUX_FUNCX(name)                                                                      \
    static inline int name() {                                                                 \
        zxlogf(ERROR, "cphoenix: You called linux function %s at line %d of file %s\n", #name, \
               __LINE__, __FILE__);                                                            \
        return 0;                                                                              \
    }
// clang-format off
#define LINUX_FUNCII(name) LINUX_FUNC(name, int, int)
#define LINUX_FUNCIV(name) LINUX_FUNC(name, int, void*)
#define LINUX_FUNCVV(name) LINUX_FUNC(name, void*, void*)
#define LINUX_FUNCVI(name) LINUX_FUNC(name, void*, int)
#define LINUX_FUNCcVI(name) LINUX_FUNC(name, const void*, int)
#define LINUX_FUNCcVV(name) LINUX_FUNC(name, const void*, void*)
#define LINUX_FUNCVU(name) LINUX_FUNC(name, void*, uint16_t)
#define LINUX_FUNCUU(name) LINUX_FUNC(name, uint32_t, uint32_t)
LINUX_FUNCVV(skb_peek_tail)
LINUX_FUNCVV(skb_peek)
LINUX_FUNCVI(skb_cloned)
LINUX_FUNCVV(eth_broadcast_addr)
LINUX_FUNCcVV(is_multicast_ether_addr)
LINUX_FUNCVV(eth_zero_addr)
LINUX_FUNCIV(kcalloc)
//LINUX_FUNCVV(brcmf_msgbuf_delete_flowring)
LINUX_FUNCVV(skb_mac_header)
LINUX_FUNCVI(pskb_expand_head)
LINUX_FUNCVV(skb_queue_head)
LINUX_FUNCVI(skb_queue_empty)
LINUX_FUNCVV(skb_dequeue)
LINUX_FUNCVV(__skb_dequeue)
LINUX_FUNCVI(skb_queue_len)
LINUX_FUNCVI(skb_pull)
LINUX_FUNCVI(__skb_trim)
LINUX_FUNCVI(brfcmf_dbg)
LINUX_FUNCII(usleep_range)
LINUX_FUNCII(time_after)
LINUX_FUNCII(msecs_to_jiffies)
LINUX_FUNCII(jiffies_to_msecs)
LINUX_FUNCII(udelay)
LINUX_FUNCX(net_ratelimit)
LINUX_FUNCVI(sdio_readb)
LINUX_FUNCVI(sdio_writeb)
LINUX_FUNCVI(sdio_claim_host)
LINUX_FUNCVI(sdio_release_host)
LINUX_FUNCVI(no_printk)
LINUX_FUNCVI(skb_unlink)
LINUX_FUNCVI(skb_push)
LINUX_FUNCVU(skb_headroom)
LINUX_FUNCVI(skb_tailroom)
LINUX_FUNCVI(skb_cow_head)
LINUX_FUNCVI(skb_queue_tail)
LINUX_FUNCVI(skb_queue_is_last)
LINUX_FUNCVI(skb_trim)
LINUX_FUNCVI(skb_linearize)
LINUX_FUNCVI(__skb_queue_after)
LINUX_FUNCVI(__skb_unlink)
LINUX_FUNCVI(skb_put)
LINUX_FUNCVI(__skb_put)
LINUX_FUNCVI(__skb_queue_head_init)
LINUX_FUNCVI(__skb_queue_tail)
LINUX_FUNCVI(kfree_skb)
LINUX_FUNCVI(skb_queue_head_init)
#define skb_queue_walk_safe(a, b, c) for((void)c, b=(a)->next;;)
#define skb_queue_walk(a, b) for(b=(a)->next;;)
LINUX_FUNCVV(netdev_priv)
LINUX_FUNCVI(free_netdev)
LINUX_FUNCcVI(is_zero_ether_addr)
LINUX_FUNCII(trace_brcmf_sdpcm_hdr)
LINUX_FUNCII(trace_brcmf_dbg)
LINUX_FUNCVI(atomic_inc)
LINUX_FUNCVI(atomic_set)
LINUX_FUNCVI(atomic_read)
LINUX_FUNCII(atomic_or)
LINUX_FUNCVI(atomic_xchg)
LINUX_FUNCVI(atomic_dec)
LINUX_FUNCUU(cpu_to_be16)
LINUX_FUNCUU(cpu_to_be32)
LINUX_FUNCUU(be16_to_cpu)
LINUX_FUNCUU(be32_to_cpu)
LINUX_FUNCVU(__get_unaligned_be16)
LINUX_FUNCVU(get_unaligned_be16)
LINUX_FUNCVU(get_unaligned_be32)
LINUX_FUNCVU(get_unaligned_le16)
LINUX_FUNCVU(get_unaligned_le32)
LINUX_FUNCUU(put_unaligned_le32)
#define a (a)
LINUX_FUNCVI(brcmf_dbg_hex_dump)
LINUX_FUNCVI(trace_brcmf_hexdump)
LINUX_FUNCVI(trace_brcmf_debug)
LINUX_FUNCVI(add_wait_queue)
LINUX_FUNCII(set_current_state)
LINUX_FUNCII(send_sig)
LINUX_FUNCVI(kthread_stop)
LINUX_FUNCVV(dev_get_drvdata)
LINUX_FUNCVI(signal_pending)
LINUX_FUNCII(schedule_timeout)
LINUX_FUNCVV(remove_wait_queue)
LINUX_FUNCVI(wake_up_interruptible)
LINUX_FUNCIV(kmalloc)
LINUX_FUNCIV(kmemdump)
LINUX_FUNCcVV(kmemdup)
LINUX_FUNCIV(vzalloc)
LINUX_FUNCIV(kzalloc)
//LINUX_FUNCIV(valloc)
LINUX_FUNCVV(vfree)
LINUX_FUNCVV(kfree)
LINUX_FUNCII(msleep)
LINUX_FUNCVI(pr_warn)
LINUX_FUNCVV(spin_unlock_bh)
LINUX_FUNCVV(spin_lock_bh)
LINUX_FUNCVI(sdio_enable_func)
LINUX_FUNCVI(sdio_disable_func)
LINUX_FUNCVV(alloc_ordered_workqueue)
LINUX_FUNCVI(INIT_WORK)
LINUX_FUNCVI(spin_lock_init)
LINUX_FUNCVI(spin_unlock_irqrestore)
LINUX_FUNCX(wmb)
LINUX_FUNCX(rmb)
#define list_add_tail bc_list_add_tail // name conflict with zircon/listnode.h
LINUX_FUNCVI(bc_list_add_tail)
LINUX_FUNCVI(list_empty)
LINUX_FUNCVI(list_del)
LINUX_FUNCVI(INIT_LIST_HEAD)
LINUX_FUNC(release_firmware, const void*, int)
#define spin_lock_irqsave(a, b) {b=0;}
LINUX_FUNCII(enable_irq)
LINUX_FUNCVV(wiphy_priv)
LINUX_FUNCVI(wiphy_register)
LINUX_FUNCVI(wiphy_unregister)
LINUX_FUNCVV(wiphy_new)
LINUX_FUNCVI(wiphy_free)
LINUX_FUNCVI(wiphy_ext_feature_set)
LINUX_FUNCVI(wiphy_read_of_freq_limits)
LINUX_FUNCVI(wiphy_apply_custom_regulatory)
LINUX_FUNCVV(wdev_priv)
LINUX_FUNCVI(set_wiphy_dev)
LINUX_FUNCVV(list_first_entry)
LINUX_FUNCVI(cfg80211_unregister_wdev)
LINUX_FUNCVI(cfg80211_sched_scan_stopped)
typedef struct wait_queue_head {
  int foo;
} wait_queue_head_t;
LINUX_FUNC(wait_event_interruptible_timeout, struct wait_queue_head, int)
LINUX_FUNC(wait_event_timeout, struct wait_queue_head, int)
LINUX_FUNCVI(sdio_f0_writeb)
#define max_t(a, b, c) (b)
LINUX_FUNCVI(queue_work)
LINUX_FUNCX(in_interrupt)
LINUX_FUNCVI(sdio_f0_readb)
LINUX_FUNCVI(pr_debug)
LINUX_FUNCVI(IS_ERR)
LINUX_FUNCII(allow_signal)
LINUX_FUNCX(kthread_should_stop)
LINUX_FUNCVI(wait_for_completion_interruptible)
LINUX_FUNCVI(reinit_completion)
LINUX_FUNCVI(complete)
LINUX_FUNCVI(mod_timer)
LINUX_FUNCVI(add_timer)
LINUX_FUNCVI(timer_setup)
LINUX_FUNCVI(timer_pending)
LINUX_FUNCII(__ffs)
LINUX_FUNCVI(init_completion)
LINUX_FUNCVV(kthread_run)
LINUX_FUNCVV(dev_name)
LINUX_FUNCVI(init_waitqueue_head)
LINUX_FUNCVI(device_release_driver)
LINUX_FUNCVI(destroy_workqueue)
LINUX_FUNCVI(del_timer_sync)
LINUX_FUNCVI(cancel_work_sync)
LINUX_FUNCVV(strnchr)
LINUX_FUNCVI(request_firmware)
#define from_timer(a, b, c) ((void*)0)
#define module_param_string(a, b, c, d)
#define module_exit(a) \
    void* __modexit() { return a; }
#define module_init(a) \
    void* __modinit() { return a; }
LINUX_FUNCVV(dev_get_platdata)
LINUX_FUNCVV(dev_set_drvdata)
LINUX_FUNCVI(platform_driver_probe)
LINUX_FUNCVI(platform_driver_unregister)
LINUX_FUNCII(set_bit)
LINUX_FUNCII(clear_bit)
LINUX_FUNCII(test_bit)
LINUX_FUNCII(test_and_clear_bit)
LINUX_FUNCVI(cfg80211_ready_on_channel)
LINUX_FUNCVI(cfg80211_sched_scan_results)
LINUX_FUNCcVI(cfg80211_get_p2p_attr)
LINUX_FUNCII(ieee80211_frequency_to_channel)
LINUX_FUNCVI(cfg80211_remain_on_channel_expired)
LINUX_FUNCII(ieee80211_channel_to_frequency)
LINUX_FUNCVV(ieee80211_get_channel)
LINUX_FUNCII(ieee80211_is_mgmt)
LINUX_FUNCII(ieee80211_is_action)
LINUX_FUNCII(ieee80211_is_probe_resp)
LINUX_FUNCVI(cfg80211_rx_mgmt)
LINUX_FUNCVI(cfg80211_mgmt_tx_status)
LINUX_FUNCX(prandom_u32)
LINUX_FUNCVI(schedule_work)
LINUX_FUNCVI(wait_for_completion_timeout)
LINUX_FUNCVI(ether_addr_equal)
LINUX_FUNCVI(mutex_lock)
LINUX_FUNCVI(mutex_unlock)
LINUX_FUNCVI(mutex_init)
LINUX_FUNCVI(mutex_destroy)
LINUX_FUNCX(rtnl_lock)
LINUX_FUNCX(rtnl_unlock)
LINUX_FUNCVI(ioread8)
LINUX_FUNCVI(ioread16)
LINUX_FUNCII(iowrite8)
LINUX_FUNCII(iowrite16)
LINUX_FUNCcVI(pci_write_config_dword)
LINUX_FUNCcVI(pci_read_config_dword)
LINUX_FUNCcVI(pci_enable_msi)
LINUX_FUNCcVI(pci_disable_msi)
LINUX_FUNCcVI(pci_enable_device)
LINUX_FUNCcVI(pci_disable_device)
LINUX_FUNCcVI(pci_set_master)
LINUX_FUNCcVI(pci_resource_start)
LINUX_FUNCcVI(pci_resource_len)
LINUX_FUNCcVI(pci_register_driver)
LINUX_FUNCcVI(pci_unregister_driver)
LINUX_FUNCcVI(pci_pme_capable)
LINUX_FUNCVI(device_wakeup_enable)
LINUX_FUNCVI(wake_up)
LINUX_FUNCII(request_threaded_irq)
LINUX_FUNCII(free_irq)
LINUX_FUNCVV(dma_alloc_coherent)
LINUX_FUNCVV(dma_free_coherent)
LINUX_FUNCVI(memcpy_fromio)
LINUX_FUNCVI(memcpy_toio)
LINUX_FUNCVI(sdio_memcpy_toio)
LINUX_FUNCVV(dma_zalloc_coherent)
LINUX_FUNCIV(ioremap_nocache)
LINUX_FUNCVV(iounmap)
LINUX_FUNCVI(pci_domain_nr)
LINUX_FUNCVI(cfg80211_check_combinations)
LINUX_FUNCVI(cfg80211_scan_done)
LINUX_FUNCVI(cfg80211_disconnected)
LINUX_FUNCVI(cfg80211_roamed)
LINUX_FUNCVI(cfg80211_connect_done)
LINUX_FUNCVV(cfg80211_inform_bss)
LINUX_FUNCVV(cfg80211_put_bss)
LINUX_FUNCVV(cfg80211_new_sta)
LINUX_FUNCVV(cfg80211_del_sta)
LINUX_FUNCVV(cfg80211_ibss_joined)
LINUX_FUNCVV(cfg80211_michael_mic_failure)
LINUX_FUNCII(MBM_TO_DBM)
LINUX_FUNCVI(SET_NETDEV_DEV)
LINUX_FUNCVV(wiphy_dev)
LINUX_FUNCX(cond_resched)
LINUX_FUNCVI(spin_lock)
LINUX_FUNCVI(spin_unlock)
#undef mdelay // conflicts with Josh's definition above
#define mdelay linux_mdelay // name conflict
LINUX_FUNCII(linux_mdelay)
LINUX_FUNCII(max)
LINUX_FUNCVI(netdev_mc_count)
LINUX_FUNCVI(netif_stop_queue)
LINUX_FUNCVI(netif_wake_queue)
LINUX_FUNCVI(dev_kfree_skb)
LINUX_FUNCVV(skb_header_cloned)
LINUX_FUNCUU(htons)
LINUX_FUNCUU(ntohs)
LINUX_FUNCVI(cfg80211_classify8021d)
LINUX_FUNCVI(netif_rx)
LINUX_FUNCVI(netif_rx_ni)
LINUX_FUNCVI(eth_type_trans)
LINUX_FUNCVI(waitqueue_active)
LINUX_FUNCVI(netif_carrier_off)
LINUX_FUNCVI(dev_net_set)
LINUX_FUNCVI(register_netdevice)
LINUX_FUNCVI(unregister_netdevice)
LINUX_FUNCVI(register_netdev)
LINUX_FUNCVI(unregister_netdev)
LINUX_FUNCVV(wiphy_net)
LINUX_FUNCVI(netif_carrier_ok)
LINUX_FUNCVI(netif_carrier_on)
LINUX_FUNCVI(dev_kfree_skb_any)
LINUX_FUNCIV(alloc_netdev)
LINUX_FUNCVI(seq_printf)
LINUX_FUNCVI(seq_write)
LINUX_FUNCVI(netif_queue_stopped)
LINUX_FUNCVI(trace_brcmf_bcdchdr)
//LINUX_FUNCVI(brcmf_proto_msgbuf_rx_trigger)
LINUX_FUNCVI(of_device_is_compatible)
LINUX_FUNCVI(of_property_read_u32)
LINUX_FUNCVI(of_find_property)
LINUX_FUNCVI(irq_of_parse_and_map)
LINUX_FUNCII(irqd_get_trigger_type)
LINUX_FUNCII(irq_get_irq_data)
LINUX_FUNCVV(bcm47xx_nvram_get_contents)
LINUX_FUNCVI(bcm47xx_nvram_release_contents)
LINUX_FUNCVI(request_firmware_nowait)
LINUX_FUNCVI(dma_map_single)
LINUX_FUNCVI(dma_mapping_error)
LINUX_FUNCVI(atomic_cmpxchg)
LINUX_FUNCVI(dma_unmap_single)
LINUX_FUNCVI(skb_orphan)
LINUX_FUNCVI(__skb_insert)
LINUX_FUNCVI(strnstr)
LINUX_FUNCX(get_random_int)
LINUX_FUNCII(gcd)
LINUX_FUNCVI(usb_fill_control_urb)
LINUX_FUNCVI(usb_submit_urb)
LINUX_FUNCVI(usb_sndctrlpipe)
LINUX_FUNCVI(usb_rcvctrlpipe)
LINUX_FUNCVI(sdio_claim_irq)
LINUX_FUNCVI(is_valid_ether_addr)
LINUX_FUNCVV(create_singlethread_workqueue)
LINUX_FUNCVI(test_and_set_bit)
#define list_entry(ptr,type,field) ((type*)0)
LINUX_FUNCVI(list_del_init)
LINUX_FUNCII(disable_irq_nosync)
LINUX_FUNCII(request_irq)
LINUX_FUNCII(enable_irq_wake)
LINUX_FUNCII(disable_irq_wake)
LINUX_FUNCVI(sdio_release_irq)
LINUX_FUNCVI(sdio_readl)
LINUX_FUNCVI(sdio_writel)
LINUX_FUNCVI(sdio_memcpy_fromio)
LINUX_FUNCVI(sdio_readsb)
LINUX_FUNCVI(dev_coredumpv)
LINUX_FUNCVV(debugfs_create_dir)
LINUX_FUNCVV(debugfs_create_devm_seqfile)
LINUX_FUNCVI(debugfs_remove_recursive)
LINUX_FUNCVI(sg_set_buf)
LINUX_FUNCVI(IS_ERR_OR_NULL)
LINUX_FUNCVI(debugfs_create_u32)
LINUX_FUNCVV(sg_next)
LINUX_FUNCVI(cfg80211_crit_proto_stopped)
LINUX_FUNCVI(scnprintf)
LINUX_FUNCVI(seq_puts)
LINUX_FUNCII(round_up)
LINUX_FUNCVV(skb_queue_prev)
LINUX_FUNCII(BITS_TO_LONGS)
LINUX_FUNCVV(cfg80211_vendor_cmd_alloc_reply_skb)
LINUX_FUNCVI(cfg80211_vendor_cmd_reply)
LINUX_FUNCVI(pr_err)
LINUX_FUNCcVI(trace_brcmf_err)
LINUX_FUNCIV(dev_alloc_skb)
LINUX_FUNCVI(usb_fill_bulk_urb)
LINUX_FUNCIV(usb_alloc_urb)
LINUX_FUNCVI(usb_free_urb)
LINUX_FUNCVI(nla_put)
LINUX_FUNCVI(nla_put_u16)
LINUX_FUNCVI(mmc_set_data_timeout)
LINUX_FUNCVI(mmc_wait_for_req)
LINUX_FUNCVI(sg_init_table)
LINUX_FUNCVI(device_set_wakeup_enable)
LINUX_FUNCVI(usb_kill_urb)
LINUX_FUNCVI(sg_free_table)
LINUX_FUNCVV(interface_to_usbdev)
LINUX_FUNCVI(sg_alloc_table)
LINUX_FUNCVI(pm_runtime_allow)
LINUX_FUNCVI(pm_runtime_forbid)
LINUX_FUNCVI(sdio_set_block_size)
#define SDIO_DEVICE(a,b) (a)
#define USB_DEVICE(a,b) .idVendor=a, .idProduct=b
LINUX_FUNCVI(sdio_register_driver)
LINUX_FUNCVV(sdio_unregister_driver)
LINUX_FUNCVI(usb_set_intfdata)
LINUX_FUNCVI(usb_endpoint_xfer_bulk)
LINUX_FUNCVI(usb_endpoint_num)
LINUX_FUNCVI(usb_rcvbulkpipe)
LINUX_FUNCVI(usb_sndbulkpipe)
LINUX_FUNCVV(usb_get_intfdata)
LINUX_FUNCVI(usb_endpoint_dir_in)
LINUX_FUNCVI(driver_for_each_device)
LINUX_FUNCVI(usb_deregister)
LINUX_FUNCVI(usb_register)
LINUX_FUNCVV(skb_dequeue_tail)
LINUX_FUNCVI(print_hex_dump_bytes)

#define list_for_each_entry(a, b, c) for (a = (void*)0;;)
#define list_for_each_entry_safe(a, b, c, d) for (a = (void*)0;;)
#define netdev_for_each_mc_addr(a, b) for (a = (void*)0;;)
#define for_each_set_bit(a, b, c) for (a = 0;;)
#define list_first_entry(a, b, c) ((b*)0)

typedef uint64_t phys_addr_t;
typedef uint64_t pm_message_t;
typedef void* usb_complete_t;
#define DEBUG                         // Turns on struct members that debug.c needs
#define CONFIG_OF                     // Turns on functions that of.c needs
#define CONFIG_BRCMFMAC_PROTO_MSGBUF  // turns on msgbuf.h
#define CONFIG_BRCMFMAC_PROTO_BCDC    // Needed to see func defs in bcdc.h
#define DECLARE_WAITQUEUE(name, b) struct linuxwait name
#define DECLARE_WORK(name, b) struct linuxwait name = {b};
#define container_of(a, b, c) ((b*)0)
#define ERR_PTR(n) ((void*)(size_t)n)
#define PTR_ERR(n) ((int)n)
#define READ_ONCE(a) (a)
#define BUG_ON(a)

struct linuxwait {
    void* foo;
};
#define WQ_MEM_RECLAIM (17)
enum {
    ENOENT,
    ENOBUFS,
    ERANGE,
    ENAVAIL,
    ESRCH,
    ENFILE,
    EOPNOTSUPP,
    EBADE,
    EPROTO,
    EIO,
    ENODATA,
    EINVAL,
    ENXIO,
    ENOMEM,
    ENODEV,
    ENOTBLK,
    ENOSR,
    ETIMEDOUT,
    ERESTARTSYS,
    EACCES,
    EBUSY,
    E2BIG,
    EPERM,
    ENOSPC,
    ENOTSUPP,
    EAGAIN,
    EFAULT,
    EBADF,
    ENOMEDIUM,
};

#define KBUILD_MODNAME "hi world"
#define THIS_MODULE ((void*)0)
#define PCI_D3hot 261
#define PCI_CLASS_NETWORK_OTHER 12
#define PCI_ANY_ID 1234
#define PCI_VENDOR_ID_BROADCOM 4623
#define BCMA_CORE_PCIE2 444
#define BCMA_CORE_ARM_CR4 445
#define BCMA_CORE_INTERNAL_MEM 446
#define IEEE80211_P2P_ATTR_DEVICE_INFO 2
#define IEEE80211_P2P_ATTR_DEVICE_ID 3
#define IEEE80211_STYPE_ACTION 0
#define IEEE80211_FCTL_STYPE 0
#define IEEE80211_P2P_ATTR_GROUP_ID 0
#define IEEE80211_STYPE_PROBE_REQ 0
#define IEEE80211_P2P_ATTR_LISTEN_CHANNEL (57)
#define SDIO_CCCR_INTx (1)
#define SDIO_DEVICE_ID_BROADCOM_4339 (2)
#define SDIO_DEVICE_ID_BROADCOM_4335_4339 (3)
#define BCMA_CORE_SDIO_DEV (4)
#define BCMA_CORE_CHIPCOMMON (5)
#define BCMA_CC_PMU_CTL_RES_RELOAD (6)
#define BCMA_CC_PMU_CTL_RES_SHIFT (6)
#define BRCMF_BUSTYPE_SDIO (6)
#define SIGTERM (55)
#define TASK_INTERRUPTIBLE (0)
#define TASK_RUNNING (1)
#define GFP_ATOMIC (1)
#define GFP_KERNEL (2)
#define ETH_ALEN (6)
#define IFNAMSIZ (32)
#define ETH_P_LINK_CTL (0)
#define ETH_HLEN (16)
#define WLAN_PMKID_LEN (16)
#define WLAN_MAX_KEY_LEN (128)
#define IEEE80211_MAX_SSID_LEN (32)
#define BRCMFMAC_PDATA_NAME ("pdata name")
enum {
    BRCMF_H2D_MSGRING_CONTROL_SUBMIT_MAX_ITEM,
    BRCMF_H2D_MSGRING_RXPOST_SUBMIT_MAX_ITEM,
    BRCMF_D2H_MSGRING_CONTROL_COMPLETE_MAX_ITEM,
    BRCMF_D2H_MSGRING_TX_COMPLETE_MAX_ITEM,
    BRCMF_D2H_MSGRING_RX_COMPLETE_MAX_ITEM,
    BRCMF_H2D_MSGRING_CONTROL_SUBMIT_ITEMSIZE,
    BRCMF_H2D_MSGRING_RXPOST_SUBMIT_ITEMSIZE,
    BRCMF_D2H_MSGRING_CONTROL_COMPLETE_ITEMSIZE,
    BRCMF_D2H_MSGRING_TX_COMPLETE_ITEMSIZE,
    BRCMF_D2H_MSGRING_RX_COMPLETE_ITEMSIZE,
    BRCMF_BUSTYPE_PCIE,
    IRQF_SHARED,
    IEEE80211_RATE_SHORT_PREAMBLE,
    WLAN_CIPHER_SUITE_AES_CMAC,
    WLAN_CIPHER_SUITE_CCMP,
    WLAN_CIPHER_SUITE_TKIP,
    WLAN_CIPHER_SUITE_WEP40,
    WLAN_CIPHER_SUITE_WEP104,
    WLAN_EID_VENDOR_SPECIFIC,
    WIPHY_PARAM_RETRY_SHORT,
    WIPHY_PARAM_RTS_THRESHOLD,
    WIPHY_PARAM_FRAG_THRESHOLD,
    WIPHY_PARAM_RETRY_LONG,
    WLAN_REASON_DEAUTH_LEAVING,
    WLAN_REASON_UNSPECIFIED,
    NL80211_WPA_VERSION_1,
    NL80211_WPA_VERSION_2,
    NL80211_AUTHTYPE_OPEN_SYSTEM,
    NL80211_AUTHTYPE_SHARED_KEY,
    WLAN_EID_RSN,
    WLAN_EID_TIM,
    WLAN_EID_COUNTRY,
    WLAN_EID_SSID,
    NL80211_AUTHTYPE_AUTOMATIC,
    WLAN_AKM_SUITE_PSK,
    WLAN_AKM_SUITE_8021X,
    WLAN_AKM_SUITE_8021X_SHA256,
    WLAN_AKM_SUITE_PSK_SHA256,
    NL80211_BSS_SELECT_ATTR_BAND_PREF = 0,
    __NL80211_BSS_SELECT_ATTR_INVALID,
    NL80211_BSS_SELECT_ATTR_RSSI_ADJUST,
    NL80211_BSS_SELECT_ATTR_RSSI,
    NL80211_STA_INFO_STA_FLAGS,
    NL80211_STA_FLAG_WME = 0,
    NL80211_STA_FLAG_ASSOCIATED,
    NL80211_STA_FLAG_AUTHENTICATED,
    NL80211_STA_FLAG_AUTHORIZED,
    NL80211_STA_INFO_BSS_PARAM,
    NL80211_STA_INFO_CONNECTED_TIME,
    NL80211_STA_INFO_RX_BITRATE,
    NL80211_STA_INFO_TX_BYTES,
    NL80211_STA_INFO_RX_BYTES,
    NL80211_STA_INFO_CHAIN_SIGNAL,
    IEEE80211_HT_STBC_PARAM_DUAL_CTS_PROT,
    BSS_PARAM_FLAGS_CTS_PROT,
    BSS_PARAM_FLAGS_SHORT_PREAMBLE,
    WLAN_CAPABILITY_SHORT_SLOT_TIME,
    BSS_PARAM_FLAGS_SHORT_SLOT_TIME,
    IEEE80211_CHAN_RADAR,
    IEEE80211_CHAN_NO_IR,
    IEEE80211_CHAN_NO_HT40,
    IEEE80211_CHAN_NO_HT40PLUS,
    IEEE80211_CHAN_DISABLED,
    IEEE80211_CHAN_NO_HT40MINUS,
    IEEE80211_CHAN_NO_80MHZ,
    NL80211_STA_INFO_TX_BITRATE,
    NL80211_STA_INFO_SIGNAL,
    NL80211_STA_INFO_TX_PACKETS,
    NL80211_STA_INFO_RX_DROP_MISC,
    NL80211_STA_INFO_TX_FAILED,
    NL80211_STA_INFO_RX_PACKETS,
    WLAN_CAPABILITY_SHORT_PREAMBLE,
    NL80211_STA_FLAG_TDLS_PEER,
    NL80211_STA_INFO_INACTIVE_TIME,
    CFG80211_BSS_FTYPE_UNKNOWN,
    WLAN_CAPABILITY_IBSS,
    UPDATE_ASSOC_IES,
    WLAN_STATUS_SUCCESS,
    WLAN_STATUS_AUTH_TIMEOUT,
    IEEE80211_HT_CAP_SGI_40,
    IEEE80211_HT_CAP_SUP_WIDTH_20_40,
    IEEE80211_HT_CAP_DSSSCCK40,
    IEEE80211_HT_MAX_AMPDU_64K,
    IEEE80211_HT_MPDU_DENSITY_16,
    IEEE80211_HT_MCS_TX_DEFINED,
    IEEE80211_HT_CAP_SGI_20,
    IEEE80211_VHT_CAP_SUPP_CHAN_WIDTH_160MHZ,
    IEEE80211_VHT_CAP_SHORT_GI_160,
    IEEE80211_VHT_MCS_SUPPORT_0_9,
    IEEE80211_VHT_CAP_SHORT_GI_80,
    IEEE80211_VHT_CAP_SU_BEAMFORMEE_CAPABLE,
    IEEE80211_VHT_CAP_SU_BEAMFORMER_CAPABLE,
    IEEE80211_VHT_CAP_BEAMFORMEE_STS_SHIFT = 0,
    IEEE80211_VHT_CAP_SOUNDING_DIMENSIONS_SHIFT,
    IEEE80211_VHT_CAP_MU_BEAMFORMEE_CAPABLE,
    IEEE80211_VHT_CAP_MU_BEAMFORMER_CAPABLE,
    IEEE80211_VHT_CAP_VHT_LINK_ADAPTATION_VHT_MRQ_MFB,
    IEEE80211_STYPE_ASSOC_REQ,
    IEEE80211_STYPE_REASSOC_REQ,
    IEEE80211_STYPE_DISASSOC,
    IEEE80211_STYPE_AUTH,
    IEEE80211_STYPE_DEAUTH,
    CFG80211_SIGNAL_TYPE_MBM,
    WIPHY_FLAG_PS_ON_BY_DEFAULT,
    WIPHY_FLAG_HAS_REMAIN_ON_CHANNEL,
    WIPHY_FLAG_SUPPORTS_TDLS,
    WIPHY_FLAG_SUPPORTS_FW_ROAM,
    NL80211_EXT_FEATURE_4WAY_HANDSHAKE_STA_PSK,
    NL80211_EXT_FEATURE_4WAY_HANDSHAKE_STA_1X,
    WIPHY_FLAG_NETNS_OK,
    WIPHY_FLAG_OFFCHAN_TX,
    REGULATORY_CUSTOM_REG,
    NL80211_FEATURE_SCHED_SCAN_RANDOM_MAC_ADDR,
    IFF_ALLMULTI = 0,
    NET_SKB_PAD,
    IFF_PROMISC,
    ETH_P_PAE,
    NETDEV_TX_OK,
    PACKET_MULTICAST,
    IFF_UP,
    NETIF_F_IP_CSUM,
    NETREG_REGISTERED,
    NET_NAME_UNKNOWN,
    ETH_FRAME_LEN,
    ETH_FCS_LEN,
    CHECKSUM_PARTIAL,
    CHECKSUM_UNNECESSARY,
    BRCMF_H2D_TXFLOWRING_MAX_ITEM,
    BRCMF_H2D_TXFLOWRING_ITEMSIZE,
    USB_DIR_IN,
    USB_TYPE_CLASS,
    USB_RECIP_INTERFACE,
    NL80211_SCAN_FLAG_RANDOM_ADDR,
    WLAN_AUTH_OPEN,
    SSB_IDHIGH_RCHI,
    SSB_IDHIGH_RCHI_SHIFT,
    SSB_IDHIGH_RCLO,
    SSB_TMSLOW_RESET,
    SSB_TMSLOW_REJECT,
    SSB_IMSTATE_REJECT,
    SSB_TMSLOW_CLOCK,
    BCMA_IOCTL,
    BCMA_IOCTL_FGC,
    BCMA_IOCTL_CLK,
    BCMA_RESET_CTL,
    BCMA_RESET_CTL_RESET,
    SSB_TMSHIGH_BUSY,
    IRQF_TRIGGER_HIGH,
    SDIO_CCCR_IENx,
    SSB_IMSTATE_BUSY,
    SSB_IDLOW_INITIATOR,
    SSB_TMSHIGH_SERR,
    SSB_IMSTATE_IBE,
    SSB_IMSTATE_TO,
    BCMA_CORE_ARM_CM3,
    BCMA_CORE_ARM_CA7,
    BCMA_CORE_SYS_MEM,
    EILSEQ,
    BCMA_CORE_80211,
    BCMA_CC_CAP_EXT_AOB_PRESENT,
    BCMA_CORE_PMU,
    PAGE_SIZE,
    SSB_TMSLOW_FGC,
    MMC_RSP_SPI_R5,
    MMC_RSP_R5,
    MMC_CMD_ADTC,
    WIPHY_VENDOR_CMD_NEED_WDEV,
    WIPHY_VENDOR_CMD_NEED_NETDEV,
    SDIO_CCCR_ABORT,
    SDIO_IO_RW_EXTENDED,
    MMC_DATA_READ,
    MMC_DATA_WRITE,
    BRCMF_SCAN_IE_LEN_MAX,
    SD_IO_RW_EXTENDED,
    SG_MAX_SINGLE_ALLOC,
    USB_DIR_OUT,
    URB_ZERO_PACKET,
    USB_TYPE_VENDOR,
    MMC_CAP_NONREMOVABLE,
    SDIO_VENDOR_ID_BROADCOM,
    SDIO_DEVICE_ID_BROADCOM_43143,
    SDIO_DEVICE_ID_BROADCOM_43241,
    SDIO_DEVICE_ID_BROADCOM_4329,
    SDIO_DEVICE_ID_BROADCOM_4330,
    SDIO_DEVICE_ID_BROADCOM_4334,
    SDIO_DEVICE_ID_BROADCOM_43340,
    SDIO_DEVICE_ID_BROADCOM_43341,
    SDIO_DEVICE_ID_BROADCOM_43362,
    SDIO_DEVICE_ID_BROADCOM_43430,
    SDIO_DEVICE_ID_BROADCOM_4345,
    SDIO_DEVICE_ID_BROADCOM_43455,
    SDIO_DEVICE_ID_BROADCOM_4354,
    SDIO_DEVICE_ID_BROADCOM_4356,
    SDIO_DEVICE_ID_CYPRESS_4373,
    MMC_QUIRK_LENIENT_FN0,
    USB_CLASS_VENDOR_SPEC,
    USB_CLASS_MISC,
    USB_CLASS_WIRELESS_CONTROLLER,
    BRCMF_BUSTYPE_USB,
    USB_SPEED_SUPER_PLUS,
    USB_SPEED_SUPER,
    USB_SPEED_HIGH,
    DUMP_PREFIX_OFFSET,
};

typedef enum { IRQ_WAKE_THREAD, IRQ_NONE, IRQ_HANDLED } irqreturn_t;

enum ieee80211_vht_mcs_support { FOOVMS };

enum dma_data_direction {
    DMA_TO_DEVICE,
    DMA_FROM_DEVICE,
};

enum nl80211_tx_power_setting {
    NL80211_TX_POWER_AUTOMATIC,
    NL80211_TX_POWER_LIMITED,
    NL80211_TX_POWER_FIXED,

};

enum nl80211_key_type {
    NL80211_KEYTYPE_GROUP,
    NL80211_KEYTYPE_PAIRWISE,
};
enum nl80211_chan_width {
    NL80211_CHAN_WIDTH_20,
    NL80211_CHAN_WIDTH_20_NOHT,
    NL80211_CHAN_WIDTH_40,
    NL80211_CHAN_WIDTH_80,
    NL80211_CHAN_WIDTH_80P80,
    NL80211_CHAN_WIDTH_160,
    NL80211_CHAN_WIDTH_5,
    NL80211_CHAN_WIDTH_10,
};

enum nl80211_auth_type { FOONLAT };

enum nl80211_crit_proto_id {
    NL80211_CRIT_PROTO_DHCP,
};

enum nl80211_tdls_operation {
    NL80211_TDLS_DISCOVERY_REQ,
    NL80211_TDLS_SETUP,
    NL80211_TDLS_TEARDOWN,
};

enum nl80211_band {
    NL80211_BAND_2GHZ,
    NL80211_BAND_5GHZ,
    NL80211_BAND_60GHZ,
};

#define CONFIG_BRCMDBG 0
#define CONFIG_BRCM_TRACING 0

enum brcmf_bus_type { FOO2 };

extern uint64_t jiffies;

#define TP_PROTO(args...) args
#define MODULE_FIRMWARE(a)
#define MODULE_AUTHOR(a)
#define MODULE_DESCRIPTION(a)
#define MODULE_LICENSE(a)
#define module_param_named(a, b, c, d)
#define MODULE_PARM_DESC(a, b)
#define MODULE_DEVICE_TABLE(a, b)
#define EXPORT_SYMBOL(a)
#define MODULE_SUPPORTED_DEVICE(a)

#define __init
#define __exit
#define __iomem
#define __always_inline
#define __used
#undef __restrict  // conflicts with zircon/public/sysroot/sysroot/include/features.h
#define __restrict
#define __acquires(a)
#define __releases(a)
#define IS_ENABLED(a) (a)
#define PTR_ERR_OR_ZERO(a) (0)
#define HZ (60)

struct firmware {
    size_t size;
    void* data;
};

struct device {
    void* of_node;
    void* parent;
};

struct sg_table {
    void* sgl;
    int orig_nents;
};

struct sk_buff;
struct sk_buff_head {
    uint32_t priority;
    int qlen;
    struct sk_buff* next;
};

struct sk_buff {
    uint16_t protocol;
    int priority;
    uint16_t len;
    uint32_t data_len;
    uint32_t end;
    uint32_t tail;
    void* data;
    void* next;
    void* prev;
    void* cb;
    uint32_t pkt_type;
    uint32_t ip_summed;
};

struct {
    int pid;
} * current;

struct brcmfmac_pd_cc_entry {
    uint8_t* iso3166;
    uint32_t rev;
    uint8_t* cc;
};

struct brcmfmac_pd_cc {
    int table_size;
    struct brcmfmac_pd_cc_entry* table;
};

struct brcmfmac_pd_device {
    uint32_t bus_type;
    uint32_t id;
    int rev;
    struct brcmfmac_pd_cc country_codes[555];
    struct {
        void* sdio;
    } bus;
};

struct brcmfmac_platform_data {
    int (*power_on)();
    int (*power_off)();
    char* fw_alternative_path;
    int device_count;
    struct brcmfmac_pd_device devices[555];
};

struct platform_device {
    void* dev;
};

struct platform_driver {
    int (*remove)(struct platform_device* pdev);
    struct {
        char* name;
    } driver;
};

struct net_device_ops {
    void* ndo_open;
    void* ndo_stop;
    void* ndo_start_xmit;
    void* ndo_set_mac_address;
    void* ndo_set_rx_mode;
};

struct ethtool_ops {
    void* get_drvinfo;
};

struct net_device {
    struct wireless_dev* ieee80211_ptr;
    const struct net_device_ops* netdev_ops;
    const struct ethtool_ops* ethtool_ops;
    void* dev_addr;
    void* name;
    uint8_t name_assign_type;
    uint32_t flags;
    struct {
        int tx_dropped;
        int tx_packets;
        int tx_bytes;
        int rx_packets;
        int rx_bytes;
        int multicast;
        int rx_errors;
        int tx_errors;
    } stats;
    uint32_t features;
    uint32_t needed_headroom;
    void* priv_destructor;
    int reg_state;
    int needs_free_netdev;
};

void ether_setup(void);

struct ieee80211_channel {
    int hw_value;
    uint32_t flags;
    int center_freq;
    int max_antenna_gain;
    int max_power;
    int band;
    uint32_t orig_flags;
};

struct ieee80211_rate {
    int bitrate;
    uint32_t flags;
    uint32_t hw_value;
};

struct ieee80211_supported_band {
    int band;
    struct ieee80211_rate* bitrates;
    int n_bitrates;
    struct ieee80211_channel* channels;
    uint32_t n_channels;
    struct {
        int ht_supported;
        uint16_t cap;
        int ampdu_factor;
        int ampdu_density;
        struct {
            void* rx_mask;
            uint32_t tx_params;
        } mcs;
    } ht_cap;
    struct {
        int vht_supported;
        uint32_t cap;
        struct {
            uint16_t rx_mcs_map;
            uint16_t tx_mcs_map;
        } vht_mcs;
    } vht_cap;
};

struct mac_address {
    uint8_t* addr;
};

struct regulatory_request {
    char alpha2[44];
    int initiator;
};

struct wiphy {
    int max_sched_scan_reqs;
    int max_sched_scan_plan_interval;
    int max_sched_scan_ie_len;
    int max_match_sets;
    int max_sched_scan_ssids;
    uint32_t rts_threshold;
    uint32_t frag_threshold;
    uint32_t retry_long;
    uint32_t retry_short;
    uint32_t interface_modes;
    struct ieee80211_supported_band* bands[555];
    int n_iface_combinations;
    struct ieee80211_iface_combination* iface_combinations;
    uint32_t max_scan_ssids;
    uint32_t max_scan_ie_len;
    uint32_t max_num_pmkids;
    struct mac_address* addresses;
    uint32_t n_addresses;
    uint32_t signal_type;
    const uint32_t* cipher_suites;
    uint32_t n_cipher_suites;
    uint32_t bss_select_support;
    uint32_t flags;
    const struct ieee80211_txrx_stypes* mgmt_stypes;
    uint32_t max_remain_on_channel_duration;
    uint32_t n_vendor_commands;
    const struct wiphy_vendor_command* vendor_commands;
    void* perm_addr;
    void (*reg_notifier)(struct wiphy*, struct regulatory_request*);
    uint32_t regulatory_flags;
    uint32_t features;
};

struct vif_params {
    void* macaddr;
};

struct wireless_dev {
    struct net_device* netdev;
    int iftype;
    void* address;
    struct wiphy* wiphy;
};

struct cfg80211_ssid {
    size_t ssid_len;
    char* ssid;
};

struct cfg80211_scan_request {
    int n_ssids;
    int n_channels;
    void* ie;
    int ie_len;
    struct ieee80211_channel* channels[555];
    struct cfg80211_ssid* ssids;
    struct wiphy* wiphy;
};

enum nl80211_iftype {
    NL80211_IFTYPE_P2P_GO,
    NL80211_IFTYPE_P2P_CLIENT,
    NL80211_IFTYPE_P2P_DEVICE,
    NL80211_IFTYPE_AP,
    NL80211_IFTYPE_ADHOC,
    NL80211_IFTYPE_STATION,
    NL80211_IFTYPE_AP_VLAN,
    NL80211_IFTYPE_WDS,
    NL80211_IFTYPE_MONITOR,
    NL80211_IFTYPE_MESH_POINT,
    NL80211_IFTYPE_UNSPECIFIED,
    NUM_NL80211_IFTYPES,
};

struct ieee80211_mgmt {
    int u;
    char* bssid;
    void* da;
    void* sa;
    uint16_t frame_control;
};

struct pci_dev {
    struct device dev;
    int device;
    int irq;
    struct {
        int number;
    } * bus;
    int vendor;
};

struct ethhdr {
    uint32_t h_proto;
    void* h_dest;
    void* h_source;
};

struct work_struct {
    int foo;
};

struct list_head {
    void* next;
};

struct mutex {
    int foo;
};

struct notifier_block {
    int foo;
};

struct in6_addr {
    int foo;
};

struct brcmfmac_sdio_pd {
    int oob_irq_nr;
    int sd_sgentry_align;
    int sd_head_align;
    int drive_strength;
    size_t txglomsz;
    int oob_irq_flags;
    int oob_irq_supported;
    int broken_sg_support;
};

struct seq_file {
    void* private;
};

struct timer_list {
    uint64_t expires;
};

struct asdf {
    int foo;
};

struct completion {
    int foo;
};

struct sdio_func {
    uint32_t class;
    uint32_t vendor;
    int cur_blksize;
    int enable_timeout;
    int device;
    struct device dev;
    int num;
    struct {
        struct mmc_host* host;
        uint32_t quirks;
        void** sdio_func;
    } * card;
};

typedef uint64_t dma_addr_t;

struct pci_device_id {
    int a, b, c, d, e, f, g;
};

struct pci_driver {
    struct pci_device_id node;
    char* name;
    const void* id_table;
    int (*probe)(struct pci_dev* pdev, const struct pci_device_id* id);
    void (*remove)(struct pci_dev* pdev);
};

struct ieee80211_regdomain {
    int n_reg_rules;
    char* alpha2;
    struct {
        struct {
            int start_freq_khz;
            int end_freq_khz;
            int max_bandwidth_khz;
        } freq_range;
        struct {
            int max_antenna_gain;
            int max_eirp;
        } power_rule;
        uint32_t flags;
        uint32_t dfs_cac_ms;
    } reg_rules[];
};
#define REG_RULE(...) \
    { .flags = 0 }  // Fill up reg_rules

struct cfg80211_sched_scan_request {
    int n_ssids;
    int n_match_sets;
    uint64_t reqid;
    int flags;
    void* mac_addr;
    struct cfg80211_ssid* ssids;
    int n_channels;
    struct ieee80211_channel* channels[555];
    struct {
        int interval;
    } * scan_plans;
    void* mac_addr_mask;
    void* match_sets;
};

struct wiphy_vendor_command {
    struct {
        int vendor_id;
        int subcmd;
    } unknown_name;
    uint32_t flags;
    void* doit;
};

struct cfg80211_chan_def {
    struct ieee80211_channel* chan;
    int center_freq1;
    int center_freq2;
    int width;
};

struct iface_combination_params {
    int num_different_channels;
    int iftype_num[555];
};

struct cfg80211_scan_info {
    int aborted;
};

struct cfg80211_ibss_params {
    char* ssid;
    int privacy;
    int beacon_interval;
    int ssid_len;
    char* bssid;
    int channel_fixed;
    struct cfg80211_chan_def chandef;
    void* ie;
    int ie_len;
    int basic_rates;
};

struct cfg80211_bss_selection {
    int behaviour;
    struct {
        int band_pref;
        struct {
            int band;
            int delta;
        } adjust;
    } param;
};

struct cfg80211_connect_params {
    struct {
        int wpa_versions;
        int ciphers_pairwise[555];
        int n_ciphers_pairwise;
        int cipher_group;
        int n_akm_suites;
        int akm_suites[555];
        void* psk;
    } crypto;
    int auth_type;
    void* ie;
    int ie_len;
    int privacy;
    uint32_t key_len;
    int key_idx;
    void* key;
    int want_1x;
    struct ieee80211_channel* channel;
    void* ssid;
    int ssid_len;
    void* bssid;
    int bssid_len;
    struct cfg80211_bss_selection bss_select;
};

struct key_params {
    uint32_t key_len;
    int cipher;
    void* key;
};

struct nl80211_sta_flag_update {
    int mask;
    int set;
};

struct station_info {
    unsigned long filled;
    struct nl80211_sta_flag_update sta_flags;
    struct {
        uint32_t flags;
        uint32_t dtim_period;
        uint32_t beacon_interval;
    } bss_param;
    struct {
        uint32_t legacy;
    } txrate;
    struct {
        uint32_t legacy;
    } rxrate;
    uint32_t signal;
    uint32_t rx_packets;
    uint32_t rx_dropped_misc;
    uint32_t tx_packets;
    uint32_t tx_failed;
    uint32_t inactive_time;
    uint32_t connected_time;
    uint32_t tx_bytes;
    uint32_t rx_bytes;
    uint32_t chain_signal_avg[555];
    uint32_t chain_signal[555];
    uint32_t chains;
    void* assoc_req_ies;
    uint32_t assoc_req_ies_len;
    uint32_t generation;
};

struct cfg80211_wowlan {
    int disconnect;
    struct {
        uint8_t* pattern;
        uint32_t pattern_len;
        uint8_t* mask;
        uint32_t pkt_offset;
    } * patterns;
    uint32_t n_patterns;
    int magic_pkt;
    void* nd_config;
    int gtk_rekey_failure;
};

struct cfg80211_wowlan_nd_match {
    struct {
        void* ssid;
        uint32_t ssid_len;
    } ssid;
    int n_channels;
    int* channels;
};

struct cfg80211_wowlan_nd_info {
    int n_matches;
    struct cfg80211_wowlan_nd_match* matches[555];
    int disconnect;
    int* patterns;
    int n_patterns;
};

struct cfg80211_pmksa {
    uint8_t* bssid;
    uint8_t* pmkid;
};

struct cfg80211_beacon_data {
    void* tail;
    int tail_len;
    void* head;
    int head_len;
    void* proberesp_ies;
    int proberesp_ies_len;
};

struct cfg80211_ap_settings {
    struct cfg80211_chan_def chandef;
    int beacon_interval;
    int dtim_period;
    void* ssid;
    size_t ssid_len;
    int auth_type;
    int inactivity_timeout;
    struct cfg80211_beacon_data beacon;
    int hidden_ssid;
};

struct station_del_parameters {
    void* mac;
    int reason_code;
};

struct station_parameters {
    uint32_t sta_flags_mask;
    uint32_t sta_flags_set;
};

struct cfg80211_mgmt_tx_params {
    struct ieee80211_channel* chan;
    uint8_t* buf;
    size_t len;
};

struct cfg80211_pmk_conf {
    void* pmk;
    int pmk_len;
};

struct cfg80211_ops {
    void* add_virtual_intf;
    void* del_virtual_intf;
    void* change_virtual_intf;
    void* scan;
    void* set_wiphy_params;
    void* join_ibss;
    void* leave_ibss;
    void* get_station;
    void* dump_station;
    void* set_tx_power;
    void* get_tx_power;
    void* add_key;
    void* del_key;
    void* get_key;
    void* set_default_key;
    void* set_default_mgmt_key;
    void* set_power_mgmt;
    void* connect;
    void* disconnect;
    void* suspend;
    void* resume;
    void* set_pmksa;
    void* del_pmksa;
    void* flush_pmksa;
    void* start_ap;
    void* stop_ap;
    void* change_beacon;
    void* del_station;
    void* change_station;
    void* sched_scan_start;
    void* sched_scan_stop;
    void* mgmt_frame_register;
    void* mgmt_tx;
    void* remain_on_channel;
    void* cancel_remain_on_channel;
    void* get_channel;
    void* start_p2p_device;
    void* stop_p2p_device;
    void* crit_proto_start;
    void* crit_proto_stop;
    void* tdls_oper;
    void* update_connect_params;
    void* set_pmk;
    void* del_pmk;
};

struct cfg80211_roam_info {
    struct ieee80211_channel* channel;
    void* bssid;
    void* req_ie;
    int req_ie_len;
    void* resp_ie;
    int resp_ie_len;
};

struct cfg80211_connect_resp_params {
    int status;
    void* bssid;
    void* req_ie;
    int req_ie_len;
    void* resp_ie;
    int resp_ie_len;
};

struct ieee80211_iface_combination {
    int num_different_channels;
    struct ieee80211_iface_limit* limits;
    int max_interfaces;
    int beacon_int_infra_match;
    int n_limits;
};

struct ieee80211_txrx_stypes {
    uint32_t tx;
    uint32_t rx;
};

struct ieee80211_iface_limit {
    int max;
    int types;
};

struct netdev_hw_addr {
    void* addr;
};

struct sockaddr {
    void* sa_data;
};

typedef int netdev_tx_t;

struct ethtool_drvinfo {
    void* driver;
    void* version;
    void* fw_version;
    void* bus_info;
};

struct mmc_request {
    void* data;
    void* cmd;
    uint32_t arg;
    uint32_t flags;
};

struct mmc_command {
    uint32_t arg;
    uint32_t flags;
    int error;
    uint32_t opcode;
};

struct mmc_data {
    int sg_len;
    int blocks;
    void* sg;
    int blksz;
    uint32_t flags;
    uint32_t error;
};

struct usb_ctrlrequest {
    uint16_t wLength;
    int bRequest;
    int wValue;
    int wIndex;
    int bRequestType;
};

struct urb {
    void* context;
    int actual_length;
    uint32_t status;
    int transfer_buffer_length;
    uint16_t transfer_flags;
};

struct cfg80211_match_set {
    struct cfg80211_ssid ssid;
    void* bssid;
};

struct va_format {
    va_list* va;
    const char* fmt;
};

struct mmc_host {
    void* parent;
    int max_blk_count;
    int max_req_size;
    uint32_t caps;
    int max_segs;
    int max_seg_size;
};

struct usb_interface_descriptor {
    int bInterfaceClass;
    int bInterfaceSubClass;
    int bInterfaceProtocol;
    int bInterfaceNumber;
    int bNumEndpoints;
};

struct usb_endpoint_descriptor {
    int foo;
};

struct usb_interface {
    struct {
        struct usb_interface_descriptor desc;
        struct {
            struct usb_endpoint_descriptor desc;
        } * endpoint;
    } * altsetting;
};

struct usb_device_id {
    int idVendor;
    int idProduct;
};

struct usb_device {
    int speed;
    struct device dev;
    struct {
        int bNumConfigurations;
        int bDeviceClass;
    } descriptor;
};

struct sdio_device_id {
    int foo;
};

struct sdio_driver {
    void* probe;
    void* remove;
    char* name;
    const void* id_table;
    struct {
        void* owner;
        void* pm;
    } drv;
};

struct device_driver {
    int foo;
};

struct usb_driver {
    char* name;
    void* probe;
    void* disconnect;
    void* suspend;
    void* resume;
    void* reset_resume;
    int disable_hub_initiated_lpm;
    const struct usb_device_id* id_table;
    struct {
        struct device_driver driver;
    } drvwrap;
};

#endif  // GARNET_DRIVERS_WLAN_THIRD_PARTY_BROADCOM_INCLUDE_LINUXISMS_H_
