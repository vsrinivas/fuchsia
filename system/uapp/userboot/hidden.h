// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This file has assembly .hidden markers for any symbols that the
// compiler might generate calls to.  It ensures there won't be any
// PLT entries generated for them.  See the comment in rules.mk for
// the full story.

__asm__(".hidden memcmp");
__asm__(".hidden memcpy");
__asm__(".hidden memmove");
__asm__(".hidden memset");
