// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package main

import (
	"context"
	"flag"
	"fmt"
	"log"
	"net"
	"os"
	"time"

	"go.fuchsia.dev/fuchsia/tools/net/mdns"
)

// TODO(jakehehrlich): This doesn't retry or anything, it just times out. It would
// be nice to make this more robust.
// sends out a request for the ip of domain. Waits for up to |dur| amount of time.
// Will stop waiting for a response if ctx is done. The default port for mDNS is
// 5353 but you're allowed to specify via port.
func mDNSResolve(ctx context.Context, domain string, port int, dur time.Duration) (net.IP, error) {
	m := mdns.NewMDNS()
	m.EnableIPv4()
	m.EnableIPv6()
	out := make(chan net.IP)
	// Add all of our handlers
	m.AddHandler(func(iface net.Interface, addr net.Addr, packet mdns.Packet) {
		for _, a := range packet.Answers {
			if a.Class != mdns.IN || a.Domain != domain {
				continue
			}
			if a.Type == mdns.A || a.Type == mdns.AAAA {
				out <- net.IP(a.Data)
				return
			}
		}
	})
	m.AddWarningHandler(func(addr net.Addr, err error) {
		log.Printf("from: %v warn: %v", addr, err)
	})
	errs := make(chan error)
	m.AddErrorHandler(func(err error) {
		errs <- err
	})
	// Before we start we need to add a context to timeout with
	ctx, cancel := context.WithTimeout(ctx, dur)
	defer cancel()
	// Start up the mdns loop
	if err := m.Start(ctx, port); err != nil {
		return nil, fmt.Errorf("starting mdns: %v", err)
	}
	// Send a packet requesting an answer to "what is the IP of |domain|?"
	m.Send(mdns.QuestionPacket(domain))
	// Now wait for either a timeout, an error, or an answer.
	select {
	case <-ctx.Done():
		return nil, fmt.Errorf("timeout")
	case err := <-errs:
		return nil, err
	case ip := <-out:
		return ip, nil
	}
}

// TODO(jakehehrlich): Add support for unicast.
// mDNSPublish will respond to requests for the ip of domain by responding with ip.
// Note that this responds on both IPv4 and IPv6 interfaces, independent on the type
// of ip itself. You can stop the server by canceling ctx. Even though mDNS is
// generally on 5353 you can specify any port via port.
func mDNSPublish(ctx context.Context, domain string, port int, ip net.IP) error {
	// Now create and mDNS server
	m := mdns.NewMDNS()
	m.EnableIPv4()
	m.EnableIPv6()
	addrType := mdns.IpToDnsRecordType(ip)
	m.AddHandler(func(iface net.Interface, addr net.Addr, packet mdns.Packet) {
		log.Printf("from %v packet %v", addr, packet)
		for _, q := range packet.Questions {
			if q.Class == mdns.IN && q.Type == addrType && q.Domain == domain {
				// We ignore the Unicast bit here but in theory this could be handled via SendTo and addr.
				m.Send(mdns.AnswerPacket(domain, ip))
			}
		}
	})
	m.AddWarningHandler(func(addr net.Addr, err error) {
		log.Printf("from: %v warn: %v", addr, err)
	})
	errs := make(chan error)
	m.AddErrorHandler(func(err error) {
		errs <- err
	})
	// Now start the server.
	if err := m.Start(ctx, port); err != nil {
		return fmt.Errorf("starting mdns: %v", err)
	}
	// Now wait for either a timeout, an error, or an answer.
	select {
	case <-ctx.Done():
		return nil
	case err := <-errs:
		return err
	}
}

// This function makes a faulty assumption. It assumes that the first
// multicast interface it finds with an ipv4 address will be the
// address the user wants. There isn't really a way to guess exactly
// the address that the user will want. If an IPv6 address is needed, then
// using the -ip <address> option is required.
func getMulticastIP() net.IP {
	ifaces, err := net.Interfaces()
	if err != nil {
		return nil
	}
	for _, i := range ifaces {
		if i.Flags&net.FlagMulticast == 0 {
			continue
		}
		addrs, err := i.Addrs()
		if err != nil {
			return nil
		}
		for _, addr := range addrs {
			switch v := addr.(type) {
			case *net.IPNet:
				if ip4 := v.IP.To4(); ip4 != nil {
					return ip4
				}
			}
		}
	}
	return nil
}

var (
	port    int
	timeout time.Duration
	ipAddr  string
)

func init() {
	flag.StringVar(&ipAddr, "ip", "", "the ip to respond with when serving.")
	flag.IntVar(&port, "port", 5353, "the port your mDNS servers operate on")
	flag.DurationVar(&timeout, "timeout", 2*time.Second, "the duration before declaring a timeout")
}

func publish(args ...string) error {
	if len(args) < 1 {
		return fmt.Errorf("missing domain to serve")
	}
	var ip net.IP
	if ipAddr == "" {
		ip = getMulticastIP()
		if ip = getMulticastIP(); ip == nil {
			return fmt.Errorf("could not find a suitable ip")
		}
	} else {
		if ip = net.ParseIP(ipAddr); ip == nil {
			return fmt.Errorf("'%s' is not a valid ip address", ipAddr)
		}
	}
	domain := args[0]
	if err := mDNSPublish(context.Background(), domain, port, ip); err != nil {
		return err
	}
	return nil
}

func resolve(args ...string) error {
	if len(args) < 1 {
		return fmt.Errorf("missing domain to request")
	}
	domain := args[0]
	ip, err := mDNSResolve(context.Background(), domain, port, timeout)
	if err != nil {
		return err
	}
	fmt.Printf("%v\n", ip)
	return nil
}

func main() {
	flag.Parse()
	args := flag.Args()
	if len(args) < 1 {
		log.Printf("error: no command given")
		os.Exit(1)
	}
	mp := map[string]func(...string) error{
		"publish": publish,
		"resolve": resolve,
	}
	if f, ok := mp[args[0]]; ok {
		if err := f(args[1:]...); err != nil {
			log.Printf("error: %v", err)
		}
		return
	} else {
		log.Printf("error: %s is not a command", args[0])
	}
	os.Exit(1)
}
