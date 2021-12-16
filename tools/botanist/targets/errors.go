// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package targets

import (
	"errors"
)

// ErrUnimplemented is an error for unimplemented methods.
var ErrUnimplemented error = errors.New("method unimplemented")
