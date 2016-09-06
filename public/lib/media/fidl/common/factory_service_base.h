// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_MEDIA_SERVICES_COMMON_FACTORY_SERVICE_BASE_H_
#define APPS_MEDIA_SERVICES_COMMON_FACTORY_SERVICE_BASE_H_

#include <memory>
#include <unordered_set>

#include "lib/ftl/logging.h"
#include "mojo/public/cpp/application/application_impl_base.h"

namespace mojo {
namespace media {

class FactoryServiceBase : public ApplicationImplBase {
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
            InterfaceRequest<Interface> request,
            FactoryServiceBase* owner)
        : ProductBase(owner), binding_(impl, request.Pass()) {
      FTL_DCHECK(impl);
      binding_.set_connection_error_handler([this]() { ReleaseFromOwner(); });
    }

    // Closes the binding and calls ReleaseFromOwner.
    void UnbindAndReleaseFromOwner() {
      if (binding_.is_bound()) {
        binding_.Close();
      }

      ReleaseFromOwner();
    }

   private:
    Binding<Interface> binding_;
  };

  FactoryServiceBase();

  ~FactoryServiceBase() override;

 protected:
  template <typename ProductImpl>
  void AddProduct(std::shared_ptr<ProductImpl> product) {
    products_.insert(std::static_pointer_cast<ProductBase>(product));
  }

 private:
  std::unordered_set<std::shared_ptr<ProductBase>> products_;
};

// For use by products when handling mojo requests.
// Checks the condition, and, if it's false, unbinds, releases from the owner
// and calls return. Doesn't support stream arguments.
// TODO(dalesat): Support stream arguments.
#define RCHECK(condition)                                             \
  if (!(condition)) {                                                 \
    FTL_LOG(ERROR) << "request precondition failed: " #condition "."; \
    UnbindAndReleaseFromOwner();                                      \
    return;                                                           \
  }

}  // namespace media
}  // namespace mojo

#endif  // APPS_MEDIA_SERVICES_COMMON_FACTORY_SERVICE_BASE_H_
