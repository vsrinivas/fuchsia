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

#ifndef SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_NXP_NXPFMAC_TEST_MLAN_MOCKS_H_
#define SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_NXP_NXPFMAC_TEST_MLAN_MOCKS_H_

#include <functional>
#include <memory>

#include "src/connectivity/wlan/drivers/third_party/nxp/nxpfmac/mlan.h"

namespace wlan::nxpfmac {

class MlanMockAdapterImpl;

// Because we are relying on link-time replacement of mlan in our unit tests there can only be one
// definition of mlan functions shared between all tests in a test executable. These functions are
// implemented in the .cc file associated with this header. In order for them to work they need to
// receive the correct mlan_adapter pointer in their calls. It should point to an object of this
// class. If no callback is provided the mlan functions will indicate success where a return code is
// needed or do nothing otherwise. If a callback is provided they will ONLY call that callback,
// nothing else. In order for this to work the caller must use the adapter pointer returned by
// GetAdapter as the adapter when calling into mlan functionality.
//
// As an example to make a call to mlan_main_process you would do:
//   MlanMockAdapter adapter;
//   adapter.SetOnMlanMainProcess([](t_void* adapter) -> mlan_status { /* do something */ });
//   mlan_main_process(adapter.GetAdapter());
//
// Or if you're using an object that will perform this call for you that object would take a
// pmlan_adapter parameter somewhere. You would use GetAdapter() for this parameter.

class MlanMockAdapter {
 public:
  MlanMockAdapter();
  ~MlanMockAdapter();

  void* GetAdapter();

  void SetOnMlanInterrupt(std::function<mlan_status(t_u16, t_void*)>&& callback);
  void SetOnMlanMainProcess(std::function<mlan_status(t_void*)>&& callback);
  void SetOnMlanIoctl(std::function<mlan_status(t_void*, pmlan_ioctl_req)>&& callback);
  void SetOnMlanSendPacket(std::function<mlan_status(t_void*, pmlan_buffer)>&& callback);
  void SetOnMlanRxProcess(std::function<mlan_status(t_void*, t_u8*)>&& callback);

 private:
  std::unique_ptr<MlanMockAdapterImpl> impl_;
};

}  // namespace wlan::nxpfmac

#endif  // SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_NXP_NXPFMAC_TEST_MLAN_MOCKS_H_
