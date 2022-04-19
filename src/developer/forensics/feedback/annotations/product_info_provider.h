// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_FORENSICS_FEEDBACK_ANNOTATIONS_PRODUCT_INFO_PROVIDER_H_
#define SRC_DEVELOPER_FORENSICS_FEEDBACK_ANNOTATIONS_PRODUCT_INFO_PROVIDER_H_

#include <fuchsia/hwinfo/cpp/fidl.h>

#include "src/developer/forensics/feedback/annotations/fidl_provider.h"
#include "src/developer/forensics/feedback/annotations/types.h"

namespace forensics::feedback {

struct ProductInfoToAnnotations {
  Annotations operator()(const fuchsia::hwinfo::ProductInfo& info);
};

// Responsible for collecting annotations for fuchsia.hwinfo/Product.
class ProductInfoProvider
    : public StaticSingleFidlMethodAnnotationProvider<
          fuchsia::hwinfo::Product, &fuchsia::hwinfo::Product::GetInfo, ProductInfoToAnnotations> {
 public:
  using StaticSingleFidlMethodAnnotationProvider::StaticSingleFidlMethodAnnotationProvider;

  std::set<std::string> GetKeys() const override;
};

}  // namespace forensics::feedback

#endif  // SRC_DEVELOPER_FORENSICS_FEEDBACK_ANNOTATIONS_PRODUCT_INFO_PROVIDER_H_
