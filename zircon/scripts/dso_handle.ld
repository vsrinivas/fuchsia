/* Copyright 2017 The Fuchsia Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/*
 * The C++ compiler generates a reference to this symbol for each static
 * destructor registered via __cxa_atexit.  In our implementation, there is
 * no need for it, so just define the symbol away rather than wasting a
 * data word defining it as a real variable somewhere.  See
 * third_party/ulib/musl/src/exit/atexit.c for a more detailed explanation.
 *
 * LLD doesn't like it when this uses 0, because it makes that SHN_ABS and
 * then is broken about handling STV_HIDDEN SHN_ABS symbols; it defines
 * __ehdr_start as not SHN_ABS (though SHN_ABS is the only thing that
 * actually makes sense for it), so that avoids the problem.
 */
PROVIDE_HIDDEN(__dso_handle = __ehdr_start);
