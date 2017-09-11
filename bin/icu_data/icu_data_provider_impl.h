// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_ICU_DATA_ICU_DATA_PROVIDER_IMPL_H_
#define GARNET_BIN_ICU_DATA_ICU_DATA_PROVIDER_IMPL_H_

#include <mx/vmo.h>

#include "lib/fidl/cpp/bindings/binding_set.h"
#include "lib/fxl/macros.h"
#include "lib/icu_data/fidl/icu_data.fidl.h"

namespace icu_data {

class ICUDataProviderImpl : public ICUDataProvider {
 public:
  ICUDataProviderImpl();
  ~ICUDataProviderImpl() override;

  // Return whether this function was able to successfully load the ICU data
  bool LoadData();

  void AddBinding(fidl::InterfaceRequest<ICUDataProvider> request);

 private:
  // |ICUData| implementation:
  void ICUDataWithSha1(const fidl::String& request,
                       const ICUDataWithSha1Callback& callback) override;

  fidl::BindingSet<ICUDataProvider> bindings_;

  mx::vmo icu_data_vmo_;

  FXL_DISALLOW_COPY_AND_ASSIGN(ICUDataProviderImpl);
};

}  // namespace icu_data

#endif  // GARNET_BIN_ICU_DATA_ICU_DATA_PROVIDER_IMPL_H_
