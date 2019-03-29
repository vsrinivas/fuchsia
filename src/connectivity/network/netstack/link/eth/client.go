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
	"reflect"
	"sync"
	"syscall/zx"
	"syscall/zx/zxwait"
	"unsafe"

	"netstack/link"

	"syslog/logger"

	"fidl/fuchsia/hardware/ethernet"
)

// #cgo CFLAGS: -I${SRCDIR}/../../../zircon/public
// #include <zircon/device/ethernet.h>
import "C"

const ZXSIO_ETH_SIGNAL_STATUS = zx.SignalUser0

var _ link.Controller = (*Client)(nil)

// A Client is an ethernet client.
// It connects to a zircon ethernet driver using a FIFO-based protocol.
// The protocol is described in system/fidl/fuchsia-hardware-ethernet/ethernet.fidl.
type Client struct {
	Info ethernet.Info

	device ethernet.Device
	fifos  ethernet.Fifos
	path   string

	mu        sync.Mutex
	state     link.State
	stateFunc func(link.State)
	arena     *Arena
	tmpbuf    []C.struct_eth_fifo_entry // used to fill rx and drain tx
	recvbuf   []C.struct_eth_fifo_entry // packets received
	sendbuf   []C.struct_eth_fifo_entry // packets ready to send

	// These are counters for buffer management purpose.
	txTotal    uint32
	rxTotal    uint32
	txInFlight uint32 // number of buffers in tx fifo
	rxInFlight uint32 // number of buffers in rx fifo
}

func checkStatus(status int32, text string) error {
	if status := zx.Status(status); status != zx.ErrOk {
		return zx.Error{Status: status, Text: text}
	}
	return nil
}

// NewClient creates a new ethernet Client.
func NewClient(clientName string, topo string, device ethernet.Device, arena *Arena) (*Client, error) {
	if status, err := device.SetClientName(clientName); err != nil {
		return nil, err
	} else if err := checkStatus(status, "SetClientName"); err != nil {
		return nil, err
	}
	info, err := device.GetInfo()
	if err != nil {
		return nil, err
	}
	status, fifos, err := device.GetFifos()
	if err != nil {
		return nil, err
	} else if err := checkStatus(status, "GetFifos"); err != nil {
		return nil, err
	}

	maxDepth := fifos.TxDepth
	if fifos.RxDepth > maxDepth {
		maxDepth = fifos.RxDepth
	}

	c := &Client{
		Info:    info,
		device:  device,
		fifos:   *fifos,
		path:    topo,
		arena:   arena,
		tmpbuf:  make([]C.struct_eth_fifo_entry, 0, maxDepth),
		recvbuf: make([]C.struct_eth_fifo_entry, 0, fifos.RxDepth),
		sendbuf: make([]C.struct_eth_fifo_entry, 0, fifos.TxDepth),
	}

	c.mu.Lock()
	defer c.mu.Unlock()
	if err := func() error {
		h, err := c.arena.iovmo.Handle().Duplicate(zx.RightSameRights)
		if err != nil {
			return fmt.Errorf("eth: failed to duplicate vmo: %v", err)
		}
		if status, err := device.SetIoBuffer(zx.VMO(h)); err != nil {
			return err
		} else if err := checkStatus(status, "SetIoBuffer"); err != nil {
			return err
		}
		if err := c.rxCompleteLocked(); err != nil {
			return fmt.Errorf("eth: failed to load rx fifo: %v", err)
		}
		return nil
	}(); err != nil {
		c.closeLocked()
		return nil, err
	}

	return c, nil
}

func (c *Client) SetOnStateChange(f func(link.State)) {
	c.mu.Lock()
	defer c.mu.Unlock()
	c.stateFunc = f
}

func (c *Client) Path() string {
	return c.path
}

func (c *Client) changeStateLocked(s link.State) {
	if s != c.state {
		c.state = s
		if fn := c.stateFunc; fn != nil {
			fn(s)
		}
	}
}

