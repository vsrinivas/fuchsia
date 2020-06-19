// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package netboot

import (
	"net"
	"strconv"
	"testing"
)

func TestUDPConnWithReusablePort(t *testing.T) {
	conn, err := UDPConnWithReusablePort(0, "", true)
	if err != nil {
		t.Fatal(err)
	}
	defer conn.Close()
	_, portStr, err := net.SplitHostPort(conn.LocalAddr().String())
	if err != nil {
		t.Fatal(err)
	}
	port, err := strconv.Atoi(portStr)
	if err != nil {
		t.Fatal(err)
	}

	conn2, err := UDPConnWithReusablePort(port, "", true)
	if err != nil {
		t.Fatal(err)
	}
	defer conn2.Close()

	conn3, err := UDPConnWithReusablePort(port, "", false)
	if err == nil {
		conn3.Close()
		t.Error("should have failed to create new connection with same port")
	}
}
