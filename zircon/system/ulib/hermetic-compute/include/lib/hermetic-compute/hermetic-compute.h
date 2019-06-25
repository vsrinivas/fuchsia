// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

// Include our sister file from the same directory we're in, first.
#include "hermetic-data.h"

#include <cstring>
#include <fbl/macros.h>
#include <lib/zx/process.h>
#include <lib/zx/suspend_token.h>
#include <lib/zx/thread.h>
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

    // Start the process with complete control over its registers.
    // The initial thread is left suspended so its state can be modified.
    zx_status_t Start(zx::handle handle,
                      zx::thread* out_thread, zx::suspend_token* out_token);

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
    // High-level interface: argument-driven loading and launching.
    //
    // This is intended to be used with hermetic compute modules built
    // using the <lib/hermetic-compute/hermetic-engine.h> library and
    // sharing data via <lib/hermetic-compute/hermetic-data.h> types.
    //
    // By calling the HermeticComputeProcess as a function, everything can be
    // controlled via arguments.  The HermeticExportAgent specializations for
    // the argument types do all the work.  Any number of arguments get
    // forwarded to the engine's entry point (see HermeticComputeEngine).
    // Arguments can be of any type for which there is a HermeticExportAgent
    // specialization (see below).  Everything else is done as a side effect
    // by HermeticExportAgent specializations, including loading the code
    // itself into the process.  Several special wrapper types are provided
    // below just to have particular side effects when passed as arguments.
    //
    // On success, the initial thread is always created and set up to receive
    // the arguments in its registers and stack.  It can be left suspended by
    // passing a Suspended argument that will receive the token to let it run.
    // Otherwise it's already running when this returns.
    template <typename... Args>
    zx_status_t operator()(Args&&... args) {
        // Side effects of transforming the arguments do all the ELF loading
        // and miscellaneous setup before Launcher::Launch does the final
        // stack setup and thread creation.
        Launcher launcher(*this);
        launcher.Flatten(std::forward<Args>(args)...);
        return launcher.status();
    }
    // Keep the initial thread suspended so its state can be modified and take
    // responsibility for letting the thread run.  The thread's register state
    // will be updated at the end of launching and should not be modified
    // before then.  Once the launch steps are all complete, the thread and
    // token handles will be moved into the locations pointed to.  The thread
    // will be allowed to run as soon as the token handle is closed.
    struct Suspended {
        zx::thread* thread;
        zx::suspend_token* token;
    };

    // Set the entry point PC for the engine process.
    struct EntryPoint {
        uintptr_t pc;
    };

    // Set the minimum stack size for the engine process.
    struct StackSize {
        size_t size;
    };

    // Load an ET_DYN file from the VMO.  The initial thread will start at its
    // entry point and its PT_GNU_STACK will determine the stack size, but
    // there are no corresponding import arguments.  Note that nothing
    // prevents passing multiple of these.  They will all be loaded, and
    // the entry point and stack size from the last one will prevail.
    struct Elf {
        const zx::vmo& vmo;
    };

    // Load an ET_DYN file from the VMO.  Imported as const Elf64_Ehdr*.
    struct ExtraElf {
        const zx::vmo& vmo;
    };

    // This exports exactly the same as ExtraElf.  HermeticComputeEngine
    // requires that this be the first imported argument.  It's distinct just
    // for clarity in its use and for its second constructor, which doubles as
    // its default constructor too.
    struct Vdso : public ExtraElf {
        explicit Vdso(const zx::vmo& vmo) : ExtraElf{vmo} {}
        explicit Vdso(const char* variant = nullptr) :
            ExtraElf{GetVdso(variant)} {}
    };

    // Launcher is a single-use object that only lives during a call.
    // It's only ever visible to HermeticExportAgent specializations.
    class Launcher {
    public:
        zx_status_t status() const { return status_; }

        auto& engine() { return engine_; }

        // Mark the launcher as having failed so later methods will
        // short-circuit.  This can be called to report a failure in a
        // complex transfer.
        void Abort(zx_status_t status) {
            ZX_DEBUG_ASSERT(status != ZX_OK);
            status_ = status;
        }

        // Map a VMO into the engine process and return the address of
        // the mapping.  This returns 0 if the mapping failed or wasn't
        // attempted due to an error shown in status().
        uintptr_t Map(const zx::vmo& vmo, uint64_t vmo_offset, size_t size,
                      bool writable) {
            uintptr_t ptr = 0;
            if (status_ == ZX_OK) {
                status_ = engine_.Map(vmo, vmo_offset, size, writable, &ptr);
            }
            return ptr;
        }

    private:
        friend HermeticComputeProcess;
        HermeticComputeProcess& engine_;
        zx::thread thread_;
        zx::suspend_token token_;

        // Only HermeticExportAgent<Suspended> sets suspended_.
        friend HermeticExportAgent<Suspended>;
        std::optional<Suspended> suspended_;

        // Only HermeticExportAgent<EntryPoint> sets entry_pc_;
        friend HermeticExportAgent<EntryPoint>;
        uintptr_t entry_pc_ = 0;

        // Only HermeticExportAgent<StackSize> sets stack_size_;
        friend HermeticExportAgent<StackSize>;
        size_t stack_size_ = 0;

        zx_status_t status_ = ZX_OK;

        DISALLOW_COPY_ASSIGN_AND_MOVE(Launcher);
        Launcher() = delete;

        explicit Launcher(HermeticComputeProcess& engine) : engine_(engine) {}
        ~Launcher();

        friend HermeticExportAgent<zx::handle>; // Only caller of SendHandle.
        zx_handle_t SendHandle(zx::handle handle);

        // Forwards any number of uintptr_t arguments.
        void Launch(size_t nargs, ...);

        // An argument list is fully flattened when it's all uintptr_t.
        // Then it's ready to pass to Launch.
        template <typename... T>
        static constexpr bool kLaunchable =
            (std::is_same_v<uintptr_t, T> && ...);

        template <typename Arg>
        using Agent = HermeticExportAgent<std::decay_t<Arg>>;

        template <typename Arg>
        auto MakeAgent(Launcher& launcher) {
            return Agent<Arg>(launcher);
        }

        // HermeticExportAgent converts an argument to a tuple of simpler
        // arguments.  Paste all the tuples together and reflatten.
        template <typename... Args>
        void Flatten(Args&&... args) {
            // Calls evaluate arguments in unspecified order.  So the
            // make_tuple call (that's not actually evaluated) would invoke
            // agents in unspecified order, just like doing it directly in
            // the tuple_cat call.  But list initialization evaluates its
            // initializers left to right.  So this invokes the agents in
            // left-to-right order as their results go into the pack.
            decltype(std::make_tuple(
                         MakeAgent<Args>(*this)(std::forward<Args>(args))...))
                pack{MakeAgent<Args>(*this)(std::forward<Args>(args))...};

            // Perfect forwarding in a generic lambda!
            auto cat =
                [](auto&&... x) {
                    return std::tuple_cat(std::forward<decltype(x)>(x)...);
                };

            // Flatten the pack of tuples to a single tuple and flatten that.
            FlattenTuple(std::apply(cat, std::move(pack)));
        }

        template <typename... T>
        using IfLaunchable = std::enable_if_t<kLaunchable<T...>>;

        template <typename... T>
        using IfNotLaunchable = std::enable_if_t<!kLaunchable<T...>>;

        // Unwrap a tuple that's not all uintptr_t and come back around.
        template <typename... T>
        IfNotLaunchable<T...> FlattenTuple(std::tuple<T...> args) {
            // Perfect forwarding in a generic lambda!
            std::apply(
                [&](auto&&... x) { Flatten(std::forward<decltype(x)>(x)...); },
                std::move(args));
        }

        // Unwrap a tuple of all uintptr_t and actually make the call.
        template <typename... T>
        IfLaunchable<T...> FlattenTuple(std::tuple<T...> args) {
            // Technically a generic lambda, but they're always all uintptr_t.
            std::apply(
                [&](auto... args) { Launch(sizeof...(T), args...); },
                std::move(args));
        }
    };

    // Shorthand for simplest cases.
    template <typename... Args>
    zx_status_t Call(int64_t *result, Args&&... args) {
        zx_status_t status = (*this)(Vdso{}, std::forward<Args>(args)...);
        if (status == ZX_OK) {
            status = Wait(result);
        }
        return status;
    }

