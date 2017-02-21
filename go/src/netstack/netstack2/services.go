// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package main

import (
	"strconv"

	"github.com/google/netstack/tcpip"
	"github.com/google/netstack/tcpip/transport/tcp"
	"github.com/google/netstack/tcpip/transport/udp"
)

type serviceKey struct {
	name      string
	transport tcpip.TransportProtocolNumber
}

// http://www.iana.org/assignments/port-numbers
var services = map[serviceKey]uint16{
	{"echo", tcp.ProtocolNumber}:   7,
	{"echo", udp.ProtocolNumber}:   7,
	{"ftp", tcp.ProtocolNumber}:    21,
	{"ssh", tcp.ProtocolNumber}:    22,
	{"telnet", tcp.ProtocolNumber}: 23,
	{"tftp", udp.ProtocolNumber}:   69,
	{"http", tcp.ProtocolNumber}:   80,
	{"ntp", tcp.ProtocolNumber}:    123,
	{"ntp", udp.ProtocolNumber}:    123,
	{"imap", tcp.ProtocolNumber}:   143,
	{"irc", tcp.ProtocolNumber}:    194,
	{"irc", udp.ProtocolNumber}:    194,
	{"ldap", tcp.ProtocolNumber}:   389,
	{"ldap", udp.ProtocolNumber}:   389,
	{"https", tcp.ProtocolNumber}:  443,
}

func serviceLookup(name string, t tcpip.TransportProtocolNumber) (port uint16) {
	portNum, err := strconv.Atoi(name)
	if err == nil {
		return uint16(portNum)
	}
	return services[serviceKey{name, t}]
}
