// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//go:build !aix && !darwin && !dragonfly && !freebsd && !linux && !netbsd && !openbsd && !solaris

package serve

import "net"

func getListener(addr string) (net.Listener, error) {
	return net.Listen("tcp", addr)
}
