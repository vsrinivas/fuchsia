// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package ir

import (
	"fidl/compiler/backend/types"
)

type Root struct {
}

func Compile(fidlData types.Root) Root {
	root := Root{}
	return root
}
