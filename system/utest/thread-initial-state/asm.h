// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef __ASM_H
#define __ASM_H

#define FUNCTION(x)    \
    .global x;         \
    .type x, STT_FUNC; \
    x:
#define DATA(x)          \
    .global x;           \
    .type x, STT_OBJECT; \
    x:

#define LOCAL_FUNCTION(x) \
    .type x, STT_FUNC;    \
    x:
#define LOCAL_DATA(x)    \
    .type x, STT_OBJECT; \
    x:

#define END(x) .size x, .- x

#endif
