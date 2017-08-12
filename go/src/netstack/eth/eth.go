// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Package eth implements a client for magenta's ethernet interface.
// It is comparable to magenta/system/ulib/inet6/eth-client.h.
//
// Sending a packet:
//
//	var buf eth.Buffer
//	for {
//		buf = c.AllocForSend()
//		if buf != nil {
//			break
//		}
//		if err := c.WaitSend(); err != nil {
//			return err // sending is impossible
//		}
//	}
//	// ... things network stacks do
//	copy(buf, dataToSend)
//	return c.Send(buf)
//
// Receiving a packet:
//
//	var buf eth.Buffer
//	var err error
//	for {
//		buf, err = c.Recv()
//		if err != mx.ErrShouldWait {
//			break
//		}
//		c.WaitRecv()
//	}
//	if err != nil {
//		return err
//	}
//	copy(dataRecvd, buf)
//	c.Free(buf)
//	return nil
package eth

import (
	"fmt"
	"os"
	"sync"
	"syscall"
	"syscall/mx"
	"unsafe"
)

// A Client is an ethernet client.
// It connects to a magenta ethernet driver using a FIFO-based protocol.
// The protocol is described in system/public/magenta/device/ethernet.h.
type Client struct {
	MTU int
	MAC [6]byte

	f       *os.File
	tx      mx.Handle
	rx      mx.Handle
	txDepth int
	rxDepth int

	mu         sync.Mutex
	state      State
	stateFunc  func(State)
	arena      *Arena
	tmpbuf     []bufferEntry // used to fill rx and drain tx
	recvbuf    []bufferEntry // packets received
	sendbuf    []bufferEntry // packets ready to send
	txTotal    int
	rxTotal    int
	txInFlight int // number of buffers in tx fifo
	rxInFlight int // number of buffers in rx fifo
}

// NewClient creates a new ethernet Client, connecting to the driver
// described by path.
func NewClient(clientName, path string, arena *Arena, stateFunc func(State)) (*Client, error) {
	f, err := os.Open(path)
	if err != nil {
		return nil, fmt.Errorf("eth: client open: %v", err)
	}
	m := syscall.MXIOForFD(int(f.Fd()))
	if m == nil {
		return nil, fmt.Errorf("eth: no mxio for %s fd: %d", path, f.Fd())
	}

	IoctlSetClientName(m, []byte(clientName))

	info, err := IoctlGetInfo(m)
	if err != nil {
		return nil, err
	}
	if info.Features&FeatureSynth != 0 {
		return nil, fmt.Errorf("eth: ignoring synthetic device")
	}

	fifos, err := IoctlGetFifos(m)
	if err != nil {
		return nil, err
	}

	txDepth := int(fifos.txDepth)
	rxDepth := int(fifos.rxDepth)
	maxDepth := txDepth
	if rxDepth > maxDepth {
		maxDepth = rxDepth
	}

	c := &Client{
		MTU:       int(info.MTU),
		f:         f,
		tx:        fifos.tx,
		rx:        fifos.rx,
		txDepth:   txDepth,
		rxDepth:   rxDepth,
		stateFunc: stateFunc,
		arena:     arena,
		tmpbuf:    make([]bufferEntry, 0, maxDepth),
		recvbuf:   make([]bufferEntry, 0, rxDepth),
		sendbuf:   make([]bufferEntry, 0, txDepth),
	}
	copy(c.MAC[:], info.MAC[:])

	c.mu.Lock()
	defer c.mu.Unlock()
	h, err := mx.Handle(c.arena.iovmo).Duplicate(mx.RightSameRights)
	if err != nil {
		c.closeLocked()
		return nil, fmt.Errorf("eth: failed to duplicate vmo: %v", err)
	}
	if err := IoctlSetIobuf(m, h); err != nil {
		c.closeLocked()
		return nil, err
	}
	if err := c.rxCompleteLocked(); err != nil {
		c.closeLocked()
		return nil, fmt.Errorf("eth: failed to load rx fifo: %v", err)
	}
	if err := IoctlStart(m); err != nil {
		c.closeLocked()
		return nil, err
	}
	c.changeStateLocked(StateStarted)
	return c, nil
}

