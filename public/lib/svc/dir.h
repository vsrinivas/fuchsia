// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_SVC_SVC_H_
#define LIB_SVC_SVC_H_

#include <lib/async/dispatcher.h>
#include <zircon/compiler.h>
#include <zircon/types.h>

__BEGIN_CDECLS

typedef void(svc_connector_t)(
    void* context, const char* service_name, zx_handle_t service_request);

typedef struct svc_dir svc_dir_t;

__EXPORT zx_status_t svc_dir_create(async_t* async,
                                    zx_handle_t directory_request,
                                    svc_dir_t** result);

__EXPORT zx_status_t svc_dir_add_service(svc_dir_t* dir,
                                         const char* type, // e.g., "public", "debug", "ctrl".
                                         const char* service_name,
                                         void* context, // passed to |handler|.
                                         svc_connector_t handler);

__EXPORT zx_status_t svc_dir_destroy(svc_dir_t* dir);

__END_CDECLS

#endif // LIB_SVC_SVC_H_
