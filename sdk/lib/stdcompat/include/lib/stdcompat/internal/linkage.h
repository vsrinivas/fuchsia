// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_STDCOMPAT_INCLUDE_LIB_STDCOMPAT_INTERNAL_LINKAGE_H_
#define LIB_STDCOMPAT_INCLUDE_LIB_STDCOMPAT_INTERNAL_LINKAGE_H_

// The libc++ headers obey this #define: When it's 1 then it's important
// that all the code in the translation unit stay in the translation unit.
// This matters for inline functions defined in the headers, which are not
// guaranteed to be inlined.  If they are not inlined and instead defined
// as functions in a COMDAT group, then it's possible link-time selection
// will use a different translation unit's definition instead.  Translation
// units that need all their code to use special compilation flags, such as
// disabling sanitizers for early initialization code, can also define this
// macro to ensure that they get their own specially-compiled functions.

#if defined(_LIBCPP_HIDE_FROM_ABI_PER_TU) && _LIBCPP_HIDE_FROM_ABI_PER_TU

#if defined(__has_attribute) && __has_attribute(internal_linkage)
#define STDCOMPAT_INLINE_LINKAGE __attribute__((__internal_linkage__))
#else
#define STDCOMPAT_INLINE_LINKAGE __attribute__((__always_inline__, __visibility__("hidden")))
#endif

#else

#define STDCOMPAT_INLINE_LINKAGE

#endif  // _LIBCPP_HIDE_FROM_ABI_PER_TU

#endif  // LIB_STDCOMPAT_INCLUDE_LIB_STDCOMPAT_INTERNAL_LINKAGE_H_
