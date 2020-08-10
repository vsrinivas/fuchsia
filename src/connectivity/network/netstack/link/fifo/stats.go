// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package fifo

import "gvisor.dev/gvisor/pkg/tcpip"

type rwStats struct {
	reads, writes tcpip.StatCounter
}

type FifoStats struct {
	// batches is an associative array from read/write batch sizes
	// (indexed at `batchSize-1`) to tcpip.StatCounters of the number of reads
	// and writes of that batch size.
	batches []rwStats
}

type RxStats struct {
	FifoStats
}

type TxStats struct {
	FifoStats
	Drops tcpip.StatCounter
}

func MakeFifoStats(depth uint32) FifoStats {
	return FifoStats{batches: make([]rwStats, depth)}
}

func (s *FifoStats) Size() uint32 {
	return uint32(len(s.batches))
}

func (s *FifoStats) Reads(batchSize uint32) *tcpip.StatCounter {
	return &s.batches[batchSize-1].reads
}

func (s *FifoStats) Writes(batchSize uint32) *tcpip.StatCounter {
	return &s.batches[batchSize-1].writes
}
