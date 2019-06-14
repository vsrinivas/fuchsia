// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

// Include our sister file from the same directory we're in, first.
#include "hermetic-data.h"

#include <cstring>
#include <fbl/macros.h>
#include <lib/zx/process.h>
#include <lib/zx/vmar.h>
#include <optional>
#include <tuple>
#include <type_traits>
#include <utility>
#include <zircon/assert.h>

// Forward declaration.
template <typename T>
class HermeticExportAgent;

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

    // Map a VMO into the process.  The location is always randomized and kept
    // far away from any other mappings.
    zx_status_t Map(const zx::vmo& vmo, uint64_t vmo_offset, size_t size,
                    bool writable, uintptr_t* ptr);

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
        // Any number of arguments get forwarded to the engine's entry point
        // (see HermeticComputeEngine).  Arguments can be of any type for which
        // there is a HermeticExportAgent specialization (see below).
        //
        // This sets up stacks etc. along with the hermetic::Tcb data.
        template <typename... Args>
        Launcher& Load(const zx::vmo& vmo, Args&&... args) {
            if (status_ == ZX_OK) {
                if (!vdso_.has_value()) {
                    vdso_.emplace(GetVdso());
                }
                LoadModule(zx::unowned_vmo{vmo}, zx::unowned_vmo{**vdso_});
                // Template shenanigans below flatten arguments to uintptr_t.
                LoadFlatten(vmo, **vdso_, std::forward<Args>(args)...);
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

        // Mark the launcher as having failed so later methods will
        // short-circuit.  This can be called from e.g. HermeticExportAgent
        // to report a failure in a complex transfer.
        Launcher& Abort(zx_status_t status) {
            ZX_ASSERT(status != ZX_OK);
            status_ = status;
            return *this;
        }

        // Map a VMO into the engine process.  This can be called by a
        // HermeticExportAgent.
        Launcher& Map(const zx::vmo& vmo, uint64_t vmo_offset, size_t size,
                      bool writable, uintptr_t* ptr) {
            if (status_ == ZX_OK) {
                status_ = engine_.Map(vmo, vmo_offset, size, writable, ptr);
            } else {
                *ptr = 0;
            }
            return *this;
        }

    private:
        friend HermeticComputeProcess;
        HermeticComputeProcess& engine_;
        std::optional<zx::unowned_vmo> vdso_;
        uintptr_t vdso_base_ = 0;
        uintptr_t entry_ = 0;
        size_t stack_size_ = 0;
        uintptr_t sp_ = 0;
        uintptr_t tcb_ = 0;
        zx_status_t status_ = ZX_OK;

        DISALLOW_COPY_ASSIGN_AND_MOVE(Launcher);
        Launcher() = delete;

        explicit Launcher(HermeticComputeProcess& engine) : engine_(engine) {}

        void LoadModule(zx::unowned_vmo vmo, zx::unowned_vmo vdso);

        // Forwards any number of uintptr_t arguments.
        void LoadStack(size_t nargs, ...);

        // HermeticParameter converts an argument to a tuple of simpler
        // arguments.  Paste all the tuples together and reflatten.
        template <typename... Args>
        void LoadFlatten(const zx::vmo& vmo, const zx::vmo& vdso,
                         Args&&... args) {
            LoadTuple(
                vmo, vdso,
                std::tuple_cat(
                    HermeticExportAgent<typename std::decay<Args>::type>(
                        *this)(std::forward<Args>(args))...));
        }

        // Unwrap a tuple that's not all uintptr_t and come back around.
        template <typename... T>
        std::enable_if_t<!(std::is_same_v<uintptr_t, T> && ...)>
        LoadTuple(const zx::vmo& vmo, const zx::vmo& vdso,
                  std::tuple<T...> args) {
            std::apply([&](auto... args) {
                           LoadFlatten(vmo, vdso, args...);
                       }, args);
        }

        // Unwrap a tuple of all uintptr_t and actually make the call.
        template <typename... T>
        std::enable_if_t<(std::is_same_v<uintptr_t, T> && ...)>
        LoadTuple(const zx::vmo& vmo, const zx::vmo& vdso,
                  std::tuple<T...> args) {
            std::apply([&](auto... args) {
                           LoadStack(sizeof...(T), args...);
                       }, args);
        }
    };

    // Shorthand for simple cases.
    template <typename... Args>
    zx_status_t Launch(const zx::vmo& vmo, zx::handle handle, Args&&... args) {
        return GetLauncher()
            .Load(vmo, std::forward<Args>(args)...)
            .Start(std::move(handle))
            .status();
    }

    // Shorthand for simplest cases.
    template <typename... Args>
    zx_status_t Call(const zx::vmo& vmo, zx::handle handle,
                     int64_t *result, Args&&... args) {
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

// This can be specialized to provide transparent argument-passing support for
// nontrivial types.  The default implementation handles trivial types that
// don't have padding bits.
//
// The HermeticExportAgent for a type packs arguments "exported" to the
// hermetic environment, ultimately by flattening everything into uintptr_t[].
// The packing protocol for an "export" type is understood in the hermetic
// engine to form a corresponding "import" type.  The engine code has a
// specialization of HermeticImportAgent that unpacks the uinptr_t[] into the
// "import" type.  Note that the "export" and "import" types need not be the
// same type, just corresponding types with a compatible packing protocol.
//
// HermeticExportAgent() takes a reference to the HermeticComputeProcess.  The
// agent object is then called with the value as argument and returns a
// std::tuple of arguments to pass instead.  Each of those arguments then gets
// passed through a HermeticExportAgent of its own if it's not already of type
// uintptr_t.  Note an agent is free to return an empty tuple, as well as a
// tuple of one or more complex types that themselves need separate packing.
// Thus marker types can be used as dummy parameters just for side effects.
//
// The agent object can poke the process, e.g. to map things into its address
// space.  The agent runs after the engine has been loaded into the process but
// before its first thread has been created and before its stacks have been
// allocated.  It can call engine() for e.g. process() or vmar().  If it
// encounters any errors it should call Launcher::Abort(zx_status_t).  This
// will short-circuit the launch.  Additional agents will be called to pack
// parameters, but the final stack setup and process start will never happen.
// An agent can check Launcher::status() to short-circuit its own work when the
// launch has been aborted, though it still has to return some value of its
// std::tuple<...> return type.
//
template <typename T>
class HermeticExportAgent {
public:
    using type = T;

    explicit HermeticExportAgent(HermeticComputeProcess::Launcher&) {}

    static_assert(std::is_standard_layout_v<T>,
                  "need converter for non-standard-layout type");

    static_assert(std::has_unique_object_representations_v<T> ||
                  std::is_floating_point_v<T>,
                  "need converter for type with padding bits");

    auto operator()(const type& x) {
        if constexpr (std::is_integral_v<T> &&
                      sizeof(T) <= sizeof(uintptr_t)) {
            // Small integer types can just be coerced to uintptr_t.
            return std::make_tuple(static_cast<uintptr_t>(x));
        } else {
            // Other things can be turned into an array of uintptr_t.
            constexpr auto nwords =
                (sizeof(T) + sizeof(uintptr_t) - 1) / sizeof(uintptr_t);
            std::tuple<std::array<uintptr_t, nwords>> result{{}};
            memcpy(std::get<0>(result).data(), &x, sizeof(x));
            return result;
        }
    }
};

// A base class for the HermeticExportAgent<T> specialization.
template <typename T>
class HermeticExportAgentBase {
public:
    using type = T;

    explicit HermeticExportAgentBase(
        HermeticComputeProcess::Launcher& launcher) : launcher_(launcher) {}

    HermeticComputeProcess::Launcher& launcher() { return launcher_; }
    auto engine() { return launcher()->engine(); }

protected:
    using Base = HermeticExportAgentBase<T>;

    void Abort(zx_status_t status) { launcher().Abort(status); }

    void Ok(zx_status_t status) {
        if (status != ZX_OK) {
            Abort(status);
        }
    }

private:
    HermeticComputeProcess::Launcher& launcher_;
};

// Specialization for tuples.
template <typename... T>
struct HermeticExportAgent<std::tuple<T...>> :
    public HermeticExportAgentBase<std::tuple<T...>> {
    using Base = HermeticExportAgentBase<std::tuple<T...>>;
    using type = typename Base::type;
    explicit HermeticExportAgent(
        HermeticComputeProcess::Launcher& launcher) : Base(launcher) {}

    auto operator()(const type& x) {
        // Tuples get flattened.  Each element will then get converted.
        return x;
    }
};

// Specialization for pairs.
template <typename T1, typename T2>
struct HermeticExportAgent<std::pair<T1, T2>> :
    public HermeticExportAgentBase<std::pair<T1, T2>> {
    using Base = HermeticExportAgentBase<std::pair<T1, T2>>;
    using type = typename Base::type;
    explicit HermeticExportAgent(
        HermeticComputeProcess::Launcher& launcher) : Base(launcher) {}

    auto operator()(const type& x) {
        // Tuplize the pair.
        return std::make_from_tuple<std::tuple<T1, T2>>(x);
    }
};

// Specialization for std::array.
template <typename T, size_t N>
class HermeticExportAgent<std::array<T, N>> :
    public HermeticExportAgentBase<std::array<T, N>> {
public:
    using Base = HermeticExportAgentBase<std::array<T, N>>;
    using type = typename Base::type;
    explicit HermeticExportAgent(
        HermeticComputeProcess::Launcher& launcher) : Base(launcher) {}

    // Template so it can take actual arrays too.
    auto operator()(const type& x) {
        // Tuplize.  Note that for sizeof(T) < uintptr_t, this is less optimal
        // packing than simply treating the whole array as a block of bytes,
        // which is what happens for structs.  But it's fully general for all
        // element types, allowing recursive type-specific packing.
        return ArrayTuple(x, std::make_index_sequence<N>());
    }

private:
    template <typename Array, size_t... I>
    auto ArrayTuple(const Array& a, std::index_sequence<I...>) {
        return std::make_tuple(a[I]...);
    }
};
