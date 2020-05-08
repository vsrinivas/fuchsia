// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package device

import (
	"fmt"
)

// EstimatedMonotonicTime is a structure that will return a string estimate of
// a device's current monotonic time.
type EstimatedMonotonicTime struct {
	client *Client
	suffix string
}

// NewEstimatedMonotonicTime constructs a new EstimatedMonotonicTime.
func NewEstimatedMonotonicTime(client *Client, suffix string) *EstimatedMonotonicTime {
	return &EstimatedMonotonicTime{
		client: client,
		suffix: suffix,
	}
}

// String() will return a string prefixed by the device's monotonic time.
func (p *EstimatedMonotonicTime) String() string {
	if t := p.client.getEstimatedMonotonicTime(); t == 0 {
		if p.suffix == "" {
			return ""
		}

		return fmt.Sprintf("%s", p.suffix)
	} else {
		if p.suffix == "" {
			return fmt.Sprintf("[%05.3f]", t.Seconds())
		}

		return fmt.Sprintf("[%05.3f] %s", t.Seconds(), p.suffix)
	}
}
