// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package sse

// TODO(raggi): add support for some kind of read limits per event and so on

import (
	"bufio"
	"fmt"
	"net/http"
	"strconv"
	"strings"
)

// ProtocolError provides details of any protocol error that has occurred, the
// original Response object and an Error that contains a description of the
// problem.
type ProtocolError struct {
	Response *http.Response
	Err      error
}

func (pe *ProtocolError) Error() string {
	return fmt.Sprintf("sse: protocol error: %s", pe.Err.Error())
}

// Client provides a handle to methods that are bound to a single SSE stream.
type Client struct {
	r *http.Response
	b *bufio.Reader
}

// New constructs a new SSE client against the http.Response. It returns an
// error if the response is not a valid SSE response, for example carrying the
// incorrect content type, or not having a response body stream. In cases where
// the error is an SSE protocol issue, ProtocolError is returned.
func New(r *http.Response) (*Client, error) {
	if r.ContentLength != -1 {
		return nil, &ProtocolError{r, fmt.Errorf("fixed content-length: %d", r.ContentLength)}
	}

	if !strings.Contains(r.Header.Get("Content-Type"), "text/event-stream") {
		return nil, &ProtocolError{r, fmt.Errorf("invalid content-type: %s", r.Header.Get("Content-Type"))}
	}

	return &Client{r, bufio.NewReader(r.Body)}, nil
}

// ReadEvent parses the next event out of the response body, blocking if none is
// yet available. It returns either an Event or an error. error is either a
// ProtocolError or one of various IO errors, such as io.EOF.
func (c *Client) ReadEvent() (*Event, error) {
	var e *Event
	for {
		line, err := c.b.ReadString('\n')
		if err != nil {
			return nil, err
		}

		if len(line) == 0 || line[0] == '\n' {
			if e != nil {
				return e, nil
			}
			continue
		}
		if line[0] == ':' {
			continue
		}

		var value string
		parts := strings.SplitN(strings.TrimRight(line, "\r\n"), ":", 2)
		if len(parts) == 2 {
			value = strings.TrimPrefix(parts[1], " ")
		}

		// TODO(raggi): if fields are repeated (other than data) decide whether to
		// catch this and error on it. Right now the last field wins.
		switch parts[0] {
		case "event":
			if e == nil {
				e = &Event{}
			}

			e.Event = value
		case "data":
			if e == nil {
				e = &Event{}
			}

			if len(e.Data) > 0 {
				e.Data = append(e.Data, '\n')
			}
			e.Data = append(e.Data, value...)
		case "id":
			if e == nil {
				e = &Event{}
			}

			e.ID = value
		case "retry":
			if e == nil {
				e = &Event{}
			}

			i, err := strconv.Atoi(value)
			if err != nil {
				return e, &ProtocolError{c.r, fmt.Errorf("invalid retry value: %q", value)}
			}
			e.Retry = &i
		}
	}
}
