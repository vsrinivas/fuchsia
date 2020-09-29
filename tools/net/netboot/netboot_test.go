// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package netboot

import (
	"bytes"
	"context"
	"encoding/binary"
	"log"
	"net"
	"strconv"
	"testing"
	"time"
)

const testTimeout = time.Millisecond * 500

func newClientWithPorts(sPort, aPort int) *Client {
	return &Client{
		Timeout:    testTimeout,
		ServerPort: sPort,
		AdvertPort: aPort,
		Cookie:     baseCookie,
	}
}

// startFakeNetbootServers Listens using len(nodenames) number of servers that
// respond with each respective nodename. Returns the server port on which the
// fake servers are listening.
func startFakeNetbootServers(t *testing.T, nodenames []string) (int, func()) {
	t.Helper()
	conn, err := net.ListenUDP("udp6", &net.UDPAddr{IP: net.IPv6zero})
	if err != nil {
		t.Fatal(err)
	}
	_, serverPortStr, err := net.SplitHostPort(conn.LocalAddr().String())
	if err != nil {
		t.Fatal(err)
	}
	serverPort, err := strconv.Atoi(serverPortStr)
	if err != nil {
		t.Fatal(err)
	}
	go func() {
		t.Helper()
		b := make([]byte, 4096)
		r := bytes.NewReader(b)
		for {
			_, addr, err := conn.ReadFromUDP(b)
			if err != nil {
				// This isn't necessarily a fatal error.
				// As this can happen when the connection
				// is closed.
				log.Printf("conn read: %v\n", err)
				break
			}
			var req netbootMessage
			if err := binary.Read(r, binary.LittleEndian, &req); err != nil {
				log.Printf("malformed binary read: %v", err)
				continue
			}
			for _, n := range nodenames {
				res := netbootMessage{
					Header: netbootHeader{
						Magic:  req.Header.Magic,
						Cookie: req.Header.Cookie,
						Cmd:    cmdAck,
						Arg:    0,
					},
				}
				copy(res.Data[:], n)
				var resBuf bytes.Buffer
				if err := binary.Write(&resBuf, binary.LittleEndian, res); err != nil {
					t.Fatalf("binary write: %v", err)
				}
				conn.WriteToUDP(resBuf.Bytes(), addr)
			}
		}
	}()
	return serverPort, func() {
		t.Helper()
		// Odds are this isn't fatal, so just log for debugging.
		if err := conn.Close(); err != nil {
			log.Printf("closing fake server: %v", err)
		}
	}
}

func TestParseBeacon(t *testing.T) {
	c := NewClient(time.Second)
	tests := []struct {
		name             string
		data             string
		expectedNodename string
		expectedVersion  string
		expectErr        bool
	}{
		{
			name:             "valid beacon",
			data:             "nodename=name;version=1.2.3",
			expectedNodename: "name",
			expectedVersion:  "1.2.3",
			expectErr:        false,
		},
		{
			name:             "missing version",
			data:             "nodename=name",
			expectedNodename: "name",
			expectedVersion:  "",
			expectErr:        true,
		},
		{
			name:             "invalid beacon",
			data:             "invalid message",
			expectedNodename: "",
			expectedVersion:  "",
			expectErr:        true,
		},
	}
	for _, test := range tests {
		t.Run(test.name, func(t *testing.T) {
			nbmsg := netbootMessage{}
			copy(nbmsg.Data[:], test.data)
			var buf bytes.Buffer
			if err := binary.Write(&buf, binary.LittleEndian, nbmsg); err != nil {
				t.Fatalf("binary write: %v", err)
			}
			msg, err := c.parseBeacon(buf.Bytes())
			if test.expectErr != (err != nil) {
				t.Errorf("expected err: %v, got err: %v", test.expectErr, err)
			}
			if msg == nil && !test.expectErr {
				t.Errorf("got unexpected nil advertisement")
			}
			if msg != nil {
				if msg.Nodename != test.expectedNodename {
					t.Errorf("expected nodename: %v, got: %v", test.expectedNodename, msg.Nodename)
				}
				if msg.BootloaderVersion != test.expectedVersion {
					t.Errorf("expected version: %v, got %v", test.expectedVersion, msg.BootloaderVersion)
				}
			}
		})
	}
}

