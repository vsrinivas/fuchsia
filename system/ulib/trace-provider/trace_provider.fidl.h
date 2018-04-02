// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//
// NOTE: This file was generated using the fidl tool then manually modified
// to workaround limitations of the current code generator including table
// symbol collisions and invalid interface types in structs.
//

#pragma once

#include <lib/fidl/coding.h>
#include <stdbool.h>
#include <stdint.h>
#include <zircon/fidl.h>
#include <zircon/syscalls/object.h>
#include <zircon/types.h>

#if defined(__cplusplus)
extern "C" {
#endif

// Forward declarations

#define ProviderStartOrdinal ((uint32_t)1)
typedef struct ProviderStartRequest ProviderStartRequest;
#define ProviderStopOrdinal ((uint32_t)2)
typedef struct ProviderStopRequest ProviderStopRequest;
#define RegistryRegisterTraceProviderOrdinal ((uint32_t)1)
typedef struct RegistryRegisterTraceProviderRequest RegistryRegisterTraceProviderRequest;

// Extern declarations

extern const fidl_type_t _ProviderStartRequestTable;
extern const fidl_type_t _ProviderStopRequestTable;
extern const fidl_type_t _RegistryRegisterTraceProviderRequestTable;

// Declarations

struct ProviderStartRequest {
    FIDL_ALIGNDECL
    fidl_message_header_t hdr;
    zx_handle_t buffer;
    zx_handle_t fence;
    fidl_vector_t categories;
};

struct ProviderStopRequest {
    FIDL_ALIGNDECL
    fidl_message_header_t hdr;
};

struct RegistryRegisterTraceProviderRequest {
    FIDL_ALIGNDECL
    fidl_message_header_t hdr;
    zx_handle_t provider;
};

#if defined(__cplusplus)
}
#endif
