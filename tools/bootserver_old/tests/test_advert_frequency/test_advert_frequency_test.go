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

	"go.fuchsia.dev/fuchsia/tools/bootserver_old/tests"
)

func TestAdvertFrequency(t *testing.T) {
	_, cleanup := bootserver.StartQemu(t, "netsvc.all-features=true, netsvc.netboot=true", "full")
	defer cleanup()

	// Get the node ipv6 address
	out := bootserver.CmdWithOutput(t, bootserver.ToolPath(t, "netls"))
	// Extract the ipv6 from the netls output
	regexString := bootserver.DefaultNodename + ` \((.*)/(.*)\)`
	match := regexp.MustCompile(regexString).FindSubmatch(out)
	if len(match) != 3 {
		t.Fatalf("node %s not found in netls output - %s", bootserver.DefaultNodename, out)
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
	defer conn.Close()
	if err != nil {
		t.Fatal(err)
	}
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
	if err := conn.SetDeadline(time.Now().Add(800 * time.Millisecond)); err != nil {
		t.Fatal(err)
	}
	var dst [100]byte
	for i := 0; i < 10; i++ {
		n, addr, err := conn.ReadFromUDP(dst[:])
		if i == 0 {
			if err != nil {
				t.Fatal(err)
			}
			continue
		}
		if err == nil {
			t.Errorf("expected one advertisement packet, got %d bytes from %s on iteration %d", n, addr, i)
			continue
		}
		if tErr, ok := err.(net.Error); !ok || !tErr.Timeout() {
			t.Errorf("expected timeout error on iteration %d, got %s", i, err)
			continue
		}
		break
	}
}
