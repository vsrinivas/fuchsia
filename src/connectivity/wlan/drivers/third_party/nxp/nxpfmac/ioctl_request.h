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

#ifndef SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_NXP_NXPFMAC_IOCTL_REQUEST_H_
#define SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_NXP_NXPFMAC_IOCTL_REQUEST_H_

#include <lib/async/task.h>
#include <zircon/types.h>

#include <array>
#include <functional>
#include <memory>

#include "src/connectivity/wlan/drivers/third_party/nxp/nxpfmac/align.h"
#include "src/connectivity/wlan/drivers/third_party/nxp/nxpfmac/mlan.h"

namespace wlan::nxpfmac {

enum class IoctlStatus {
  Success,
  Pending,
  Timeout,
  Canceled,
  Failure,
};

class IoctlAdapter;

using IoctlRequestCallback = std::function<void(mlan_ioctl_req*, IoctlStatus)>;

// A class representing an ioctl request. This class holds the basic request along with ioctl
// specific request data specified by RequestType. Example usage:
//
// mlan_ds_scan scan{
//     .sub_command = MLAN_OID_SCAN_NORMAL,
//     .param = {.scan_req = {.scan_type = MLAN_SCAN_TYPE_ACTIVE}},
// };
// IoctlRequest<mlan_ds_scan> request(MLAN_IOCTL_SCAN, MLAN_ACT_SET, scan);
//
// This creates a scan ioctl request that can then be used with Ioctl.
//
// For ioctl that have additional data, specify a TrailingSpace. For example to allow room for
// an additional uint64_t at the end do:
//
// IoctlRequest<mlan_ds_scan, sizeof(uint64_t)> request;
//
// This additional data can be access through TrailingData().
template <typename RequestType, size_t TrailingSpace = 0>
class IoctlRequest {
  // We mark these request with a magic number so that we can verify on callback that they are
  // of this type. Keep the magic private so that it's not accidentally used by other parts of the
  // code to mis-identify something. Make this number odd to reduce the chance of accidentally
  // matching an actual address. Still the chance is 1 in 2^64, pretty small.
  static constexpr t_ptr kMlanIoctlRequestMagic = 0x8347473338474559ull;

  static constexpr size_t StorageSize = sizeof(RequestType) + TrailingSpace;

  using type = IoctlRequest<RequestType, TrailingSpace>;

 public:
  IoctlRequest() = default;
  // Create a request with the given id, action and bss_index. The user_request data is copied into
  // the request as well, it does not have to be kept alive after the request has been created.
  IoctlRequest(uint32_t request_id, uint32_t action, uint32_t bss_index,
               const RequestType& user_request)
      : ioctl_req_{.bss_index = bss_index,
                   .req_id = request_id,
                   .action = action,
                   .pbuf = reinterpret_cast<uint8_t*>(&user_req_),
                   .buf_len = StorageSize,
                   .reserved_1 = kMlanIoctlRequestMagic},
        user_req_(user_request) {
    static_assert(
        offsetof(type, ioctl_req_) == 0,
        "ioctl_req_ must be the first data member and IoctlRequest must not have a vtable");
    static_assert(align(offsetof(type, user_req_) + sizeof(user_req_) + TrailingSpace,
                        alignof(type)) == sizeof(type),
                  "user_req_ has to be the last data member");
    memset(TrailingData(), 0, TrailingSpace);
  }

  IoctlRequest(IoctlRequest&& other) noexcept
      : ioctl_req_(other.ioctl_req_),
        callback_(std::move(other.callback_), storage_(other.storage_)) {
    // This pointer must be changed, it should now point to the user request in this new object.
    ioctl_req_.pbuf = reinterpret_cast<uint8_t*>(&user_req_);
  }

  IoctlRequest& operator=(IoctlRequest&& other) noexcept {
    ioctl_req_ = other.ioctl_req_;
    // This pointer must be changed, it should now point to the user request in this new object.
    ioctl_req_.pbuf = reinterpret_cast<uint8_t*>(&user_req_);
    callback_ = std::move(other.callback_);
    storage_ = other.storage_;
    return *this;
  }

  // Check if a request is an instance of this class.
  static bool IsIoctlRequest(const mlan_ioctl_req& req) {
    // Use the magic we inserted in the constructor.
    return req.reserved_1 == kMlanIoctlRequestMagic;
  }

  // Access the main ioctl_req
  mlan_ioctl_req& IoctlReq() { return ioctl_req_; }
  // Access the user specified request data
  RequestType& UserReq() { return user_req_; }
  // Access any trailing space allocated after the request data.
  uint8_t* TrailingData() { return storage_.data() + sizeof(RequestType); }
  // Access any trailing space allocated after the request data as a specific type.
  template <typename TrailingDataType>
  TrailingDataType* TrailingDataAs() {
    static_assert(sizeof(TrailingDataType) <= TrailingSpace,
                  "Attempt to access more data than available");
    return reinterpret_cast<TrailingDataType*>(storage_.data() + sizeof(RequestType));
  }
  constexpr size_t TrailingSize() const { return TrailingSpace; }

 private:
  // Ioctl needs to set callback but we don't want to provide that functionality to anyone else.
  friend class IoctlAdapter;

  struct TimeoutTask : public async_task_t {
    TimeoutTask(IoctlAdapter* ioctl_adapter, zx_time_t deadline, async_task_handler_t* handler,
                mlan_ioctl_req* request)
        : async_task_t{ASYNC_STATE_INIT, handler, deadline},
          ioctl_adapter(ioctl_adapter),
          request(request) {}
    IoctlAdapter* ioctl_adapter;
    mlan_ioctl_req* request;
    std::optional<bool> timed_out;
  };

  // Note that ioctl_req always has to be first in this class. This is because it needs to be
  // castable to both ioctl_req and IoctlRequest<RequestType>.
  mlan_ioctl_req ioctl_req_ = {};
  // Keep the callback as a shared_ptr. This allows us to hold on to weak pointers in timeout
  // callbacks to check if the callback and request still exist or if they've been deleted.
  std::shared_ptr<IoctlRequestCallback> callback_;
  std::unique_ptr<TimeoutTask> timeout_task_;
  // This has to be last at all times, this is because some mlan ioctl types are of variable size
  // and any data member of variable size must be placed last in a class, otherwise the location of
  // any following data members cannot be determined. Using variable size ioctl types also makes
  // the particular template instantiation of this class variable size.
  // storage_ will contain both the user_req_ and any trailing space, the union allows for easy
  // access to the RequestType data while maintaining correct alignment for that type. This also
  // means the trailing space will be a continuation of the request data, just as you would expect
  // if you allocated memory for a request with extra data at the end. To access the trailing data
  // the methods above have to use an offset into storage_ to compensate for this.
  union {
    RequestType user_req_;
    std::array<uint8_t, StorageSize> storage_;
  };
};

}  // namespace wlan::nxpfmac

#endif  // SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_NXP_NXPFMAC_IOCTL_REQUEST_H_
