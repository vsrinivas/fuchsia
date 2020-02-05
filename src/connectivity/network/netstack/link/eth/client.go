// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package eth

import (
	"fmt"
	"math"
	"reflect"
	"sync"
	"syscall/zx"
	"syscall/zx/zxwait"
	"unsafe"

	"netstack/link"
	"syslog"

	"fidl/fuchsia/hardware/ethernet"

	"gvisor.dev/gvisor/pkg/tcpip"
	"gvisor.dev/gvisor/pkg/tcpip/buffer"
	"gvisor.dev/gvisor/pkg/tcpip/stack"
)

// #cgo CFLAGS: -I${SRCDIR}/../../../zircon/public
// #include <zircon/device/ethernet.h>
import "C"

const zxsioEthSignalStatus = zx.SignalUser0
const tag = "eth"

const FifoEntrySize = C.sizeof_struct_eth_fifo_entry

var _ link.Controller = (*Client)(nil)

var _ stack.LinkEndpoint = (*Client)(nil)
var _ stack.GSOEndpoint = (*Client)(nil)

// A Client is an ethernet client.
// It connects to a zircon ethernet driver using a FIFO-based protocol.
// The protocol is described in system/fidl/fuchsia-hardware-ethernet/ethernet.fidl.
type Client struct {
	dispatcher stack.NetworkDispatcher
	wg         sync.WaitGroup

	Info ethernet.Info

	device ethernet.Device
	fifos  ethernet.Fifos
	path   string

	mu        sync.Mutex
	state     link.State
	stateFunc func(link.State)
	arena     *Arena

	rxQueue []C.struct_eth_fifo_entry
	tx      struct {
		mu struct {
			sync.Mutex
			waiters int

			storage []C.struct_eth_fifo_entry

			// available is the index after the last available entry.
			available int

			// queued is the index after the last queued entry.
			queued int

			// detached signals to incoming writes that the receiver is unable to service them.
			detached bool
		}
		cond sync.Cond
	}
}

