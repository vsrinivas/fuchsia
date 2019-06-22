// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

// Include our sister file from the same directory we're in, first.
#include "hermetic-data.h"

#include <cstring>
#include <cstdio>
#include <fbl/macros.h>
#include <lib/zx/process.h>
#include <lib/zx/vmar.h>
#include <optional>
#include <type_traits>
#include <utility>

// Manage a process that will run a hermetic compute module.
class HermeticComputeProcess {
public:
    // Only Init() and the accessors should be called on default-constructed.
    HermeticComputeProcess() = default;

    // An object created from an existing process and its root VMAR
    // (or smaller child VMAR) is ready to be used.
    HermeticComputeProcess(zx::process proc, zx::vmar vmar) :
        process_(std::move(proc)), vmar_(std::move(vmar)) {
    }

    // Create a new process.
    zx_status_t Init(const zx::job& job, const char* name) {
        return zx::process::create(
            job, name, static_cast<uint32_t>(strlen(name)), 0,
            &process_, &vmar_);
    }

    // The process handle lives as long as the object.
    const zx::process& process() const { return process_; }

    // The VMAR handle is not usually needed after setting up the module.
    // It's reset by Start() but can be used, reset, or moved before that.
    const zx::vmar& vmar() const { return vmar_; }
    zx::vmar& vmar() { return vmar_; }

    //
    // Low-level interface: loading up the memory.
    //

    // Load an ET_DYN file from the VMO.
    zx_status_t LoadElf(const zx::vmo& vmo,
                        uintptr_t* out_base, uintptr_t* out_entry,
                        size_t* out_stack_size);

    // Allocate a stack VMO and map it into the process, yielding the base of
    // the stack (corresponding to VMO offset 0).
    zx_status_t LoadStack(size_t* size, zx::vmo* out_vmo,
                          uintptr_t* out_stack_base);

    // Acquire the VMO for the vDSO.
    static const zx::vmo& GetVdso(const char* variant = nullptr);

    //
    // Low-level interface: take-off and landing.
    //
    // This can be used with any kind of code.  The low-level
    // setup and communication details are left to the caller.
    //

    // Start the process with an initial thread.
    // Parameters are passed directly into zx_process_start().
    zx_status_t Start(uintptr_t entry, uintptr_t sp,
                      zx::handle arg1, uintptr_t arg2);

    // Wait for the process to finish and (optionally) yield its exit status.
    // This is just a convenient way to wait for the ZX_PROCESS_TERMINATED
    // signal on process() and then collect zx_info_process_t::return_code.
    // To synchronize in more complex ways, use process() directly.
    zx_status_t Wait(int64_t* result = nullptr,
                     zx::time deadline = zx::time::infinite());

    //
    // High-level interface: loading and launching.
    //
    // This is intended to be used with hermetic compute modules built
    // using the <lib/hermetic-compute/hermetic-engine.h> library and
    // sharing data via <lib/hermetic-compute/hermetic-data.h> types.
    //

    // Launcher provides a fluent API for setting up and launching an engine.
    auto GetLauncher() { return Launcher(*this); }

    // Launcher is a single-use object, meant to be used in "fluent" style:
    //
    //   zx_status_t result = engine.GetLauncher()
    //       .UseVdso(...)
    //       .Load(...)
    //       .Start(...)
    //       .status();
    //
    // Each call short-circuits and returns immediately if a previous call
    // failed.  The result of the first failing call can be fetched with
    // status(), which returns ZX_OK if nothing has failed.
    class Launcher {
    public:
        zx_status_t status() const { return status_; }

        Launcher& Init(const zx::job& job, const char* name) {
            if (engine_.process()) {
                status_ = ZX_ERR_BAD_STATE;
            } else {
                status_ = engine_.Init(job, name);
            }
            return *this;
        }

        // Supply the given vDSO to the engine.  |vmo| may be invalid to give
        // it no vDSO at all, so it cannot make any system calls at all.
        // If this is not called, it's equivalent to calling it with GetVdso().
        // This only has any effect when called before Load().  Note that this
        // uses (but does not own) the reference until Load() is called.
        Launcher& UseVdso(const zx::vmo& vmo) {
            vdso_.emplace(vmo);
            return *this;
        }

        // Load a standard hermetic module, which is a single ELF module that
        // optionally gets a vDSO loaded (controlled by a prior UseVdso call).
        //
        // Any number of arguments of integer types no larger than uintptr_t get
        // forwarded to the engine's entry point (see HermeticComputeEngine).
        //
        // This sets up stacks etc. along with the hermetic::Tcb data.
        template <typename... Args>
        Launcher& Load(const zx::vmo& vmo, Args... args) {
            if (status_ == ZX_OK) {
                if (!vdso_.has_value()) {
                    vdso_.emplace(GetVdso());
                }
                static_assert((std::is_scalar_v<decltype(args)> && ...));
                static_assert((!std::is_floating_point_v<decltype(args)> &&
                               ...));
                LoadModule(vmo, zx::unowned<zx::vmo>{**vdso_},
                           sizeof...(Args), static_cast<uintptr_t>(args)...);
                vdso_.reset();
            }
            return *this;
        }

        // Start the module running after everything is loaded up.  After this,
        // it's running.  Call HermeticComputeProcess::Wait() to let it finish.
        Launcher& Start(zx::handle handle = {}) {
            if (status_ == ZX_OK) {
                status_ = engine_.Start(entry_, sp_, std::move(handle), tcb_);
            }
            return *this;
        }

    private:
        friend HermeticComputeProcess;
        HermeticComputeProcess& engine_;
        std::optional<zx::unowned_vmo> vdso_;
        uintptr_t entry_ = 0;
        uintptr_t sp_ = 0;
        uintptr_t tcb_ = 0;
        zx_status_t status_ = ZX_OK;

        DISALLOW_COPY_ASSIGN_AND_MOVE(Launcher);
        Launcher() = delete;

        explicit Launcher(HermeticComputeProcess& engine) : engine_(engine) {}

        // Forwards any number of uintptr_t arguments.
        void LoadModule(const zx::vmo& vmo, zx::unowned<zx::vmo> vdso,
                        size_t nargs, ...);
    };

    // Shorthand for simple cases.
    template <typename... Args>
    zx_status_t Launch(const zx::vmo& vmo, zx::handle handle, Args... args) {
        return GetLauncher()
            .Load(vmo, std::forward<Args>(args)...)
            .Start(std::move(handle))
            .status();
    }

    // Shorthand for simplest cases.
    template <typename... Args>
    zx_status_t Call(const zx::vmo& vmo, zx::handle handle,
                     int64_t *result, Args... args) {
        zx_status_t status =
            Launch(vmo, std::move(handle), std::forward<Args>(args)...);
        if (status == ZX_OK) {
            status = Wait(result);
        }
        return status;
    }

private:
    zx::process process_;
    zx::vmar vmar_;
};
