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

func listMDNSHandler(resp mDNSResponse, localResolve bool, f chan<- *fuchsiaDevice) {
	for _, a := range resp.rxPacket.Answers {
		if a.Class == mdns.IN && a.Type == mdns.PTR {
			// DX-1498: Some protection against malformed responses.
			if len(a.Data) == 0 {
				log.Print("Empty data in response. Ignoring...")
				continue
			}
			nameLength := int(a.Data[0])
			if len(a.Data) < nameLength+1 {
				log.Printf("Too short data in response. Got %d bytes; expected %d", len(a.Data), nameLength+1)
				continue
			}

			// This is a bit convoluted: the domain param is being used
			// as a "service", and the Data field actually contains the
			// domain of the device.
			fuchsiaDomain := string(a.Data[1 : nameLength+1])
			if localResolve {
				recvIP, zone, err := resp.getReceiveIP()
				if err != nil {
					f <- &fuchsiaDevice{err: err}
					return
				}
				f <- &fuchsiaDevice{addr: recvIP, domain: fuchsiaDomain, zone: zone}
				continue
			}
			if ip, zone, err := addrToIP(resp.devAddr); err != nil {
				f <- &fuchsiaDevice{
					err: fmt.Errorf("could not find addr for %v: %v", resp.devAddr, err),
				}
			} else {
				f <- &fuchsiaDevice{
					addr:   ip,
					domain: fuchsiaDomain,
					zone:   zone,
				}
			}
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

func resolveMDNSHandler(resp mDNSResponse, localResolve bool, f chan<- *fuchsiaDevice) {
	for _, a := range resp.rxPacket.Answers {
		if a.Class == mdns.IN && a.Type == mdns.A &&
			len(a.Data) == ipv4AddrLength {
			localStr := ".local"
			strIdx := strings.Index(a.Domain, localStr)
			// Determines if ".local" is on the end of the string.
			if strIdx == -1 || strIdx != len(a.Domain)-len(localStr) {
				continue
			}
			// Trims off localStr for proper formatting.
			fuchsiaDomain := a.Domain[:strIdx]
			if len(fuchsiaDomain) == 0 {
				f <- &fuchsiaDevice{err: fmt.Errorf("fuchsia domain empty: %q", a.Domain)}
				return
			}
			if localResolve {
				recvIP, zone, err := resp.getReceiveIP()
				if err != nil {
					f <- &fuchsiaDevice{err: err}
					return
				}
				f <- &fuchsiaDevice{addr: recvIP, domain: fuchsiaDomain, zone: zone}
				continue
			}
			f <- &fuchsiaDevice{addr: net.IP(a.Data), domain: fuchsiaDomain}
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

type netbootFinder struct {
	deviceFinderBase
}

func (n *netbootFinder) list(ctx context.Context, f chan *fuchsiaDevice) error {
	return n.resolve(ctx, f, netboot.NodenameWildcard)
}

func (n *netbootFinder) resolve(ctx context.Context, f chan *fuchsiaDevice, nodenames ...string) error {
	ctx, cancel := context.WithTimeout(ctx, time.Duration(n.cmd.timeout)*time.Millisecond)
	// Timeout isn't really used for this application of the client, so
	// just pick an arbitrary timeout.
	c := n.cmd.newNetbootClient(time.Second)
	t := make(chan *netboot.Target)
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
					if n.cmd.localResolve {
						addr = target.HostAddress
					}
					f <- &fuchsiaDevice{
						addr:   addr,
						domain: target.Nodename,
						zone:   zone,
					}
				case <-ctx.Done():
					return
				}
			}
		}()
	}
	return nil
}
