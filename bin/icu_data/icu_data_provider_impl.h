// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_ICU_DATA_ICU_DATA_PROVIDER_IMPL_H_
#define APPS_ICU_DATA_ICU_DATA_PROVIDER_IMPL_H_

#include <mx/vmo.h>

#include "mojo/public/cpp/bindings/binding_set.h"
#include "mojo/services/icu_data/interfaces/icu_data.mojom.h"

namespace icu_data {

class ICUDataProviderImpl : public mojo::ICUDataProvider {
 public:
  ICUDataProviderImpl();
  ~ICUDataProviderImpl() override;

  // Return whether this function was able to successfully load the ICU data
  bool LoadData();

  void AddBinding(mojo::InterfaceRequest<mojo::ICUDataProvider> request);

 private:
  // |ICUData| implementation:
  void ICUDataWithSha1(const mojo::String& request,
                       const ICUDataWithSha1Callback& callback) override;

  mojo::BindingSet<mojo::ICUDataProvider> bindings_;

  mx::vmo icu_data_vmo_;

  MOJO_DISALLOW_COPY_AND_ASSIGN(ICUDataProviderImpl);
};

}  // namespace icu_data

#endif  // APPS_ICU_DATA_ICU_DATA_PROVIDER_IMPL_H_
