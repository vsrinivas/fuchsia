// Copyright 2015 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_ICU_DATA_LIB_ICU_DATA_H_
#define APPS_ICU_DATA_LIB_ICU_DATA_H_

namespace modular {
class ServiceProvider;
}

namespace icu_data {

bool Initialize(modular::ServiceProvider* services);
bool Release();

}  // namespace icu_data

#endif  // MOJO_SERVICES_ICU_DATA_CPP_ICU_DATA_H_
