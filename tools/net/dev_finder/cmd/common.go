// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
package main

import (
	"context"
	"encoding/json"
	"errors"
	"flag"
	"fmt"
	"io"
	"log"
	"net"
	"os"
	"sort"
	"strconv"
	"strings"
	"time"

	"go.fuchsia.dev/fuchsia/tools/net/mdns"
	"go.fuchsia.dev/fuchsia/tools/net/netboot"
)

type mDNSResponse struct {
	rxIface  net.Interface
	devAddr  net.Addr
	rxPacket mdns.Packet
}

func (m *mDNSResponse) getReceiveIP() (net.IP, error) {
	if unicastAddrs, err := m.rxIface.Addrs(); err != nil {
		return nil, err
	} else {
		for _, addr := range unicastAddrs {
			var ip net.IP
			switch v := addr.(type) {
			case *net.IPNet:
				ip = v.IP
			case *net.IPAddr:
				ip = v.IP
			}
			if ip == nil || ip.To4() == nil {
				continue
			}
			return ip, nil
		}
	}
	return nil, fmt.Errorf("no IPv4 unicast addresses found on iface %v", m.rxIface)
}

type mDNSHandler func(mDNSResponse, bool, chan<- *fuchsiaDevice)

type mdnsInterface interface {
	AddHandler(f func(net.Interface, net.Addr, mdns.Packet))
	AddWarningHandler(f func(net.Addr, error))
	AddErrorHandler(f func(error))
	SendTo(packet mdns.Packet, dst *net.UDPAddr) error
	Send(packet mdns.Packet) error
	Start(ctx context.Context, port int) error
}

type newMDNSFunc func(address string) mdnsInterface

type netbootClientInterface interface {
	StartDiscover(chan<- *netboot.Target, string, bool) (func() error, error)
}

type newNetbootFunc func(timeout time.Duration) netbootClientInterface

// Contains common command information for embedding in other dev_finder commands.
type devFinderCmd struct {
	// Outputs in JSON format if true.
	json bool
	// The mDNS addresses to connect to.
	mdnsAddrs string
	// The mDNS ports to connect to.
	mdnsPorts string
	// The timeout in ms to either give up or to exit the program after finding at least one
	// device.
	timeout int
	// Determines whether to return the address of the address of the interface that
	// established a connection to the Fuchsia device (rather than the address of the
	// Fuchsia device on its own).
	localResolve bool
	// Determines whether to accept incoming unicast mDNS responses. This can happen if the
	// receiving device is on a different subnet, or the receiving device's listener port
	// has been forwarded to from a non-standard port.
	acceptUnicast bool
	// The limit of devices to discover. If this number of devices has been discovered before
	// the timeout has been reached the program will exit successfully.
	deviceLimit int
	// The TTL for multicast messages. This is primarily for debugging and testing. Setting
	// this to zero restricts all packets to the host machine. Setting this to a negative
	// number is ignored (continues default behavior). Setting this to greater than
	// 255 is an error.
	ttl int
	// If set to true, uses netboot protocol.
	netboot bool
	// If set to true, uses mdns protocol.
	mdns bool
	// If set to true, uses the netsvc address instead of the netstack
	// address.
	useNetsvcAddress bool

	mdnsHandler mDNSHandler

	// Only for testing.
	newMDNSFunc    newMDNSFunc
	newNetbootFunc newNetbootFunc
	output         io.Writer
}

type fuchsiaDevice struct {
	addr net.IP
	// domain is the nodename of the fuchsia target.
	domain string
	// zone is the IPv6 zone to connect to the target.
	zone string
	err  error
}

func (f *fuchsiaDevice) addrString() string {
	addr := net.IPAddr{IP: f.addr, Zone: f.zone}
	return addr.String()
}

