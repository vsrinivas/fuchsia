// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_LIB_FIDL_PROXY_H_
#define PERIDOT_LIB_FIDL_PROXY_H_

#include <memory>
#include <vector>

#include <lib/fidl/cpp/binding.h>
#include <lib/fidl/cpp/interface_request.h>
#include <lib/fxl/macros.h>

namespace modular {

class ProxyBase;

// A proxy allows to bind a fidl interface request to an existing fidl interface
// ptr of the same interface type.
//
// Proxy instances are held in a proxy set, from which they are automatically
// removed once one of their connection closes, which then also closes the other
// connection.
//
// A proxy set can polymorphically hold proxies of various interface types.
class ProxySet {
 public:
  ProxySet();
  ~ProxySet();

  template <typename I>
  void Connect(fidl::InterfacePtr<I> ptr, fidl::InterfaceRequest<I> request);

 private:
  friend class ProxyBase;
  void Drop(ProxyBase* proxy);

  std::vector<std::unique_ptr<ProxyBase>> proxies_;
  FXL_DISALLOW_COPY_AND_ASSIGN(ProxySet);
};

// Used internally by ProxySet, but needs to be here because it supports the
// Proxy template class below.
class ProxyBase {
 public:
  explicit ProxyBase(ProxySet* set);
  virtual ~ProxyBase();

 protected:
  void Drop();

 private:
  ProxySet* const set_;

  FXL_DISALLOW_COPY_AND_ASSIGN(ProxyBase);
};

// Used internally by ProxySet, but needs to be here because it is a
// template.
template <typename I>
class Proxy : public ProxyBase {
 public:
  Proxy(ProxySet* set, fidl::InterfacePtr<I> ptr,
        fidl::InterfaceRequest<I> request);
  ~Proxy() override;

 private:
  fidl::InterfacePtr<I> ptr_;
  fidl::Binding<I> binding_;

  FXL_DISALLOW_COPY_AND_ASSIGN(Proxy);
};

template <typename I>
void ProxySet::Connect(fidl::InterfacePtr<I> ptr,
                       fidl::InterfaceRequest<I> request) {
  proxies_.emplace_back(new Proxy<I>(this, std::move(ptr), std::move(request)));
}

template <typename I>
Proxy<I>::Proxy(ProxySet* const set, fidl::InterfacePtr<I> ptr,
                fidl::InterfaceRequest<I> request)
    : ProxyBase(set),
      ptr_(std::move(ptr)),
      binding_(ptr_.get(), std::move(request)) {
  ptr_.set_error_handler([this] { Drop(); });
  binding_.set_error_handler([this] { Drop(); });
}

template <typename I>
Proxy<I>::~Proxy() = default;

}  // namespace modular

#endif  // PERIDOT_LIB_FIDL_PROXY_H_
