// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Package eth implements a client for zircon's ethernet interface.
// It is comparable to zircon/system/ulib/inet6/eth-client.h.
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
//		if err != zx.ErrShouldWait {
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
	"log"
	"os"
	"path/filepath"
	"sync"
	"syscall"
	"syscall/zx"
	"syscall/zx/zxwait"
	"unsafe"

	"netstack/trace"
	nsfidl "fidl/fuchsia/netstack"
)

const ZXSIO_ETH_SIGNAL_STATUS = zx.SignalUser0

// A Client is an ethernet client.
// It connects to a zircon ethernet driver using a FIFO-based protocol.
// The protocol is described in system/public/zircon/device/ethernet.h.
type Client struct {
	MTU  int
	MAC  [6]byte
	Path string

	f       *os.File
	tx      zx.Handle
	rx      zx.Handle
	txDepth int
	rxDepth int

	Features uint32 // cache of link/eth/ioctl.go's EthInfo

	mu        sync.Mutex
	state     State
	stateFunc func(State)
	arena     *Arena
	tmpbuf    []bufferEntry // used to fill rx and drain tx
	recvbuf   []bufferEntry // packets received
	sendbuf   []bufferEntry // packets ready to send

	// These are counters for buffer management purpose.
	txTotal    int
	rxTotal    int
	txInFlight int // number of buffers in tx fifo
	rxInFlight int // number of buffers in rx fifo
}

