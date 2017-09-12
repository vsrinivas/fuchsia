// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package main

import (
	"flag"
	"fmt"
	"os"
	"strings"
	"time"

	"app/context"
	"fidl/bindings"

	"garnet/public/lib/netstack/fidl/netstack"

	"github.com/google/netstack/tcpip"
)

type netstackClientApp struct {
	ctx      *context.Context
	netstack *netstack.Netstack_Proxy
}

func (a *netstackClientApp) start(nicName string) {
	ifaces, err := a.netstack.GetInterfaces()
	if err != nil {
		fmt.Printf("Failed to fetch interfaces: %v\n", err)
		return
	}

	for _, iface := range ifaces {
		// TODO (porce): Both Watcher(), Golang map[] do no sort the network interfaces.
		// Sort them.

		if nicName != "" && nicName != iface.Name {
			continue
		}
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
}

func formattedSince(since time.Time) (sinceStr string) {
	// Returns human-friendly duration string in Sec: eg. "4m10s".
	s := time.Since(since)
	s -= s % time.Second // Truncate to sec.
	return s.String()
}

func formattedSinceInt(since int64) (sinceStr string) {
	return formattedSince(time.Unix(since, 0))
}

func printIfStats(ni netstack.NetInterface, stats netstack.NetInterfaceStats) { // experiment
	// TODO (porce): if the interface is down, up_howlong := "-"
	datetime := time.Unix(stats.UpSince, 0)

	fmt.Printf("\n%s:\n", ni.Name)
	fmt.Printf("Up since: %s (%s ago)\n\n", datetime, formattedSinceInt(stats.UpSince))

	alignTitle := "%-14s %-6s %-12s %-10s\n" // Bucket, Unit, Rx stat, Tx stat
	fmt.Printf(alignTitle, "Bucket", "Unit", "Rx", "Tx")
	fmt.Printf(alignTitle, strings.Repeat("-", 14), strings.Repeat("-", 6), strings.Repeat("-", 12), strings.Repeat("-", 12))

	align := "%14s %6s %12d %12d\n" // Bucket, Unit, Rx stat, Tx stat
	const C = "[#]"                 // Counts
	const B = "[B]"                 // Bytes

	fmt.Printf(align, "Total", B, stats.Rx.BytesTotal, stats.Tx.BytesTotal)
	fmt.Printf(align, "Total", C, stats.Rx.PktsTotal, stats.Tx.PktsTotal)
	fmt.Printf(align, "EchoReQ", C, stats.Rx.PktsEchoReq, stats.Tx.PktsEchoReq)
	fmt.Printf(align, "EchoRep", C, stats.Rx.PktsEchoRep, stats.Tx.PktsEchoRep)
	fmt.Printf(align, "v6 EchoReQ", C, stats.Rx.PktsEchoReqV6, stats.Tx.PktsEchoReqV6)
	fmt.Printf(align, "v6 EchoRep", C, stats.Rx.PktsEchoRepV6, stats.Tx.PktsEchoRepV6)

	fmt.Printf("\n")
}

func main() {
	flag.Usage = usage
	var watch bool
	var sec uint
	var period uint // sec
	flag.BoolVar(&watch, "watch", false, "watch a NIC every period")
	flag.UintVar(&sec, "sec", 10, "how many sec to watch. 0 for forever")
	flag.UintVar(&period, "period", 1, "stat update period in sec")
	flag.Parse()

	a := &netstackClientApp{ctx: context.CreateFromStartupInfo()}

	nicName := ""
	if len(flag.Args()) != 0 {
		nicName = flag.Args()[0]
	}
	if watch == true && nicName == "" {
		// Monitoring requires a single NIC
		flag.Usage()
		return
	}

	r, p := a.netstack.NewRequest(bindings.GetAsyncWaiter())
	a.netstack = p
	a.ctx.ConnectToEnvService(r)
	defer a.netstack.Close()

	if watch == false { // This trick makes it possible to the check the presence of the flag, not only its value.
		a.start(nicName)
		return
	}

	// Watch the interface every second.
	isForever := (sec == 0)
	err := a.watchIface(nicName, sec, isForever, period)
	if err != nil {
		fmt.Printf("%v\n", err)
	}
}

func (a *netstackClientApp) getNICID(nicName string) (nicid tcpip.NICID, err error) {
	ifaces, err := a.netstack.GetInterfaces()
	if err != nil {
		return tcpip.NICID(0), err
	}

	for _, iface := range ifaces {
		if nicName == iface.Name {
			return tcpip.NICID(iface.Id), nil
		}
	}
	return tcpip.NICID(0), fmt.Errorf("No NIC for such name: %s", nicName)
}

func (a *netstackClientApp) watchIface(nicName string, sec uint, isForever bool, period uint) (err error) {
	if nicName == "" {
		return fmt.Errorf("Please specify the NIC name")
	}

	nicid, err := a.getNICID(nicName)
	if err != nil {
		return err
	}

	statsPrev := netstack.NetInterfaceStats{}
	timeRx := time.Time{}
	timeTx := time.Time{}
	errDuration := 0 // sec for prolonged error in querying stats.

	for isForever || sec > 0 {
		stats, err := a.netstack.GetStats(uint32(nicid))
		now := time.Now()
		nowStr := now.Truncate(time.Second).String()

		if err != nil {
			errDuration += 1
			nowStr += fmt.Sprintf(" : Error for %d sec (%s)", errDuration, err.Error())
			goto sleep
		}

		errDuration = 0
		if statsPrev == stats {
			goto sleep
		}

		if statsPrev.Rx.BytesTotal != stats.Rx.BytesTotal {
			timeRx = now
		}
		if statsPrev.Tx.BytesTotal != stats.Tx.BytesTotal {
			timeTx = now
		}
		statsPrev = stats

	sleep:
		statsOneliner(nicName, stats, formattedSince(timeRx), formattedSince(timeTx), nowStr)
		time.Sleep(time.Duration(period) * time.Second)
		sec -= period
	}

	fmt.Printf("\n\n")
	return nil
}

func statsOneliner(nicName string, stats netstack.NetInterfaceStats, timeRx string, timeTx string, nowStr string) {
	// ANSI escape sequence: Erases everything written on line before this
	// and move the cursor to the beginning of the line.
	// fmt.Printf("\033[2K\r")  // .. does not work well.
	fmt.Printf("\r%s: Rx [%5s ago] %10d pkts      Tx [%5s ago] %10d pkts      %s",
		nicName, timeRx, stats.Rx.PktsTotal, timeTx, stats.Tx.PktsTotal, nowStr)
}

func usage() {
	fmt.Printf("Usage: %s [OPTIONS] [NIC_NAME]\n", os.Args[0])
	flag.PrintDefaults()
}
