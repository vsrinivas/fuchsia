// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

// The hermetic decompressor library provides a simple interface for using
// decompression kernels spawned as hermetic compute engines.

#include <lib/hermetic-compute/hermetic-compute.h>
#include <lib/hermetic-compute/vmo-span.h>
#include <lib/zx/job.h>

// HermeticDecompressor is parameterized by an EngineService class that's
// responsible for supplying the executable VMOs that get loaded into the
// hermetic compute process: the decompression kernel and the vDSO.
//
// This is the default EngineService used if no template parameter is given.
// A different class can be provided; it must provide these two methods.
struct HermeticDecompressorEngineService {
    // Magic number at the start of a compressed image.
    // Reading this much is enough to identify the format.
    using Magic = uint32_t;

    // These are the magic numbers for the formats GetEngine groks.  They're
    // only here because they aren't exported by the normal public headers of
    // the format support libraries themselves.
    static constexpr Magic kLz4fMagic = 0x184D2204;
    static constexpr Magic kZstdMagic = 0xFD2FB528;

    // This finds the appropriate decompression kernel for the magic number
    // found at the beginning of the compressed image.  It should return
    // ZX_ERR_NOT_FOUND for an unrecognized magic number.
    zx_status_t GetEngine(Magic magic, zx::unowned_vmo* vmo);

    // This finds the appropriate vDSO to support a decompression kernel.
    zx_status_t GetVdso(zx::unowned_vmo* vmo) const {
        *vmo = zx::unowned_vmo{HermeticComputeProcess::GetVdso()};
        return ZX_OK;
    }

    auto job() const { return zx::job::default_job(); }
};

template <typename EngineService>
class HermeticDecompressorWithEngineService {
public:
    // If the EngineService wants ctor arguments, pass them through.
    template <typename... Args>
    explicit HermeticDecompressorWithEngineService(Args&&... args) :
        engine_service_(std::forward<Args>(args)...) {}

    zx_status_t operator()(const zx::vmo& vmo,
                           uint64_t vmo_offset, size_t size,
                           const zx::vmo& output,
                           uint64_t output_offset, size_t output_size) {
        // Read the magic number to determine the compression algorithm.
        typename EngineService::Magic magic;
        zx_status_t status = vmo.read(&magic, vmo_offset, sizeof(magic));
        if (status != ZX_OK) {
            return status;
        }

        // Let the service provide the engine that handles this magic number.
        zx::unowned_vmo engine_vmo;
        status = engine_service_.GetEngine(magic, &engine_vmo);
        if (status != ZX_OK) {
            return status;
        }

        // The service also supplies the vDSO to use.
        zx::unowned_vmo vdso;
        status = engine_service_.GetVdso(&vdso);
        if (status != ZX_OK) {
            return status;
        }

        // Set up the engine.
        HermeticComputeProcess hcp;
        status = hcp.Init(*engine_service_.job(), "hermetic-decompressor");
        if (status != ZX_OK) {
            return status;
        }

        // Spin up the engine and start it running.
        // It will write directly into the output VMO.
        status = hcp(HermeticComputeProcess::Vdso{*vdso},
                     HermeticComputeProcess::Elf{*engine_vmo},
                     LeakyVmoSpan{vmo, vmo_offset, size},
                     WritableVmoSpan{output, output_offset, output_size});

        if (status == ZX_OK) {
            // Wait for it to finish.
            int64_t result;
            status = hcp.Wait(&result);
            if (status == ZX_OK) {
                status = static_cast<zx_status_t>(result);
            }
        }

        // Wait for it to finish.
        int64_t result;
        status = hcp.Wait(&result);
        return status == ZX_OK ? static_cast<zx_status_t>(result) : status;
    }

private:
    EngineService engine_service_;
};

using HermeticDecompressor =
    HermeticDecompressorWithEngineService<HermeticDecompressorEngineService>;
