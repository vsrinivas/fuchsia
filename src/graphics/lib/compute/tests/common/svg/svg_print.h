// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_GRAPHICS_LIB_COMPUTE_TESTS_COMMON_SVG_SVG_PRINT_H_
#define SRC_GRAPHICS_LIB_COMPUTE_TESTS_COMMON_SVG_SVG_PRINT_H_

// Functions to print an svg/svg.h document instance into a human-readable
// textual representation. Useful during testing and debugging.

#include <stdarg.h>

#include "svg/svg.h"

#ifdef __cplusplus

#include <iostream>

extern "C" {
#endif

// A function type used to send formatted text to any output stream.
typedef void(svg_printf_func_t)(void * opaque, const char * format, va_list args);

// Print a textual representation of the svg document identified by |svg|
// to an output stream, using |printf_func| and |print_opaque|.
// Usage example:
//
//    // Dump svg textual representation to stdout.
//    #include <stdio.h>
//    ...
//    svg_print(svg, (svg_printf_func_t *)&vfprint, stdout);
//
extern void
svg_print(const struct svg * svg, svg_printf_func_t * print_func, void * print_opaque);

// Convenience function to print |svg| to stdout. In particular, this can be called
// directly from a debugger.
extern void
svg_print_stdout(const struct svg * svg);

#ifdef __cplusplus

}  // extern "C"

// Dump textual representation of the svg document identified by |svg| to a
// C++ output stream.
extern std::ostream &
operator<<(std::ostream & os, const svg * svg);

#endif

#endif  // SRC_GRAPHICS_LIB_COMPUTE_TESTS_COMMON_SVG_SVG_PRINT_H_
