// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <lib/stdcompat/span.h>

#include <string_view>

// This defines a common "passive" ABI that runtime code like a C library can
// use to interrogate basic dynamic linking details.  It's called a "passive"
// ABI because it exports almost no entry points, but only some immutable data
// structures and the ELF symbol names by which to find them.
//
// The traditional PT_INTERP dynamic linker sets up this data in memory while
// doing the initial-exec dynamic linking, and then makes it all read-only so
// it's guaranteed never to change again.  The runtime dynamic linking support
// (-ldl) can ingest this data into its own data structures and manage those
// to provide a richer runtime ABI.  Basic fallback implementations of simple
// support calls like dl_iterate_phdr and dlsym can be provided by the C
// library when libdl.so is not linked in.
//
// For out-of-process dynamic linking, a simple stub implementation of this
// same ABI can be loaded in lieu of the traditional dynamic linker, giving
// the same simple runtime ABI for data that is populated out of process.

namespace ld::abi {

// Forward declarations.
struct Module;
struct TlsModule;
struct TlsGetAddrGot;

// This is the DT_SONAME value representing the ABI declared in this file.
inline constexpr std::string_view kSoname = "ld.so.1";

// This is the standard PT_INTERP value for using a compatible dynamic linker
// as the startup dynamic linker.  The actual PT_INTERP value in an executable
// ET_DYN file might have a prefix to select a particular implementation.
inline constexpr std::string_view kInterp = kSoname;

// These are the sole exported symbols in the ld.so ABI.  They should be used
// in C++ via their scoped names such as ld::abi::_ld_loaded_modules normally.
// But the ld.so symbolic ABI does not include any C++ name mangling, so these
// use simple C linkage names in the name space reserved for the
// implementation.
extern "C" {

// This lists all the initial-exec modules.  Embedded `link_map::l_prev` and
// `link_map::l_next` form a doubly-linked list in load order, which is a
// breadth-first pre-order of the DT_NEEDED dependencies where the main
// executable is always first and dependents always precede dependencies
// (except for any redundancies).
extern const Module& _ld_loaded_modules;

// TLS details for initial-exec modules that have PT_TLS segments.  The entry
// at index `.tls_mod_id - 1` describes that module's PT_TLS.  Modules where
// `.tls_mod_id == 0` have no PT_TLS segments.  TLS module ID numbers above
// _ld_static_tls_modules.size() are not used at startup but may be assigned
// to dynamically-loaded modules later.
extern const cpp20::span<const TlsModule> _ld_static_tls_modules;

// Offset from the thread pointer to each module's segment in the static TLS
// block.  The entry at index `.tls_mod_id - 1` is the offset of that module's
// PT_TLS segment.
//
// This offset is actually a negative number on some machines like x86, but
// it's always calculated using address-sized unsigned arithmetic.  On
// machines where it's positive, there is a nonempty psABI-specified reserved
// region right after the thread pointer.  Hence a real offset is never zero.
// Since the initial-exec dynamic linker loads everything into static TLS at
// startup, this will never be zero in initial-exec modules.
extern const cpp20::span<const uintptr_t> _ld_static_tls_offsets;

// This matches _ld_static_tls_offsets.back() +
// _ld_static_tls_modules.back().tls_initial_data.size() +
// _ld_static_tls_modules.back().tls_bss_size.
extern const size_t _ld_static_tls_size;

// This matches the max of _ld_static_tls_modules[...].tls_alignment and
// the psABI-specified minimum alignment.
extern const size_t _ld_static_tls_alignment;

// This is the symbol that compilers generate calls to for GD/LD TLS accesses
// in the original ABI (without TLSDESC).  The implementation in ld.so only
// handles the initial-exec set (see <lib/dl/tls.h>).  It's overridden by a
// different implementation if dynamic module loading with TLS is available.
void* __tls_get_addr(TlsGetAddrGot& got);

}  // extern "C"

}  // namespace ld::abi
