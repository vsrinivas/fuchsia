// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package sse

import (
	"bytes"
	"fmt"
	"io"
)

// Event represents a single Server-Sent Event.
type Event struct {
	Event string
	Data  []byte
	ID    string
	Retry *int
}

// Marshal returns a string in the SSE wire format, with the Data field repeated
// for an ocurrences of line breaks in the bytes.
func (e *Event) Marshal() []byte {
	result := []byte{}

	if e.Event != "" {
		result = append(result, "event: "...)
		result = append(result, e.Event...)
		result = append(result, '\n')
	}
	if e.ID != "" {
		result = append(result, "id: "...)
		result = append(result, e.ID...)
		result = append(result, '\n')
	}
	if e.Retry != nil && *e.Retry > 0 {
		result = append(result, fmt.Sprintf("retry: %d\n", *e.Retry)...)
	}

	for _, d := range bytes.Split(e.Data, []byte("\n")) {
		result = append(result, "data: "...)
		result = append(result, d...)
		result = append(result, '\n')
	}

	return append(result, '\n')

}

// String returns the same format as Marshal
func (e *Event) String() string {
	return string(e.Marshal())
}

// WriteTo marshals the event and writes it to the given stream. HTTP users will
// likely prefer to use sse.Write instead, as that handles flushing the stream.
func (e *Event) WriteTo(w io.Writer) (int64, error) {
	n, err := w.Write(e.Marshal())
	return int64(n), err
}
