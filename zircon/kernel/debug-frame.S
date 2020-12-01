// Copyright 2020 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

// This is poked into each assembly file via an `-include` command-line switch
// to ensure that all assembly code going into kernel links is consistent with
// compiler-generated code: it has only .debug_frame (offline) unwind info and
// no .eh_frame (runtime) unwind info.
.cfi_sections .debug_frame
