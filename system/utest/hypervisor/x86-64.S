// Copyright 2017 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#define FUNCTION(x) .global x; .type x,STT_FUNC; x:

.text

FUNCTION(guest_start)
    inc %rax
    jmp guest_start
FUNCTION(guest_end)
