// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package botanist

import (
	"context"
	"fmt"
	"net"
	"time"

	"github.com/kr/pretty"

	"go.fuchsia.dev/fuchsia/tools/lib/logger"
	"go.fuchsia.dev/fuchsia/tools/net/mdns"
)

// Interval at which ResolveIP will wait for a response to a question packet.
const mDNSTimeout time.Duration = 2 * time.Second

func getLocalDomain(nodename string) string {
	return nodename + ".local"
}

// ResolveIP returns an IP address of a fuchsia node via mDNS.
//
// TODO(joshuaseaton): Refactor dev_finder to share 'resolve' logic with botanist.
func ResolveIP(ctx context.Context, nodename string) (net.IP, net.IPAddr, error) {
	m := mdns.NewMDNS()
	defer m.Close()
	m.EnableIPv4()
	m.EnableIPv6()
	out := make(chan net.IPAddr, 1)
	domain := getLocalDomain(nodename)
	m.AddHandler(func(addr net.Addr, packet mdns.Packet) {
		logger.Debugf(ctx, "mdns packet from %s: %# v", addr, pretty.Formatter(packet))
		var zone string
		switch addr := addr.(type) {
		case *net.IPAddr:
			zone = addr.Zone
		case *net.UDPAddr:
			zone = addr.Zone
		}
		for _, records := range [][]mdns.Record{
			packet.Answers,
			packet.Additional,
		} {
			for _, record := range records {
				if record.Class == mdns.IN && record.Domain == domain {
					switch record.Type {
					case mdns.A, mdns.AAAA:
						out <- net.IPAddr{
							IP:   net.IP(record.Data),
							Zone: zone,
						}
						return
					}
				}
			}
		}
	})
	m.AddWarningHandler(func(addr net.Addr, err error) {
		logger.Infof(ctx, "from: %s; warn: %s", addr, err)
	})
	errs := make(chan error, 1)
	m.AddErrorHandler(func(err error) {
		errs <- err
	})

	ctx, cancel := context.WithCancel(ctx)
	defer cancel()

	if err := m.Start(ctx, mdns.DefaultPort); err != nil {
		return nil, net.IPAddr{}, fmt.Errorf("could not start mDNS client: %w", err)
	}

	var ipv4Addr net.IP
	var ipv6Addr net.IPAddr
	var earlyStop <-chan time.Time
	t := time.NewTicker(mDNSTimeout)
	defer t.Stop()
	for {
		if err := m.Send(ctx, mdns.QuestionPacket(domain)); err != nil {
			return nil, net.IPAddr{}, fmt.Errorf("could not send mDNS question: %w", err)
		}

		for {
			select {
			case <-ctx.Done():
				return ipv4Addr, ipv6Addr, nil
			case err := <-errs:
				return ipv4Addr, ipv6Addr, err
			case addr := <-out:
				switch len(addr.IP) {
				case net.IPv4len:
					ipv4Addr = addr.IP
				case net.IPv6len:
					ipv6Addr = addr
				}
				if ipv4Addr != nil && ipv6Addr.IP != nil {
					return ipv4Addr, ipv6Addr, nil
				}
				if earlyStop == nil {
					earlyStop = time.After(mDNSTimeout / 2)
				}
				continue
			case <-earlyStop:
				return ipv4Addr, ipv6Addr, nil
			case <-t.C:
				// Resend question.
			}
			break
		}
	}
}