func (cmd *devFinderCmd) SetCommonFlags(f *flag.FlagSet) {
	f.BoolVar(&cmd.json, "json", false, "Outputs in JSON format.")
	f.StringVar(&cmd.mdnsAddrs, "addr", "224.0.0.251,ff02::fb", "Comma separated list of addresses to issue mDNS queries to.")
	f.StringVar(&cmd.mdnsPorts, "port", "5353", "Comma separated list of ports to issue mDNS queries to.")
	f.IntVar(&cmd.timeout, "timeout", 200, "The number of milliseconds before declaring a timeout.")
	f.BoolVar(&cmd.localResolve, "local", false, "Returns the address of the interface to the host when doing service lookup/domain resolution.")
	f.BoolVar(&cmd.acceptUnicast, "accept-unicast", false, "Accepts unicast responses. For if the receiving device responds from a different subnet or behind port forwarding.")
	f.IntVar(&cmd.deviceLimit, "device-limit", 0, "Exits before the timeout at this many devices per resolution (zero means no limit).")
	f.IntVar(&cmd.ttl, "ttl", -1, "Sets the TTL for outgoing mcast messages. Primarily for debugging and testing. Setting this to zero limits messages to the localhost.")
	f.BoolVar(&cmd.netboot, "netboot", true, "Determines whether to use netboot protocol")
	f.BoolVar(&cmd.mdns, "mdns", true, "Determines whether to use mDNS protocol")
	f.BoolVar(&cmd.useNetsvcAddress, "netsvc-address", false, "Determines whether to use the Fuchsia netsvc address. Ignored if |netboot| is set to false.")
}

func (cmd *devFinderCmd) Output() io.Writer {
	if cmd.output == nil {
		return os.Stdout
	}
	return cmd.output
}

// Extracts the IP from its argument, returning an error if the type is unsupported.
func addrToIP(addr net.Addr) (net.IP, error) {
	switch v := addr.(type) {
	case *net.IPNet:
		return v.IP, nil
	case *net.IPAddr:
		return v.IP, nil
	case *net.UDPAddr:
		return v.IP, nil
	}
	return nil, errors.New("unsupported address type")
}

func (cmd *devFinderCmd) newMDNS(address string) mdnsInterface {
	if cmd.newMDNSFunc != nil {
		return cmd.newMDNSFunc(address)
	}
	m := mdns.NewMDNS()
	ip := net.ParseIP(address)
	if ip.To4() != nil {
		m.EnableIPv4()
	} else {
		m.EnableIPv6()
	}
	m.SetAddress(address)
	m.SetAcceptUnicastResponses(cmd.acceptUnicast)
	if err := m.SetMCastTTL(cmd.ttl); err != nil {
		log.Fatalf("unable to set mcast TTL: %v", err)
	}
	return m
}

func (cmd *devFinderCmd) newNetbootClient(timeout time.Duration) netbootClientInterface {
	if cmd.newNetbootFunc != nil {
		return cmd.newNetbootFunc(timeout)
	}
	return netboot.NewClient(timeout)
}

func sortDeviceMap(deviceMap map[string]*fuchsiaDevice) []*fuchsiaDevice {
	keys := make([]string, 0)
	for k := range deviceMap {
		keys = append(keys, k)
	}
	sort.Strings(keys)
	res := make([]*fuchsiaDevice, 0)
	for _, k := range keys {
		res = append(res, deviceMap[k])
	}
	return res
}

func (cmd *devFinderCmd) sendMDNSPacket(ctx context.Context, packet mdns.Packet, f chan *fuchsiaDevice) error {
	if cmd.mdnsHandler == nil {
		return fmt.Errorf("packet handler is nil")
	}
	if cmd.timeout <= 0 {
		return fmt.Errorf("invalid timeout value: %v", cmd.timeout)
	}

	addrs := strings.Split(cmd.mdnsAddrs, ",")
	var ports []int
	for _, s := range strings.Split(cmd.mdnsPorts, ",") {
		p, err := strconv.ParseUint(s, 10, 16)
		if err != nil {
			return fmt.Errorf("could not parse port number %v: %v\n", s, err)
		}
		ports = append(ports, int(p))
	}

	for _, addr := range addrs {
		for _, p := range ports {
			m := cmd.newMDNS(addr)
			m.AddHandler(func(recv net.Interface, addr net.Addr, rxPacket mdns.Packet) {
				response := mDNSResponse{recv, addr, rxPacket}
				cmd.mdnsHandler(response, cmd.localResolve, f)
			})
			m.AddErrorHandler(func(err error) {
				f <- &fuchsiaDevice{err: err}
			})
			m.AddWarningHandler(func(addr net.Addr, err error) {
				log.Printf("from: %v warn: %v\n", addr, err)
			})
			if err := m.Start(ctx, p); err != nil {
				return fmt.Errorf("starting mdns: %v", err)
			}
			m.Send(packet)
		}
	}

	return nil
}

