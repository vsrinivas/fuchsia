/* Copyright 2021 The Fuchsia Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/*
 * This is included as an input linker script when the library is used.  The
 * Self::LoadBias() function uses it: the elfldltl.kBase symbol points to the
 * runtime location of the link-time base (which is usually zero).  It doesn't
 * use __ehdr_start directly since that won't be defined (with GNU linkers)
 * when the ELF headers are not in fact in the image.  In those cases, the
 * custom linker script must define elfldltl.kBase explicitly to preempt this
 * definition.
 */
PROVIDE_HIDDEN(elfldltl.kBase = __ehdr_start);
