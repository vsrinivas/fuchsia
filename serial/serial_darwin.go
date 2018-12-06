// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package serial

import (
	"errors"
	"io"
)

func open(name string, baudRate int, timeoutSecs int) (io.ReadWriteCloser, error) {
	return nil, errors.New("not supported")
}
