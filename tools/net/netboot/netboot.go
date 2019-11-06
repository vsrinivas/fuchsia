// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This package implements the Zircon netboot protocol.
//
// TODO(fxb/35957): Add testing for this package.
package netboot

import (
	"bytes"
	"context"
	"encoding/binary"
	"errors"
	"fmt"
	"net"
	"strings"
	"time"

	"golang.org/x/net/ipv6"
)

// Magic constants used by the netboot protocol.
const (
	baseCookie     = uint32(0x12345678)
	magic          = 0xAA774217 // see //zircon/system/public/zircon/boot/netboot.h
	targetWildcard = "*"        // see https://fuchsia.googlesource.com/fuchsia/+/b95bb07fc5b61c66788c9ebec778734c21089a00/zircon/tools/netprotocol/netprotocol.c#69
)

// Port numbers used by the netboot protocol.
const (
	serverPort = 33330 // netboot server port
	advertPort = 33331 // advertisement port
)

// Commands supported by the netboot protocol.
const (
	cmdAck      = uint32(0)  // ack
	cmdCommand  = uint32(1)  // command
	cmdSendFile = uint32(2)  // send file
	cmdData     = uint32(3)  // data
	cmdBoot     = uint32(4)  // boot command
	cmdQuery    = uint32(5)  // query command
	cmdShell    = uint32(6)  // shell command
	cmdOpen     = uint32(7)  // open file
	cmdRead     = uint32(8)  // read data
	cmdWrite    = uint32(9)  // write data
	cmdClose    = uint32(10) // close file
	cmdLastData = uint32(11) //
	cmdReboot   = uint32(12) // reboot command
)

// Client implements the netboot protocol.
type Client struct {
	ServerPort int
	AdvertPort int
	Cookie     uint32
	Timeout    time.Duration
	Wait       bool
}

// netbootHeader is the netboot protocol message header.
type netbootHeader struct {
	Magic  uint32
	Cookie uint32
	Cmd    uint32
	Arg    uint32
}

// netbootMessage is the netboot protocol message.
type netbootMessage struct {
	Header netbootHeader
	Data   [1024]byte
}

// Target defines a netboot protocol target, which includes information about how
// to find said target on the network: the target's nodename, its address, the
// address from the target back to the host, and the interface used to connect
// from the host to the target.
type Target struct {
	// Nodename is target's nodename: thumb-set-human-neon is an example.
	// This is derived from the NIC mac address.
	Nodename string

	// TargetAddress is the address of the target from the host.
	TargetAddress net.IP

	// HostAddress is the "local" address, i.e. the one to which the target
	// is responding. This would be the address the target would send to in
	// order to communicate with the host.
	HostAddress net.IP

	// Interface is the index of the "local" interface connecting to
	// the Fuchsia device. nil if this does not apply.
	Interface *net.Interface
}

// NewClient creates a new Client instance.
func NewClient(timeout time.Duration) *Client {
	return &Client{
		Timeout:    timeout,
		ServerPort: serverPort,
		AdvertPort: advertPort,
		Cookie:     baseCookie,
	}
}

type netbootQueryListener struct {
	targets <-chan *Target
	errors  <-chan error
	cleanup func() error
}

type netbootQuery struct {
	message netbootMessage
	fuchsia bool

	conn6 *ipv6.PacketConn
	conn  *net.UDPConn
	port  int // The port to write on.
}

func newNetbootQuery(nodename string, cookie uint32, port int, fuchsia bool) (*netbootQuery, error) {
	conn, err := net.ListenUDP("udp6", &net.UDPAddr{IP: net.IPv6zero})
	if err != nil {
		return nil, fmt.Errorf("bind to udp6 port: %v", err)
	}
	req := netbootMessage{
		Header: netbootHeader{
			Magic:  magic,
			Cookie: cookie,
			Cmd:    cmdQuery,
			Arg:    0,
		},
	}
	copy(req.Data[:], nodename)
	conn6 := ipv6.NewPacketConn(conn)
	conn6.SetControlMessage(ipv6.FlagDst|ipv6.FlagSrc|ipv6.FlagInterface, true)
	return &netbootQuery{
		message: req,
		fuchsia: fuchsia,
		conn:    conn,
		conn6:   conn6,
		port:    port}, nil
}

