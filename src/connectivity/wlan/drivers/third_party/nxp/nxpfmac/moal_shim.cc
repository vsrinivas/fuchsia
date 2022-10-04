// Copyright (c) 2022 The Fuchsia Authors
//
// Permission to use, copy, modify, and/or distribute this software for any purpose with or without
// fee is hereby granted, provided that the above copyright notice and this permission notice
// appear in all copies.
//
// THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES WITH REGARD TO THIS
// SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE
// AUTHOR BE LIABLE FOR ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
// WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT,
// NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR PERFORMANCE
// OF THIS SOFTWARE.

#include "moal_shim.h"

#include <string.h>
#include <zircon/syscalls.h>

#include <algorithm>
#include <mutex>

#include <wlan/drivers/timer/timer.h>

#include "src/connectivity/wlan/drivers/third_party/nxp/nxpfmac/align.h"
#include "src/connectivity/wlan/drivers/third_party/nxp/nxpfmac/debug.h"
#include "src/connectivity/wlan/drivers/third_party/nxp/nxpfmac/device.h"
#include "src/connectivity/wlan/drivers/third_party/nxp/nxpfmac/device_context.h"
#include "src/connectivity/wlan/drivers/third_party/nxp/nxpfmac/ioctl_adapter.h"

namespace {

mlan_status moal_malloc(t_void *pmoal, t_u32 size, t_u32 flag, t_u8 **ppbuf) {
  *ppbuf = static_cast<t_u8 *>(malloc(size));
  return MLAN_STATUS_SUCCESS;
}

mlan_status moal_free(t_void *pmoal, t_u8 *pbuf) {
  free(pbuf);
  return MLAN_STATUS_SUCCESS;
}

t_void *moal_memcpy(t_void *pmoal, t_void *pdest, const t_void *psrc, t_u32 num) {
  return memcpy(pdest, psrc, num);
}

t_void *moal_memcpy_ext(t_void *pmoal, t_void *pdest, const t_void *psrc, t_u32 num,
                        t_u32 dest_size) {
  return memcpy(pdest, psrc, std::min(num, dest_size));
}

t_void *moal_memset(t_void *pmoal, t_void *pmem, t_u8 byte, t_u32 num) {
  return memset(pmem, byte, num);
}

t_void *moal_memmove(t_void *pmoal, t_void *pdest, const t_void *psrc, t_u32 num) {
  return memmove(pdest, psrc, num);
}

t_s32 moal_memcmp(t_void *pmoal, const t_void *pmem1, const t_void *pmem2, t_u32 num) {
  return memcmp(pmem1, pmem2, num);
}

void moal_udelay(t_void *pmoal, t_u32 delay) { zx_nanosleep(zx_deadline_after(ZX_USEC(delay))); }

void moal_usleep_range(t_void *pmoal, t_u32 min_delay, t_u32 max_delay) {
  // TODO(fxbug.dev/106939): Investigate need for timer slack here. Unfortunately we can't specify
  // slack for a single call to zx_nanosleep, it would have to be applied to the entire job. Another
  // option would be to create a timer with a specific slack for this. For now just sleep.
  uint64_t halfway = (static_cast<uint64_t>(min_delay) + static_cast<uint64_t>(max_delay)) / 2;
  zx_nanosleep(zx_deadline_after(ZX_USEC(halfway)));
}

mlan_status moal_get_boot_ktime(t_void *pmoal, t_u64 *pnsec) {
  *pnsec = zx_clock_get_monotonic();
  return MLAN_STATUS_SUCCESS;
}
mlan_status moal_get_system_time(t_void *pmoal, t_u32 *psec, t_u32 *pusec) {
  zx_time_t micros = zx_clock_get_monotonic() / 1'000;
  zx_time_t seconds = micros / 1'000'000;
  micros -= seconds * 1'000'000;

  *psec = static_cast<uint32_t>(seconds);
  *pusec = static_cast<uint32_t>(micros);

  return MLAN_STATUS_SUCCESS;
}
mlan_status moal_init_timer(t_void *pmoal, t_void **pptimer,
                            IN t_void (*callback)(t_void *pcontext), t_void *pcontext) {
  using wlan::drivers::timer::Timer;
  wlan::nxpfmac::Device *device = static_cast<wlan::nxpfmac::DeviceContext *>(pmoal)->device_;

  *pptimer = new Timer(device->GetDispatcher(), callback, pcontext);

  return MLAN_STATUS_SUCCESS;
}
mlan_status moal_free_timer(t_void *pmoal, t_void *ptimer) {
  delete static_cast<wlan::drivers::timer::Timer *>(ptimer);
  return MLAN_STATUS_SUCCESS;
}

mlan_status moal_start_timer(t_void *pmoal, t_void *ptimer, t_u8 periodic, t_u32 msec) {
  auto timer = static_cast<wlan::drivers::timer::Timer *>(ptimer);

  zx_status_t status = ZX_OK;
  if (periodic) {
    status = timer->StartPeriodic(ZX_MSEC(msec));
  } else {
    status = timer->StartOneshot(ZX_MSEC(msec));
  }

  return status == ZX_OK ? MLAN_STATUS_SUCCESS : MLAN_STATUS_FAILURE;
}

mlan_status moal_stop_timer(t_void *pmoal, t_void *ptimer) {
  auto timer = static_cast<wlan::drivers::timer::Timer *>(ptimer);

  zx_status_t status = timer->Stop();

  return status == ZX_OK ? MLAN_STATUS_SUCCESS : MLAN_STATUS_FAILURE;
}

mlan_status moal_init_lock(t_void *pmoal, t_void **pplock) {
  *pplock = new std::mutex;
  return MLAN_STATUS_SUCCESS;
}

mlan_status moal_free_lock(t_void *pmoal, t_void *pplock) {
  delete static_cast<std::mutex *>(pplock);
  return MLAN_STATUS_SUCCESS;
}

// The thread safety analyzer doesn't like that the mutex is locked but never released. Disable
// it manually and hope that the calling code behaves.
mlan_status moal_spin_lock(t_void *pmoal, t_void *plock) __TA_NO_THREAD_SAFETY_ANALYSIS {
  static_cast<std::mutex *>(plock)->lock();
  return MLAN_STATUS_SUCCESS;
}

// The thread safety analyzer doesn't like that the mutex is unlocked when it was never locked
// in the first place. Disable it manually and hope that the calling code behaves.
mlan_status moal_spin_unlock(t_void *pmoal, t_void *plock) __TA_NO_THREAD_SAFETY_ANALYSIS {
  static_cast<std::mutex *>(plock)->unlock();
  return MLAN_STATUS_SUCCESS;
}

t_void moal_print(t_void *pmoal, t_u32 level, char *pformat, IN...) {
  NXPF_ERR("%s called", __func__);
}
t_void moal_print_netintf(t_void *pmoal, t_u32 bss_index, t_u32 level) {
  NXPF_ERR("%s called", __func__);
}
t_void moal_assert(t_void *pmoal, t_u32 cond) { NXPF_ERR("%s called", __func__); }
t_void moal_hist_data_add(t_void *pmoal, t_u32 bss_index, t_u16 rx_rate, t_s8 snr, t_s8 nflr,
                          t_u8 antenna) {
  // Implement this but don't log it for now, this will be called on each received frame.
}
t_void moal_updata_peer_signal(t_void *pmoal, t_u32 bss_index, t_u8 *peer_addr, t_s8 snr,
                               t_s8 nflr) {
  NXPF_ERR("%s called", __func__);
}
t_u64 moal_do_div(t_u64 num, t_u32 base) { return num / base; }
void moal_tp_accounting(t_void *pmoal, t_void *buf, t_u32 drop_point) {
  NXPF_ERR("%s called", __func__);
}
void moal_tp_accounting_rx_param(t_void *pmoal, unsigned int type, unsigned int rsvd1) {
  // Implement this but don't log it for now, this will be called on each received frame.
}
void moal_amsdu_tp_accounting(t_void *pmoal, t_s32 delay, t_s32 copy_delay) {
  NXPF_ERR("%s called", __func__);
}

mlan_status moal_get_fw_data(t_void *pmoal, t_u32 offset, t_u32 len, t_u8 *pbuf) {
  NXPF_ERR("%s called", __func__);
  return MLAN_STATUS_FAILURE;
}

mlan_status moal_get_vdll_data(t_void *pmoal, t_u32 len, t_u8 *pbuf) {
  NXPF_ERR("%s called", __func__);
  return MLAN_STATUS_FAILURE;
}

mlan_status moal_get_hw_spec_complete(t_void *pmoal, mlan_status status, pmlan_hw_info phw,
                                      pmlan_bss_tbl ptbl) {
  // There doesn't seem to be a lot of interesting things we can do here.
  if (status != MLAN_STATUS_SUCCESS) {
    NXPF_ERR("Getting hardware spec failed: %d", status);
    // Other mlan-based drivers always return success here, even if the callback indicates failure.
  }
  return MLAN_STATUS_SUCCESS;
}

mlan_status moal_init_fw_complete(t_void *pmoal, mlan_status status) {
  wlan::nxpfmac::Device *device = static_cast<wlan::nxpfmac::DeviceContext *>(pmoal)->device_;
  device->OnFirmwareInitComplete(status == MLAN_STATUS_SUCCESS ? ZX_OK : ZX_ERR_INTERNAL);
  return MLAN_STATUS_SUCCESS;
}

mlan_status moal_shutdown_fw_complete(t_void *pmoal, mlan_status status) {
  wlan::nxpfmac::Device *device = static_cast<wlan::nxpfmac::DeviceContext *>(pmoal)->device_;
  device->OnFirmwareShutdownComplete(status == MLAN_STATUS_SUCCESS ? ZX_OK : ZX_ERR_INTERNAL);
  return MLAN_STATUS_SUCCESS;
}

mlan_status moal_send_packet_complete(t_void *pmoal, pmlan_buffer pmbuf, mlan_status status) {
  wlan::nxpfmac::DataPlane *plane = static_cast<wlan::nxpfmac::DeviceContext *>(pmoal)->data_plane_;
  auto frame = reinterpret_cast<wlan::drivers::components::Frame *>(pmbuf->pdesc);
  plane->CompleteTx(std::move(*frame), status == MLAN_STATUS_SUCCESS ? ZX_OK : ZX_ERR_INTERNAL);
  return MLAN_STATUS_SUCCESS;
}

mlan_status moal_recv_complete(t_void *pmoal, pmlan_buffer pmbuf, t_u32 port, mlan_status status) {
  NXPF_ERR("%s called", __func__);
  return MLAN_STATUS_FAILURE;
}

mlan_status moal_recv_packet(t_void *pmoal, pmlan_buffer pmbuf) {
  wlan::nxpfmac::DataPlane *plane = static_cast<wlan::nxpfmac::DeviceContext *>(pmoal)->data_plane_;
  auto frame = reinterpret_cast<wlan::drivers::components::Frame *>(pmbuf->pdesc);

  uint8_t *data = pmbuf->pbuf + pmbuf->data_offset;
  frame->ShrinkHead(static_cast<uint32_t>(data - frame->Data()));
  frame->SetSize(pmbuf->data_len);
  frame->SetPortId((uint8_t)pmbuf->bss_index);

  plane->CompleteRx(std::move(*frame));
  return MLAN_STATUS_SUCCESS;
}

mlan_status moal_recv_amsdu_packet(t_void *pmoal, pmlan_buffer pmbuf) {
  NXPF_ERR("%s called", __func__);
  return MLAN_STATUS_FAILURE;
}

mlan_status moal_recv_event(t_void *pmoal, pmlan_event pmevent) {
  static_cast<wlan::nxpfmac::DeviceContext *>(pmoal)->event_handler_->OnEvent(pmevent);
  return MLAN_STATUS_SUCCESS;
}

mlan_status moal_ioctl_complete(t_void *pmoal, pmlan_ioctl_req pioctl_req, mlan_status ml_status) {
  if (!pioctl_req) {
    return MLAN_STATUS_FAILURE;
  }

  if (!wlan::nxpfmac::IoctlRequest<nullptr_t>::IsIoctlRequest(*pioctl_req)) {
    NXPF_WARN("Unexpected ioctl request is not an MlanIoctlRequest");
    // mlan ignores the return value of this function so it doesn't matter what we return here.
    return MLAN_STATUS_SUCCESS;
  }

  auto context = static_cast<wlan::nxpfmac::DeviceContext *>(pmoal);
  context->ioctl_adapter_->OnIoctlComplete(pioctl_req, ml_status == MLAN_STATUS_SUCCESS
                                                           ? wlan::nxpfmac::IoctlStatus::Success
                                                           : wlan::nxpfmac::IoctlStatus::Failure);
  return MLAN_STATUS_SUCCESS;
}

mlan_status moal_alloc_mlan_buffer(t_void *pmoal, t_u32 size, ppmlan_buffer pmbuf) {
  wlan::nxpfmac::DataPlane *plane = static_cast<wlan::nxpfmac::DeviceContext *>(pmoal)->data_plane_;
  std::optional<wlan::drivers::components::Frame> frame = plane->AcquireFrame();

  if (!frame.has_value()) {
    NXPF_WARN("Failed to acquire RX frame");
    return MLAN_STATUS_FAILURE;
  }

  // Align the headroom used for storage to whatever size the bus wants. This ensures that the
  // actual data transferred is aligned properly.
  constexpr size_t kOccupiedHeadroom = wlan::nxpfmac::align<size_t>(
      sizeof(mlan_buffer) + sizeof(wlan::drivers::components::Frame), MLAN_SDIO_BLOCK_SIZE);

  if (size + kOccupiedHeadroom > frame->Size()) {
    NXPF_ERR("Requested mlan buffer size of %u is too big", size);
    return MLAN_STATUS_FAILURE;
  }

  // In order to avoid additional allocation we will place both the Frame object and mlan_buffer
  // struct at the beginning of the RX frame. Get the location where the Frame will be first.
  auto frame_ptr = reinterpret_cast<wlan::drivers::components::Frame *>(frame->Data());

  // Then use placement new to allocate a Frame by move construction in this location.
  new (frame_ptr) wlan::drivers::components::Frame(std::move(*frame));

  // Then place the mlan_buffer after that and clear it.
  *pmbuf = reinterpret_cast<mlan_buffer *>(frame_ptr->Data() + sizeof(*frame_ptr));
  memset(*pmbuf, 0, sizeof(**pmbuf));

  // Now all we have to do is remember where our frame is located and point to the actual point in
  // memory where data should be received.
  (*pmbuf)->pdesc = frame_ptr;
  (*pmbuf)->pbuf = frame_ptr->Data() + kOccupiedHeadroom;

  return MLAN_STATUS_SUCCESS;
}

mlan_status moal_free_mlan_buffer(t_void *pmoal, pmlan_buffer pmbuf) {
  auto frame = reinterpret_cast<wlan::drivers::components::Frame *>(pmbuf->pdesc);
  // Call destructor on frame, if it was received it should have been released from storage and
  // nothing should really happen. If it failed then it will be returned to frame storage. We call
  // the destructor and not delete because the frame wasn't allocated, it was just placed in this
  // location.
  frame->~Frame();
  return MLAN_STATUS_SUCCESS;
}

}  // namespace

