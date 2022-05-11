// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_STORAGE_LIB_PAVER_INCLUDE_LIB_PAVER_PROVIDER_H_
#define SRC_STORAGE_LIB_PAVER_INCLUDE_LIB_PAVER_PROVIDER_H_

#include <lib/svc/service.h>
#include <zircon/compiler.h>

__BEGIN_CDECLS

const zx_service_provider_t* paver_get_service_provider(void);

__END_CDECLS

#endif  // SRC_STORAGE_LIB_PAVER_INCLUDE_LIB_PAVER_PROVIDER_H_
