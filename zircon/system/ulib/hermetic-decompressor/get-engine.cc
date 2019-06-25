// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/hermetic-decompressor/hermetic-decompressor.h>

#include <fuchsia/ldsvc/c/fidl.h>
#include <string_view>
#include <zircon/dlfcn.h>

namespace {

// This "borrows" the ldsvc handle from libdl by stealing it and then giving it
// back.  So it's not thread-safe with respect to concurrent dlopen calls.
class UseLdsvc {
public:
    UseLdsvc(const UseLdsvc&) = delete;
    UseLdsvc(UseLdsvc&&) = delete;

    UseLdsvc() : ldsvc_(dl_set_loader_service({})) {}
    ~UseLdsvc() { dl_set_loader_service(ldsvc_->get()); }

    zx_status_t operator()(std::string_view name, zx::vmo* vmo) {
        int32_t rv;
        zx_status_t status = fuchsia_ldsvc_LoaderLoadObject(
            ldsvc_->get(), name.data(), name.size(),
            &rv, vmo->reset_and_get_address());
        return status == ZX_OK ? rv : status;
    }

private:
    zx::unowned_channel ldsvc_;
};

struct Decompressor {
    std::string_view name;
    zx::vmo* cache;
    HermeticDecompressorEngineService::Magic magic;
};

zx::vmo cache_lz4f, cache_zstd;

constexpr Decompressor kDecompressors[] = {
    {"hermetic/decompress-lz4f.so", &cache_lz4f,
     HermeticDecompressorEngineService::kLz4fMagic},
    {"hermetic/decompress-zstd.so", &cache_zstd,
     HermeticDecompressorEngineService::kZstdMagic},
};

} // namespace

zx_status_t HermeticDecompressorEngineService::GetEngine(
    Magic magic, zx::unowned_vmo* vmo) {

    for (const auto& engine : kDecompressors) {
        if (engine.magic == magic) {
            if (!*engine.cache) {
                // Fetch the applicable engine by name from the loader service.
                zx_status_t status = UseLdsvc()(engine.name, engine.cache);
                if (status != ZX_OK) {
                    return status;
                }
            }
            *vmo = zx::unowned_vmo{*engine.cache};
            return ZX_OK;
        }
    }

    return ZX_ERR_NOT_FOUND;
}