func (c *Client) changeStateLocked(s State) {
	c.state = s
	go func() {
		c.mu.Lock()
		defer c.mu.Unlock()
		if c.stateFunc == nil {
			return
		}
		c.stateFunc(s)
	}()
}

// Close closes a Client, releasing any held resources.
func (c *Client) Close() {
	c.mu.Lock()
	defer c.mu.Unlock()
	c.closeLocked()
}

func (c *Client) closeLocked() {
	if c.state == StateStopped {
		return
	}

	m := syscall.MXIOForFD(int(c.f.Fd()))
	IoctlStop(m)

	c.tx.Close()
	c.rx.Close()
	c.tmpbuf = c.tmpbuf[:0]
	c.recvbuf = c.recvbuf[:0]
	c.sendbuf = c.sendbuf[:0]
	c.f.Close()
	c.arena.freeAll(c)
	c.changeStateLocked(StateStopped)
}

// AllocForSend returns a Buffer to be passed to Send.
// If there are too many outstanding transmission buffers, then
// AllocForSend will return nil. WaitSend can be called to block
// until a transmission buffer is available.
func (c *Client) AllocForSend() Buffer {
	c.mu.Lock()
	defer c.mu.Unlock()
	// TODO: Use more than txDepth here. More like 2x.
	//       We cannot have more than txDepth outstanding for the fifo,
	//       but we can have more between AllocForSend and Send. When
	//       this is the entire netstack, we want a lot of buffer.
	//       But there is missing tooling here. In particular, calling
	//       Send won't 'release' the buffer, we need an extra step
	//       for that, because we will want to keep the buffer around
	//       until the ACK comes back.
	if c.txInFlight == c.txDepth {
		return nil
	}
	c.txInFlight++
	return c.arena.alloc(c)
}

// Send sends a Buffer to the ethernet driver.
// Send does not block.
// If the client is closed, Send returns mx.ErrPeerClosed.
func (c *Client) Send(b Buffer) error {
	c.mu.Lock()
	defer c.mu.Unlock()
	if _, err := c.txCompleteLocked(); err != nil {
		return err
	}
	c.sendbuf = append(c.sendbuf, c.arena.entry(b))
	entries, entriesSize := fifoEntries(c.sendbuf)
	var count uint32
	status := mx.Sys_fifo_write(c.tx, entries, entriesSize, &count)
	copy(c.sendbuf, c.sendbuf[count:])
	c.sendbuf = c.sendbuf[:len(c.sendbuf)-int(count)]
	if status != mx.ErrOk && status != mx.ErrShouldWait {
		return mx.Error{Status: status, Text: "eth.Client.Send"}
	}
	return nil
}

// Free frees a Buffer obtained from Recv.
//
// TODO: c.Free(c.AllocForSend()) will leak txInFlight and eventually jam
//       up the client. This is not an expected use of this library, but
//       tracking to handle it could be useful for debugging.
func (c *Client) Free(b Buffer) {
	c.mu.Lock()
	defer c.mu.Unlock()
	c.arena.free(c, b)
}

func fifoEntries(b []bufferEntry) (unsafe.Pointer, uint) {
	return unsafe.Pointer(&b[0]), uint(unsafe.Sizeof(b[0])) * uint(len(b))
}

func (c *Client) txCompleteLocked() (bool, error) {
	buf := c.tmpbuf[:c.txDepth]
	entries, entriesSize := fifoEntries(buf)
	var count uint32
	status := mx.Sys_fifo_read(c.tx, entries, entriesSize, &count)
	n := int(count)
	c.txInFlight -= n
	c.txTotal += n
	for i := 0; i < n; i++ {
		c.arena.free(c, c.arena.bufferFromEntry(buf[i]))
	}
	canSend := c.txInFlight < c.txDepth
	if status != mx.ErrOk && status != mx.ErrShouldWait {
		return canSend, mx.Error{Status: status, Text: "eth.Client.TX"}
	}
	return canSend, nil
}

