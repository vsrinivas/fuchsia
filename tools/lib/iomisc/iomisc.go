// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

package iomisc

import (
	"io"
)

// MultiCloser returns an io.Closer that is the logical concatenation of the
// provided input closers. They are closed sequentially in the provided order.
// It is guaranteed that all underlying closers will be closed.
func MultiCloser(closers ...io.Closer) io.Closer {
	return &multiCloser{closers: []io.Closer(closers)}
}

type multiCloser struct {
	closers []io.Closer
}

func (c *multiCloser) Close() error {
	var err error
	for _, closer := range c.closers {
		if err == nil {
			err = closer.Close()
		} else {
			closer.Close()
		}
	}
	return err
}
