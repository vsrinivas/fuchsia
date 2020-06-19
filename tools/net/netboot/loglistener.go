// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This file implements the Zircon loglistener protocol.
package netboot

import (
	"bytes"
	"encoding/binary"
	"errors"
	"net"
)

// Magic constants used by the netboot protocol.
const (
	debugMagic = 0xAEAE1123 // see system/public/zircon/boot/netboot.h
)

// Port numbers used by the netboot protocol.
const (
	debugPort = 33337 // debugging log port
)

// logpacket is the network logging protocol packet.
type logpacket struct {
	Magic    uint32
	Seqno    uint32
	Nodename [64]byte
	Data     [1216]byte
}

// LogListener is Zircon's debug log listener.
type LogListener struct {
	seq      uint32
	conn     *net.UDPConn
	nodename string
}

// NewLogListener creates and connects a new instance of debug log listener.
func NewLogListener(nodename string) (*LogListener, error) {
	conn, err := UDPConnWithReusablePort(debugPort, "", true)
	if err != nil {
		return nil, err
	}

	return &LogListener{
		conn:     conn,
		nodename: nodename,
	}, nil
}

// Listen receive a single debug log packet.
func (l *LogListener) Listen() (string, error) {
	b := make([]byte, 4096)

	// Wait for logpacket.
	_, addr, err := l.conn.ReadFrom(b)
	if err != nil {
		return "", err
	}

	var pkt logpacket
	if err := binary.Read(bytes.NewReader(b), binary.LittleEndian, &pkt); err != nil {
		return "", err
	}
	if pkt.Magic != debugMagic {
		// Not a valid debug packet.
		return "", errors.New("invalid magic")
	}
	name, err := netbootString(pkt.Nodename[:])
	if err != nil {
		return "", err
	}
	if name != l.nodename {
		// Not a packet from our node.
		return "", errors.New("invalid nodename")
	}

	var data string
	if pkt.Seqno != l.seq {
		data, err = netbootString(pkt.Data[:])
		if err != nil {
			return data, err
		}
		l.seq = pkt.Seqno
	}

	// Acknowledge the packet.
	if _, err = l.conn.WriteTo(b[:8], addr); err != nil {
		return "", err
	}

	return data, nil
}

// Close shuts down the log listener underlying connection.
func (l *LogListener) Close() error {
	return l.conn.Close()
}
