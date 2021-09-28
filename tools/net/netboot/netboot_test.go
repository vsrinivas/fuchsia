// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package netboot

import (
	"bytes"
	"context"
	"encoding/binary"
	"fmt"
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

func newConnWithPort(reusable bool) (*net.UDPConn, int, error) {
	conn, err := UDPConnWithReusablePort(0, "", reusable)
	if err != nil {
		return nil, 0, err
	}
	_, portStr, err := net.SplitHostPort(conn.LocalAddr().String())
	if err != nil {
		return nil, 0, err
	}
	port, err := strconv.Atoi(portStr)
	if err != nil {
		return nil, 0, err
	}
	return conn, port, err
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

func writeNetbootMessageToPort(conn *net.UDPConn, port int, message string) error {
	ifaces, err := net.Interfaces()
	if err != nil {
		return err
	}
	wrote := false
	for _, iface := range ifaces {
		if iface.Flags&net.FlagUp == 0 {
			continue
		}
		if iface.Flags&net.FlagLoopback != 0 {
			continue
		}
		res := netbootMessage{}
		copy(res.Data[:], message)
		var resBuf bytes.Buffer
		if err = binary.Write(&resBuf, binary.LittleEndian, res); err != nil {
			return fmt.Errorf("binary write: %v", err)
		}
		_, err = conn.WriteToUDP(resBuf.Bytes(), &net.UDPAddr{IP: net.IPv6linklocalallnodes, Port: port, Zone: iface.Name})
		if err == nil {
			wrote = true
		}
	}
	if !wrote {
		return err
	}
	return nil
}

func getIpv6Addr(conn *net.UDPConn) (*net.UDPAddr, error) {
	// Make a new connection with a separate port which `conn` will write
	// to so that we can read the address from it.
	conn2, err := net.ListenUDP("udp6", &net.UDPAddr{IP: net.IPv6zero})
	if err != nil {
		return nil, err
	}
	defer conn2.Close()
	conn2Addr, err := net.ResolveUDPAddr("udp6", conn2.LocalAddr().String())
	if err != nil {
		return nil, err
	}

	if err := writeNetbootMessageToPort(conn, conn2Addr.Port, "message"); err != nil {
		return nil, err
	}

	var b []byte
	_, addr, err := conn2.ReadFromUDP(b)
	return addr, err
}

func startFakeAdvertServers(t *testing.T, nodenames []string, advertPort, maxAdverts int) (*net.UDPAddr, func()) {
	t.Helper()
	conn, err := net.ListenUDP("udp6", &net.UDPAddr{IP: net.IPv6zero})
	if err != nil {
		t.Fatal(err)
	}
	serverAddr, err := getIpv6Addr(conn)
	if err != nil {
		conn.Close()
		t.Fatal(err)
	}
	go func(t *testing.T) {
		t.Helper()
		i := 0
		for {
			if maxAdverts > 0 && i == maxAdverts {
				break
			}
			i += 1
			for _, n := range nodenames {
				if err := writeNetbootMessageToPort(conn, advertPort, n); err != nil {
					conn.Close()
					break
				}
			}
		}
	}(t)
	return serverAddr, func() {
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
	// Get connection with non-reusable port to find a port to use as the advert port.
	conn, port, err := newConnWithPort(false)
	if err != nil {
		t.Fatal(err)
	}
	defer conn.Close()

	c := newClientWithPorts(port+1, port)

	_, err = c.Beacon()
	if err == nil {
		t.Errorf("Expected error for multiple Beacon() calls")
	}
}

func TestBeaconForDevice(t *testing.T) {
	nodename := "nodename"
	noNodenameServers := []string{
		"nodename=name;version=1.2.3",
		"nodename=name2;version=1.2.3",
	}
	withNodenameServers := append(noNodenameServers, fmt.Sprintf("nodename=%s;version=1.2.3", nodename))
	for _, tc := range []struct {
		name     string
		servers  []string
		nodename string
		useIpv6  bool
		// expectedNodenames is a list of possible nodenames we expect to get a beacon for.
		expectedNodenames []string
		wantErr           bool
	}{
		{
			name:              "found nodename",
			servers:           withNodenameServers,
			nodename:          nodename,
			expectedNodenames: []string{nodename},
			wantErr:           false,
		},
		{
			name:     "cannot find nodename",
			servers:  noNodenameServers,
			nodename: nodename,
			wantErr:  true,
		},
		{
			name:              "returned first nodename for wildcard",
			servers:           noNodenameServers,
			nodename:          NodenameWildcard,
			expectedNodenames: []string{"name", "name2"},
			wantErr:           false,
		},
		{
			name:              "found ipv6 address",
			servers:           noNodenameServers,
			nodename:          NodenameWildcard,
			useIpv6:           true,
			expectedNodenames: []string{"name", "name2"},
			wantErr:           false,
		},
		{
			name:     "cannot find ipv6 address",
			servers:  noNodenameServers,
			nodename: NodenameWildcard,
			useIpv6:  true,
			wantErr:  true,
		},
	} {
		t.Run(tc.name, func(t *testing.T) {
			// Continually send advertisements until Beacon returns.
			maxAdverts := 0
			if tc.wantErr {
				// If we expect an err, we only need to send the adverts a few times
				// before timing out. Otherwise, there will be a spam of logs for
				// finding a beacon for the wrong nodename.
				maxAdverts = 5
			}
			// Get connection with reusable port to find a port to use as the advert port.
			conn, port, err := newConnWithPort(true)
			if err != nil {
				t.Fatal(err)
			}
			defer conn.Close()
			serverAddr, cleanup := startFakeAdvertServers(t, tc.servers, port, maxAdverts)
			defer cleanup()

			c := newClientWithPorts(port+1, port)
			var ipv6Address *net.UDPAddr
			if tc.useIpv6 {
				if tc.wantErr {
					ipv6Address = &net.UDPAddr{IP: serverAddr.IP, Zone: "nonexistent"}
				} else {
					ipv6Address = serverAddr
				}
			}
			_, msg, cleanup, err := c.BeaconForDevice(context.Background(), tc.nodename, ipv6Address, true)
			defer cleanup()
			if tc.wantErr != (err != nil) {
				t.Errorf("expected err: %v, got: %v", tc.wantErr, err)
			}
			if tc.wantErr != (msg == nil) {
				t.Errorf("expected advertisement: %v, got advertisement: %v", !tc.wantErr, msg != nil)
			}
			if msg != nil {
				foundExpectedNodename := false
				for _, n := range tc.expectedNodenames {
					if msg.Nodename == n {
						foundExpectedNodename = true
						break
					}
				}
				if !foundExpectedNodename {
					t.Errorf("expected nodename from: %v, got: %s", tc.expectedNodenames, msg.Nodename)
				}
			}
		})
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
		t.Log("Note this test requires a specific local network configuration. Make sure you can run `fx emu -N --headless`.")
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
		t.Log("Note this test requires a specific local network configuration. Make sure you can run `fx emu -N --headless`.")
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
