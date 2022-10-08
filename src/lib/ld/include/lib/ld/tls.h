// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <lib/stdcompat/span.h>

#include <cstddef>

namespace ld::abi {

// This describes the details gleaned from the PT_TLS header for a module.
// These are stored in an array indexed by TLS module ID number - 1, as the
// module ID number zero is never used.
//
// Note that while module ID number 1 is most often the main executable, that
// need not always be so: if the main executable has no PT_TLS of its own, then
// the earliest module loaded that does have a PT_TLS gets module ID 1.
//
// What is importantly special about the main executable is that offsets in the
// static TLS block are chosen with the main executable first--it may have been
// linked with LE/GE TLS access code where the linker chose its expected
// offsets at static link time.  When the dynamic linker follows the usual
// procedure of assigning module IDs in load order and then doing static TLS
// layout in the same order, it always comes out the same.  But the only real
// constraint on the runtime layout chosen is that if the main executable has a
// PT_TLS segment, it must be first and its offset from the thread pointer must
// be the fixed value prescribed by the psABI.  The adjacent private portions
// of the runtime thread descriptor must be located such that both their own
// alignment requirements and the p_align of module 1's PT_TLS are respected.

struct TlsModule {
  // Initial data image in memory, usually a pointer into the RODATA or RELRO
  // segment of the module's load image.
  cpp20::span<const std::byte> tls_initial_data;

  // If the module has a PT_TLS, its total size in memory (for each thread) is
  // determined by the initial data (tls_initial_data.size_bytes(), from .tdata
  // et al) plus this size of zero-initialized bytes (from .tbss et al).
  size_t tls_bss_size;

  // The runtime memory for each thread's copy of the initialized PT_TLS data
  // for this segment must have at least this minimum alignment (p_align).
  // This is validated to be a power of two before the module is loaded.
  size_t tls_alignment;
};

}  // namespace ld::abi

namespace ld {

// When the compiler generates a call to __tls_get_addr, the linker generates
// two corresponding dynamic relocation entries applying to adjacent GOT slots
// that form a pair describing what module and symbol resolved the reference
// at dynamic link time.  The first slot holds the module ID, a 1-origin
// index.  The second slot holds the offset from that module's PT_TLS segment.
struct TlsGetAddrGot {
  uintptr_t tls_mod_id;  // R_*_DTPMOD* et al relocations set this.
  uintptr_t offset;      // R_*_DTPOFF* et al relocations set this.
};

}  // namespace ld
