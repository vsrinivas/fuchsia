// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/paver/provider.h>

#include <fuchsia/paver/c/fidl.h>
#include <lib/fidl-async/bind.h>
#include <lib/paver/paver.h>
#include <zircon/status.h>
#include <zircon/syscalls.h>

namespace {

zx_status_t QueryActiveConfiguration(void* ctx, fidl_txn_t* txn) {
    fuchsia_paver_Paver_QueryActiveConfiguration_Result result;
    result.tag = fuchsia_paver_Paver_QueryActiveConfiguration_ResultTag_err;
    result.err = ZX_ERR_NOT_SUPPORTED;
    return fuchsia_paver_PaverQueryActiveConfiguration_reply(txn, &result);
}

zx_status_t SetActiveConfiguration(void* ctx, fuchsia_paver_Configuration configuration,
                                   fidl_txn_t* txn) {
    return fuchsia_paver_PaverSetActiveConfiguration_reply(txn, ZX_ERR_NOT_SUPPORTED);
}

zx_status_t MarkActiveConfigurationSuccessful(void* ctx, fidl_txn_t* txn) {
    return fuchsia_paver_PaverMarkActiveConfigurationSuccessful_reply(txn, ZX_ERR_NOT_SUPPORTED);
}

zx_status_t ForceRecoveryConfiguration(void* ctx, fidl_txn_t* txn) {
    return fuchsia_paver_PaverForceRecoveryConfiguration_reply(txn, ZX_ERR_NOT_SUPPORTED);
}

zx_status_t WriteAsset(void* ctx, fuchsia_paver_Configuration configuration,
                       fuchsia_paver_Asset asset, const fuchsia_mem_Buffer* payload,
                       fidl_txn_t* txn) {
    auto status = static_cast<paver::Paver*>(ctx)->WriteAsset(configuration, asset, *payload);
    return fuchsia_paver_PaverWriteAsset_reply(txn, status);
}

zx_status_t WriteVolumes(void* ctx, zx_handle_t payload_stream, fidl_txn_t* txn) {
    auto status = static_cast<paver::Paver*>(ctx)->WriteVolumes(zx::channel(payload_stream));
    return fuchsia_paver_PaverWriteVolumes_reply(txn, status);
}

zx_status_t WriteBootloader(void* ctx, const fuchsia_mem_Buffer* payload, fidl_txn_t* txn) {
    auto status = static_cast<paver::Paver*>(ctx)->WriteBootloader(*payload);
    return fuchsia_paver_PaverWriteBootloader_reply(txn, status);
}

zx_status_t WriteDataFile(void* ctx, const char* filename, size_t filename_len,
                          const fuchsia_mem_Buffer* payload, fidl_txn_t* txn) {
    auto* paver = static_cast<paver::Paver*>(ctx);
    auto status = paver->WriteDataFile(fbl::String(filename, filename_len), *payload);
    return fuchsia_paver_PaverWriteDataFile_reply(txn, status);
}

zx_status_t WipeVolumes(void* ctx, fidl_txn_t* txn) {
    auto status = static_cast<paver::Paver*>(ctx)->WipeVolumes();
    return fuchsia_paver_PaverWipeVolumes_reply(txn, status);
}

constexpr fuchsia_paver_Paver_ops_t kFidlOps = {
    .QueryActiveConfiguration = QueryActiveConfiguration,
    .SetActiveConfiguration = SetActiveConfiguration,
    .MarkActiveConfigurationSuccessful = MarkActiveConfigurationSuccessful,
    .ForceRecoveryConfiguration = ForceRecoveryConfiguration,
    .WriteAsset = WriteAsset,
    .WriteVolumes = WriteVolumes,
    .WriteBootloader = WriteBootloader,
    .WriteDataFile = WriteDataFile,
    .WipeVolumes = WipeVolumes,
};

zx_status_t Connect(void* ctx, async_dispatcher_t* dispatcher, const char* service_name,
                    zx_handle_t request) {
    if (!strcmp(service_name, fuchsia_paver_Paver_Name)) {
        return fidl_bind(dispatcher, request,
                         reinterpret_cast<fidl_dispatch_t*>(fuchsia_paver_Paver_dispatch), ctx,
                         &kFidlOps);
    }

    zx_handle_close(request);
    return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t Init(void** out_ctx) {
    *out_ctx = new paver::Paver;
    return ZX_OK;
}

void Release(void* ctx) {
    delete static_cast<paver::Paver*>(ctx);
}

constexpr const char* kPaverServices[] = {
    fuchsia_paver_Paver_Name,
    nullptr,
};

constexpr zx_service_ops_t kPaverOps = {
    .init = Init,
    .connect = Connect,
    .release = Release,
};

constexpr zx_service_provider_t paver_service_provider = {
    .version = SERVICE_PROVIDER_VERSION,
    .services = kPaverServices,
    .ops = &kPaverOps,
};

} // namespace

const zx_service_provider_t* paver_get_service_provider() {
    return &paver_service_provider;
}