type deviceFinder interface {
	list(context.Context, chan *fuchsiaDevice) error
	resolve(context.Context, chan *fuchsiaDevice, ...string) error
}

func (cmd *devFinderCmd) deviceFinders() []deviceFinder {
	res := make([]deviceFinder, 0)
	if cmd.netboot {
		res = append(res, &netbootFinder{deviceFinderBase{cmd: cmd}})
	}
	if cmd.mdns {
		res = append(res, &mdnsFinder{deviceFinderBase{cmd: cmd}})
	}
	return res
}

// filterInboundDevices takes a context and a channel (which has already been passed to some setup
// code that will be writing into it asynchronously), and reads inbound fuchsiaDevice objects
// until a timeout is reached.
//
// This applies all base command filters.
//
// This function executes synchronously.
func (cmd *devFinderCmd) filterInboundDevices(ctx context.Context, f <-chan *fuchsiaDevice, domains ...string) ([]*fuchsiaDevice, error) {
	ctx, cancel := context.WithTimeout(ctx, time.Duration(cmd.timeout)*time.Millisecond)
	defer cancel()
	devices := make(map[string]*fuchsiaDevice)
	resolveDomains := make(map[string]int)
	for _, d := range domains {
		resolveDomains[d] = 1
	}
	for {
		select {
		case <-ctx.Done():
			devices := sortDeviceMap(devices)
			if len(resolveDomains) == 0 {
				return devices, nil
			}
			res := make([]*fuchsiaDevice, 0)
			for _, d := range devices {
				if resolveDomains[d.domain] == 1 {
					res = append(res, d)
				}
			}
			return res, nil
		case device := <-f:
			if err := device.err; err != nil {
				return nil, err
			}
			devices[device.domain] = device
			if cmd.deviceLimit != 0 && len(devices) == cmd.deviceLimit {
				return sortDeviceMap(devices), nil
			}
		}
	}
}

func (cmd *devFinderCmd) outputNormal(filteredDevices []*fuchsiaDevice, includeDomain bool) error {
	for _, device := range filteredDevices {
		if includeDomain {
			fmt.Fprintf(cmd.Output(), "%v %v\n", device.addrString(), device.domain)
		} else {
			fmt.Fprintf(cmd.Output(), "%v\n", device.addrString())
		}
	}
	return nil
}

// jsonOutput represents the output in JSON format.
type jsonOutput struct {
	// List of devices found.
	Devices []jsonDevice `json:"devices"`
}

type jsonDevice struct {
	// Device IP address.
	Addr string `json:"addr"`
	// Device domain name. Can be omitted.
	Domain string `json:"domain,omitempty"`
}

func (cmd *devFinderCmd) outputJSON(filteredDevices []*fuchsiaDevice, includeDomain bool) error {
	jsonOut := jsonOutput{Devices: make([]jsonDevice, 0, len(filteredDevices))}

	for _, device := range filteredDevices {
		dev := jsonDevice{Addr: device.addrString()}
		if includeDomain {
			dev.Domain = device.domain
		}
		jsonOut.Devices = append(jsonOut.Devices, dev)
	}

	e := json.NewEncoder(cmd.Output())
	e.SetIndent("", "  ")
	return e.Encode(jsonOut)
}
