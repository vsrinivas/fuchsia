// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

.macro syscall_entry_begin name
.globl _\name
.type _\name,STT_FUNC
_\name:
.endm

.macro syscall_entry_end name
.size _\name, . - _\name

.weak \name
.type \name,STT_FUNC
\name = _\name
.size \name, . - _\name

.globl VDSO_\name
.hidden VDSO_\name
.type VDSO_\name,STT_FUNC
VDSO_\name = _\name
.size VDSO_\name, . - _\name

.endm