func (n *netbootQuery) write() error {
	// Cleanup function is used here in favor of defer to be explicit about
	// what is being returned. It is difficult to reason about otherwise.
	cleanup := func(e error) error {
		if e != nil {
			n.conn.Close()
		}
		return e
	}
	var buf bytes.Buffer
	if err := binary.Write(&buf, binary.LittleEndian, n.message); err != nil {
		return cleanup(err)
	}
	ifaces, err := net.Interfaces()
	if err != nil {
		return cleanup(err)
	}
	wrote := false
	// Tracks last write error (in the event that all writes fail, for debugging).
	var lastWriteErr error
	for _, iface := range ifaces {
		if iface.Flags&net.FlagUp == 0 {
			continue
		}
		if iface.Flags&net.FlagLoopback != 0 {
			continue
		}
		addrs, err := iface.Addrs()
		if err != nil {
			return cleanup(err)
		}

		for _, addr := range addrs {
			var ip net.IP
			switch v := addr.(type) {
			case *net.IPNet:
				ip = v.IP
			case *net.IPAddr:
				ip = v.IP
			}
			if ip == nil || ip.To16() == nil {
				continue
			}

			_, err := n.conn.WriteToUDP(buf.Bytes(), &net.UDPAddr{
				IP:   net.IPv6linklocalallnodes,
				Port: n.port,
				Zone: iface.Name,
			})
			// Skip errors here, as it may be possible to write on
			// some interfaces but not others. Track last error in
			// case all writes fail on all interfaces.
			if err != nil {
				lastWriteErr = err
				continue
			}
			wrote = true
		}
	}
	if !wrote {
		return cleanup(fmt.Errorf("write on any iface. Last err: %v", lastWriteErr))
	}
	return nil
}

func (n *netbootQuery) read() (*Target, error) {
	b := make([]byte, 4096)
	_, cm, _, err := n.conn6.ReadFrom(b)
	if err != nil {
		return nil, fmt.Errorf("query read error: %v", err)
	}
	node, err := n.parse(b)
	if err != nil {
		return nil, err
	}
	if len(node) == 0 {
		return nil, nil
	}
	if n.fuchsia {
		// The netstack link-local address has 11th byte always set to 0xff, set
		// this byte to transform netsvc address to netstack address if needed.
		cm.Src[11] = 0xff
	}

	var iface *net.Interface
	if cm.IfIndex > 0 {
		iface, err = net.InterfaceByIndex(cm.IfIndex)
		if err != nil {
			return nil, fmt.Errorf("query iface lookup: err")
		}
	}
	return &Target{
		Nodename:      node,
		TargetAddress: cm.Src,
		HostAddress:   cm.Dst,
		Interface:     iface,
	}, nil
}

func (n *netbootQuery) parse(b []byte) (string, error) {
	r := bytes.NewReader(b)
	var res netbootMessage
	if err := binary.Read(r, binary.LittleEndian, &res); err != nil {
		return "", fmt.Errorf("query parse error: %v", err)
	}
	if res.Header.Magic != n.message.Header.Magic || res.Header.Cookie != n.message.Header.Cookie || res.Header.Cmd != cmdAck {
		return "", nil
	}
	data, err := netbootString(res.Data[:])
	if err != nil {
		return "", err
	}
	return data, nil
}

func (n *netbootQuery) close() error {
	return n.conn.Close()
}

// startQueryListener is a utility function which starts by sending a query, and
// then listening for all responses.
func (n *Client) startQueryListener(nodename string, fuchsia bool) (*netbootQueryListener, error) {
	n.Cookie++
	q, err := newNetbootQuery(nodename, n.Cookie, n.ServerPort, fuchsia)
	if err != nil {
		return nil, err
	}
	errCh := make(chan error)
	tCh := make(chan *Target)
	go func() {
		defer q.close()
		if err := q.write(); err != nil {
			errCh <- err
			return
		}

		for {
			target, err := q.read()
			if err != nil {
				errCh <- err
				return
			}
			if target != nil {
				tCh <- target
			}
		}
	}()
	return &netbootQueryListener{tCh, errCh, q.close}, nil
}