// Up enables the interface.
func (c *Client) Up() error {
	c.mu.Lock()
	defer c.mu.Unlock()
	if c.state != link.StateStarted {
		if status, err := c.device.Start(); err != nil {
			return err
		} else if err := checkStatus(status, "Start"); err != nil {
			return err
		}
		if status, err := c.GetStatus(); err != nil {
			return err
		} else {
			switch status {
			case LinkDown:
				c.changeStateLocked(link.StateDown)
			case LinkUp:
				c.changeStateLocked(link.StateStarted)
			}
		}
	}

	return nil
}

// Down disables the interface.
func (c *Client) Down() error {
	c.mu.Lock()
	defer c.mu.Unlock()
	if c.state != link.StateDown {
		if err := c.device.Stop(); err != nil {
			return err
		}
		c.changeStateLocked(link.StateDown)
	}
	return nil
}

// Close closes a Client, releasing any held resources.
func (c *Client) Close() error {
	c.mu.Lock()
	defer c.mu.Unlock()
	return c.closeLocked()
}

func (c *Client) closeLocked() error {
	if c.state == link.StateClosed {
		return nil
	}
	err := c.device.Stop()
	if err != nil {
		err = fmt.Errorf("fuchsia.hardware.ethernet.Device.Stop() for path %q failed: %v", c.path, err)
	}

	if err := c.fifos.Tx.Close(); err != nil {
		logger.Warnf("eth: failed to close tx fifo: %s", err)
	}
	if err := c.fifos.Rx.Close(); err != nil {
		logger.Warnf("eth: failed to close rx fifo: %s", err)
	}
	c.tmpbuf = c.tmpbuf[:0]
	c.recvbuf = c.recvbuf[:0]
	c.sendbuf = c.sendbuf[:0]
	c.arena.freeAll(c)
	c.changeStateLocked(link.StateClosed)

	return err
}

func (c *Client) SetPromiscuousMode(enabled bool) error {
	c.mu.Lock()
	defer c.mu.Unlock()

	if status, err := c.device.SetPromiscuousMode(enabled); err != nil {
		return err
	} else if err := checkStatus(status, "SetPromiscuousMode"); err != nil {
		return err
	}
	return nil
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
	if c.txInFlight == c.fifos.TxDepth {
		return nil
	}
	buf := c.arena.alloc(c)
	if buf != nil {
		c.txInFlight++
	}
	return buf
}