private:
    zx::process process_;
    zx::vmar vmar_;
};

// A base class for the HermeticExportAgent<T> specialization.
template <typename T>
class HermeticExportAgentBase {
public:
    using type = T;

    explicit HermeticExportAgentBase(
        HermeticComputeProcess::Launcher& launcher) : launcher_(launcher) {}

    auto& launcher() const { return launcher_; }
    auto& engine() const { return launcher_.engine(); }

protected:
    using Base = HermeticExportAgentBase<T>;

    zx_status_t status() const { return launcher().status(); }

    void Abort(zx_status_t status) { launcher().Abort(status); }

    bool Ok() const {
        return status() == ZX_OK;
    }

    bool Ok(zx_status_t status) {
        if (status != ZX_OK) {
            Abort(status);
        }
        return Ok();
    }

private:
    HermeticComputeProcess::Launcher& launcher_;
};

// This can be specialized to provide transparent argument-passing support for
// nontrivial types.  The default implementation handles trivial types that
// don't have padding bits, and zx::* handle types.
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
// Handle types (zx::object et al) yield zx_handle_t.  There can be only one
// handle-typed argument in a call, since only one handle is transferred.
//
// TODO(mcgrathr): When the engine side can actually make use of handles, add
// a magic type that makes a channel stashed in the Launcher and sends that
// handle, unpacked on the other side to fetch the multiple handles and/or
// large(r) data blobs(?).  Then make this automagically detect if that has
// been used and send handles into the channel instead.
template <typename T>
class HermeticExportAgent : public HermeticExportAgentBase<T> {
private:
    static constexpr bool kIsHandle = std::is_base_of_v<zx::object_base, T>;

public:
    using Base = HermeticExportAgentBase<T>;
    using typename Base::type;
    explicit HermeticExportAgent(
        HermeticComputeProcess::Launcher& launcher) : Base(launcher) {}

