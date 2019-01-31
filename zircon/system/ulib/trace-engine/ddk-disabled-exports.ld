/*
 * Copyright 2018 The Fuchsia Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/*
 * This is an input linker script used in linking libdriver.so.
 * The linker is pulling in our symbols from an archive, thus they won't get
 * pulled in unless they are referenced. 
 * This file injects synthetic references to keep the symbols alive.
 *
 * N.B. This file is used when tracing is disabled in the DDK because these
 * symbols can be used by tracing clients even when tracing is disabled with
 * #define NTRACE.
 */

EXTERN(trace_generate_nonce)
