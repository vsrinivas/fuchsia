// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Package sse provides support for Server-Sent Events, as a handler for
// net/http, and a client that can read SSE events from net/http responses.
//
// SSE is specified here: http://www.w3.org/TR/eventsource/
//
// The package implements only partial support for SSE, specifically the client
// and server do not handle retries or event buffering.
package sse

import (
	"errors"
	"net/http"
	"strings"
)

// ErrNotAcceptable occurs when the incoming request does not Accept
// text/event-stream
var ErrNotAcceptable = errors.New("sse: invalid content-type")
var headers = map[string]string{
	"Content-Type":      "text/event-stream",
	"Cache-Control":     "no-cache",
	"X-Accel-Buffering": "no",
}

// Start asserts that the client wants SSE, and if so, begins writing an SSE
// stream with the appropriate response headers. It performs a forced flush on
// the stream to ensure that the client receives the headers immediately.
func Start(w http.ResponseWriter, r *http.Request) error {
	if !strings.Contains(r.Header.Get("Accept"), "text/event-stream") {
		return ErrNotAcceptable
	}

	for h, v := range headers {
		w.Header().Set(h, v)
	}

	w.WriteHeader(200)

	w.(http.Flusher).Flush()

	return nil
}

// Write sends an event to an http ResponseWriter, flushing afterward.
func Write(w http.ResponseWriter, e *Event) error {
	_, err := e.WriteTo(w)
	w.(http.Flusher).Flush()
	return err
}
