// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_CONTAINERS_CPP_OWNERSHIP_H_
#define LIB_CONTAINERS_CPP_OWNERSHIP_H_

// Lifetime analysis
#ifndef __OWNER
#ifdef __clang__
#define __OWNER(x) [[gsl::Owner(x)]]
#define __POINTER(x) [[gsl::Pointer(x)]]
#else
#define __OWNER(x)
#define __POINTER(x)
#endif
#endif

#endif // LIB_CONTAINERS_OWNERSHIP_H_