// NewClient creates a new ethernet Client, connecting to the driver
// described by path.
func NewClient(clientName, path string, arena *Arena, stateFunc func(State)) (*Client, error) {
	success := false
	f, err := os.Open(path)
	if err != nil {
		return nil, fmt.Errorf("eth: client open: %v", err)
	}
	defer func() {
		if !success {
			f.Close()
		}
	}()
	m := syscall.FDIOForFD(int(f.Fd()))
	if m == nil {
		return nil, fmt.Errorf("eth: no fdio for %s fd: %d", path, f.Fd())
	}

	IoctlSetClientName(m, []byte(clientName))

	topo, err := IoctlGetTopoPath(m)
	if err != nil {
		return nil, err
	}

	info, err := IoctlGetInfo(m)
	if err != nil {
		return nil, err
	}
	if info.Features&nsfidl.InterfaceFeatureSynth != 0 {
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
		Path:      topo,
		tx:        fifos.tx,
		rx:        fifos.rx,
		txDepth:   txDepth,
		rxDepth:   rxDepth,
		Features:  info.Features,
		stateFunc: stateFunc,
		arena:     arena,
		tmpbuf:    make([]bufferEntry, 0, maxDepth),
		recvbuf:   make([]bufferEntry, 0, rxDepth),
		sendbuf:   make([]bufferEntry, 0, txDepth),
	}
	copy(c.MAC[:], info.MAC[:])

	c.mu.Lock()
	defer c.mu.Unlock()
	h, err := c.arena.iovmo.Handle().Duplicate(zx.RightSameRights)
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

	success = true
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

// Up enables the interface.
func (c *Client) Up() error {
	c.mu.Lock()
	defer c.mu.Unlock()
	if c.state != StateStarted {
		m := syscall.FDIOForFD(int(c.f.Fd()))
		err := IoctlStart(m)
		if err != nil {
			return err
		}
		c.changeStateLocked(StateStarted)
	}

	return nil
}

// Down disables the interface.
func (c *Client) Down() error {
	c.mu.Lock()
	defer c.mu.Unlock()
	if c.state != StateDown {
		m := syscall.FDIOForFD(int(c.f.Fd()))
		err := IoctlStop(m)
		if err != nil {
			return err
		}
		c.changeStateLocked(StateDown)
	}
	return nil
}

// Close closes a Client, releasing any held resources.
func (c *Client) Close() {
	c.mu.Lock()
	defer c.mu.Unlock()
	c.closeLocked()
}

func (c *Client) closeLocked() {
	if c.state == StateClosed {
		return
	}

	m := syscall.FDIOForFD(int(c.f.Fd()))
	if err := IoctlStop(m); err != nil {
		if fp, fperr := filepath.Abs(c.f.Name()); fperr != nil {
			log.Printf("Failed to close ethernet file %s, error: %s", c.f.Name(), err)
		} else {
			log.Printf("Failed to close ethernet path %s, error: %s", fp, err)
		}
	}

	c.tx.Close()
	c.rx.Close()
	c.tmpbuf = c.tmpbuf[:0]
	c.recvbuf = c.recvbuf[:0]
	c.sendbuf = c.sendbuf[:0]
	c.f.Close()
	c.arena.freeAll(c)
	c.changeStateLocked(StateClosed)
}

func (c *Client) SetPromiscuousMode(enabled bool) error {
	c.mu.Lock()
	defer c.mu.Unlock()

	m := syscall.FDIOForFD(int(c.f.Fd()))
	return IoctlSetPromisc(m, enabled)
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
// If the client is closed, Send returns zx.ErrPeerClosed.
func (c *Client) Send(b Buffer) error {
	c.mu.Lock()
	defer c.mu.Unlock()
	if _, err := c.txCompleteLocked(); err != nil {
		return err
	}
	c.sendbuf = append(c.sendbuf, c.arena.entry(b))
	status, count := fifoWrite(c.tx, c.sendbuf)
	copy(c.sendbuf, c.sendbuf[count:])
	c.sendbuf = c.sendbuf[:len(c.sendbuf)-int(count)]
	if status != zx.ErrOk && status != zx.ErrShouldWait {
		return zx.Error{Status: status, Text: "eth.Client.Send"}
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

func fifoWrite(handle zx.Handle, b []bufferEntry) (zx.Status, uint) {
	var actual uint
	status := zx.Sys_fifo_write(handle, uint(unsafe.Sizeof(b[0])), unsafe.Pointer(&b[0]), uint(len(b)), &actual)
	return status, actual
}

func fifoRead(handle zx.Handle, b []bufferEntry) (zx.Status, uint) {
	var actual uint
	status := zx.Sys_fifo_read(handle, uint(unsafe.Sizeof(b[0])), unsafe.Pointer(&b[0]), uint(len(b)), &actual)
	return status, actual
}

func (c *Client) txCompleteLocked() (bool, error) {
	buf := c.tmpbuf[:c.txDepth]
	status, count := fifoRead(c.tx, buf)
	n := int(count)

	c.txInFlight -= n
	c.txTotal += n
	for i := 0; i < n; i++ {
		c.arena.free(c, c.arena.bufferFromEntry(buf[i]))
	}
	canSend := c.txInFlight < c.txDepth
	if status != zx.ErrOk && status != zx.ErrShouldWait {
		return canSend, zx.Error{Status: status, Text: "eth.Client.TX"}
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
// returns a nil Buffer and zx.ErrShouldWait.
//
// If the client is closed, Recv returns zx.ErrPeerClosed.
func (c *Client) Recv() (b Buffer, err error) {
	c.mu.Lock()
	defer c.mu.Unlock()
	if len(c.recvbuf) > 0 {
		return c.popRecvLocked(), nil
	}
	status, count := fifoRead(c.rx, c.recvbuf[:cap(c.recvbuf)])
	n := int(count)
	c.recvbuf = c.recvbuf[:n]
	c.rxInFlight -= n
	if status != zx.ErrOk {
		return nil, zx.Error{Status: status, Text: "eth.Client.Recv"}
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
	status, count := fifoWrite(c.rx, buf)
	for _, entry := range buf[count:] {
		b := c.arena.bufferFromEntry(entry)
		c.arena.free(c, b)
	}
	c.rxInFlight += int(count)
	if status != zx.ErrOk {
		return zx.Error{Status: status, Text: "eth.Client.RX"}
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
		zxwait.Wait(c.tx, zx.SignalFIFOReadable|zx.SignalFIFOPeerClosed, zx.TimensecInfinite)
	}
}

// WaitRecv blocks until it is possible to receive a buffer,
// or the client is closed.
func (c *Client) WaitRecv() {
	for {
		obs, err := zxwait.Wait(c.rx, zx.SignalFIFOReadable|zx.SignalFIFOPeerClosed|ZXSIO_ETH_SIGNAL_STATUS, zx.TimensecInfinite)
		if err != nil || obs&zx.SignalFIFOPeerClosed != 0 {
			c.Close()
		} else if obs&ZXSIO_ETH_SIGNAL_STATUS != 0 {
			// TODO(): The wired Ethernet should receive this signal upon being
			// hooked up with a (an active) Ethernet cable.
			m := syscall.FDIOForFD(int(c.f.Fd()))
			status, err := IoctlGetStatus(m)

			trace.DebugTraceDeep(5, "status %d FD %d", status, int(c.f.Fd()))

			c.mu.Lock()
			switch status {
			case 0:
				c.changeStateLocked(StateDown)
			case 1:
				c.changeStateLocked(StateStarted)
			default:
				log.Printf("Unknown eth status=%d, %v", status, err)
			}
			c.mu.Unlock()

			continue
		}

		break
	}
}

// ListenTX tells the ethernet driver to reflect all transmitted
// packets back to this ethernet client.
func (c *Client) ListenTX() {
	m := syscall.FDIOForFD(int(c.f.Fd()))
	IoctlTXListenStart(m)
}

type State int

const (
	StateUnknown = State(iota)
	StateStarted
	StateDown
	StateClosed
)

func (s State) String() string {
	switch s {
	case StateUnknown:
		return "eth unknown state"
	case StateStarted:
		return "eth started"
	case StateDown:
		return "eth down"
	case StateClosed:
		return "eth stopped"
	default:
		return fmt.Sprintf("eth bad state(%d)", int(s))
	}
}
