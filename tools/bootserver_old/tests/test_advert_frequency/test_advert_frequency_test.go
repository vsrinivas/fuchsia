// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package main

import (
	"net"
	"regexp"
	"strconv"
	"testing"
	"time"

	bootservertest "go.fuchsia.dev/fuchsia/tools/bootserver_old/tests"
)

func TestAdvertFrequency(t *testing.T) {
	_ = bootservertest.StartQemu(t, "netsvc.all-features=true, netsvc.netboot=true", "full")

	// Get the node ipv6 address
	out := bootservertest.CmdWithOutput(t, bootservertest.ToolPath("netls"))
	// Extract the ipv6 from the netls output
	regexString := bootservertest.DefaultNodename + ` \((.*)/(.*)\)`
	match := regexp.MustCompile(regexString).FindSubmatch(out)
	if len(match) != 3 {
		t.Fatalf("node %s not found in netls output - %s", bootservertest.DefaultNodename, out)
	}
	index, err := strconv.Atoi(string(match[2]))
	if err != nil {
		t.Fatal(err)
	}
	ifi, err := net.InterfaceByIndex(index)
	if err != nil {
		t.Fatal(err)
	}

	conn, err := net.ListenMulticastUDP("udp6", ifi, &net.UDPAddr{
		IP:   net.IPv6linklocalallnodes,
		Port: 33331,
	})
	if err != nil {
		t.Fatal(err)
	}
	defer conn.Close()
	ip := net.ParseIP(string(match[1]))
	if ip == nil {
		t.Fatalf("can't parse %s as IP", match[1])
	}

	for i := 0; i < 50; i++ {
		if _, err := conn.WriteToUDP(nil, &net.UDPAddr{
			IP:   ip,
			Port: 1337,
			Zone: strconv.Itoa(index),
		}); err != nil {
			t.Fatal(err)
		}
	}

	var dst [1]byte

	// The system may be slow to start; do the first read without deadlines.
	if _, _, err := conn.ReadFromUDP(dst[:]); err != nil {
		t.Fatal(err)
	}

	if err := conn.SetDeadline(time.Now().Add(800 * time.Millisecond)); err != nil {
		t.Fatal(err)
	}
	if n, addr, err := conn.ReadFromUDP(dst[:]); err == nil {
		t.Errorf("expected one advertisement packet, got %d bytes from %s", n, addr)
	} else if tErr, ok := err.(net.Error); !ok || !tErr.Timeout() {
		t.Errorf("expected timeout error, got %s", err)
	}
}
