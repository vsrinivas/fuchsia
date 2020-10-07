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
	"runtime"
	"sort"
	"strconv"
	"strings"
	"time"

	"go.fuchsia.dev/fuchsia/tools/net/mdns"
	"go.fuchsia.dev/fuchsia/tools/net/netboot"
)

type noConnectableAddressError struct {
	lastConnectionError error
}

func (n noConnectableAddressError) Error() string {
	return fmt.Sprintf("unable to connect to any address. last err: %v", n.lastConnectionError)
}

func (n noConnectableAddressError) Is(other error) bool {
	otherConv, ok := other.(noConnectableAddressError)
	if !ok {
		return false
	}
	return errors.Is(n.lastConnectionError, otherConv.lastConnectionError)
}

type mDNSResponse struct {
	devAddr  net.Addr
	rxPacket mdns.Packet
}

type mDNSHandler func(*devFinderCmd, mDNSResponse, chan<- *fuchsiaDevice)

type mdnsInterface interface {
	AddHandler(f func(net.Addr, mdns.Packet))
	AddWarningHandler(f func(net.Addr, error))
	AddErrorHandler(f func(error))
	SendTo(packet mdns.Packet, dst *net.UDPAddr) error
	Send(packet mdns.Packet) error
	Start(ctx context.Context, port int) error
}

type newMDNSFunc func(address string) mdnsInterface

