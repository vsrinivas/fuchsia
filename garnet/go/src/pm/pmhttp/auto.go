// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package pmhttp

import (
	"log"
	"net/http"
	"sync"

	"fuchsia.googlesource.com/sse"
)

type AutoServer struct {
	mu sync.Mutex

	clients map[http.ResponseWriter]struct{}
}

func NewAutoServer() *AutoServer {
	return &AutoServer{clients: map[http.ResponseWriter]struct{}{}}
}

func (a *AutoServer) ServeHTTP(w http.ResponseWriter, r *http.Request) {
	// Grab the lock first before starting the SSE server so we block
	// broadcasting events until we've finished adding the client to our
	// client set. Otherwise, a client could miss an event that came in
	// during the registration process.
	a.mu.Lock()
	err := sse.Start(w, r)
	if err != nil {
		a.mu.Unlock()
		log.Printf("SSE request failure: %s", err)
		w.WriteHeader(http.StatusInternalServerError)
		return
	}
	a.clients[w] = struct{}{}
	defer func() {
		a.mu.Lock()
		delete(a.clients, w)
		a.mu.Unlock()
	}()
	a.mu.Unlock()
	<-r.Context().Done()
}

func (a *AutoServer) Broadcast(name, data string) {
	a.mu.Lock()
	defer a.mu.Unlock()
	for w := range a.clients {
		sse.Write(w, &sse.Event{
			Event: name,
			Data:  []byte(data),
		})
	}
}
