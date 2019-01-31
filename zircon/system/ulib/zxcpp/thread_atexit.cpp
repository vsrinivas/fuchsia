// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This is defined by the C library, but not declared anywhere.
extern "C" int __cxa_thread_atexit_impl(void(*dtor)(void*), void* obj,
                                        void* dso_symbol);

extern "C" int __cxa_thread_atexit(void(*dtor)(void*), void* obj,
                                   void* dso_symbol) throw()
{
    return __cxa_thread_atexit_impl(dtor, obj, dso_symbol);
}
