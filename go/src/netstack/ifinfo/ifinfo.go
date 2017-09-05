// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package main

import (
	"fmt"
	"strconv"
	"strings"
	"time"

	"application/lib/app/context"
	"fidl/bindings"

	"garnet/public/lib/netstack/fidl/netstack"
)

type netstackClientApp struct {
	ctx      *context.Context
	netstack *netstack.Netstack_Proxy
}

func (a *netstackClientApp) start() {
	r, p := a.netstack.NewRequest(bindings.GetAsyncWaiter())
	a.netstack = p
	a.ctx.ConnectToEnvService(r)

	ifaces, err := a.netstack.GetInterfaces()
	if err != nil {
		fmt.Printf("Failed to fetch interfaces: %v\n", err)
		return
	}

	for _, iface := range ifaces {
		// TODO (porec): Both Watcher(), Golang map[] do no sort the network interfaces.
		// Sort them.
		nicid := iface.Id
		stats, err := a.netstack.GetStats(uint32(nicid))
		if err != nil {
			fmt.Printf("Failed to get stats for interface nicid %d name %s: %v\n", nicid, iface.Name, err)
			// TODO (porce): A single failure leads to
			// ErrPeerClosed: mx.Channel.Read and
			// ErrPeerClosed: bindings.Router.AcceptWithResponse.
			// Think of how a particular interface error will be handled.
			continue
		}

		printIfStats(iface, stats)
		fmt.Printf("\n")
	}

	a.netstack.Close()
}

func getDurationSince(since int64) (durationStr string) {
	// TODO (porce):
	// Returns human-friendly duration string, such as
	// "4s", "2m", "3.7h", "5d", "2mon"..
	// Use time.duration
	now := int64(time.Now().Unix())
	return strconv.Itoa(int(now-since)) + "s" // for now
}

func getDurationSecSince(since int64) (durationSec int64) {
	// TODO (porce):
	// Returns human-friendly duration string, such as
	// "4s", "2m", "3.7h", "5d", "2mon"..
	// Use time.duration
	now := int64(time.Now().Unix())
	return (now - since)
}

func printIfStats(ni netstack.NetInterface, stats netstack.NetInterfaceStats) { // experiment
	// TODO (porce): if the interface is down, up_howlong := "-"
	datetime := time.Unix(stats.UpSince, 0)

	fmt.Printf("\n%s:\n", ni.Name)
	fmt.Printf("Up since: %s (%s ago)\n\n", datetime, getDurationSince(stats.UpSince))

	alignTitle := "%-14s %-6s %-12s %-10s\n" // Bucket, Unit, Rx stat, Tx stat
	fmt.Printf(alignTitle, "Bucket", "Unit", "Rx", "Tx")
	fmt.Printf(alignTitle, strings.Repeat("-", 14), strings.Repeat("-", 6), strings.Repeat("-", 12), strings.Repeat("-", 12))

	align := "%14s %6s %12d %12d\n" // Bucket, Unit, Rx stat, Tx stat
	const C = "[#]"                 // Counts
	const B = "[B]"                 // Bytes

	fmt.Printf(align, "Total", C, stats.RxPktsTotal, stats.TxPktsTotal)
	fmt.Printf(align, "Total", B, stats.RxBytesTotal, stats.TxBytesTotal)
	fmt.Printf("\n")
}

func main() {
	a := &netstackClientApp{ctx: context.CreateFromStartupInfo()}
	a.start()
}
