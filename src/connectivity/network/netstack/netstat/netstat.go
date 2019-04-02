// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package main

import (
	"encoding/binary"
	"flag"
	"fmt"
	"math"
	"os"
	"strings"
	"syscall"
	"syscall/zx"

	"app/context"
	"netstack/fidlconv"

	"fidl/fuchsia/io"
	"fidl/fuchsia/net"
	"fidl/fuchsia/netstack"

	"github.com/pkg/errors"
)

type netInterfaceStats netstack.NetInterfaceStats

func (s netInterfaceStats) String() string {
	return fmt.Sprintf(
		`%d total packets received
%d total bytes received
%d total packets sent
%d total bytes sent`,
		s.Rx.PktsTotal,
		s.Rx.BytesTotal,
		s.Tx.PktsTotal,
		s.Tx.BytesTotal)
}

func netAddressZero(addr net.IpAddress) bool {
	switch addr.Which() {
	case net.IpAddressIpv4:
		for _, b := range addr.Ipv4.Addr {
			if b != 0 {
				return false
			}
		}
		return true
	case net.IpAddressIpv6:
		for _, b := range addr.Ipv6.Addr {
			if b != 0 {
				return false
			}
		}
		return true
	}
	return true
}

// TODO(tamird): this is the same as netAddrToString in ifconfig.
func netAddressToString(addr net.IpAddress) string {
	return fidlconv.ToTCPIPAddress(addr).String()
}

func visit(node *io.NodeInterface, name string, indent int) error {
	info, err := node.Describe()
	if err != nil {
		return errors.Wrapf(err, "fuchsia.io.Node.Describe(%s)", name)
	}
	switch info.Which() {
	case io.NodeInfoFile:
		file := io.FileInterface{Channel: node.Channel}
		status, bytes, err := file.Read(math.MaxUint64)
		if err != nil {
			return errors.Wrapf(err, "fuchsia.io.File.Read(%s)", name)
		}
		if zx.Status(status) != zx.ErrOk {
			return errors.Wrapf(zx.Error{Status: zx.Status(status)}, "fuchsia.io.File.Read(%s)", name)
		}
		for i := 0; i < indent; i++ {
			fmt.Print(" ")
		}
		fmt.Printf("%s: %d\n", name, binary.LittleEndian.Uint64(bytes))
	case io.NodeInfoDirectory:
		dir := io.DirectoryInterface{Channel: node.Channel}
		status, dirents, err := dir.ReadDirents(math.MaxInt32)
		if err != nil {
			return errors.Wrapf(err, "fuchsia.io.Directory.ReadDirents(%s)", name)
		}
		if zx.Status(status) != zx.ErrOk {
			return errors.Wrapf(zx.Error{Status: zx.Status(status)}, "fuchsia.io.Directory.ReadDirents(%s)", name)
		}
		n, _, names := syscall.ParseDirent(dirents, -1, nil)
		if l := len(dirents); n != l {
			return errors.Errorf("syscall.ParseDirents(%s) = %d/%d", name, n, l)
		}
		for i := 0; i < indent; i++ {
			fmt.Print(" ")
		}
		fmt.Printf("%s:\n", name)
		for _, name := range names {
			req, node, err := io.NewNodeInterfaceRequest()
			if err != nil {
				return errors.Wrap(err, "fuchsia.io.NewNodeInterfaceRequest")
			}
			if err := dir.Open(0, 0, name, req); err != nil {
				return errors.Wrapf(err, "fuchsia.io.Directory.Open(%s)", name)
			}
			if err := visit(node, name, indent+2); err != nil {
				return err
			}
		}
	default:
		return errors.Errorf("unexpected node info: %+v", info)
	}
	return nil
}

func main() {
	ctx := context.CreateFromStartupInfo()

	flags := flag.NewFlagSet(os.Args[0], flag.ContinueOnError)
	showRouteTables := flags.Bool("r", false, "Dump the Route Tables")
	showStats := flags.Bool("s", false, "Show network statistics")
	chosenInterface := flags.String("interface", "", "Choose an interface")

	if err := func() error {
		if err := flags.Parse(os.Args[1:]); err != nil {
			return errors.Wrapf(err, "flag.FlagSet.Parse(%s)", os.Args[1:])
		}

		req, netstack, err := netstack.NewNetstackInterfaceRequest()
		if err != nil {
			return errors.Wrap(err, "fuchsia.netstack.NewNetstackInterfaceRequest")
		}
		defer netstack.Close()

		ctx.ConnectToEnvService(req)

		if *showRouteTables {
			if len(*chosenInterface) > 0 {
				fmt.Println("scoping route table to interface not supported, printing all routes:")
			}
			entries, err := netstack.GetRouteTable2()
			if err != nil {
				return errors.Wrap(err, "fuchsia.netstack.GetRouteTable")
			}
			for _, entry := range entries {
				if netAddressZero(entry.Destination) {
					if entry.Gateway != nil && !netAddressZero(*entry.Gateway) {
						fmt.Printf("default via %s, ", netAddressToString(*entry.Gateway))
					} else {
						fmt.Printf("default through ")
					}
				} else {
					fmt.Printf("Destination: %s, ", netAddressToString(entry.Destination))
					fmt.Printf("Mask: %s, ", netAddressToString(entry.Netmask))
					if entry.Gateway != nil && !netAddressZero(*entry.Gateway) {
						fmt.Printf("Gateway: %s, ", netAddressToString(*entry.Gateway))
					}
				}
				fmt.Printf("NICID: %d Metric: %v\n", entry.Nicid, entry.Metric)
			}
		}
		if *showStats {
			if len(*chosenInterface) > 0 {
				nics, err := netstack.GetInterfaces2()
				if err != nil {
					return errors.Wrap(err, "fuchsia.netstack.GetInterfaces")
				}
				knownInterfaces := make([]string, 0, len(nics))
				for _, iface := range nics {
					if strings.HasPrefix(iface.Name, *chosenInterface) {
						s, err := netstack.GetStats(iface.Id)
						if err != nil {
							return errors.Wrap(err, "fuchsia.netstack.GetStats")
						}
						fmt.Println(netInterfaceStats(s).String())
						return nil
					}
					knownInterfaces = append(knownInterfaces, iface.Name)
				}

				return fmt.Errorf("no interface matched %s in %s", *chosenInterface, knownInterfaces)
			} else {
				req, stats, err := io.NewNodeInterfaceRequest()
				if err != nil {
					return errors.Wrap(err, "fuchsia.io.NewNodeInterfaceRequest")
				}
				if err := netstack.GetAggregateStats(req); err != nil {
					return errors.Wrap(err, "fuchsia.netstack.GetAggregateStats")
				}
				if err := visit(stats, "AggregateStats", 0); err != nil {
					return err
				}
			}
		}
		return nil
	}(); err != nil {
		if _, err := fmt.Fprintln(os.Stderr, err); err != nil {
			panic(err)
		}
		os.Exit(2)
	}
}
