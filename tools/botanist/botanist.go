// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package botanist

const (
	// SerialLogBufferSize gives the amount of data to buffer when doing serial
	// I/O.
	// The maximum amount of data that should need buffering is everything
	// emitted before tests start running, at which point botanist will have
	// handed things off to a subprocess that will be free to begin reading
	// from this buffer. As of 2020-01-18, the size of this data, corresponding
	// to paving, booting, and then establishing a network connection is one
	// the order of 10Kb; we liberally overestimate that by an order of
	// magnitude.
	SerialLogBufferSize = 10000 * 10
)
