// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_MEDIA_NET_MEDIA_SERVICE_FACTORY_SERVICE_BASE_H_
#define GARNET_BIN_MEDIA_NET_MEDIA_SERVICE_FACTORY_SERVICE_BASE_H_

#include <memory>
#include <mutex>
#include <thread>
#include <unordered_set>

#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/loop.h>
#include <lib/async/cpp/task.h>
#include <lib/async/default.h>

#include "lib/component/cpp/startup_context.h"
#include "lib/fxl/logging.h"
#include "lib/fxl/macros.h"
#include "lib/fxl/synchronization/thread_annotations.h"

template <typename Factory>
class FactoryServiceBase {
 public:
  // Provides common behavior for all objects created by the factory service.
  class ProductBase : public std::enable_shared_from_this<ProductBase> {
   public:
    virtual ~ProductBase() {}

   protected:
    explicit ProductBase(Factory* owner) : owner_(owner) { FXL_DCHECK(owner_); }

    // Returns the owner.
    Factory* owner() { return owner_; }

    // Tells the factory service to release this product. This method can only
    // be called after the first shared_ptr to the product is created.
    void ReleaseFromOwner() {
      owner_->RemoveProduct(ProductBase::shared_from_this());
    }

   private:
    Factory* owner_;
  };

  // A |ProductBase| that exposes FIDL interface |Interface| via a single
  // binding.
  template <typename Interface>
  class Product : public ProductBase {
   public:
    virtual ~Product() {}

   protected:
    Product(Interface* impl, fidl::InterfaceRequest<Interface> request,
            Factory* owner)
        : ProductBase(owner), binding_(impl, std::move(request)) {
      FXL_DCHECK(impl);
      Retain();
      binding_.set_error_handler([this]() {
        binding_.set_error_handler(nullptr);
        binding_.Unbind();
        Release();
      });
    }

    // Returns the binding established via the request in the constructor.
    const fidl::Binding<Interface>& binding() { return binding_; }

    // Increments the retention count.
    void Retain() { ++retention_count_; }

    // Decrements the retention count and calls UnbindAndReleaseFromOwner if
    // the count has reached zero. This method can only be called after the
    // first shared_ptr to the product is created.
    void Release() {
      if (--retention_count_ == 0) {
        UnbindAndReleaseFromOwner();
      }
    }

    // Closes the binding.
    void Unbind() {
      if (binding_.is_bound()) {
        binding_.Unbind();
      }
    }

    // Closes the binding and calls ReleaseFromOwner. This method can only
    // be called after the first shared_ptr to the product is created.
    void UnbindAndReleaseFromOwner() {
      Unbind();
      ProductBase::ReleaseFromOwner();
    }

   private:
    size_t retention_count_ = 0;
    fidl::Binding<Interface> binding_;
  };

  // A |ProductBase| that exposes FIDL interface |Interface| via multiple
  // bindings.
  template <typename Interface>
  class MultiClientProduct : public ProductBase {
   public:
    virtual ~MultiClientProduct() {}

   protected:
    MultiClientProduct(Interface* impl,
                       fidl::InterfaceRequest<Interface> request,
                       Factory* owner)
        : ProductBase(owner), impl_(impl) {
      FXL_DCHECK(impl);

      if (request) {
        AddBinding(std::move(request));
      }

      bindings_.set_empty_set_handler([this]() {
        bindings_.set_empty_set_handler(nullptr);
        ProductBase::ReleaseFromOwner();
      });
    }

    // Returns the bindings for this product.
    const fidl::BindingSet<Interface>& bindings() { return bindings_; }

    // Adds a binding.
    void AddBinding(fidl::InterfaceRequest<Interface> request) {
      FXL_DCHECK(request);
      bindings_.AddBinding(impl_, std::move(request));
    }

    // Closes the bindings.
    void Unbind() { bindings_.CloseAll(); }

    // Closes the bindings and calls ReleaseFromOwner. This method can only
    // be called after the first shared_ptr to the product is created.
    void UnbindAndReleaseFromOwner() {
      Unbind();
      ProductBase::ReleaseFromOwner();
    }

   private:
    Interface* impl_;
    fidl::BindingSet<Interface> bindings_;
  };

  FactoryServiceBase(
      std::unique_ptr<component::StartupContext> startup_context)
      : startup_context_(std::move(startup_context)),
        dispatcher_(async_get_default_dispatcher()) {}

  virtual ~FactoryServiceBase() {}

  // Gets the application context.
  component::StartupContext* startup_context() {
    return startup_context_.get();
  }

  // Connects to a service registered with the application environment.
  template <typename Interface>
  fidl::InterfacePtr<Interface> ConnectToEnvironmentService(
      const std::string& interface_name = Interface::Name_) {
    return startup_context_->ConnectToEnvironmentService<Interface>(
        interface_name);
  }

 protected:
  // Adds a product to the factory's collection of products. Threadsafe.
  template <typename ProductImpl>
  void AddProduct(std::shared_ptr<ProductImpl> product) {
    std::lock_guard<std::mutex> locker(mutex_);
    products_.insert(std::static_pointer_cast<ProductBase>(product));
  }

  // Removes a product from the factory's collection of products. Threadsafe.
  void RemoveProduct(std::shared_ptr<ProductBase> product) {
    std::lock_guard<std::mutex> locker(mutex_);
    bool erased = products_.erase(product);
    FXL_DCHECK(erased);
    if (products_.empty()) {
      OnLastProductRemoved();
    }
  }

  // Called when the number of products transitions from one to zero. The
  // default implementation does nothing.
  virtual void OnLastProductRemoved() {}

 private:
  std::unique_ptr<component::StartupContext> startup_context_;
  async_dispatcher_t* dispatcher_;
  mutable std::mutex mutex_;
  std::unordered_set<std::shared_ptr<ProductBase>> products_
      FXL_GUARDED_BY(mutex_);

  FXL_DISALLOW_COPY_AND_ASSIGN(FactoryServiceBase);
};

// For use by products when handling fidl requests. Checks the condition, and,
// if it's false, unbinds, releases from the owner and calls return. Doesn't
// support stream arguments.
// TODO(dalesat): Support stream arguments.
// The unbind happens synchronously to prevent any pending method calls from
// happening. The release is deferred so that RCHECK works in a product
// constructor.
#define RCHECK(condition)                                                   \
  if (!(condition)) {                                                       \
    FXL_LOG(ERROR) << "request precondition failed: " #condition ".";       \
    Unbind();                                                               \
    async::PostTask(async_get_default_dispatcher(), [this]() { ReleaseFromOwner(); }); \
    return;                                                                 \
  }

#endif  // GARNET_BIN_MEDIA_NET_MEDIA_SERVICE_FACTORY_SERVICE_BASE_H_