// Discover resolves the address of host and returns either the netsvc or
// Fuchsia address dependending on the value of fuchsia.
func (n *Client) Discover(ctx context.Context, nodename string, fuchsia bool) (*net.UDPAddr, error) {
	ctx, cancel := context.WithTimeout(ctx, n.Timeout)
	defer cancel()
	listener, err := n.startQueryListener(nodename, fuchsia)
	if err != nil {
		return nil, err
	}
	defer listener.cleanup()
	for {
		select {
		case <-ctx.Done():
			return nil, fmt.Errorf("timed out waiting for results")
		case target := <-listener.targets:
			if strings.Contains(target.Nodename, nodename) {
				ifaceName := ""
				if target.Interface != nil {
					ifaceName = target.Interface.Name
				}
				return &net.UDPAddr{IP: target.TargetAddress, Zone: ifaceName}, nil
			}
			continue
		case err := <-listener.errors:
			return nil, err
		}
	}
}

// DiscoverAll attempts to discover all Fuchsia targets on the network. Returns
// either the netsvc address or the Fuchsia address depending on the |fuchsia|
// parameter.
//
// If no devices are found, returns a nil array.
func (n *Client) DiscoverAll(ctx context.Context, fuchsia bool) ([]*Target, error) {
	ctx, cancel := context.WithTimeout(ctx, n.Timeout)
	defer cancel()
	listener, err := n.startQueryListener(targetWildcard, fuchsia)
	if err != nil {
		return nil, err
	}
	defer listener.cleanup()
	results := []*Target{}
	for {
		select {
		case <-ctx.Done():
			if len(results) == 0 {
				return nil, nil
			}
			return results, nil
		case target := <-listener.targets:
			results = append(results, target)
		case err = <-listener.errors:
			return nil, err
		}
	}
}

// Beacon receives the beacon packet, returning the address of the sender.
func (n *Client) Beacon() (*net.UDPAddr, error) {
	conn, err := net.ListenUDP("udp6", &net.UDPAddr{
		IP:   net.IPv6zero,
		Port: n.AdvertPort,
	})
	defer conn.Close()

	conn.SetReadDeadline(time.Now().Add(n.Timeout))

	b := make([]byte, 4096)
	_, addr, err := conn.ReadFromUDP(b)
	if err != nil {
		return nil, err
	}

	r := bytes.NewReader(b)
	var res netbootMessage
	if err := binary.Read(r, binary.LittleEndian, &res); err != nil {
		return nil, err
	}

	data, err := netbootString(res.Data[:])
	if err != nil {
		return nil, err
	}
	// The query packet payload contains fields separated by ;.
	for _, f := range strings.Split(string(data[:]), ";") {
		// The field has a key=value format.
		vars := strings.SplitN(f, "=", 2)
		// The field with the "nodename" key contains the name of the device.
		if vars[0] == "nodename" {
			return addr, nil
		}
	}

	return nil, errors.New("no valid beacon")
}

// Boot sends a boot packet to the address.
func (n *Client) Boot(addr *net.UDPAddr) error {
	n.Cookie++
	msg := &netbootHeader{
		Magic:  magic,
		Cookie: n.Cookie,
		Cmd:    cmdBoot,
		Arg:    0,
	}
	if err := sendPacket(msg, addr, n.ServerPort); err != nil {
		return fmt.Errorf("send boot command: %v\n", err)
	}
	return nil
}

// Reboot sends a reboot packet the address.
func (n *Client) Reboot(addr *net.UDPAddr) error {
	n.Cookie++
	msg := &netbootHeader{
		Magic:  magic,
		Cookie: n.Cookie,
		Cmd:    cmdReboot,
		Arg:    0,
	}
	if err := sendPacket(msg, addr, n.ServerPort); err != nil {
		return fmt.Errorf("send reboot command: %v\n", err)
	}
	return nil
}

func sendPacket(msg *netbootHeader, addr *net.UDPAddr, port int) error {
	if msg == nil {
		return errors.New("no message provided")
	}
	var buf bytes.Buffer
	if err := binary.Write(&buf, binary.LittleEndian, *msg); err != nil {
		return err
	}

	conn, err := net.ListenUDP("udp6", &net.UDPAddr{IP: net.IPv6zero})
	if err != nil {
		return fmt.Errorf("create a socket: %v\n", err)
	}
	defer conn.Close()

	_, err = conn.WriteToUDP(buf.Bytes(), &net.UDPAddr{
		IP:   addr.IP,
		Port: port,
		Zone: addr.Zone,
	})
	return err
}

func netbootString(bs []byte) (string, error) {
	for i, b := range bs {
		if b == 0 {
			return string(bs[:i]), nil
		}
	}
	return "", errors.New("no null terminated string found")
}
