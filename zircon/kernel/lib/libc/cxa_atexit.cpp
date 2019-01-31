// Copyright 2016 The Fuchsia Authors
// Copyright (c) 2006 Travis Geiselbrecht
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

extern "C" int __cxa_atexit(void (*destructor)(void *), void *arg, void *__dso_handle)
{
    return 0;
}
