// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package wol

import (
	"errors"
	"fmt"
	"net"
	"regexp"
	"time"
)

var (
	macAddrRegex = regexp.MustCompile(`(?i)^([0-9A-F]{2}[:-]){5}([0-9A-F]{2})$`)
	// Magic Packet header is 0xFF repeated 6 times.
	magicPacketHeader = []byte{0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF}
)

const (
	magicPacketLength = 102
)

// Reboot sends a WakeOnLAN magic packet {magicPacketHeader + macAddr x 16}
// using the specified network interface to the broadcast address
func Reboot(broadcastAddr, interfaceName, macAddr string) error {
	if !macAddrRegex.Match([]byte(macAddr)) {
		return fmt.Errorf("Invalid MAC: %s", macAddr)
	}

	remoteHwAddr, err := net.ParseMAC(macAddr)
	if err != nil {
		return err
	}

	localAddr, err := getUDPAddrFromIFace(interfaceName)
	if err != nil {
		return err
	}
	remoteAddr, err := net.ResolveUDPAddr("udp", broadcastAddr)
	if err != nil {
		return err
	}

	return sendMagicPacket(localAddr, remoteAddr, remoteHwAddr)
}

func getUDPAddrFromIFace(ifaceName string) (*net.UDPAddr, error) {
	iface, err := net.InterfaceByName(ifaceName)
	if err != nil {
		return nil, err
	}

	addrs, err := iface.Addrs()
	if err != nil {
		return nil, err
	}

	for _, addr := range addrs {
		if ipAddr, ok := addr.(*net.IPNet); ok {
			// Need an IPv4, non-loopback address to send on
			if !ipAddr.IP.IsLoopback() && ipAddr.IP.To4() != nil {
				return &net.UDPAddr{
					IP: ipAddr.IP,
				}, nil
			}
		}
	}

	return nil, errors.New("No UDPAddr found on interface")
}

func sendMagicPacket(localAddr, remoteAddr *net.UDPAddr, remoteHwAddr net.HardwareAddr) error {
	packet := magicPacketHeader
	for i := 0; i < 16; i++ {
		packet = append(packet, remoteHwAddr...)
	}

	if len(packet) != magicPacketLength {
		return fmt.Errorf("Wake-On-LAN packet incorrect length: %d", len(packet))
	}

	conn, err := net.DialUDP("udp", localAddr, remoteAddr)
	if err != nil {
		return err
	}
	defer conn.Close()

	// Attempt to send the Magic Packet TEN times in a row.  The UDP packet sometimes
	// does not make it to the DUT and this is the simplest way to increase the chance
	// the device reboots.
	for i := 0; i < 10; i++ {
		n, err := conn.Write(packet)

		if n != magicPacketLength {
			return errors.New("Failed to send correct Wake-On-LAN packet length")
		}

		if err != nil {
			return err
		}
		time.Sleep(1 * time.Second)
	}

	return nil
}
