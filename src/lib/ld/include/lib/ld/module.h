// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <lib/elfldltl/init-fini.h>
#include <lib/elfldltl/symbol.h>
#include <lib/stdcompat/span.h>

#include <cstddef>
#include <cstdint>

#include "elf.h"

// ld::abi::Module holds all the information about an ELF module that's still
// relevant at runtime after it's been loaded and dynamically linked.  This is
// enough for basic dl_iterate_phdr and dlsym implementations and the like to
// interrogate the iniital-exec set of modules.
//
// This type is defined in the ld::abi namespace because these layouts form
// part of the quasi-public, quasi-stable "passive" data ABI between startup or
// OOP dynamic linking and other runtime dynamic linking support code that
// might live in libc.so or libdl.so.  Note that this brings a few toolkit API
// types, which are not otherwise presumed ABI stable nor even public outside a
// single module, into the ld::abi regime for quasi-public, quasi-stable ABI.
//
// The leading portion of ld::abi::Module matches the long-standing de facto
// standard ABI layout of `struct link_map` from SVR4.  The doubly-linked list
// structure of `link_map::l_prev` and `link_map::l_next` is maintained to
// navigate the initial-exec set in load order (which is also symbol resolution
// precedence order for standard initial-exec symbol resolution).  The pointers
// are actually to ld::abi::Module structures with leading `link_map` portions.

namespace ld::abi {

struct Module {
  // This is known to be the first member in the struct layout.  It forms the
  // old de facto ABI from SVR4 (traditionally `struct link_map` in <link.h>)
  // for enumerating the modules and their load/symbol-resolution order through
  // its doubly-linked list structure.  It also holds the load bias (.addr); a
  // pointer (.name) to the NUL-terminated DT_SONAME (or other) name string by
  // which the module was loaded; and a pointer (.ld) to the absolute runtime
  // address of the PT_DYNAMIC segment (aka .dynamic section) in this module's
  // load image.  The .dynamic data has already been parsed and validated so
  // it's safe to rely on the DT_NULL terminator being found by iteration.
  //
  // It's traditional for dynamic linkers to use this well-known format as the
  // leading portion of their own internal data structure.  For example, some
  // users may assume that the void* value returned by `dlopen` is in fact the
  // `struct link_map*` for the module just fetched.  This is not good practice
  // and no standard or documentation has ever made guarantees avoid the void*
  // values used in <dlfcn.h> interfaces being used in any such ways.  But it
  // makes sense that we follow suit here.  The `struct link_map` list serves
  // as the ld::abi::Module list as well.
  Elf::LinkMap link_map;

  // The rest of the ld::abi::Module layout is a distinct extension that does
  // not overlap with any historical ABI.  (Traditional uses that have longer
  // data structures prefixed with `struct link_map` are all private formats.)
  //
  // However, it does form the core of the quasi-public, quasi-stable ABI for
  // ld.so; see <lib/ld/abi.h> for full details on the ABI stability model.

  // This module's whole-page load image occupies the absolute virtual address
  // range [vaddr_start, vaddr_end).  The vaddr_start value is most often
  // redundant with link_map.addr (the load bias), but is not required to be.
  uintptr_t vaddr_start = 0;
  uintptr_t vaddr_end = 0;

  // This points to the module's program headers in some read-only memory,
  // usually in its own load image as located by PT_PHDR.  (The span may be
  // empty if the program headers are not visible in memory, and this won't
  // always prevent a module from being loaded.  But any such unusual modules
  // with that limitation may have unexpected behavior due to their metadata
  // pointers such as PT_GNU_EH_FRAME not being found at runtime.)
  cpp20::span<const Elf::Phdr> phdrs;

  // This collects information about the dynamic symbol table and can be
  // used to look up symbols.  See <lib/elfldltl/symbol.h> for details.
  elfldltl::SymbolInfo<Elf> symbols;

  // Cached and hashed for quick comparison; possibly empty.
  elfldltl::Soname soname;

  // This lists the initializer functions this module expects to have run after
  // it's loaded.  This information isn't really needed after startup (or later
  // dynamic module loading).  But some means is needed to communicate this
  // information from the dynamic linker that gleans (and relocates it) to the
  // runtime code that calls initializers after C library setup.  The read-only
  // module data here is the means already at hand, and this data is only a few
  // words.  So it's just kept here permanently with everything else.
  elfldltl::InitFiniInfo<Elf> init;

  // This lists the finalizer functions this module expects to have run at
  // program exit or when it's dynamically unloaded (if that's possible).
  elfldltl::InitFiniInfo<Elf> fini;

  // Each module that has a PT_TLS segment of its own is assigned a module ID,
  // which is a nonzero index.  This value is zero if the module has no PT_TLS.
  // Note that a module's code might use TLS relocations (resolved to external
  // symbols) even if that module has no PT_TLS segment of its own.
  Elf::size_type tls_modid = 0;

  // Each and every module gets a "module ID" number that's used in symbolizer
  // markup contextual elements describing the module.  These are expected to
  // be arbitrary integers, probably small, that are unique within the process
  // at a given moment.  The initial-exec dynamic linker assigns monotonically
  // increasing numbers to each module in the order they're loaded and linked
  // via the `link_map` member above.  For additional runtime-loaded modules,
  // it's reasonable to take the tail initial-exec module's ID and increase
  // from there for every new module loaded, without reusing old IDs when
  // modules are unloaded.
  Elf::size_type symbolizer_modid = 0;

  // If nonempty, this is the (first) NT_GNU_BUILD_ID note payload (not
  // including Elf::Nhdr or name parts): just the build ID bytes themselves.
  // This is simply the result of parsing PT_NOTE segments in the phdrs that
  // presumably point to read-only data in the module's load image, which can
  // always be repeated; this just caches the parsing result from load time.
  cpp20::span<const std::byte> build_id;
};

static_assert(offsetof(Module, link_map) == 0);

}  // namespace ld::abi
