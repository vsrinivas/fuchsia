// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_ICU_DATA_ICU_DATA_PROVIDER_IMPL_H_
#define GARNET_BIN_ICU_DATA_ICU_DATA_PROVIDER_IMPL_H_

#include <icu_data/cpp/fidl.h>
#include "lib/fidl/cpp/binding_set.h"
#include "lib/fsl/vmo/sized_vmo.h"
#include "lib/fxl/macros.h"

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
  void ICUDataWithSha1(fidl::StringPtr request,
                       ICUDataWithSha1Callback callback) override;

  fidl::BindingSet<ICUDataProvider> bindings_;

  fsl::SizedVmo icu_data_vmo_;

  FXL_DISALLOW_COPY_AND_ASSIGN(ICUDataProviderImpl);
};

}  // namespace icu_data

#endif  // GARNET_BIN_ICU_DATA_ICU_DATA_PROVIDER_IMPL_H_