// NewClient creates a new ethernet Client.
func NewClient(clientName string, topo string, device ethernet.Device, arena *Arena) (*Client, error) {
	if status, err := device.SetClientName(clientName); err != nil {
		return nil, err
	} else if err := checkStatus(status, "SetClientName"); err != nil {
		return nil, err
	}
	// TODO(NET-57): once we support IGMP, don't automatically set multicast promisc true
	if status, err := device.ConfigMulticastSetPromiscuousMode(true); err != nil {
		return nil, err
	} else if err := checkStatus(status, "ConfigMulticastSetPromiscuousMode"); err != nil {
		// Some drivers - most notably virtio - don't support this setting.
		if err.(*zx.Error).Status != zx.ErrNotSupported {
			return nil, err
		}
		_ = syslog.WarnTf(tag, "%s", err)
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

	c := &Client{
		Info:   info,
		device: device,
		fifos:  *fifos,
		path:   topo,
		arena:  arena,
		// TODO: use 2x depth so that the driver always has rx buffers available. Be careful to
		// preserve the invariant: len(rxQueue) >= cap(rxQueue) - RxDepth.
		rxQueue: make([]C.struct_eth_fifo_entry, 0, fifos.RxDepth),
	}
	for i := 0; i < cap(c.rxQueue); i++ {
		b := arena.alloc(c)
		if b == nil {
			return nil, fmt.Errorf("%s: failed to allocate initial RX buffer %d/%d", tag, i, cap(c.rxQueue))
		}
		c.rxQueue = append(c.rxQueue, arena.entry(b))
	}
	c.tx.mu.storage = make([]C.struct_eth_fifo_entry, 0, 2*fifos.TxDepth)
	for i := 0; i < cap(c.tx.mu.storage); i++ {
		b := arena.alloc(c)
		if b == nil {
			return nil, fmt.Errorf("%s: failed to allocate initial TX buffer %d/%d", tag, i, cap(c.tx.mu.storage))
		}
		c.tx.mu.storage = append(c.tx.mu.storage, arena.entry(b))
		c.tx.mu.available++
	}
	c.tx.cond.L = &c.tx.mu.Mutex

	c.mu.Lock()
	defer c.mu.Unlock()
	if err := func() error {
		h, err := c.arena.iovmo.Handle().Duplicate(zx.RightSameRights)
		if err != nil {
			return fmt.Errorf("%s: failed to duplicate vmo: %s", tag, err)
		}
		if status, err := device.SetIoBuffer(zx.VMO(h)); err != nil {
			return err
		} else if err := checkStatus(status, "SetIoBuffer"); err != nil {
			return err
		}
		return nil
	}(); err != nil {
		_ = c.closeLocked()
		return nil, err
	}

	return c, nil
}

func (c *Client) MTU() uint32 { return c.Info.Mtu }

func (c *Client) Capabilities() stack.LinkEndpointCapabilities {
	// TODO(tamird/brunodalbo): expose hardware offloading capabilities.
	return stack.CapabilitySoftwareGSO
}

func (c *Client) MaxHeaderLength() uint16 {
	return 0
}

func (c *Client) LinkAddress() tcpip.LinkAddress {
	return tcpip.LinkAddress(c.Info.Mac.Octets[:])
}

func (c *Client) write(pkts []tcpip.PacketBuffer) (int, *tcpip.Error) {
	for i := 0; i < len(pkts); {
		c.tx.mu.Lock()
		for {
			if c.tx.mu.detached {
				c.tx.mu.Unlock()
				return i, tcpip.ErrClosedForSend
			}

			// queued can never exceed available because both are indices and available is never zero
			// because it represents a storage array double the size of the tx fifo depth.
			if c.tx.mu.queued != c.tx.mu.available {
				break
			}

			c.tx.mu.waiters++
			c.tx.cond.Wait()
			c.tx.mu.waiters--
		}

		prevQueued := c.tx.mu.queued
		// Queue as many remaining packets as possible; if we run out of space, we'll return to the
		// waiting state in the outer loop.
		for _, pkt := range pkts[i:] {
			// This is being reused, reset its length to get an appropriately sized buffer.
			entry := &c.tx.mu.storage[c.tx.mu.queued]
			entry.length = bufferSize
			b := c.arena.bufferFromEntry(*entry)
			used := copy(b, pkt.Header.View())
			offset := pkt.DataOffset
			size := pkt.DataSize
			// Some code paths do not set DataSize; a value of zero means "use all the data provided".
			if size == 0 {
				size = pkt.Data.Size()
			}
			for _, v := range pkt.Data.Views() {
				if size == 0 {
					break
				}
				if offset > len(v) {
					offset -= len(v)
					continue
				} else {
					v = v[offset:]
					offset = 0
				}
				if len(v) > size {
					v = v[:size]
				}
				size -= len(v)
				used += copy(b[used:], v)
			}
			*entry = c.arena.entry(b[:used])
			c.tx.mu.queued++
			if c.tx.mu.queued == c.tx.mu.available {
				break
			}
		}
		postQueued := c.tx.mu.queued
		c.tx.mu.Unlock()
		c.tx.cond.Broadcast()

		i += postQueued - prevQueued
	}

	return len(pkts), nil
}

func (c *Client) WritePacket(_ *stack.Route, _ *stack.GSO, _ tcpip.NetworkProtocolNumber, pkt tcpip.PacketBuffer) *tcpip.Error {
	_, err := c.write([]tcpip.PacketBuffer{pkt})
	return err
}

func (c *Client) WritePackets(_ *stack.Route, _ *stack.GSO, pkts []tcpip.PacketBuffer, _ tcpip.NetworkProtocolNumber) (int, *tcpip.Error) {
	return c.write(pkts)
}

func (c *Client) WriteRawPacket(vv buffer.VectorisedView) *tcpip.Error {
	_, err := c.write([]tcpip.PacketBuffer{{
		Data: vv,
	}})
	return err
}

func (c *Client) Attach(dispatcher stack.NetworkDispatcher) {
	c.wg.Add(1)
	go func() {
		defer c.wg.Done()
		if err := func() error {
			scratch := make([]C.struct_eth_fifo_entry, c.fifos.TxDepth)
			for {
				if _, err := zxwait.Wait(c.fifos.Tx, zx.SignalFIFOReadable|zx.SignalFIFOPeerClosed, zx.TimensecInfinite); err != nil {
					return err
				}

				switch status, count := fifoRead(c.fifos.Tx, scratch); status {
				case zx.ErrOk:
					c.tx.mu.Lock()
					c.tx.mu.available += copy(c.tx.mu.storage[c.tx.mu.available:], scratch[:count])
					c.tx.mu.Unlock()
					c.tx.cond.Broadcast()
				default:
					return &zx.Error{Status: status, Text: "fifoRead(TX)"}
				}
			}
		}(); err != nil {
			_ = syslog.WarnTf(tag, "TX read loop: %s", err)
			c.tx.mu.Lock()
			c.tx.mu.detached = true
			c.tx.mu.Unlock()
			c.tx.cond.Broadcast()
		}
	}()

	c.wg.Add(1)
	go func() {
		defer c.wg.Done()
		if err := func() error {
			scratch := make([]C.struct_eth_fifo_entry, c.fifos.TxDepth)
			for {
				var batch []C.struct_eth_fifo_entry
				c.tx.mu.Lock()
				for {
					if batchSize := len(scratch) - (len(c.tx.mu.storage) - c.tx.mu.available); batchSize != 0 {
						if c.tx.mu.queued != 0 {
							// We have queued packets.
							if c.tx.mu.waiters == 0 || c.tx.mu.queued == c.tx.mu.available {
								// No application threads are waiting OR application threads are waiting on the
								// reader.
								//
								// This condition is an optimization; if application threads are waiting when
								// buffers are available then we were probably just woken up by the reader having
								// retrieved buffers from the fifo. We avoid creating a batch until the application
								// threads have all been satisfied, or until the buffers have all been used up.
								batch = scratch[:batchSize]
								break
							}
						}
					}
					c.tx.cond.Wait()
				}
				n := copy(batch, c.tx.mu.storage[:c.tx.mu.queued])
				c.tx.mu.available = copy(c.tx.mu.storage, c.tx.mu.storage[n:c.tx.mu.available])
				c.tx.mu.queued -= n
				c.tx.mu.Unlock()

				switch status, count := fifoWrite(c.fifos.Tx, batch[:n]); status {
				case zx.ErrOk:
					if n := uint32(n); count != n {
						return fmt.Errorf("fifoWrite(TX): tx_depth invariant violation; observed=%d expected=%d", c.fifos.TxDepth-n+count, c.fifos.TxDepth)
					}
				default:
					return &zx.Error{Status: status, Text: "fifoWrite(TX)"}
				}
			}
		}(); err != nil {
			_ = syslog.WarnTf(tag, "TX write loop: %s", err)
			c.tx.mu.Lock()
			c.tx.mu.detached = true
			c.tx.mu.Unlock()
			c.tx.cond.Broadcast()
		}
	}()

	c.wg.Add(1)
	go func() {
		defer c.wg.Done()
		if err := func() error {
			for {
				if len(c.rxQueue) != 0 {
					status, sent := fifoWrite(c.fifos.Rx, c.rxQueue)
					switch status {
					case zx.ErrOk:
					default:
						return &zx.Error{Status: status, Text: "fifoWrite(RX)"}
					}
					c.rxQueue = append(c.rxQueue[:0], c.rxQueue[sent:]...)
				}

				for {
					signals := zx.Signals(zx.SignalFIFOReadable | zx.SignalFIFOPeerClosed | zxsioEthSignalStatus)
					if len(c.rxQueue) != 0 {
						signals |= zx.SignalFIFOWritable
					}
					obs, err := zxwait.Wait(c.fifos.Rx, signals, zx.TimensecInfinite)
					if err != nil {
						return err
					}
					if obs&zxsioEthSignalStatus != 0 {
						if status, err := c.GetStatus(); err != nil {
							_ = syslog.WarnTf(tag, "status error: %s", err)
						} else {
							_ = syslog.VLogTf(syslog.TraceVerbosity, tag, "status: %d", status)

							c.mu.Lock()
							switch status {
							case LinkDown:
								c.changeStateLocked(link.StateDown)
							case LinkUp:
								c.changeStateLocked(link.StateStarted)
							}
							c.mu.Unlock()
						}
					}
					if obs&(zx.SignalFIFOReadable) != 0 {
						dst := c.rxQueue[len(c.rxQueue):cap(c.rxQueue)]
						switch status, received := fifoRead(c.fifos.Rx, dst); status {
						case zx.ErrOk:
							c.rxQueue = c.rxQueue[:uint32(len(c.rxQueue))+received]
							for i, entry := range dst[:received] {
								var emptyLinkAddress tcpip.LinkAddress
								dispatcher.DeliverNetworkPacket(c, emptyLinkAddress, emptyLinkAddress, 0, tcpip.PacketBuffer{
									Data: append(buffer.View(nil), c.arena.bufferFromEntry(entry)...).ToVectorisedView(),
								})
								// This entry is going back to the driver; it can be reused.
								dst[i].length = bufferSize
							}
						default:
							return &zx.Error{Status: status, Text: "fifoRead(RX)"}
						}
					}
					if obs&(zx.SignalFIFOWritable) != 0 {
						break
					}
				}
			}
		}(); err != nil {
			_ = syslog.WarnTf(tag, "RX loop: %s", err)
		}
	}()

	c.dispatcher = dispatcher
}

func (c *Client) IsAttached() bool {
	return c.dispatcher != nil
}

// Wait implements stack.LinkEndpoint. It blocks until an error in the dispatch
// goroutine(s) spawned in Attach occurs.
func (c *Client) Wait() {
	c.wg.Wait()
}

func (c *Client) GSOMaxSize() uint32 {
	// There's no limit on how much data we can take in a single software GSO write.
	return math.MaxUint32
}

func checkStatus(status int32, text string) error {
	if status := zx.Status(status); status != zx.ErrOk {
		return &zx.Error{Status: status, Text: text}
	}
	return nil
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
		err = fmt.Errorf("fuchsia.hardware.ethernet.Device.Stop() for path %q failed: %s", c.path, err)
	}

	if err := c.fifos.Tx.Close(); err != nil {
		_ = syslog.WarnTf(tag, "failed to close tx fifo: %s", err)
	}
	if err := c.fifos.Rx.Close(); err != nil {
		_ = syslog.WarnTf(tag, "failed to close rx fifo: %s", err)
	}
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
