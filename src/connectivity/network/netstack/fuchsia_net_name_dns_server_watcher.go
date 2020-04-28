// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package netstack

import (
	"fmt"
	"sync"
	"syscall/zx"
	"syscall/zx/dispatch"
	"syscall/zx/fidl"

	"netstack/dns"
	"netstack/fidlconv"

	"fidl/fuchsia/net/name"
)

type broadcastChannel struct {
	sync.Mutex
	channel chan struct{}
}

func (c *broadcastChannel) broadcast() {
	c.Lock()
	close(c.channel)
	c.channel = make(chan struct{})
	c.Unlock()
}

func (c *broadcastChannel) getChannel() chan struct{} {
	c.Lock()
	ch := c.channel
	c.Unlock()
	return ch
}

type dnsServerWatcher struct {
	dnsClient  *dns.Client
	dispatcher *dispatch.Dispatcher
	channel    zx.Channel
	broadcast  *broadcastChannel
	mu         struct {
		sync.Mutex
		isHanging    bool
		isDead       bool
		lastObserved []dns.Server
	}
}

var _ name.DnsServerWatcherWithCtx = (*dnsServerWatcher)(nil)

// serverListEquals compares if two slices of configured servers the same elements in the same
// order.
func serverListEquals(a []dns.Server, b []dns.Server) bool {
	if len(a) != len(b) {
		return false
	}
	for i := range a {
		if a[i] != b[i] {
			return false
		}
	}
	return true
}

func (w *dnsServerWatcher) WatchServers(ctx fidl.Context) ([]name.DnsServer, error) {
	w.mu.Lock()
	defer w.mu.Unlock()

	if w.mu.isHanging {
		return nil, fmt.Errorf("dnsServerWatcher: not allowed to watch twice")
	}

	servers := w.dnsClient.GetServersCache()
	for !w.mu.isDead && serverListEquals(servers, w.mu.lastObserved) {
		w.mu.isHanging = true

		// Spawn a new goroutine serving on the dispatcher because we're blocking the current one.
		go w.dispatcher.Serve()

		w.mu.Unlock()

		ch := w.broadcast.getChannel()

		var err error
		select {
		case <-ch:
		case <-ctx.Done():
			err = fmt.Errorf("context cancelled during hanging get: %w", ctx.Err())
		}
		w.mu.Lock()

		// Reap one goroutine from the dispatcher to return to the previous value.
		_ = w.dispatcher.ShutdownOne()

		w.mu.isHanging = false

		if err != nil {
			w.mu.isDead = true
			return nil, err
		}

		servers = w.dnsClient.GetServersCache()
	}

	if w.mu.isDead {
		return nil, fmt.Errorf("dnsServerWatcher: watcher killed")
	}

	dnsServer := make([]name.DnsServer, 0, len(servers))
	for _, v := range servers {
		s := name.DnsServer{
			Address:        fidlconv.ToNetSocketAddress(v.Address),
			AddressPresent: true,
			Source:         v.Source,
			SourcePresent:  true,
		}
		dnsServer = append(dnsServer, s)
	}
	// Store the last observed servers to compare in subsequent calls.
	w.mu.lastObserved = servers
	return dnsServer, nil
}

type dnsServerWatcherCollection struct {
	dnsClient  *dns.Client
	dispatcher *dispatch.Dispatcher
	bindingSet fidl.BindingSet
	broadcast  broadcastChannel
}

// newDnsServerWatcherCollection creates a new dnsServerWatcherCollection that will observe the
// server configuration of dnsClient.
// Callers are responsible for installing the notification callback on dnsClient and calling
// NotifyServersChanged when changes to the client's configuration occur.
func newDnsServerWatcherCollection(dnsClient *dns.Client) (*dnsServerWatcherCollection, error) {
	dispatcher, err := dispatch.NewDispatcher()
	if err != nil {
		return nil, fmt.Errorf("failed to create dispatcher: %s", err)
	}
	// Start serving on the new dispatcher.
	go dispatcher.Serve()
	collection := &dnsServerWatcherCollection{
		dnsClient:  dnsClient,
		dispatcher: dispatcher,
	}
	collection.broadcast.channel = make(chan struct{})
	return collection, nil
}

func (c *dnsServerWatcherCollection) Close() error {
	c.bindingSet.Close()

	return c.dispatcher.ShutdownOne()
}

// Bind binds a new fuchsia.net.name.DnsServerWatcher request to the collection of watchers and
// starts serving on its channel.
func (c *dnsServerWatcherCollection) Bind(request name.DnsServerWatcherWithCtxInterfaceRequest) error {
	watcher := &dnsServerWatcher{
		dnsClient:  c.dnsClient,
		dispatcher: c.dispatcher,
		channel:    request.Channel,
	}
	watcher.broadcast = &c.broadcast

	_, err := c.bindingSet.AddToDispatcher(&name.DnsServerWatcherWithCtxStub{
		Impl: watcher,
	}, request.Channel, c.dispatcher, func(e error) {
		watcher.mu.Lock()
		watcher.mu.isDead = true
		watcher.mu.Unlock()
		c.broadcast.broadcast()
	})
	return err
}

// NotifyServersChanged notifies all bound fuchsia.net.name.DnsServerWatchers that the list of DNS
// servers has changed.
func (c *dnsServerWatcherCollection) NotifyServersChanged() {
	c.broadcast.broadcast()
}
