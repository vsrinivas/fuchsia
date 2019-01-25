// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This package implements the Zircon netboot protocol.
package netboot

import (
	"bytes"
	"encoding/binary"
	"errors"
	"fmt"
	"net"
	"strings"
	"time"
)

// Magic constants used by the netboot protocol.
const (
	magic = 0xAA774217 // see system/public/zircon/boot/netboot.h
)

// Port numbers used by the netboot protocol.
const (
	serverPort = 33330 // netboot server port
	advertPort = 33331 // advertisement port
)

// Commands supported by the netboot protocol.
const (
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
	Port    int
	Cookie  uint32
	Timeout time.Duration
	Wait    bool
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

// NewClient creates a new Client instance.
func NewClient(timeout time.Duration) *Client {
	return &Client{
		Timeout: timeout,
		Cookie:  uint32(0x12345678),
	}
}

// Discover resolves the address of host and returns either the netsvc or
// Fuchsia address dependending on the value of fuchsia.
func (n *Client) Discover(nodename string, fuchsia bool) (*net.UDPAddr, error) {
	conn, err := net.ListenUDP("udp6", &net.UDPAddr{IP: net.IPv6zero})
	if err != nil {
		return nil, fmt.Errorf("failed to bind to udp6 port: %v\n", err)
	}
	defer conn.Close()

	n.Cookie++
	req := netbootMessage{
		Header: netbootHeader{
			Magic:  magic,
			Cookie: n.Cookie,
			Cmd:    cmdQuery,
			Arg:    0,
		},
	}
	copy(req.Data[:], nodename)

	var buf bytes.Buffer
	if err := binary.Write(&buf, binary.LittleEndian, req); err != nil {
		return nil, err
	}

	// Enumerate all available network interfaces.
	ifaces, err := net.Interfaces()
	if err != nil {
		return nil, fmt.Errorf("failed to enumerate network interfaces: %v\n", err)
	}
	for _, iface := range ifaces {
		// Skip interfaces that are down.
		if iface.Flags&net.FlagUp == 0 {
			continue
		}
		// Skip loopback interfaces.
		if iface.Flags&net.FlagLoopback != 0 {
			continue
		}
		addrs, err := iface.Addrs()
		if err != nil {
			return nil, err
		}

		// Enumerate all interface addresses and find the usable ones.
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

			// Send a broadcast query command using the interface.
			conn.WriteToUDP(buf.Bytes(), &net.UDPAddr{
				IP:   net.IPv6linklocalallnodes,
				Port: serverPort,
				Zone: iface.Name,
			})
		}
	}

	// Wait for response, this is a best effort approach so we may not receive
	// any response if there're no usable interfaces or no devices connected.
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
	// The query packet payload contains the nodename.
	if data != nodename {
		return nil, fmt.Errorf("invalid nodename `%s`", data)
	}

	if fuchsia {
		// The netstack link-local address has 11th byte always set to 0xff, set
		// this byte to transform netsvc address to netstack address if needed.
		addr.IP[11] = 0xff
	}

	return addr, nil
}

// Beacon receives the beacon packet, returning the address of the sender.
func (n *Client) Beacon() (*net.UDPAddr, error) {
	conn, err := net.ListenUDP("udp6", &net.UDPAddr{
		IP:   net.IPv6zero,
		Port: advertPort,
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
	if err := sendPacket(msg, addr); err != nil {
		return fmt.Errorf("failed to send boot command: %v\n", err)
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
	if err := sendPacket(msg, addr); err != nil {
		return fmt.Errorf("failed to send reboot command: %v\n", err)
	}
	return nil
}

func sendPacket(msg *netbootHeader, addr *net.UDPAddr) error {
	if msg == nil {
		return errors.New("no message provided")
	}
	var buf bytes.Buffer
	if err := binary.Write(&buf, binary.LittleEndian, *msg); err != nil {
		return err
	}

	conn, err := net.ListenUDP("udp6", &net.UDPAddr{IP: net.IPv6zero})
	if err != nil {
		return fmt.Errorf("failed to create a socket: %v\n", err)
	}
	defer conn.Close()

	_, err = conn.WriteToUDP(buf.Bytes(), &net.UDPAddr{
		IP:   addr.IP,
		Port: serverPort,
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