func (c *Client) popRecvLocked() Buffer {
	c.rxTotal++
	b := c.recvbuf[0]
	copy(c.recvbuf, c.recvbuf[1:])
	c.recvbuf = c.recvbuf[:len(c.recvbuf)-1]
	return c.arena.bufferFromEntry(b)
}

// Recv receives a Buffer from the ethernet driver.
//
// Recv does not block. If no data is available, this function
// returns a nil Buffer and mx.ErrShouldWait.
//
// If the client is closed, Recv returns mx.ErrPeerClosed.
func (c *Client) Recv() (b Buffer, err error) {
	c.mu.Lock()
	defer c.mu.Unlock()
	if len(c.recvbuf) > 0 {
		return c.popRecvLocked(), nil
	}
	entries, entriesSize := fifoEntries(c.recvbuf[:cap(c.recvbuf)])
	var count uint32
	status := mx.Sys_fifo_read(c.rx, entries, entriesSize, &count)
	n := int(count)
	c.recvbuf = c.recvbuf[:n]
	c.rxInFlight -= n
	if status != mx.ErrOk {
		return nil, mx.Error{Status: status, Text: "eth.Client.Recv"}
	}
	return c.popRecvLocked(), c.rxCompleteLocked()
}

func (c *Client) rxCompleteLocked() error {
	buf := c.tmpbuf[:0]
	for i := c.rxInFlight; i < c.rxDepth; i++ {
		b := c.arena.alloc(c)
		if b == nil {
			break
		}
		buf = append(buf, c.arena.entry(b))
	}
	if len(buf) == 0 {
		return nil // nothing to do
	}
	entries, entriesSize := fifoEntries(buf)
	var count uint32
	status := mx.Sys_fifo_write(c.rx, entries, entriesSize, &count)
	for _, entry := range buf[count:] {
		b := c.arena.bufferFromEntry(entry)
		c.arena.free(c, b)
	}
	c.rxInFlight += int(count)
	if status != mx.ErrOk {
		return mx.Error{Status: status, Text: "eth.Client.RX"}
	}
	return nil
}

// WaitSend blocks until it is possible to allocate a send buffer,
// or the client is closed.
func (c *Client) WaitSend() error {
	for {
		c.mu.Lock()
		canSend, err := c.txCompleteLocked()
		c.mu.Unlock()
		if canSend || err != nil {
			return err
		}
		// Errors from waiting handled in txComplete.
		c.tx.WaitOne(mx.SignalFIFOReadable|mx.SignalFIFOPeerClosed, mx.TimensecInfinite)
	}
}

// WaitRecv blocks until it is possible to receive a buffer,
// or the client is closed.
func (c *Client) WaitRecv() {
	obs, err := c.rx.WaitOne(mx.SignalFIFOReadable|mx.SignalFIFOPeerClosed, mx.TimensecInfinite)
	if err != nil || obs&mx.SignalFIFOPeerClosed != 0 {
		c.Close()
	}
}

// ListenTX tells the ethernet driver to reflect all transmitted
// packets back to this ethernet client.
func (c *Client) ListenTX() {
	m := syscall.MXIOForFD(int(c.f.Fd()))
	IoctlTXListenStart(m)
}

type State int

const (
	StateUnknown = State(iota)
	StateStarted
	StateStopped
)

func (s State) String() string {
	switch s {
	case StateUnknown:
		return "eth unknown state"
	case StateStarted:
		return "eth started"
	case StateStopped:
		return "eth stopped"
	default:
		return fmt.Sprintf("eth bad state(%d)", int(s))
	}
}
