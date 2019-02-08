// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package gonet_test

import (
	"fmt"
	"net"
	"runtime"
	"sync"
	"syscall"
	"syscall/zx"
	"syscall/zx/fdio"
	"syscall/zx/fidl"
	"syscall/zx/mxerror"
	zxnet "syscall/zx/net"
	"testing"
)

const loopbackAddr = "127.0.0.1:1234"
const msg = "hello"

func TestMultipleCloseCalls(t *testing.T) {
	c0, c1, err := zx.NewChannel(0)
	if err != nil {
		t.Fatal(err)
	}
	if err := fdio.ServiceConnect("/svc/"+zxnet.SocketProviderName, zx.Handle(c0)); err != nil {
		t.Fatal(err)
	}
	sp := zxnet.SocketProviderInterface(fidl.ChannelProxy{Channel: c1})
	code, s, err := sp.Socket(syscall.AF_INET, syscall.SOCK_STREAM, syscall.IPPROTO_IP)
	if err != nil {
		t.Fatal(err)
	}
	if code != 0 {
		t.Fatal(syscall.Errno(code))
	}
	si := zxnet.SocketControlInterface{Socket: s}

	var wg sync.WaitGroup
	start := make(chan struct{})
	for i := 0; i < 5; i++ {
		wg.Add(1)
		go func() {
			<-start
			for {
				code, err := si.Close()
				switch mxerror.Status(err) {
				case zx.ErrOk, zx.ErrPeerClosed:
				case zx.ErrShouldWait:
					continue
				default:
					t.Errorf("%T: %+v", err, err)
				}
				if code != 0 {
					t.Error(syscall.Errno(code))
				}
				break
			}
			wg.Done()
		}()
	}
	close(start)
	wg.Wait()
}

func TestLoopbackStreamConnectRead(t *testing.T) {
	ln, err := net.Listen("tcp", loopbackAddr)
	if err != nil {
		t.Fatalf("net.Listen error: %v", err)
	}
	defer ln.Close()

	done := make(chan error, 1)
	go func() {
		done <- func() error {
			conn, err := net.Dial("tcp", loopbackAddr)
			if err != nil {
				return fmt.Errorf("net.Dial error: %v", err)
			}
			defer conn.Close()
			if _, err := conn.Write([]byte(msg)); err != nil {
				return fmt.Errorf("conn.Write error: %v", err)
			}
			return nil
		}()
	}()

	conn, err := ln.Accept()
	if err != nil {
		t.Fatalf("net.Accept error: %v", err)
	}
	defer conn.Close()

	if err := <-done; err != nil {
		t.Fatal(err)
	}

	b := make([]byte, 1024)
	n, err := conn.Read(b)
	if err != nil {
		t.Fatalf("conn.Read error: %v", err)
	}
	b = b[:n]
	if string(b) != msg {
		t.Fatalf("want = %v, got = %v", msg, string(b))
	}
}

func TestLoopbackDatagram(t *testing.T) {
	if runtime.GOOS == "fuchsia" {
		t.Skip("unimplemented; see https://fuchsia.googlesource.com/third_party/go/+/1064b67/src/net/udpsock_fuchsia.go")
	}

	conn, err := net.ListenPacket("udp", loopbackAddr)
	if err != nil {
		t.Fatalf("net.ListenPacket error: %v", err)
	}
	defer conn.Close()

	done := make(chan error, 1)
	go func() {
		done <- func() error {
			conn, err := net.Dial("udp", loopbackAddr)
			if err != nil {
				return fmt.Errorf("net.Dial error: %v", err)
			}
			defer conn.Close()

			if _, err = conn.Write([]byte(msg)); err != nil {
				return fmt.Errorf("conn.Write error: %v", err)
			}
			return nil
		}()
	}()

	if err := <-done; err != nil {
		t.Fatal(err)
	}

	b := make([]byte, 1024)
	n, _, err := conn.ReadFrom(b)
	if err != nil {
		t.Fatalf("conn.ReadFrom error: %v", err)
	}
	b = b[:n]
	if string(b) != msg {
		t.Fatalf("want = %v, got = %v", msg, string(b))
	}
}
