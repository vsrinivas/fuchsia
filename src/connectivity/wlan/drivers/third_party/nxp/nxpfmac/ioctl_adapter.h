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

#ifndef SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_NXP_NXPFMAC_IOCTL_ADAPTER_H_
#define SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_NXP_NXPFMAC_IOCTL_ADAPTER_H_

#include <lib/async-loop/cpp/loop.h>
#include <lib/zx/status.h>
#include <zircon/types.h>

#include <functional>

#include "src/connectivity/wlan/drivers/third_party/nxp/nxpfmac/debug.h"
#include "src/connectivity/wlan/drivers/third_party/nxp/nxpfmac/ioctl_request.h"

namespace wlan::nxpfmac {

class BusInterface;

class IoctlAdapter {
 public:
  static zx::status<std::unique_ptr<IoctlAdapter>> Create(void* mlan_adapter, BusInterface* bus);
  ~IoctlAdapter();

  // Issue an IOCTL with the specified request. The call returns IoctlStatus::Success if the
  // request completes immediately. IoctlStatus::Pending is returned if the request is not yet
  // complete but will be completed at some point in the future. If and only if IoctlStatus::Pending
  // is returned the `on_complete` callback will be called once the request completes. If
  // IoctlStatus::Success or any other error code is returned the callback will NOT be called.
  //
  // The request object must be kept alive until the request completes. This means that if the call
  // returns IoctlStatus::Pending the request object must kept alive until `on_complete` is called.
  // For any other return value the request will no longer be referenced after this call returns.
  template <typename RequestType, size_t TrailingSpace>
  IoctlStatus IssueIoctl(IoctlRequest<RequestType, TrailingSpace>* request,
                         IoctlRequestCallback&& on_complete,
                         zx_duration_t timeout = ZX_TIME_INFINITE) {
    request->callback_ = std::make_shared<IoctlRequestCallback>(std::move(on_complete));

    if (!IoctlRequest<RequestType>::IsIoctlRequest(request->IoctlReq())) {
      NXPF_ERR("Attempted to use an MlanIoctlRequest with modified reserved_1");
      return IoctlStatus::Failure;
    }
    if (request->IoctlReq().action == MLAN_ACT_CANCEL) {
      // Use CancelIoctl to perform a cancelation, this guards against accidentally reusing request
      // object without re-initializing them.
      return IoctlStatus::Failure;
    }
    // Reset the status code in case the request is being reused.
    request->IoctlReq().status_code = MLAN_ERROR_NO_ERROR;

    return IssueIoctl(&request->IoctlReq(), timeout);
  }

  // Issue a synchronous IOCTL with the specified request. The call will wait for the request to
  // complete before returning. This may be immediately or for asynchronous ioctls it will wait
  // until the ioctl completes. An optional timeout can be specified to limit the amount of time to
  // wait. If the operation succeeds IoctlStatus::Success is returned, if a timeout was provided and
  // the requested timed out IoctlStatus::Timeout is returned. This call will never return
  // IoctlStatus::Pending.
  template <typename RequestType, size_t TrailingSpace>
  IoctlStatus IssueIoctlSync(IoctlRequest<RequestType, TrailingSpace>* request,
                             zx_duration_t timeout = ZX_TIME_INFINITE) {
    if (!IoctlRequest<RequestType>::IsIoctlRequest(request->IoctlReq())) {
      NXPF_ERR("Attempted to use an MlanIoctlRequest with modified reserved_1");
      return IoctlStatus::Failure;
    }

    if (request->IoctlReq().action == MLAN_ACT_CANCEL) {
      // Use CancelIoctl to perform a cancelation, this guards against accidentally reusing request
      // object without re-initializing them.
      return IoctlStatus::Failure;
    }
    // Reset the status code in case the request is being reused.
    request->IoctlReq().status_code = MLAN_ERROR_NO_ERROR;

    return IssueIoctlSync(&request->IoctlReq(), timeout);
  }

  // Cancel a pending ioctl request. Returns true if the ioctl was canceled or false if the ioctl
  // was not found or could not be canceled. If the ioctl request is canceled it will have its
  // callback (provided when it was issued) called with a canceled status. Note that because of how
  // cancelation works in mlan this will modify the action of the request parameter. In order to use
  // the request object again it should be reinitialized.
  template <typename RequestType, size_t TrailingSpace>
  bool CancelIoctl(IoctlRequest<RequestType, TrailingSpace>* request) {
    return CancelIoctl(&request->IoctlReq());
  }
  // Cancel all pending ioctls. All canceled ioctls will have any associated callback called with a
  // canceled status code.
  void CancelAllIoctls();

  void OnIoctlComplete(mlan_ioctl_req* request, IoctlStatus status);

 private:
  IoctlAdapter(void* mlan_adapter, BusInterface* bus);

  IoctlStatus IssueIoctl(mlan_ioctl_req* request, zx_duration_t timeout);
  IoctlStatus IssueIoctlSync(mlan_ioctl_req* request, zx_duration_t timeout);
  bool CancelIoctl(mlan_ioctl_req* request);
  void ScheduleTimeoutTask(mlan_ioctl_req* request,
                           const std::weak_ptr<IoctlRequestCallback>& weak_callback,
                           zx_time_t deadline);

  async::Loop loop_;
  void* mlan_adapter_ = nullptr;
  BusInterface* bus_ = nullptr;
};

}  // namespace wlan::nxpfmac

#endif  // SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_NXP_NXPFMAC_IOCTL_ADAPTER_H_
