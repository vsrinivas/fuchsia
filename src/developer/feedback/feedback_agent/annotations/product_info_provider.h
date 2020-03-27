// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_FEEDBACK_FEEDBACK_AGENT_ANNOTATIONS_PRODUCT_INFO_PROVIDER_H_
#define SRC_DEVELOPER_FEEDBACK_FEEDBACK_AGENT_ANNOTATIONS_PRODUCT_INFO_PROVIDER_H_

#include <fuchsia/hwinfo/cpp/fidl.h>
#include <lib/async/dispatcher.h>
#include <lib/fit/promise.h>
#include <lib/sys/cpp/service_directory.h>
#include <zircon/time.h>

#include "src/developer/feedback/feedback_agent/annotations/annotation_provider.h"
#include "src/developer/feedback/utils/bridge.h"
#include "src/developer/feedback/utils/cobalt.h"
#include "src/lib/fxl/macros.h"

namespace feedback {

// Get the requested parts of fuchsia.hwinfo.ProductInfo as annotations.
class ProductInfoProvider : public AnnotationProvider {
 public:
  // fuchsia.hwinfo.Product is expected to be in |services|.
  ProductInfoProvider(const AnnotationKeys& annotations_to_get, async_dispatcher_t* dispatcher,
                      std::shared_ptr<sys::ServiceDirectory> services, zx::duration timeout,
                      Cobalt* cobalt);

  static AnnotationKeys GetSupportedAnnotations();

  fit::promise<Annotations> GetAnnotations() override;

 private:
  AnnotationKeys annotations_to_get_;
  async_dispatcher_t* dispatcher_;
  const std::shared_ptr<sys::ServiceDirectory> services_;
  const zx::duration timeout_;
  Cobalt* cobalt_;
};

namespace internal {

// Wraps around fuchsia::hwinfo::ProductPtr to handle establishing the connection, losing
// the connection, waiting for the callback, enforcing a timeout, etc.
//
// Will ever only make one call to fuchsia::hwinfo::Product::GetInfo.
class ProductInfoPtr {
 public:
  ProductInfoPtr(async_dispatcher_t* dispatcher, std::shared_ptr<sys::ServiceDirectory> services,
                 Cobalt* cobalt);

  fit::promise<Annotations> GetProductInfo(zx::duration timeout);

 private:
  const std::shared_ptr<sys::ServiceDirectory> services_;
  Cobalt* cobalt_;

  // Enforces the one-shot nature of GetProductInfo().
  bool has_called_get_product_info_ = false;

  fuchsia::hwinfo::ProductPtr product_ptr_;
  Bridge<Annotations> bridge_;

  FXL_DISALLOW_COPY_AND_ASSIGN(ProductInfoPtr);
};

}  // namespace internal
}  // namespace feedback

#endif  // SRC_DEVELOPER_FEEDBACK_FEEDBACK_AGENT_ANNOTATIONS_PRODUCT_INFO_PROVIDER_H_
