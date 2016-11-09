// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <lib/vdso.h>

#include "vdso-code.h"

// This is defined in assembly by vdso-image.S; vdso-code.h
// gives details about the image's size and layout.
extern "C" const char vdso_image[];

VDso::VDso() : RoDso("vdso", vdso_image, VDSO_CODE_END, VDSO_CODE_START) {}
