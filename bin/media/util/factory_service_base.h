// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <memory>
#include <unordered_set>

#include "application/lib/app/application_context.h"
#include "lib/ftl/logging.h"
#include "lib/ftl/macros.h"

class FactoryServiceBase {
 public:
  // Provides common behavior for all objects created by the factory service.
  class ProductBase : public std::enable_shared_from_this<ProductBase> {
   public:
    virtual ~ProductBase();

   protected:
    explicit ProductBase(FactoryServiceBase* owner);

    // Returns the owner.
    FactoryServiceBase* owner() { return owner_; }

    // Tells the factory service to release this product.
    void ReleaseFromOwner() {
      size_t erased = owner_->products_.erase(shared_from_this());
      FTL_DCHECK(erased);
    }

   private:
    FactoryServiceBase* owner_;
  };

  template <typename Interface>
  class Product : public ProductBase {
   public:
    virtual ~Product() {}

   protected:
    Product(Interface* impl,
            fidl::InterfaceRequest<Interface> request,
            FactoryServiceBase* owner)
        : ProductBase(owner), binding_(impl, std::move(request)) {
      FTL_DCHECK(impl);
      Retain();
      binding_.set_connection_error_handler([this]() { Release(); });
    }

    // Increments the retention count.
    void Retain() { ++retention_count_; }

    // Decrements the retention count and calls UnbindAndReleaseFromOwner if
    // the count has reached zero.
    void Release() {
      if (--retention_count_ == 0) {
        UnbindAndReleaseFromOwner();
      }
    }

    // Closes the binding and calls ReleaseFromOwner.
    void UnbindAndReleaseFromOwner() {
      if (binding_.is_bound()) {
        binding_.Close();
      }

      ReleaseFromOwner();
    }

   private:
    size_t retention_count_ = 0;
    fidl::Binding<Interface> binding_;
  };

  FactoryServiceBase();

  virtual ~FactoryServiceBase();

  modular::ApplicationContext* application_context() {
    return application_context_.get();
  }

  template <typename Interface>
  fidl::InterfacePtr<Interface> ConnectToEnvironmentService(
      const std::string& interface_name = Interface::Name_) {
    return application_context_->ConnectToEnvironmentService<Interface>(
        interface_name);
  }

 protected:
  template <typename ProductImpl>
  void AddProduct(std::shared_ptr<ProductImpl> product) {
    products_.insert(std::static_pointer_cast<ProductBase>(product));
  }

 private:
  std::unique_ptr<modular::ApplicationContext> application_context_;
  std::unordered_set<std::shared_ptr<ProductBase>> products_;

  FTL_DISALLOW_COPY_AND_ASSIGN(FactoryServiceBase);
};

// For use by products when handling fidl requests.
// Checks the condition, and, if it's false, unbinds, releases from the owner
// and calls return. Doesn't support stream arguments.
// TODO(dalesat): Support stream arguments.
#define RCHECK(condition)                                             \
  if (!(condition)) {                                                 \
    FTL_LOG(ERROR) << "request precondition failed: " #condition "."; \
    UnbindAndReleaseFromOwner();                                      \
    return;                                                           \
  }
