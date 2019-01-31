// Copyright 2018 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#pragma once

// The kernel doesn't want this file but some libc++ headers we need
// wind up including it and they need this type.

typedef struct {} mbstate_t;
