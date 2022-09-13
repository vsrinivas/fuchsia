// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package device

import (
	"bufio"
	"fmt"
	"net"
	"sync"
)

type SerialConn struct {
	serial    net.Conn
	mu        sync.Mutex
	bufReader *bufio.Reader
}

// Create the new serial connection.
func NewSerialConn(serialSocketPath string) (*SerialConn, error) {
	if serialSocketPath == "" {
		return nil, fmt.Errorf("serialSocketPath not set")
	}

	serial, err := net.Dial("unix", serialSocketPath)

	if err != nil {
		return nil, fmt.Errorf("failed to open serial socket connection: %w", err)
	}

	bufReader := bufio.NewReader(serial)

	s := &SerialConn{
		serial:    serial,
		bufReader: bufReader,
	}

	return s, nil
}

// Write a line into the serial connection.
func (s *SerialConn) WriteLine(cmd []byte) error {
	s.mu.Lock()
	defer s.mu.Unlock()
	_, err := s.serial.Write(cmd)
	return err
}

// Read a line from the serial connection.
func (s *SerialConn) ReadLine() (string, error) {
	s.mu.Lock()
	defer s.mu.Unlock()
	bytes, err := s.bufReader.ReadBytes('\n')
	return string(bytes), err
}
