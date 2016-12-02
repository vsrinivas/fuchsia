/* Copyright 2016 The Fuchsia Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/*
 * The C++ compiler generates a reference to this symbol for each static
 * destructor registered via __cxa_atexit.  In our implementation, there is
 * no need for it, so just define the symbol away rather than wasting a
 * data word defining it as a real variable somewhere.  See
 * third_party/ulib/musl/src/exit/atexit.c for a more detailed explanation.
 */
PROVIDE_HIDDEN(__dso_handle = 0);
