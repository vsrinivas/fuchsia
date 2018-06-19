// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fbl/algorithm.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/crashanalyzer/crashanalyzer.h>
#include <lib/fdio/util.h>
#include <lib/process-launcher/launcher.h>
#include <lib/svc/outgoing.h>
#include <zircon/process.h>
#include <zircon/processargs.h>
#include <zircon/status.h>

// An instance of a zx_service_provider_t.
//
// Includes the |ctx| pointer for the zx_service_provider_t.
typedef struct zx_service_provider_instance {
    // The service provider for which this structure is an instance.
    const zx_service_provider_t* provider;

    // The |ctx| pointer returned by the provider's |init| function, if any.
    void* ctx;
} zx_service_provider_instance_t;

static zx_status_t provider_init(zx_service_provider_instance_t* instance) {
    if (instance->provider->ops->init) {
        zx_status_t status = instance->provider->ops->init(&instance->ctx);
        if (status != ZX_OK)
            return status;
    }
    return ZX_OK;
}

static zx_status_t provider_publish(zx_service_provider_instance_t* instance,
                                    async_t* async, const fbl::RefPtr<fs::PseudoDir>& dir) {
    const zx_service_provider_t* provider = instance->provider;

    if (!provider->services || !provider->ops->connect)
        return ZX_ERR_INVALID_ARGS;

    for (size_t i = 0; provider->services[i]; ++i) {
        const char* service_name = provider->services[i];
        zx_status_t status = dir->AddEntry(
            service_name,
            fbl::MakeRefCounted<fs::Service>([instance, async, service_name](zx::channel request) {
                return instance->provider->ops->connect(instance->ctx, async, service_name, request.release());
            }));
        if (status != ZX_OK) {
            for (size_t j = 0; j < i; ++j)
                dir->RemoveEntry(provider->services[j]);
            return status;
        }
    }

    return ZX_OK;
}

static void provider_release(zx_service_provider_instance_t* instance) {
    if (instance->provider->ops->release)
        instance->provider->ops->release(instance->ctx);
    instance->ctx = nullptr;
}

static zx_status_t provider_load(zx_service_provider_instance_t* instance,
                                 async_t* async, const fbl::RefPtr<fs::PseudoDir>& dir) {
    if (instance->provider->version != SERVICE_PROVIDER_VERSION) {
        return ZX_ERR_INVALID_ARGS;
    }

    zx_status_t status = provider_init(instance);
    if (status != ZX_OK) {
        return status;
    }

    status = provider_publish(instance, async, dir);
    if (status != ZX_OK) {
        provider_release(instance);
        return status;
    }

    return ZX_OK;
}

static zx_handle_t appmgr_svc;

// We should host the tracelink service ourselves instead of routing the request
// to appmgr.
zx_status_t publish_tracelink(const fbl::RefPtr<fs::PseudoDir>& dir) {
    const char* service_name = "fuchsia.tracelink.Registry";
    return dir->AddEntry(
        service_name,
        fbl::MakeRefCounted<fs::Service>([service_name](zx::channel request) {
            return fdio_service_connect_at(appmgr_svc, service_name, request.release());
        }));
}

// We shouldn't need to access these non-Zircon services from svchost, but
// currently some tests assume they can reach these services from the test
// environment. Instead, we should make the test environment hermetic and
// remove the dependencies on these services.
static constexpr const char* deprecated_services[] = {
    // remove amber.Control when CP-50 is resolved
    "fuchsia.amber.Control",
    "fuchsia.cobalt.CobaltEncoderFactory",
    "fuchsia.devicesettings.DeviceSettingsManager",
    "fuchsia.logger.Log",
    "fuchsia.logger.LogSink",
    "fuchsia.media.Audio",
    "fuchsia.mediaplayer.MediaPlayer",
    "fuchsia.netstack.Netstack",
    "fuchsia.net_stack.Stack",
    "fuchsia.power.PowerManager",
    "fuchsia.sys.Environment",
    "fuchsia.sys.Launcher",
    "fuchsia.wlan.service.Wlan",
    // fdio name for Netstack. Will be removed with the new interfaces defined
    // in NET-863.
    "net.Netstack",
    nullptr,
    // DO NOT ADD MORE ENTRIES TO THIS LIST.
    // Tests should not be accessing services from the environment. Instead,
    // they should run in containers that have their own service instances.
};

void publish_deprecated_services(const fbl::RefPtr<fs::PseudoDir>& dir) {
    for (size_t i = 0; deprecated_services[i]; ++i) {
        const char* service_name = deprecated_services[i];
        dir->AddEntry(
            service_name,
            fbl::MakeRefCounted<fs::Service>([service_name](zx::channel request) {
                fprintf(stderr, "svchost: warning: Using deprecated path to %s.\n", service_name);
                return fdio_service_connect_at(appmgr_svc, service_name, request.release());
            }));
    }
}

int main(int argc, char** argv) {
    async::Loop loop;
    async_t* async = loop.async();
    svc::Outgoing outgoing(async);

    appmgr_svc = zx_take_startup_handle(PA_HND(PA_USER0, 0));

    zx_status_t status = outgoing.ServeFromStartupInfo();
    if (status != ZX_OK) {
        fprintf(stderr, "svchost: error: Failed to serve outgoing directory: %d (%s).\n",
                status, zx_status_get_string(status));
        return 1;
    }

    zx_service_provider_instance_t service_providers[] = {
        {.provider = launcher_get_service_provider(), .ctx = nullptr},
        {.provider = crashanalyzer_get_service_provider(), .ctx = nullptr},
    };

    for (size_t i = 0; i < fbl::count_of(service_providers); ++i) {
        status = provider_load(&service_providers[i], async, outgoing.public_dir());
        if (status != ZX_OK) {
            fprintf(stderr, "svchost: error: Failed to load service provider %zu: %d (%s).\n",
                    i, status, zx_status_get_string(status));
            return 1;
        }
    }

    status = publish_tracelink(outgoing.public_dir());
    if (status != ZX_OK) {
        fprintf(stderr, "svchost: error: Failed to publish tracelink: %d (%s).\n",
                status, zx_status_get_string(status));
        return 1;
    }

    publish_deprecated_services(outgoing.public_dir());

    status = loop.Run();

    for (size_t i = 0; i < fbl::count_of(service_providers); ++i) {
        provider_release(&service_providers[i]);
    }

    return status;
}
