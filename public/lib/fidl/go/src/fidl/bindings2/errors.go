// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package bindings2

import (
	"errors"
)

// TODO(mknyszek): Flesh these out and make them better.
var (
	ErrInvalidUnionTag = errors.New("invalid union tag")
	ErrUnknownOrdinal  = errors.New("unknown ordinal")
)