    static_assert(kIsHandle || std::is_standard_layout_v<T>,
                  "need converter for non-standard-layout type");

    static_assert(kIsHandle ||
                  std::has_unique_object_representations_v<T> ||
                  std::is_floating_point_v<T>,
                  "need converter for type with padding bits");

    template <typename ArgType>
    auto operator()(ArgType&& x) {
        static_assert(std::is_same_v<std::decay_t<ArgType>, type>);
        if constexpr (kIsHandle) {
            return std::make_tuple(zx::handle{std::move(x)});;
        } else if constexpr (std::is_integral_v<T> &&
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

// Specialization for tuples.
template <typename... T>
struct HermeticExportAgent<std::tuple<T...>> :
    public HermeticExportAgentBase<std::tuple<T...>> {
    using Base = HermeticExportAgentBase<std::tuple<T...>>;
    using typename Base::type;
    explicit HermeticExportAgent(
        HermeticComputeProcess::Launcher& launcher) : Base(launcher) {}

    auto operator()(type x) {
        // Tuples get flattened.  Each element will then get converted.
        return x;
    }
};

// Specialization for pairs.
template <typename T1, typename T2>
struct HermeticExportAgent<std::pair<T1, T2>> :
    public HermeticExportAgentBase<std::pair<T1, T2>> {
    using Base = HermeticExportAgentBase<std::pair<T1, T2>>;
    using typename Base::type;
    explicit HermeticExportAgent(
        HermeticComputeProcess::Launcher& launcher) : Base(launcher) {}

    auto operator()(type x) {
        // Tuplize the pair.
        return std::make_from_tuple<std::tuple<T1, T2>>(std::move(x));
    }
};

// Specialization for std::array.
template <typename T, size_t N>
class HermeticExportAgent<std::array<T, N>> :
    public HermeticExportAgentBase<std::array<T, N>> {
public:
    using Base = HermeticExportAgentBase<std::array<T, N>>;
    using typename Base::type;
    explicit HermeticExportAgent(
        HermeticComputeProcess::Launcher& launcher) : Base(launcher) {}

    auto operator()(type x) {
        // Tuplize.  Note that for sizeof(T) < uintptr_t, this is less optimal
        // packing than simply treating the whole array as a block of bytes,
        // which is what happens for structs.  But it's fully general for all
        // element types, allowing recursive type-specific packing.
        return ArrayTuple(std::move(x), std::make_index_sequence<N>());
    }

private:
    template <typename Array, size_t... I>
    auto ArrayTuple(Array a, std::index_sequence<I...>) {
        return std::make_tuple(std::move(a[I])...);
    }
};

// Specialization for simple ELF loading.
// Yields an imported argument of const Elf64_Ehdr*.
template <>
struct HermeticExportAgent<HermeticComputeProcess::ExtraElf> :
    public HermeticExportAgentBase<HermeticComputeProcess::ExtraElf> {
    using Base = HermeticExportAgentBase<HermeticComputeProcess::ExtraElf>;
    using typename Base::type;
    explicit HermeticExportAgent(
        HermeticComputeProcess::Launcher& launcher) : Base(launcher) {}

    auto operator()(const type& elf) {
        uintptr_t base = 0;
        if (Ok()) {
            Ok(engine().LoadElf(elf.vmo, &base, nullptr, nullptr));
        }
        return std::make_tuple(base);
    }
};

// Vdso is the same as ExtraElf.
template <>
struct HermeticExportAgent<HermeticComputeProcess::Vdso> :
    public HermeticExportAgent<HermeticComputeProcess::ExtraElf> {
    using Base = HermeticExportAgent<HermeticComputeProcess::ExtraElf>;
    using typename Base::type;
    explicit HermeticExportAgent(
        HermeticComputeProcess::Launcher& launcher) : Base(launcher) {}
};

// Specialization for loading the main ELF file.
// Yields no imported arguments.
template <>
struct HermeticExportAgent<HermeticComputeProcess::Elf> :
    public HermeticExportAgentBase<HermeticComputeProcess::Elf> {
    using Base = HermeticExportAgentBase<HermeticComputeProcess::Elf>;
    using typename Base::type;
    explicit HermeticExportAgent(
        HermeticComputeProcess::Launcher& launcher) : Base(launcher) {}

    // Load the ELF image by side effect and then reduce to the arguments
    // that set the entry point and stack size.
    using return_type = std::tuple<HermeticComputeProcess::EntryPoint,
                                   HermeticComputeProcess::StackSize>;
    return_type operator()(const type& elf) {
        uintptr_t entry = 0;
        size_t stack_size = 0;
        if (Ok()) {
            Ok(engine().LoadElf(elf.vmo, nullptr, &entry, &stack_size));
        }
        return {{entry}, {stack_size}};
    }
};

// Specialization for catching the thread before it runs.
// Yields no imported arguments.
template <>
struct HermeticExportAgent<HermeticComputeProcess::Suspended> :
    public HermeticExportAgentBase<HermeticComputeProcess::Suspended> {
    using Base = HermeticExportAgentBase<HermeticComputeProcess::Suspended>;
    using typename Base::type;
    explicit HermeticExportAgent(
        HermeticComputeProcess::Launcher& launcher) : Base(launcher) {}

    auto operator()(const type& suspended) {
        // TODO(mcgrathr): Make it statically impossible to have two.
        if (launcher().suspended_) {
            Abort(ZX_ERR_BAD_STATE);
        } else {
            launcher().suspended_ = suspended;
        }
        return std::make_tuple();
    }
};

// Specialization for setting the entry point.
// Yields no imported arguments.
template <>
struct HermeticExportAgent<HermeticComputeProcess::EntryPoint> :
    public HermeticExportAgentBase<HermeticComputeProcess::EntryPoint> {
    using Base = HermeticExportAgentBase<HermeticComputeProcess::EntryPoint>;
    using typename Base::type;
    explicit HermeticExportAgent(
        HermeticComputeProcess::Launcher& launcher) : Base(launcher) {}

    auto operator()(const type& entry) {
        // TODO(mcgrathr): Make it statically impossible to have two.
        if (launcher().entry_pc_ != 0) {
            Abort(ZX_ERR_BAD_STATE);
        } else {
            launcher().entry_pc_ = entry.pc;
        }
        return std::make_tuple();
    }
};

// Specialization for setting the stack size.
// Yields no imported arguments.
template <>
struct HermeticExportAgent<HermeticComputeProcess::StackSize> :
    public HermeticExportAgentBase<HermeticComputeProcess::StackSize> {
    using Base = HermeticExportAgentBase<HermeticComputeProcess::StackSize>;
    using typename Base::type;
    explicit HermeticExportAgent(
        HermeticComputeProcess::Launcher& launcher) : Base(launcher) {}

    auto operator()(const type& stack) {
        // TODO(mcgrathr): Make it statically impossible to have two.
        if (launcher().stack_size_ != 0) {
            Abort(ZX_ERR_BAD_STATE);
        } else {
            launcher().stack_size_ = stack.size;
        }
        return std::make_tuple();
    }
};
// Specialization for passing a handle into the process.
// Yields zx_handle_t (remote handle value as seen in the process).
//
// This can only be used once in the whole call, since only a single handle
// can be transferred at process startup.  A second use will fail and set
// status() to ZX_ERR_BAD_STATE.  Note it's valid to use this with an
// invalid handle; it will yield ZX_HANDLE_INVALID and prevent other uses.
//
// TODO(mcgrathr): Make it statically impossible to have two.
template <>
struct HermeticExportAgent<zx::handle> :
    public HermeticExportAgentBase<zx::handle> {
    using Base = HermeticExportAgentBase<zx::handle>;
    using typename Base::type;
    explicit HermeticExportAgent(
        HermeticComputeProcess::Launcher& launcher) : Base(launcher) {}

    auto operator()(zx::handle handle) {
        zx_handle_t remote_handle = ZX_HANDLE_INVALID;
        if (Ok()) {
            remote_handle = this->launcher().SendHandle(std::move(handle));
        }
        return std::make_tuple(remote_handle);
    }
};
