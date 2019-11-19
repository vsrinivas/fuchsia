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
	_, err := c.Discover(context.Background(), nodename, false)
	if err != nil {
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
	_, err := c.Discover(context.Background(), nodename, false)
	if err == nil {
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
	got, err := c.DiscoverAll(context.Background(), false)
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
