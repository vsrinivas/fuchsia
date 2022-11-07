// Copyright 2022 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef ELF_INTERP_INC
#error "ELF_INTERP_INC must be defined to a \"\" quoted file name string"
#endif

// The linker interprets this magic section name and generates a PT_INTERP
// segment pointing at it.  It contains the NUL-terminated interpreter
// name.  In kernel-land, we use this for physboot-compatible ELF objects
// to identify the particular physboot binary they were built to go with by
// its ELF build ID as a text string of lowercase hexadecimal.

.section .interp, "aR", %progbits

// This is a generated file that does `.incbin "some-file-name"`.
// The file it includes is expected to contain text with no trailing newline.
// For example, a stamp file from buildidtool that has just a build ID in hex.
#include ELF_INTERP_INC

// Provide the traditional NUL terminator that's expected to be included in
// the p_filesz calculation for the PT_INTERP segment.
.byte 0
