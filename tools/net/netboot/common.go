// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package netboot

import (
	"net"
	"os"
	"syscall"

	"golang.org/x/sys/unix"
)

// UDPConnWithReusablePort creates a new connection that binds to the specified port while allowing it to be reused.
func UDPConnWithReusablePort(port int, zone string, reusable bool) (*net.UDPConn, error) {
	syscall.ForkLock.RLock()
	fd, err := syscall.Socket(syscall.AF_INET6, syscall.SOCK_DGRAM, syscall.IPPROTO_UDP)
	if err == nil {
		unix.CloseOnExec(fd)
	}
	syscall.ForkLock.RUnlock()
	if err != nil {
		return nil, err
	}

	if reusable {
		// SO_REUSEADDR and SO_REUSEPORT allows binding to the same port multiple
		// times which is necessary in the case when there are multiple instances.
		if err := syscall.SetsockoptInt(fd, syscall.SOL_SOCKET, unix.SO_REUSEADDR, 1); err != nil {
			syscall.Close(fd)
			return nil, err
		}

		if err := syscall.SetsockoptInt(fd, syscall.SOL_SOCKET, unix.SO_REUSEPORT, 1); err != nil {
			syscall.Close(fd)
			return nil, err
		}
	}

	// Bind the socket to the provided port and zone.
	sockaddr := &syscall.SockaddrInet6{Port: port}
	if zone != "" {
		ift, err := net.InterfaceByName(zone)
		if err != nil {
			return nil, err
		}
		sockaddr.ZoneId = uint32(ift.Index)
	}
	if err := syscall.Bind(fd, sockaddr); err != nil {
		syscall.Close(fd)
		return nil, err
	}

	f := os.NewFile(uintptr(fd), "")
	conn, err := net.FilePacketConn(f)
	f.Close()
	if err != nil {
		return nil, err
	}
	return conn.(*net.UDPConn), err
}
