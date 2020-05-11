// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package netstack

import (
	"fmt"
	"sync"
	"syscall/zx/dispatch"
	"syscall/zx/fidl"

	"netstack/dns"

	"fidl/fuchsia/net/name"
)

type broadcastChannel struct {
	mu struct {
		sync.Mutex
		channel chan struct{}
	}
}

func (c *broadcastChannel) broadcast() {
	c.mu.Lock()
	ch := c.mu.channel
	c.mu.channel = make(chan struct{})
	c.mu.Unlock()

	close(ch)
}

func (c *broadcastChannel) getChannel() <-chan struct{} {
	c.mu.Lock()
	ch := c.mu.channel
	c.mu.Unlock()
	return ch
}

type dnsServerWatcher struct {
	dnsClient  *dns.Client
	dispatcher *dispatch.Dispatcher
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

		var err error
		select {
		case <-w.broadcast.getChannel():
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
		dnsServer = append(dnsServer, dnsServerToFidl(v))
	}
	// Store the last observed servers to compare in subsequent calls.
	w.mu.lastObserved = servers
	return dnsServer, nil
}

type dnsServerWatcherCollection struct {
	dnsClient  *dns.Client
	dispatcher *dispatch.Dispatcher
	broadcast  broadcastChannel
}

// newDnsServerWatcherCollection creates a new dnsServerWatcherCollection that will observe the
// server configuration of dnsClient.
// Callers are responsible for installing the notification callback on dnsClient and calling
// NotifyServersChanged when changes to the client's configuration occur.
func newDnsServerWatcherCollection(dispatcher *dispatch.Dispatcher, dnsClient *dns.Client) *dnsServerWatcherCollection {
	collection := dnsServerWatcherCollection{
		dnsClient:  dnsClient,
		dispatcher: dispatcher,
	}
	collection.broadcast.mu.channel = make(chan struct{})
	return &collection
}

// Bind binds a new fuchsia.net.name.DnsServerWatcher request to the collection of watchers and
// starts serving on its channel.
func (c *dnsServerWatcherCollection) Bind(request name.DnsServerWatcherWithCtxInterfaceRequest) error {
	watcher := dnsServerWatcher{
		dnsClient:  c.dnsClient,
		dispatcher: c.dispatcher,
		broadcast:  &c.broadcast,
	}

	// This should be a fidl.Binding, but it isn't possible to construct one with
	// a non-nil context.  Oops. Hopefully goroutine-oriented dispatch obviates
	// this soon.
	var service name.DnsServerWatcherService
	_, err := service.AddToDispatcher(&name.DnsServerWatcherWithCtxStub{
		Impl: &watcher,
	}, request.Channel, c.dispatcher, func(error) {
		watcher.mu.Lock()
		watcher.mu.isDead = true
		watcher.mu.Unlock()
		watcher.broadcast.broadcast()
	})
	return err
}

// NotifyServersChanged notifies all bound fuchsia.net.name.DnsServerWatchers that the list of DNS
// servers has changed.
func (c *dnsServerWatcherCollection) NotifyServersChanged() {
	c.broadcast.broadcast()
}
