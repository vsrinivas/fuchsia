// Copyright 2020 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef ZIRCON_KERNEL_LIB_CODE_PATCHING_INCLUDE_LIB_CODE_PATCHING_ASM_H_
#define ZIRCON_KERNEL_LIB_CODE_PATCHING_INCLUDE_LIB_CODE_PATCHING_ASM_H_

#ifdef __ASSEMBLER__  // clang-format off

/// Defines an instruction range to be patched. Effectively pushes a
/// code_patching::Directive onto the special section of ".code-patches".
///
/// Parameters
///
///   * begin
///     - Required: Label giving the beginning of the range.
///
///   * end
///     - Required: Label giving the end of the range.
///
///   * ident
///     - Required: Integer giving the associated patch case ID (i.e., a
///       code_patching::CaseId), which corresponds to hard-coded details on
///       how and when to patch.
///
.macro .code_patching.range begin, end, ident
  // The `M` means to set the SHF_MERGE flag, which tells the linker it can
  // merge identical entries. While there will not be any identical entries in
  // this section in practice, the use of `M` allows us to specify the entry
  // size (16), which makes for nicer `readelf` output. `?` tells the assembler
  // to use the current section's section group (if any). Inside `.function`,
  // that ensures that the .code-patches data is attached to the function and
  // gets GC'd if and only if the function itself gets GC'd.
  .pushsection .code-patches, "M?", %progbits, 16
  .quad \begin
  .int \end - \begin
  .int \ident
  .popsection
.endm  // .code_patching.range

/// Defines an instruction range to be patched.  See .code_patching.range.
/// This starts a range that ends with the next `.code_patching.end` directive.
///
/// Parameters
///
///   * ident
///     - Required: See .code_patching.range.
///
.macro .code_patching.start ident
  .L.code_patching.start.\@\():
  .purgem .code_patching.end
  .macro .code_patching.end
    _.code_patching.end .L.code_patching.start.\@, .L.code_patching.end.\@, \ident
  .endm
.endm

.macro _.code_patching.end.reset purge
  .ifnb \purge
    .purgem .code_patching.end
  .endif
  .macro .code_patching.end
    .error "unmatched .code_patching.end directive"
  .endm
.endm
_.code_patching.end.reset

.macro _.code_patching.end start, end, ident
  _.code_patching.end.reset purge
  \end\():
  .code_patching.range \start, \end, \ident
.endm

#endif  // clang-format on

#endif  // ZIRCON_KERNEL_LIB_CODE_PATCHING_INCLUDE_LIB_CODE_PATCHING_ASM_H_