namespace wlan::nxpfmac {

void populate_callbacks(mlan_device *mlan_dev) {
  mlan_dev->callbacks.moal_malloc = &moal_malloc;
  mlan_dev->callbacks.moal_mfree = &moal_free;
  mlan_dev->callbacks.moal_memset = &moal_memset;
  mlan_dev->callbacks.moal_memmove = &moal_memmove;
  mlan_dev->callbacks.moal_udelay = &moal_udelay;
  mlan_dev->callbacks.moal_usleep_range = &moal_usleep_range;
  mlan_dev->callbacks.moal_init_lock = &moal_init_lock;
  mlan_dev->callbacks.moal_free_lock = &moal_free_lock;
  mlan_dev->callbacks.moal_spin_lock = &moal_spin_lock;
  mlan_dev->callbacks.moal_spin_unlock = &moal_spin_unlock;
  mlan_dev->callbacks.moal_get_fw_data = &moal_get_fw_data;
  mlan_dev->callbacks.moal_get_vdll_data = &moal_get_vdll_data;
  mlan_dev->callbacks.moal_get_hw_spec_complete = &moal_get_hw_spec_complete;
  mlan_dev->callbacks.moal_init_fw_complete = &moal_init_fw_complete;
  mlan_dev->callbacks.moal_shutdown_fw_complete = &moal_shutdown_fw_complete;
  mlan_dev->callbacks.moal_send_packet_complete = &moal_send_packet_complete;
  mlan_dev->callbacks.moal_recv_complete = &moal_recv_complete;
  mlan_dev->callbacks.moal_recv_packet = &moal_recv_packet;

  // TODO(https://fxbug.dev/110577): Implement support for ASMDU packet delivery. By not providing
  // this callback we let mlan handle ASMDU packets for now. It will deaggregate them into multiple
  // single packet deliveries.
  // mlan_dev->callbacks.moal_recv_amsdu_packet = &moal_recv_amsdu_packet;
  // Reference the function for now so we don't get errors about unused functions.
  (void)moal_recv_amsdu_packet;

  mlan_dev->callbacks.moal_recv_event = &moal_recv_event;
  mlan_dev->callbacks.moal_ioctl_complete = &moal_ioctl_complete;
  mlan_dev->callbacks.moal_alloc_mlan_buffer = &moal_alloc_mlan_buffer;
  mlan_dev->callbacks.moal_free_mlan_buffer = &moal_free_mlan_buffer;
  mlan_dev->callbacks.moal_memset = &moal_memset;
  mlan_dev->callbacks.moal_memcpy = &moal_memcpy;
  mlan_dev->callbacks.moal_memcpy_ext = &moal_memcpy_ext;
  mlan_dev->callbacks.moal_memmove = &moal_memmove;
  mlan_dev->callbacks.moal_memcmp = &moal_memcmp;
  mlan_dev->callbacks.moal_udelay = &moal_udelay;
  mlan_dev->callbacks.moal_usleep_range = &moal_usleep_range;
  mlan_dev->callbacks.moal_get_boot_ktime = &moal_get_boot_ktime;
  mlan_dev->callbacks.moal_get_system_time = &moal_get_system_time;
  mlan_dev->callbacks.moal_init_timer = &moal_init_timer;
  mlan_dev->callbacks.moal_free_timer = &moal_free_timer;
  mlan_dev->callbacks.moal_start_timer = &moal_start_timer;
  mlan_dev->callbacks.moal_stop_timer = &moal_stop_timer;
  mlan_dev->callbacks.moal_init_lock = &moal_init_lock;
  mlan_dev->callbacks.moal_free_lock = &moal_free_lock;
  mlan_dev->callbacks.moal_spin_lock = &moal_spin_lock;
  mlan_dev->callbacks.moal_spin_unlock = &moal_spin_unlock;
  mlan_dev->callbacks.moal_print = &moal_print;
  mlan_dev->callbacks.moal_print_netintf = &moal_print_netintf;
  mlan_dev->callbacks.moal_assert = &moal_assert;
  mlan_dev->callbacks.moal_hist_data_add = &moal_hist_data_add;
  mlan_dev->callbacks.moal_updata_peer_signal = &moal_updata_peer_signal;
  mlan_dev->callbacks.moal_do_div = &moal_do_div;
  mlan_dev->callbacks.moal_tp_accounting = &moal_tp_accounting;
  mlan_dev->callbacks.moal_tp_accounting_rx_param = &moal_tp_accounting_rx_param;
  mlan_dev->callbacks.moal_amsdu_tp_accounting = &moal_amsdu_tp_accounting;
}

}  // namespace wlan::nxpfmac
