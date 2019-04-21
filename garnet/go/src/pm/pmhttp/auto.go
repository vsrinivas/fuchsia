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
	err := sse.Start(w, r)
	if err != nil {
		log.Printf("SSE request failure: %s", err)
		w.WriteHeader(http.StatusInternalServerError)
		return
	}
	a.mu.Lock()
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
