// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_ASYNC_CPP_PAGED_VMO_H_
#define LIB_ASYNC_CPP_PAGED_VMO_H_

#include <lib/async/paged_vmo.h>
#include <lib/fit/function.h>
#include <lib/zx/pager.h>
#include <lib/zx/vmo.h>

namespace async {

// Holds content for a paged vmo packet receiver and its handler.
//
// After successfully binding the port, the client is responsible for
// retaining the structure in memory (and unmodified) until all packets have
// been received by the handler or the dispatcher shuts down.
//
// Concrete implementations: |async::PagedVmo|, |async::PagedVmoMethod|.
// Please do not create subclasses of PagedVmoBase outside of this library.
class PagedVmoBase {
 protected:
  explicit PagedVmoBase(async_paged_vmo_handler_t* handler);
  ~PagedVmoBase();

  PagedVmoBase(const PagedVmoBase&) = delete;
  PagedVmoBase(PagedVmoBase&&) = delete;
  PagedVmoBase& operator=(const PagedVmoBase&) = delete;
  PagedVmoBase& operator=(PagedVmoBase&&) = delete;

  template <typename T>
  static T* Dispatch(async_paged_vmo_t* paged_vmo, zx_status_t status) {
    static_assert(offsetof(PagedVmoBase, paged_vmo_) == 0, "Non-castable offset");
    auto self = reinterpret_cast<PagedVmoBase*>(paged_vmo);
    if (status != ZX_OK) {
      self->dispatcher_ = nullptr;
    }
    return static_cast<T*>(self);
  }

 public:
  // Return true if this object is bound to a VMO.
  bool is_bound() const { return dispatcher_ != nullptr; }

  // Creates a paged VMO registered with |pager|, which will receive notifications on the
  // receiver provided in the constructor of |PagedVmoBase|.
  //
  // Returns |ZX_ERR_ALREADY_EXISTS| if this object is already associated with a VMO.
  // May return any error from |async_create_paged_vmo()|.
  zx_status_t CreateVmo(async_dispatcher_t* dispatcher, zx::unowned_pager pager, uint32_t options,
                        uint64_t vmo_size, zx::vmo* vmo_out);

  // Detach the paged VMO from the underlying port.
  //
  // Returns |ZX_OK| if the VMO is successfully detached.
  // Returns |ZX_ERR_NOT_FOUND| if this object is not bound.
  // May return any error from |async_detach_paged_vmo()|.
  zx_status_t Detach();

 private:
  async_paged_vmo_t paged_vmo_ = {};
  async_dispatcher_t* dispatcher_ = nullptr;
};

// A receiver whose handler is bound to a |async::PagedVmo::Handler| function.
//
// Prefer using |async::PagedVmoMethod| instead for binding to a fixed class member
// function since it is more efficient to dispatch.
class PagedVmo final : public PagedVmoBase {
 public:
  // Handles receipt of packets containing page requests.
  //
  // The |status| is |ZX_OK| if the packet was successfully delivered and |page_request|
  // contains the information from the packet, otherwise |page_request| is null.
  using Handler =
      fit::function<void(async_dispatcher_t* dispatcher, async::PagedVmo* paged_vmo,
                         zx_status_t status, const zx_packet_page_request_t* page_request)>;

  explicit PagedVmo(Handler handler = nullptr);
  ~PagedVmo();

  void set_handler(Handler handler) { handler_ = std::move(handler); }
  bool has_handler() const { return !!handler_; }

 private:
  static void CallHandler(async_dispatcher_t* dispatcher, async_paged_vmo_t* paged_vmo,
                          zx_status_t status, const zx_packet_page_request_t* page_request);

  Handler handler_;
};

// A receiver whose handler is bound to a fixed class member function.
//
// Usage:
//
// class Foo {
//   void Handle(async_dispatcher_t* dispatcher,
//               async::PagedVmoBase* paged_vmo,
//               zx_status_t status,
//               const zx_packet_page_request_t* page_request) {
//     ...
//   }
//   async::PagedVmoMethod<Foo, &Foo::Handle> paged_vmo_{this};
// };
template <class Class,
          void (Class::*method)(async_dispatcher_t* dispatcher, async::PagedVmoBase* receiver,
                                zx_status_t status, const zx_packet_page_request_t* page_request)>
class PagedVmoMethod final : public PagedVmoBase {
 public:
  explicit PagedVmoMethod(Class* instance)
      : PagedVmoBase(&PagedVmoMethod::CallHandler), instance_(instance) {}

 private:
  static void CallHandler(async_dispatcher_t* dispatcher, async_paged_vmo_t* paged_vmo,
                          zx_status_t status, const zx_packet_page_request_t* page_request) {
    auto self = Dispatch<PagedVmoMethod>(paged_vmo, status);
    (self->instance_->*method)(dispatcher, self, status, page_request);
  }

  Class* const instance_;
};

}  // namespace async

#endif  // LIB_ASYNC_CPP_PAGED_VMO_H_
