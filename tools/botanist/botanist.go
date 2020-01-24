// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can
// found in the LICENSE file.

package botanist

const (
	// SerialLogBufferSize gives the amount of data to buffer when doing serial
	// I/O.
	// The size of the serial log of an entire test run is on the order of 1MB;
	// accordingly, we allow a buffer size of an order of magnitude less for
	// conservative estimate.
	SerialLogBufferSize = (1000 * 1000) / 10
)
