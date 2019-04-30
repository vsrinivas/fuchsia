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
	"strconv"
	"strings"
	"time"

	"fuchsia.googlesource.com/tools/mdns"
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

type mDNSHandler func(mDNSResponse, bool, chan<- *fuchsiaDevice, chan<- error)

type mdnsInterface interface {
	AddHandler(f func(net.Interface, net.Addr, mdns.Packet))
	AddWarningHandler(f func(net.Addr, error))
	AddErrorHandler(f func(error))
	SendTo(packet mdns.Packet, dst *net.UDPAddr) error
	Send(packet mdns.Packet) error
	Start(ctx context.Context, port int) error
}

type newMDNSFunc func(address string) mdnsInterface

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
	// The limit of devices to discover. If this number of devices has been discovered before
	// the timeout has been reached the program will exit successfully.
	deviceLimit int

	mdnsHandler mDNSHandler

	// Only for testing.
	newMDNSFunc newMDNSFunc
	output      io.Writer
}

type fuchsiaDevice struct {
	addr   net.IP
	domain string
}

func (cmd *devFinderCmd) SetCommonFlags(f *flag.FlagSet) {
	f.BoolVar(&cmd.json, "json", false, "Outputs in JSON format.")
	f.StringVar(&cmd.mdnsAddrs, "addr", "224.0.0.251,224.0.0.250", "Comma separated list of addresses to issue mDNS queries to.")
	f.StringVar(&cmd.mdnsPorts, "port", "5353,5356", "Comma separated list of ports to issue mDNS queries to.")
	f.IntVar(&cmd.timeout, "timeout", 2000, "The number of milliseconds before declaring a timeout.")
	f.BoolVar(&cmd.localResolve, "local", false, "Returns the address of the interface to the host when doing service lookup/domain resolution.")
	f.IntVar(&cmd.deviceLimit, "device-limit", 0, "Exits before the timeout at this many devices per resolution (zero means no limit).")
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
	return &mdns.MDNS{Address: address}
}

func (cmd *devFinderCmd) sendMDNSPacket(ctx context.Context, packet mdns.Packet) ([]*fuchsiaDevice, error) {
	if cmd.mdnsHandler == nil {
		return nil, fmt.Errorf("packet handler is nil")
	}
	if cmd.timeout <= 0 {
		return nil, fmt.Errorf("invalid timeout value: %v", cmd.timeout)
	}

	addrs := strings.Split(cmd.mdnsAddrs, ",")
	var ports []int
	for _, s := range strings.Split(cmd.mdnsPorts, ",") {
		p, err := strconv.ParseUint(s, 10, 16)
		if err != nil {
			return nil, fmt.Errorf("Could not parse port number %v: %v\n", s, err)
		}
		ports = append(ports, int(p))
	}

	ctx, cancel := context.WithTimeout(ctx, time.Duration(cmd.timeout)*time.Millisecond)
	defer cancel()
	errChan := make(chan error)
	devChan := make(chan *fuchsiaDevice)
	for _, addr := range addrs {
		for _, p := range ports {
			m := cmd.newMDNS(addr)
			m.AddHandler(func(recv net.Interface, addr net.Addr, rxPacket mdns.Packet) {
				response := mDNSResponse{recv, addr, rxPacket}
				cmd.mdnsHandler(response, cmd.localResolve, devChan, errChan)
			})
			m.AddErrorHandler(func(err error) {
				errChan <- err
			})
			m.AddWarningHandler(func(addr net.Addr, err error) {
				log.Printf("from: %v warn: %v\n", addr, err)
			})
			if err := m.Start(ctx, p); err != nil {
				return nil, fmt.Errorf("starting mdns: %v", err)
			}
			m.Send(packet)
		}
	}

	devices := make([]*fuchsiaDevice, 0)
	for {
		select {
		case <-ctx.Done():
			if len(devices) == 0 {
				return nil, fmt.Errorf("timeout")
			}
			return devices, nil
		case err := <-errChan:
			return nil, err
		case device := <-devChan:
			devices = append(devices, device)
			if cmd.deviceLimit != 0 && len(devices) == cmd.deviceLimit {
				return devices, nil
			}
		}
	}
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

func (cmd *devFinderCmd) outputNormal(filteredDevices []*fuchsiaDevice, includeDomain bool) error {
	for _, device := range filteredDevices {
		if includeDomain {
			fmt.Fprintf(cmd.Output(), "%v %v\n", device.addr, device.domain)
		} else {
			fmt.Fprintf(cmd.Output(), "%v\n", device.addr)
		}
	}
	return nil
}

func (cmd *devFinderCmd) outputJSON(filteredDevices []*fuchsiaDevice, includeDomain bool) error {
	jsonOut := jsonOutput{Devices: make([]jsonDevice, 0, len(filteredDevices))}

	for _, device := range filteredDevices {
		dev := jsonDevice{Addr: device.addr.String()}
		if includeDomain {
			dev.Domain = device.domain
		}
		jsonOut.Devices = append(jsonOut.Devices, dev)
	}

	e := json.NewEncoder(cmd.Output())
	e.SetIndent("", "  ")
	return e.Encode(jsonOut)
}
