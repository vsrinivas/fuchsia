// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// *** PT-127 ****************************************************************
// This file is temporary, and provides a sufficient API to exercise
// the old fuchsia.tracelink FIDL API. It will go away once all providers have
// updated to use the new fuchsia.tracing.provider FIDL API (which is
// different from fuchsia.tracelink in name only).
// ***************************************************************************

#pragma once

#include <zircon/compiler.h>
#include <zircon/types.h>

#include <lib/async/dispatcher.h>

__BEGIN_CDECLS

// Represents a tracelink-based trace provider.
typedef struct tracelink_provider tracelink_provider_t;

// See provider.h for descriptions of these functions, replacing
// "tracelink" with "trace".

tracelink_provider_t* tracelink_provider_create_with_name_etc(
    zx_handle_t to_service, async_dispatcher_t* dispatcher, const char* name);

tracelink_provider_t* tracelink_provider_create_synchronously_etc(
    zx_handle_t to_service, async_dispatcher_t* dispatcher, const char* name,
    bool* out_already_started);

tracelink_provider_t* tracelink_provider_create_with_name_fdio(
    async_dispatcher_t* dispatcher, const char* name);

tracelink_provider_t* tracelink_provider_create_synchronously_with_fdio(
    async_dispatcher_t* dispatcher, const char* name,
    bool* out_already_started);

void tracelink_provider_destroy(tracelink_provider_t* provider);

__END_CDECLS

#ifdef __cplusplus

#include <memory>
#include <lib/zx/channel.h>

namespace trace {

// See provider.h for descriptions of these classes,
// replacing "Tracelink" with "Trace".

class TracelinkProviderEtc {
public:
    static bool CreateSynchronously(
            zx::channel to_service,
            async_dispatcher_t* dispatcher,
            const char* name,
            std::unique_ptr<TracelinkProviderEtc>* out_provider,
            bool* out_already_started) {
        auto provider = tracelink_provider_create_synchronously_etc(
            to_service.release(), dispatcher, name, out_already_started);
        if (!provider)
            return false;
        *out_provider = std::unique_ptr<TracelinkProviderEtc>(
            new TracelinkProviderEtc(provider));
        return true;
    }

    TracelinkProviderEtc(zx::channel to_service, async_dispatcher_t* dispatcher,
                         const char* name)
        : provider_(tracelink_provider_create_with_name_etc(to_service.release(),
                                                            dispatcher, name)) {}

    ~TracelinkProviderEtc() {
        if (provider_)
            tracelink_provider_destroy(provider_);
    }

    bool is_valid() const {
        return provider_ != nullptr;
    }

protected:
    explicit TracelinkProviderEtc(tracelink_provider_t* provider)
        : provider_(provider) {}

private:
    tracelink_provider_t* const provider_;
};

class TracelinkProviderWithFdio : public TracelinkProviderEtc {
public:
    static bool CreateSynchronously(
            async_dispatcher_t* dispatcher,
            const char* name,
            std::unique_ptr<TracelinkProviderWithFdio>* out_provider,
            bool* out_already_started) {
        auto provider = tracelink_provider_create_synchronously_with_fdio(
            dispatcher, name, out_already_started);
        if (!provider)
            return false;
        *out_provider = std::unique_ptr<TracelinkProviderWithFdio>(
            new TracelinkProviderWithFdio(provider));
        return true;
    }

    explicit TracelinkProviderWithFdio(async_dispatcher_t* dispatcher,
                                       const char* name)
        : TracelinkProviderWithFdio(
            tracelink_provider_create_with_name_fdio(dispatcher, name)) {}

private:
    explicit TracelinkProviderWithFdio(tracelink_provider_t* provider)
        : TracelinkProviderEtc(provider) {}
};

} // namespace trace

#endif // __cplusplus