type netbootClientInterface interface {
	StartDiscover(chan<- *netboot.Target, string) (func() error, error)
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
	// The timeout to either give up or to exit the program after finding at
	// least one device.
	timeout time.Duration
	// Determines whether to return the address of the address of the interface that
	// established a connection to the Fuchsia device (rather than the address of the
	// Fuchsia device on its own).
	localResolve bool
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
	ipv4 bool
	ipv6 bool

	// Used to visit flags after parsing, necessary for complex if-set logic.
	flagSet *flag.FlagSet

	mdnsHandler mDNSHandler
	finders     []deviceFinder

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

// outbound returns a copy of the device to containing the preferred outbound
// connection, as is requested when using the `--local` flag.
func (f *fuchsiaDevice) outbound() (*fuchsiaDevice, error) {
	var udpProto string
	if f.addr.To4() != nil {
		udpProto = "udp4"
	} else {
		udpProto = "udp6"
	}
	// This is just dialing a nonsense port. No packets are being sent.
	tmpConn, err := net.DialUDP(udpProto, nil, &net.UDPAddr{IP: f.addr, Zone: f.zone, Port: 22})
	if err != nil {
		return nil, fmt.Errorf("getting output ip of %q: %b", f.domain, err)
	}
	defer tmpConn.Close()
	localAddr := tmpConn.LocalAddr().(*net.UDPAddr)
	return &fuchsiaDevice{
		domain: f.domain,
		addr:   localAddr.IP,
		zone:   localAddr.Zone,
	}, nil
}

const (
	mdnsFlag    = "mdns"
	netbootFlag = "netboot"
)

func (cmd *devFinderCmd) SetCommonFlags(f *flag.FlagSet) {
	cmd.flagSet = f
	f.BoolVar(&cmd.json, "json", false, "Outputs in JSON format.")
	f.StringVar(&cmd.mdnsAddrs, "addr", "224.0.0.251,ff02::fb", "[linux only] Comma separated list of addresses to issue mDNS queries to.")
	f.StringVar(&cmd.mdnsPorts, "port", "5353", "[linux only] Comma separated list of ports to issue mDNS queries to.")
	f.DurationVar(&cmd.timeout, "timeout", time.Second, "The duration before declaring a timeout.")
	f.BoolVar(&cmd.localResolve, "local", false, "Returns the address of the interface to the host when doing service lookup/domain resolution.")
	f.IntVar(&cmd.deviceLimit, "device-limit", 0, "Exits before the timeout at this many devices per resolution (zero means no limit).")
	f.IntVar(&cmd.ttl, "ttl", -1, "[linux only] Sets the TTL for outgoing mcast messages. Primarily for debugging and testing. Setting this to zero limits messages to the localhost.")
	f.BoolVar(&cmd.mdns, mdnsFlag, true, "Determines whether to use mDNS protocol")
	f.BoolVar(&cmd.netboot, netbootFlag, false, "Determines whether to use netboot protocol")
	f.BoolVar(&cmd.ipv6, "ipv6", true, "Set whether to query using IPv6. Disabling IPv6 will also disable netboot.")
	f.BoolVar(&cmd.ipv4, "ipv4", true, "Set whether to query using IPv4")
}

func (cmd *devFinderCmd) Output() io.Writer {
	if cmd.output == nil {
		return os.Stdout
	}
	return cmd.output
}

func (cmd *devFinderCmd) newMDNS(address string) mdnsInterface {
	if cmd.newMDNSFunc != nil {
		return cmd.newMDNSFunc(address)
	}
	m := mdns.NewMDNS()
	if !cmd.ipv4 && !cmd.ipv6 {
		log.Fatalf("either --ipv4 or --ipv6 must be set to true")
	}
	// Ultimately there can be only one MDNS per address, so either it is
	// enabled somewhere in here or nil is returned.
	ip := net.ParseIP(address)
	if ip.To4() == nil {
		if cmd.ipv6 {
			m.EnableIPv6()
		} else {
			return nil
		}
	} else {
		if cmd.ipv4 {
			m.EnableIPv4()
		} else {
			return nil
		}
	}
	m.SetAddress(address)
	if err := m.SetMCastTTL(cmd.ttl); err != nil {
		log.Fatalf("unable to set mcast TTL: %s", err)
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

func startMDNSHandlers(ctx context.Context, cmd *devFinderCmd, packet mdns.Packet, addrs []string, ports []int, f chan *fuchsiaDevice) error {
	startedMDNS := false
	var lastErr error
	for _, addr := range addrs {
		for _, p := range ports {
			m := cmd.newMDNS(addr)
			m.AddHandler(func(addr net.Addr, rxPacket mdns.Packet) {
				response := mDNSResponse{devAddr: addr, rxPacket: rxPacket}
				cmd.mdnsHandler(cmd, response, f)
			})
			m.AddErrorHandler(func(err error) {
				f <- &fuchsiaDevice{err: err}
			})
			m.AddWarningHandler(func(addr net.Addr, err error) {
				log.Printf("from: %s warn: %s\n", addr, err)
			})
			if err := m.Start(ctx, p); err != nil {
				lastErr = fmt.Errorf("starting mdns: %w", err)
				continue
			}
			if err := m.Send(packet); err != nil {
				lastErr = fmt.Errorf("sending mdns: %w", err)
				continue
			}
			startedMDNS = true
		}
	}
	if startedMDNS {
		return nil
	}
	return noConnectableAddressError{lastConnectionError: lastErr}
}

func (cmd *devFinderCmd) sendMDNSPacket(ctx context.Context, packet mdns.Packet, f chan *fuchsiaDevice) error {
	if cmd.mdnsHandler == nil {
		return fmt.Errorf("packet handler is nil")
	}
	if cmd.timeout <= 0 {
		return fmt.Errorf("invalid timeout value: %s", cmd.timeout)
	}

	cmdAddrs := strings.Split(cmd.mdnsAddrs, ",")
	var ports []int
	for _, s := range strings.Split(cmd.mdnsPorts, ",") {
		p, err := strconv.ParseUint(s, 10, 16)
		if err != nil {
			return fmt.Errorf("could not parse port number %s: %w\n", s, err)
		}
		ports = append(ports, int(p))
	}
	if len(ports) == 0 {
		return fmt.Errorf("no viable ports from %q", cmd.mdnsAddrs)
	}
	var addrs []string
	for _, addr := range cmdAddrs {
		ip := net.ParseIP(addr)
		if ip == nil {
			return fmt.Errorf("%q not a valid IP", addr)
		}
		if cmd.shouldIgnoreIP(ip) {
			continue
		}
		addrs = append(addrs, addr)
	}
	if len(addrs) == 0 {
		return fmt.Errorf("no viable addresses")
	}
	return startMDNSHandlers(ctx, cmd, packet, addrs, ports, f)
}

type deviceFinder interface {
	list(context.Context, chan *fuchsiaDevice) error
	resolve(context.Context, chan *fuchsiaDevice, ...string) error
	close()
}

func (cmd *devFinderCmd) close() {
	for _, finder := range cmd.finders {
		finder.close()
	}
}

func (cmd *devFinderCmd) deviceFinders() ([]deviceFinder, error) {
	if len(cmd.finders) == 0 {
		{
			mdnsSet := false
			netbootSet := false
			cmd.flagSet.Visit(func(f *flag.Flag) {
				switch f.Name {
				case mdnsFlag:
					mdnsSet = true
				case netbootFlag:
					netbootSet = true
				}
			})
			if !mdnsSet {
				if netbootSet {
					// Turn off the mDNS default if only netboot is specified.
					cmd.mdns = false
				} else if cmd.localResolve {
					// Local resolution produces the same result regardless of protocol; default to enabling
					// both if neither is specified.
					cmd.netboot = true
				}
			}
		}
		if !cmd.localResolve && cmd.netboot && cmd.mdns {
			return nil, errors.New("only one of mdns and netboot may be specified")
		}
		if cmd.mdns {
			if runtime.GOOS == "darwin" {
				cmd.finders = append(cmd.finders, newDNSSDFinder(cmd))
			} else {
				cmd.finders = append(cmd.finders, &mdnsFinder{deviceFinderBase{cmd: cmd}})
			}
		}
		if cmd.netboot && cmd.ipv6 {
			cmd.finders = append(cmd.finders, &netbootFinder{deviceFinderBase{cmd: cmd}})
		} else if !cmd.mdns {
			return nil, errors.New("either mdns or netboot and ipv6 must be specified")
		}
	}
	return cmd.finders, nil
}

func (cmd *devFinderCmd) shouldIgnoreIP(addr net.IP) bool {
	return addr.To4() != nil && !cmd.ipv4 || addr.To4() == nil && !cmd.ipv6
}

// filterInboundDevices takes a context and a channel (which has already been passed to some setup
// code that will be writing into it asynchronously), and reads inbound fuchsiaDevice objects
// until a timeout is reached.
//
// This applies all base command filters.
//
// This function executes synchronously.
func (cmd *devFinderCmd) filterInboundDevices(ctx context.Context, f <-chan *fuchsiaDevice, domains ...string) ([]*fuchsiaDevice, error) {
	ctx, cancel := context.WithTimeout(ctx, cmd.timeout)
	defer cancel()
	defer cmd.close()
	devices := make(map[string]*fuchsiaDevice)
	resolveDomains := make(map[string]struct{})
	for _, d := range domains {
		resolveDomains[d] = struct{}{}
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
				if _, ok := resolveDomains[d.domain]; ok {
					res = append(res, d)
				}
			}
			return res, nil
		case device := <-f:
			if err := device.err; err != nil {
				return nil, err
			}
			if cmd.shouldIgnoreIP(device.addr) {
				continue
			}
			if _, ok := resolveDomains[device.domain]; ok || len(resolveDomains) == 0 {
				devices[device.domain] = device
			}
			if cmd.deviceLimit != 0 && len(devices) == cmd.deviceLimit {
				return sortDeviceMap(devices), nil
			}
		}
	}
}

func (cmd *devFinderCmd) outputNormal(filteredDevices []*fuchsiaDevice, includeDomain bool) error {
	for _, device := range filteredDevices {
		if includeDomain {
			fmt.Fprintf(cmd.Output(), "%s %s\n", device.addrString(), device.domain)
		} else {
			fmt.Fprintf(cmd.Output(), "%s\n", device.addrString())
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
