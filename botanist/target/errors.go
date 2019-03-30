// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package target

import (
	"errors"
)

var (
	// ErrUnimplemented is an error for unimplemented methods.
	ErrUnimplemented error = errors.New("method unimplemented")
)
