// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package cli

import (
	"flag"
	"net"
	"os"
)

type SerialConfig struct {
	serialPath string
}

func NewSerialConfig(fs *flag.FlagSet) *SerialConfig {
	c := &SerialConfig{}

	fs.StringVar(&c.serialPath, "device-serial", os.Getenv("FUCHSIA_SERIAL_SOCKET"), "device serial path")

	return c
}

func (f *SerialConfig) Serial() (net.Conn, error) {
	if f.serialPath == "" {
		return nil, nil
	}

	return net.Dial("unix", f.serialPath)
}
