// Copyright 2021 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <phys/symbolize.h>

const char Symbolize::kProgramName_[] = "multiboot-shim";

// TODO(mcgrathr): depthcharge adds a zbi item with uart info, but in an
// obsolete format.  also maybe still has zbi container size vs multiboot
// module size bug (see workaround in multiboot-init.cc)? could build
// special-case version for depthcharge or maybe can do quirk detection if it
// passes the bootloader string to id?
