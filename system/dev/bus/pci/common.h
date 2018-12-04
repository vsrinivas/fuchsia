// Copyright 2018 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT
#pragma once

#include <ddk/debug.h>

#define pci_tracef(...) zxlogf(TRACE, "pci: " __VA_ARGS__)
#define pci_errorf(...) zxlogf(ERROR, "pci: " __VA_ARGS__)
#define pci_infof(...) zxlogf(INFO, "pci: " __VA_ARGS__)

