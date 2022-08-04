// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//go:build aix || darwin || dragonfly || freebsd || linux || netbsd || openbsd || solaris

package serve

import (
	"context"
	"net"
	"syscall"

	"golang.org/x/sys/unix"
)

func getListener(addr string) (net.Listener, error) {
	config := &net.ListenConfig{Control: func(net, addr string, conn syscall.RawConn) error {
		return conn.Control(func(descriptor uintptr) {
			unix.SetsockoptInt(int(descriptor), unix.SOL_SOCKET, unix.SO_REUSEADDR, 1)
		})
	}}
	return config.Listen(context.Background(), "tcp", addr)
}