func TestBeacon(t *testing.T) {
	c := NewClient(time.Second)
	conn, err := net.ListenUDP("udp6", &net.UDPAddr{
		IP:   net.IPv6zero,
		Port: c.AdvertPort,
	})
	if err != nil {
		t.Fatalf("unable to listen UDP: %v", err)
	}
	defer conn.Close()

	_, err = c.Beacon()
	if err == nil {
		t.Errorf("Expected error for multiple Beacon() calls")
	}
}

func TestInvalidNetbootHeaders(t *testing.T) {
	invalidHeaders := []netbootMessage{
		{
			// Bad magic
			Header: netbootHeader{
				Magic:  2,
				Cookie: 1,
				Cmd:    cmdAck,
				Arg:    0,
			},
		},
		{
			// Bad cookie
			Header: netbootHeader{
				Magic:  1,
				Cookie: 2,
				Cmd:    cmdAck,
				Arg:    0,
			},
		},
		{
			// Bad cmd.
			Header: netbootHeader{
				Magic:  1,
				Cookie: 1,
				Cmd:    cmdQuery,
				Arg:    0,
			},
		},
	}
	msg := netbootMessage{
		Header: netbootHeader{
			Magic:  1,
			Cookie: 1,
			Cmd:    cmdQuery,
			Arg:    0,
		},
	}
	q := &netbootQuery{
		message: msg,
	}
	for _, resp := range invalidHeaders {
		var buf bytes.Buffer
		if err := binary.Write(&buf, binary.LittleEndian, resp); err != nil {
			t.Fatalf("failed to write struct: %v", err)
		}
		data, err := q.parse(buf.Bytes())
		if err != nil {
			t.Errorf("Expecting no error for invalid magic number, received: %v", err)
		}
		if len(data) > 0 {
			t.Errorf("Expecting no error for malformed header")
		}
	}
}

func TestValidQueryResponse(t *testing.T) {
	msg := netbootMessage{
		Header: netbootHeader{
			Magic:  1,
			Cookie: 1,
			Cmd:    cmdQuery,
			Arg:    0,
		},
	}
	res := netbootMessage{
		Header: netbootHeader{
			Magic:  1,
			Cookie: 1,
			Cmd:    cmdAck,
			Arg:    0,
		},
	}
	want := "somenode"
	copy(res.Data[:], want)
	q := &netbootQuery{
		message: msg,
	}
	var buf bytes.Buffer
	if err := binary.Write(&buf, binary.LittleEndian, res); err != nil {
		t.Fatalf("failed to write struct: %v", err)
	}
	got, err := q.parse(buf.Bytes())
	if err != nil {
		t.Errorf("Expecting no error, but received: %v", err)
	}
	if got != want {
		t.Errorf("Data parsed, want %q, got %q", want, got)
	}
}

func TestDiscover(t *testing.T) {
	nodename := "hallothere"
	servers := []string{
		"notwhatwewant1",
		nodename,
		"notwhatwewant2",
	}
	port, cleanup := startFakeNetbootServers(t, servers)
	defer cleanup()

	c := newClientWithPorts(port, port+1)
	if _, err := c.Discover(context.Background(), nodename); err != nil {
		t.Errorf("discovery: %v", err)
	}
}

func TestDiscoverNoNodes(t *testing.T) {
	nodename := "stringThatIsNotInTheListOfServers"
	servers := []string{
		"notwhatwewant1",
		"alsoNotwhatWeWant",
		"notwhatwewant2",
	}
	port, cleanup := startFakeNetbootServers(t, servers)
	defer cleanup()

	c := newClientWithPorts(port, port+1)
	if _, err := c.Discover(context.Background(), nodename); err == nil {
		t.Error("expected failure, but succeeded")
	}
}

func TestDiscoverAll(t *testing.T) {
	servers := []string{
		"this",
		"that",
		"those",
	}
	port, cleanup := startFakeNetbootServers(t, servers)
	defer cleanup()

	c := newClientWithPorts(port, port+1)
	got, err := c.DiscoverAll(context.Background())
	if err != nil {
		t.Fatalf("discover all: %v", err)
	}
	m := make(map[string]*Target)
	for _, target := range got {
		m[target.Nodename] = target
	}
	for _, node := range servers {
		g := m[node]
		if g == nil {
			t.Errorf("expected nodename %q", node)
			continue
		}
		t.Logf("found address of %q", g.TargetAddress.String())
		delete(m, node)
	}
	if missing := len(m); missing > 0 {
		t.Errorf("%d missing nodes", missing)
	}
}
