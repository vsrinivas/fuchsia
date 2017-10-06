// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package gonet_test

import (
	"fmt"
	"net"
	"runtime"
	"testing"
)

const loopbackAddr = "127.0.0.1:1234"

func loopbackStreamAcceptWrite(msg string, ready chan struct{}, done chan struct{}, cancel chan error) {
	ln, err := net.Listen("tcp", loopbackAddr)
	if err != nil {
		cancel <- fmt.Errorf("net.Listen error: %v", err)
		return
	}
	defer ln.Close()

	ready <- struct{}{}

	conn, err := ln.Accept()
	if err != nil {
		cancel <- fmt.Errorf("net.Accept error: %v", err)
		return
	}
	defer conn.Close()

	_, err = conn.Write([]byte(msg))
	if err != nil {
		cancel <- fmt.Errorf("conn.Write error: %v", err)
		return
	}

	done <- struct{}{}
}

func TestLoopbackStreamConnectRead(t *testing.T) {
	ready := make(chan struct{})
	done := make(chan struct{})
	cancel := make(chan error)

	msg := "hello"
	go loopbackStreamAcceptWrite(msg, ready, done, cancel)

	select {
	case <-ready:
		// Server is ready.
	case err := <-cancel:
		t.Fatal(err)
	}

	conn, err := net.Dial("tcp", loopbackAddr)
	if err != nil {
		t.Fatal("net.Dial error:", err)
	}
	defer conn.Close()

	select {
	case <-done:
		reply := make([]byte, 1024)
		n, err := conn.Read(reply)
		if err != nil {
			t.Fatal("conn.Read error:", err)
		}
		rmsg := string(reply[:n])
		if rmsg != msg {
			t.Fatalf("strings differ: %v != %v", rmsg, msg)
		}
	case err := <-cancel:
		t.Fatal(err)
	}
}

func loopbackDatagramRead(buffer *[]byte, ready chan struct{}, done chan struct{}, cancel chan error) {
	ready <- struct{}{}

	pconn, err := net.ListenPacket("udp", loopbackAddr)
	if err != nil {
		cancel <- fmt.Errorf("net.ListenPacket error: %v", err)
		return
	}
	defer pconn.Close()

	n, _, err := pconn.ReadFrom(*buffer)
	if err != nil {
		cancel <- fmt.Errorf("pconn.ReadFrom error: %v", err)
		return
	}
	*buffer = (*buffer)[:n]

	done <- struct{}{}
}

func TestLoopbackDatagram(t *testing.T) {
	// TODO: Stop skipping once this test works.
	if runtime.GOOS == "fuchsia" {
		t.SkipNow()
	}

	ready := make(chan struct{})
	done := make(chan struct{})
	cancel := make(chan error)

	buffer := make([]byte, 1024)
	go loopbackDatagramRead(&buffer, ready, done, cancel)

	select {
	case <-ready:
		// Server is ready.
	case err := <-cancel:
		t.Fatal(err)
	}

	conn, err := net.Dial("udp", loopbackAddr)
	if err != nil {
		t.Fatal("net.Dial error:", err)
	}
	defer conn.Close()

	msg := "hello"
	_, err = conn.Write([]byte(msg))
	if err != nil {
		t.Fatal("conn.Write error:", err)
	}

	select {
	case <-done:
		rmsg := string(buffer)
		if rmsg != msg {
			t.Fatalf("strings differ: %v != %v", rmsg, msg)
		}
	case err := <-cancel:
		t.Fatal(err)
	}
}
