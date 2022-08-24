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

#include "src/connectivity/wlan/drivers/third_party/nxp/nxpfmac/test/mlan_mocks.h"

namespace wlan::nxpfmac {

class MlanMockAdapterImpl {
 public:
  mlan_status OnMlanInterrupt(t_u16 msg_id, t_void* padapter) {
    if (on_mlan_interrupt_) {
      return on_mlan_interrupt_(msg_id, padapter);
    }
    return MLAN_STATUS_SUCCESS;
  }
  mlan_status OnMlanMainProcess(t_void* padapter) {
    if (on_mlan_main_process_) {
      return on_mlan_main_process_(padapter);
    }
    return MLAN_STATUS_SUCCESS;
  }

 private:
  friend class MlanMockAdapter;

  std::function<mlan_status(t_u16 msg_id, t_void* padapter)> on_mlan_interrupt_;
  std::function<mlan_status(t_void*)> on_mlan_main_process_;
};

MlanMockAdapter::MlanMockAdapter() { impl_ = std::make_unique<MlanMockAdapterImpl>(); }

// We need to declare this so that std::unqiue_ptr can point to an incomplete type in the header.
MlanMockAdapter::~MlanMockAdapter() = default;

void* MlanMockAdapter::GetAdapter() { return impl_.get(); }

void MlanMockAdapter::SetOnMlanInterrupt(
    std::function<mlan_status(t_u16 msg_id, t_void* padapter)>&& callback) {
  impl_->on_mlan_interrupt_ = std::move(callback);
}

void MlanMockAdapter::SetOnMlanMainProcess(std::function<mlan_status(t_void*)>&& callback) {
  impl_->on_mlan_main_process_ = std::move(callback);
}

}  // namespace wlan::nxpfmac

extern "C" mlan_status mlan_interrupt(t_u16 msg_id, t_void* padapter) {
  auto test = static_cast<wlan::nxpfmac::MlanMockAdapterImpl*>(padapter);
  return test->OnMlanInterrupt(msg_id, padapter);
}

extern "C" mlan_status mlan_main_process(t_void* padapter) {
  auto test = static_cast<wlan::nxpfmac::MlanMockAdapterImpl*>(padapter);
  return test->OnMlanMainProcess(padapter);
}
