// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_FIDL_CPP_WIRE_INCLUDE_LIB_FIDL_CPP_WIRE_SERVER_H_
#define LIB_FIDL_CPP_WIRE_INCLUDE_LIB_FIDL_CPP_WIRE_SERVER_H_

#include <lib/fidl/cpp/wire/internal/arrow.h>
#include <lib/fidl/cpp/wire/internal/endpoints.h>
#include <lib/fidl/cpp/wire/internal/server_details.h>
#include <lib/fidl/cpp/wire/wire_messaging_declarations.h>
#include <lib/fit/function.h>
#include <zircon/types.h>

namespace fidl {

namespace internal {

class AsyncServerBinding;
class ServerBindingRefBase;
std::weak_ptr<AsyncServerBinding> BorrowBinding(const ServerBindingRefBase&);

// |ServerBindingRefBase| controls a server binding that does not have
// threading restrictions.
class ServerBindingRefBase {
 public:
  explicit ServerBindingRefBase(std::weak_ptr<AsyncServerBinding> binding)
      : binding_(std::move(binding)) {}
  ~ServerBindingRefBase() = default;

  ServerBindingRefBase(const ServerBindingRefBase&) = default;
  ServerBindingRefBase& operator=(const ServerBindingRefBase&) = default;

  ServerBindingRefBase(ServerBindingRefBase&&) = default;
  ServerBindingRefBase& operator=(ServerBindingRefBase&&) = default;

  void Unbind() {
    if (auto binding = binding_.lock())
      binding->StartTeardown(std::move(binding));
  }

 protected:
  const std::weak_ptr<AsyncServerBinding>& binding() const { return binding_; }

 private:
  friend std::weak_ptr<AsyncServerBinding> internal::BorrowBinding(const ServerBindingRefBase&);

  std::weak_ptr<AsyncServerBinding> binding_;
};

inline std::weak_ptr<AsyncServerBinding> BorrowBinding(const ServerBindingRefBase& binding_ref) {
  return binding_ref.binding();
}

// |UniqueServerBindingOwner| tears down the managed binding when it destructs.
//
// There must be at most one unique owner of a binding.
class UniqueServerBindingOwner {
 public:
  explicit UniqueServerBindingOwner(ServerBindingRefBase&& ref) : ref_(std::move(ref)) {}
  ~UniqueServerBindingOwner() { ref_.Unbind(); }

  UniqueServerBindingOwner(UniqueServerBindingOwner&&) = default;
  UniqueServerBindingOwner& operator=(UniqueServerBindingOwner&&) = default;

  ServerBindingRefBase& ref() { return ref_; }
  const ServerBindingRefBase& ref() const { return ref_; }

 private:
  ServerBindingRefBase ref_;
};

template <typename FidlProtocol>
class ServerBindingBase {
 public:
  template <typename Impl, typename CloseHandler>
  static void CloseHandlerRequirement() {
    // TODO(fxbug.dev/112648): Cannot use |std::is_invocable_v| as that fails
    // on the latest clang.
    using SimpleCloseHandler = fit::callback<void(UnbindInfo)>;
    using InstanceCloseHandler = fit::callback<void(Impl*, UnbindInfo)>;
    static_assert(std::is_convertible_v<CloseHandler, SimpleCloseHandler> ||
                      std::is_convertible_v<CloseHandler, InstanceCloseHandler>,
                  "The close handler must have a signature of "
                  "void(fidl::UnbindInfo) or void(Impl*, fidl::UnbindInfo)");
  }

  template <typename Impl, typename CloseHandler>
  ServerBindingBase(async_dispatcher_t* dispatcher,
                    fidl::internal::ServerEndType<FidlProtocol> server_end, Impl* impl,
                    CloseHandler&& close_handler) {
    CloseHandlerRequirement<Impl, CloseHandler>();
    lifetime_ = std::make_shared<Lifetime>();
    binding_.emplace(internal::UniqueServerBindingOwner(BindServerImpl(
        dispatcher, std::move(server_end), impl,
        [error_handler = std::forward<CloseHandler>(close_handler),
         weak_lifetime = std::weak_ptr(lifetime_)](
            Impl* impl, fidl::UnbindInfo info,
            fidl::internal::ServerEndType<FidlProtocol>) mutable {
          if (weak_lifetime.expired()) {
            // Binding is already destructed. Don't call the error handler to avoid
            // calling into a destructed server.
            return;
          }
          using SimpleErrorHandler = fit::callback<void(UnbindInfo)>;
          if constexpr (std::is_convertible_v<CloseHandler, SimpleErrorHandler>) {
            error_handler(info);
          } else {
            error_handler(impl, info);
          }
        },
        ThreadingPolicy::kCreateAndTeardownFromDispatcherThread)));
  }

  ServerBindingBase(ServerBindingBase&& other) noexcept = delete;
  ServerBindingBase& operator=(ServerBindingBase&& other) noexcept = delete;

  ServerBindingBase(const ServerBindingBase& other) noexcept = delete;
  ServerBindingBase& operator=(const ServerBindingBase& other) noexcept = delete;

  ~ServerBindingBase() = default;

 protected:
  internal::UniqueServerBindingOwner& binding() { return *binding_; }
  const internal::UniqueServerBindingOwner& binding() const { return *binding_; }

 private:
  template <typename P>
  friend std::weak_ptr<AsyncServerBinding> BorrowBinding(const ServerBindingBase<P>&);
  struct Lifetime {};

  std::optional<internal::UniqueServerBindingOwner> binding_;
  std::shared_ptr<Lifetime> lifetime_;
};

template <typename FidlProtocol>
inline std::weak_ptr<AsyncServerBinding> BorrowBinding(
    const ServerBindingBase<FidlProtocol>& binding) {
  return BorrowBinding(binding.binding_.value().ref());
}

}  // namespace internal

}  // namespace fidl

#endif  // LIB_FIDL_CPP_WIRE_INCLUDE_LIB_FIDL_CPP_WIRE_SERVER_H_
