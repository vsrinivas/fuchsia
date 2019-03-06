// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package botanist

import (
	"context"
	"fmt"
	"net"
	"time"

	"fuchsia.googlesource.com/tools/logger"
	"fuchsia.googlesource.com/tools/mdns"
	"fuchsia.googlesource.com/tools/retry"
)

// Interval at which ResolveIP will wait for a response to a question packet.
const mDNSTimeout time.Duration = 2 * time.Second

func getLocalDomain(nodename string) string {
	return nodename + ".local"
}

// ResolveIP returns the IPv4 address of a fuchsia node via mDNS.
//
// TODO(joshuaseaton): Refactor dev_finder to share 'resolve' logic with botanist.
func ResolveIPv4(ctx context.Context, nodename string, timeout time.Duration) (net.IP, error) {
	var m mdns.MDNS
	out := make(chan net.IP)
	domain := getLocalDomain(nodename)
	m.AddHandler(func(iface net.Interface, addr net.Addr, packet mdns.Packet) {
		for _, a := range packet.Answers {
			if a.Class == mdns.IN && a.Type == mdns.A && a.Domain == domain {
				out <- net.IP(a.Data)
				return
			}
		}
	})
	m.AddWarningHandler(func(addr net.Addr, err error) {
		logger.Infof(ctx, "from: %v; warn: %v", addr, err)
	})
	errs := make(chan error)
	m.AddErrorHandler(func(err error) {
		errs <- err
	})

	ctx, cancel := context.WithTimeout(ctx, timeout)
	defer cancel()

	if err := m.Start(ctx, mdns.DefaultPort); err != nil {
		return nil, fmt.Errorf("could not start mDNS client: %v", err)
	}

	// Send question packets to the mDNS server at intervals of mDNSTimeout for a total of
	// |timeout|; retry, as it takes time for the netstack and server to be brought up.
	var ip net.IP
	var err error
	err = retry.Retry(ctx, &retry.ZeroBackoff{}, func() error {
		m.Send(mdns.QuestionPacket(domain))
		ctx, cancel := context.WithTimeout(context.Background(), mDNSTimeout)
		defer cancel()

		select {
		case <-ctx.Done():
			return fmt.Errorf("timeout")
		case err = <-errs:
			return err
		case ip = <-out:
			return nil
		}
	}, nil)

	return ip, err
}
