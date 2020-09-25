// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package main

import (
	"context"
	"fmt"
	"log"
	"net"
	"strings"
	"time"

	"go.fuchsia.dev/fuchsia/tools/net/mdns"
	"go.fuchsia.dev/fuchsia/tools/net/netboot"
)

type deviceFinderBase struct {
	cmd *devFinderCmd
}

type mdnsFinder struct {
	deviceFinderBase
}

const fuchsiaMDNSService = "_fuchsia._udp.local"

func parseAnswer(cmd *devFinderCmd, a mdns.Record, resp mDNSResponse) *fuchsiaDevice {
	if a.Class == mdns.IN && (a.Type == mdns.A || a.Type == mdns.AAAA) &&
		(len(a.Data) == net.IPv4len || len(a.Data) == net.IPv6len) {
		localStr := ".local"
		strIdx := strings.Index(a.Domain, localStr)
		// Determines if ".local" is on the end of the string.
		if strIdx == -1 || strIdx != len(a.Domain)-len(localStr) {
			return nil
		}
		// Trims off localStr for proper formatting.
		fuchsiaDomain := a.Domain[:strIdx]
		if len(fuchsiaDomain) == 0 {
			return &fuchsiaDevice{err: fmt.Errorf("fuchsia domain empty: %q", a.Domain)}
		}
		var zone string
		if isIPv6LinkLocal(a.Data) {
			zone = resp.rxIface.Name
		}
		fdev := &fuchsiaDevice{
			addr:   net.IP(a.Data),
			domain: fuchsiaDomain,
			zone:   zone,
		}
		// If not ignoring a NAT, update the address to use the source addr.
		if !cmd.ignoreNAT {
			ip, zone, err := addrToIP(resp.devAddr)
			if err != nil {
				return &fuchsiaDevice{err: err}
			}
			fdev.addr = ip
			fdev.zone = zone
		}
		if cmd.localResolve {
			var err error
			fdev, err = fdev.outbound()
			if err != nil {
				return &fuchsiaDevice{err: err}
			}
		}
		return fdev
	}
	return nil
}

func listMDNSHandler(cmd *devFinderCmd, resp mDNSResponse, f chan<- *fuchsiaDevice) {
	if len(resp.rxPacket.Answers) == 0 {
		return
	}
	a := resp.rxPacket.Answers[0]
	if a.Class != mdns.IN || a.Type != mdns.PTR || a.Domain != fuchsiaMDNSService {
		return
	}
	// fxbug.dev/6296: Some protection against malformed responses.
	if len(a.Data) == 0 {
		log.Print("Empty data in response. Ignoring...")
		return
	}
	nameLength := int(a.Data[0])
	if len(a.Data) < nameLength+1 {
		log.Printf("Too short data in response. Got %d bytes; expected %d", len(a.Data), nameLength+1)
		return
	}

	for _, a := range resp.rxPacket.Additional {
		fdev := parseAnswer(cmd, a, resp)
		if fdev != nil {
			f <- fdev
		}
	}
}

func (m *mdnsFinder) list(ctx context.Context, f chan *fuchsiaDevice) error {
	listPacket := mdns.Packet{
		Header: mdns.Header{QDCount: 1},
		Questions: []mdns.Question{
			{
				Domain:  fuchsiaMDNSService,
				Type:    mdns.PTR,
				Class:   mdns.IN,
				Unicast: false,
			},
		},
	}
	return m.cmd.sendMDNSPacket(ctx, listPacket, f)
}

func resolveMDNSHandler(cmd *devFinderCmd, resp mDNSResponse, f chan<- *fuchsiaDevice) {
	for _, a := range resp.rxPacket.Answers {
		if a.Type == mdns.A && !cmd.ipv4 || a.Type == mdns.AAAA && !cmd.ipv6 {
			continue
		}
		fdev := parseAnswer(cmd, a, resp)
		if fdev != nil {
			f <- fdev
		}
	}
}

func (m *mdnsFinder) resolve(ctx context.Context, f chan *fuchsiaDevice, domains ...string) error {
	for _, domain := range domains {
		mdnsDomain := fmt.Sprintf("%s.local", domain)
		if err := m.cmd.sendMDNSPacket(ctx, mdns.QuestionPacket(mdnsDomain), f); err != nil {
			return err
		}
	}
	return nil
}

func (m *mdnsFinder) close() {}

type netbootFinder struct {
	deviceFinderBase
}

func (n *netbootFinder) list(ctx context.Context, f chan *fuchsiaDevice) error {
	if !n.cmd.ipv6 {
		return fmt.Errorf("netboot finder is ipv6 only")
	}
	return n.resolve(ctx, f, netboot.NodenameWildcard)
}

func (n *netbootFinder) resolve(ctx context.Context, f chan *fuchsiaDevice, nodenames ...string) error {
	if !n.cmd.ipv6 {
		return fmt.Errorf("netboot finder is ipv6 only")
	}
	ctx, cancel := context.WithTimeout(ctx, n.cmd.timeout)
	// Timeout isn't really used for this application of the client, so
	// just pick an arbitrary timeout.
	c := n.cmd.newNetbootClient(time.Second)
	t := make(chan *netboot.Target, 1024)
	for _, nodename := range nodenames {
		cleanup, err := c.StartDiscover(t, nodename, !n.cmd.useNetsvcAddress)
		if err != nil {
			return fmt.Errorf("netboot client setup: %v", err)
		}
		go func() {
			defer cancel()
			defer cleanup()
			for {
				select {
				case target := <-t:
					if target.Error != nil {
						f <- &fuchsiaDevice{
							err: target.Error,
						}
						return
					}
					zone := ""
					if target.Interface != nil {
						zone = target.Interface.Name
					}
					addr := target.TargetAddress
					fdev := &fuchsiaDevice{
						addr:   addr,
						domain: target.Nodename,
						zone:   zone,
					}
					if n.cmd.localResolve {
						var err error
						fdev, err = fdev.outbound()
						if err != nil {
							f <- &fuchsiaDevice{err: err}
							return
						}
					}
					f <- fdev
				case <-ctx.Done():
					return
				}
			}
		}()
	}
	return nil
}

func (n *netbootFinder) close() {}