// Send sends a Buffer to the ethernet driver.
// Send does not block.
// If the client is closed, Send returns zx.ErrPeerClosed.
func (c *Client) Send(b Buffer) error {
	c.mu.Lock()
	defer c.mu.Unlock()
	if err := c.txCompleteLocked(); err != nil {
		return err
	}
	c.sendbuf = append(c.sendbuf, c.arena.entry(b))

	switch status, count := fifoWrite(c.fifos.Tx, c.sendbuf); status {
	case zx.ErrOk:
		n := copy(c.sendbuf, c.sendbuf[count:])
		c.sendbuf = c.sendbuf[:n]
	case zx.ErrShouldWait:
	default:
		return zx.Error{Status: status, Text: "eth.Client.RX"}
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

func fifoWrite(handle zx.Handle, b []C.struct_eth_fifo_entry) (zx.Status, uint32) {
	var actual uint
	data := unsafe.Pointer((*reflect.SliceHeader)(unsafe.Pointer(&b)).Data)
	status := zx.Sys_fifo_write(handle, C.sizeof_struct_eth_fifo_entry, data, uint(len(b)), &actual)
	return status, uint32(actual)
}

func fifoRead(handle zx.Handle, b []C.struct_eth_fifo_entry) (zx.Status, uint32) {
	var actual uint
	data := unsafe.Pointer((*reflect.SliceHeader)(unsafe.Pointer(&b)).Data)
	status := zx.Sys_fifo_read(handle, C.sizeof_struct_eth_fifo_entry, data, uint(len(b)), &actual)
	return status, uint32(actual)
}

func (c *Client) txCompleteLocked() error {
	buf := c.tmpbuf[:c.fifos.TxDepth]

	switch status, count := fifoRead(c.fifos.Tx, buf); status {
	case zx.ErrOk:
		c.txInFlight -= count
		c.txTotal += count
		for _, entry := range buf[:count] {
			c.arena.free(c, c.arena.bufferFromEntry(entry))
		}
	case zx.ErrShouldWait:
	default:
		return zx.Error{Status: status, Text: "eth.Client.TX"}
	}

	return nil
}

// Recv receives a Buffer from the ethernet driver.
//
// Recv does not block. If no data is available, this function
// returns a nil Buffer and zx.ErrShouldWait.
//
// If the client is closed, Recv returns zx.ErrPeerClosed.
func (c *Client) Recv() (Buffer, error) {
	c.mu.Lock()
	defer c.mu.Unlock()
	if len(c.recvbuf) == 0 {
		status, count := fifoRead(c.fifos.Rx, c.recvbuf[:cap(c.recvbuf)])
		if status != zx.ErrOk {
			return nil, zx.Error{Status: status, Text: "eth.Client.Recv"}
		}

		c.recvbuf = c.recvbuf[:count]
		c.rxInFlight -= count
		if err := c.rxCompleteLocked(); err != nil {
			return nil, err
		}
	}
	c.rxTotal++
	b := c.recvbuf[0]
	n := copy(c.recvbuf, c.recvbuf[1:])
	c.recvbuf = c.recvbuf[:n]
	return c.arena.bufferFromEntry(b), nil
}

func (c *Client) rxCompleteLocked() error {
	buf := c.tmpbuf[:0]
	for i := c.rxInFlight; i < c.fifos.RxDepth; i++ {
		b := c.arena.alloc(c)
		if b == nil {
			break
		}
		buf = append(buf, c.arena.entry(b))
	}
	if len(buf) == 0 {
		return nil // nothing to do
	}
	status, count := fifoWrite(c.fifos.Rx, buf)
	if status != zx.ErrOk {
		return zx.Error{Status: status, Text: "eth.Client.RX"}
	}

	c.rxInFlight += count
	for _, entry := range buf[count:] {
		c.arena.free(c, c.arena.bufferFromEntry(entry))
	}
	return nil
}

// WaitSend blocks until it is possible to allocate a send buffer,
// or the client is closed.
func (c *Client) WaitSend() error {
	for {
		c.mu.Lock()
		err := c.txCompleteLocked()
		canSend := c.txInFlight < c.fifos.TxDepth
		c.mu.Unlock()
		if err != nil {
			return err
		}
		if canSend {
			return nil
		}
		// Errors from waiting handled in txComplete.
		zxwait.Wait(c.fifos.Tx, zx.SignalFIFOReadable|zx.SignalFIFOPeerClosed, zx.TimensecInfinite)
	}
}

// WaitRecv blocks until it is possible to receive a buffer,
// or the client is closed.
func (c *Client) WaitRecv() {
	for {
		obs, err := zxwait.Wait(c.fifos.Rx, zx.SignalFIFOReadable|zx.SignalFIFOPeerClosed|ZXSIO_ETH_SIGNAL_STATUS, zx.TimensecInfinite)
		if err != nil || obs&zx.SignalFIFOPeerClosed != 0 {
			c.Close()
		} else if obs&ZXSIO_ETH_SIGNAL_STATUS != 0 {
			// TODO(): The wired Ethernet should receive this signal upon being
			// hooked up with a (an active) Ethernet cable.
			if status, err := c.GetStatus(); err != nil {
				logger.WarnTf("eth", "status error: %s", err)
			} else {
				logger.VLogTf(logger.TraceVerbosity, "eth", "status: %d", status)

				c.mu.Lock()
				switch status {
				case LinkDown:
					c.changeStateLocked(link.StateDown)
				case LinkUp:
					c.changeStateLocked(link.StateStarted)
				}
				c.mu.Unlock()

				continue
			}
		}

		break
	}
}

// ListenTX tells the ethernet driver to reflect all transmitted
// packets back to this ethernet client.
func (c *Client) ListenTX() error {
	if status, err := c.device.ListenStart(); err != nil {
		return err
	} else if err := checkStatus(status, "ListenStart"); err != nil {
		return err
	}
	return nil
}

type LinkStatus uint32

const (
	LinkDown LinkStatus = 0
	LinkUp   LinkStatus = 1
)

// GetStatus returns the underlying device's status.
func (c *Client) GetStatus() (LinkStatus, error) {
	status, err := c.device.GetStatus()
	return LinkStatus(status & ethernet.DeviceStatusOnline), err
}
