// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package eth

import (
	"fmt"
	"math"
	"math/bits"
	"reflect"
	"sync"
	"syscall/zx"
	"syscall/zx/fidl"
	"syscall/zx/zxwait"
	"unsafe"

	"netstack/link"
	"syslog"

	"fidl/fuchsia/hardware/ethernet"

	"gvisor.dev/gvisor/pkg/tcpip"
	"gvisor.dev/gvisor/pkg/tcpip/buffer"
	"gvisor.dev/gvisor/pkg/tcpip/stack"
)

// #include <zircon/device/ethernet.h>
// #include <zircon/types.h>
import "C"

const tag = "eth"

type FifoEntry = C.struct_eth_fifo_entry

func (e FifoEntry) index() int32 {
	if e.cookie>>32 != cookieMagic {
		panic(fmt.Sprintf("buffer entry has bad cookie: %x", e.cookie))
	}
	return int32(e.cookie)
}

// SetLength sets the length field. It is exported for use in tests which cannot depend on "C".
func (e *FifoEntry) SetLength(length int) {
	e.length = C.uint16_t(length)
}

const bufferSize = 2048

// A Buffer is a segment of memory backed by a mapped VMO.
//
// A Buffer must not outlive its VMO's mapping.
// A Buffer's head must not change (by slicing or appending).
type Buffer []byte

type IOBuffer struct {
	vaddr zx.Vaddr
	size  uint64
}

func MakeIOBuffer(vmo zx.VMO) (IOBuffer, error) {
	size, err := vmo.Size()
	if err != nil {
		return IOBuffer{}, err
	}
	vaddr, err := zx.VMARRoot.Map(0 /* vmarOffset */, vmo, 0 /* vmoOffset */, size, zx.VMFlagPermRead|zx.VMFlagPermWrite)
	if err != nil {
		_ = vmo.Close()
		return IOBuffer{}, err
	}
	return IOBuffer{
		vaddr: vaddr,
		size:  size,
	}, nil
}

func (iob *IOBuffer) buffer(i int32) Buffer {
	return *(*Buffer)(unsafe.Pointer(&reflect.SliceHeader{
		Data: uintptr(iob.vaddr + zx.Vaddr(i)*bufferSize),
		Len:  bufferSize,
		Cap:  bufferSize,
	}))
}

func (iob *IOBuffer) index(b Buffer) int {
	return int((*(*reflect.SliceHeader)(unsafe.Pointer(&b))).Data-uintptr(iob.vaddr)) / bufferSize
}

const cookieMagic = 0x42420102 // used to fill top 32-bits of FifoEntry.cookie

func (iob *IOBuffer) entry(b Buffer) FifoEntry {
	i := iob.index(b)

	return FifoEntry{
		offset: C.uint32_t(i) * bufferSize,
		length: C.uint16_t(len(b)),
		cookie: (cookieMagic << 32) | C.uint64_t(i),
	}
}

func (iob *IOBuffer) BufferFromEntry(e FifoEntry) Buffer {
	return iob.buffer(e.index())[:e.length]
}

func (iob *IOBuffer) Close() error {
	return zx.VMARRoot.Unmap(iob.vaddr, iob.size)
}

const FifoMaxSize = C.ZX_FIFO_MAX_SIZE_BYTES

type entries struct {
	// len(storage) must be a power of two; we rely on this fact to enable
	// masking instead of modulus operations.
	storage []FifoEntry

	// sent, queued, readied are indices modulo (len(storage) << 1). They
	// implement a ring buffer with 3 regions:
	//
	// - sent:queued: entries describing populated buffers, ready to be
	// sent to the driver
	//
	// - queued:readied: entries describing unpopulated buffers, ready to
	// accept outbound data
	//
	// - readied:sent: entries describing buffers currently owned by the
	// driver, not yet returned
	sent, queued, readied uint16
}

func (e *entries) init(depth uint32) {
	*e = entries{}

	// Round up to the next power of two.
	power := bits.Len32(depth-1) + 1
	size := uint32(1 << power)
	e.storage = make([]FifoEntry, size)
}

func (e *entries) mask(val uint16) uint16 {
	return val & uint16(len(e.storage)-1)
}

func (e *entries) mask2(val uint16) uint16 {
	return val & uint16((len(e.storage)<<1)-1)
}

func (e *entries) incrementSent(delta uint16) {
	e.sent = e.mask2(e.sent + delta)
}

func (e *entries) incrementQueued(delta uint16) {
	e.queued = e.mask2(e.queued + delta)
}

func (e *entries) incrementReadied(delta uint16) {
	e.readied = e.mask2(e.readied + delta)
}

func (e *entries) haveQueued() bool {
	return e.sent != e.queued
}

func (e *entries) getReadied() *FifoEntry {
	return &e.storage[e.mask(e.queued)]
}

func (e *entries) haveReadied() bool {
	return e.queued != e.readied
}

func (e *entries) inFlight() uint16 {
	if readied, sent := e.mask(e.readied), e.mask(e.sent); readied > sent {
		return uint16(len(e.storage)) - (readied - sent)
	} else {
		return sent - readied
	}
}

func (e *entries) addReadied(src []FifoEntry) int {
	if readied, sent := e.mask(e.readied), e.mask(e.sent); readied < sent {
		return copy(e.storage[readied:sent], src)
	} else {
		n := copy(e.storage[readied:], src)
		n += copy(e.storage[:sent], src[n:])
		return n
	}
}

func (e *entries) getQueued(dst []FifoEntry) int {
	if sent, queued := e.mask(e.sent), e.mask(e.queued); sent < queued {
		return copy(dst, e.storage[sent:queued])
	} else {
		n := copy(dst, e.storage[sent:])
		n += copy(dst[n:], e.storage[:queued])
		return n
	}
}

type rwStats struct {
	reads, writes tcpip.StatCounter
}

type FifoStats struct {
	// batches is an associative array from read/write batch sizes
	// (indexed at `batchSize-1`) to tcpip.StatCounters of the number of reads
	// and writes of that batch size.
	batches []rwStats
}

func makeFifoStats(depth uint32) FifoStats {
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

	Stats struct {
		Tx, Rx FifoStats
	}

	device ethernet.DeviceWithCtx
	fifos  ethernet.Fifos

	iob IOBuffer

	topopath, filepath string

	mu        sync.Mutex
	state     link.State
	stateFunc func(link.State)

	rx entries
	tx struct {
		mu struct {
			sync.Mutex
			waiters int

			entries entries

			// detached signals to incoming writes that the receiver is unable to service them.
			detached bool
		}
		cond sync.Cond
	}
}

// NewClient creates a new ethernet Client.
func NewClient(clientName string, topopath, filepath string, device ethernet.DeviceWithCtx) (*Client, error) {
	if status, err := device.SetClientName(fidl.Background(), clientName); err != nil {
		return nil, err
	} else if err := checkStatus(status, "SetClientName"); err != nil {
		return nil, err
	}
	// TODO(NET-57): once we support IGMP, don't automatically set multicast promisc true
	if status, err := device.ConfigMulticastSetPromiscuousMode(fidl.Background(), true); err != nil {
		return nil, err
	} else if err := checkStatus(status, "ConfigMulticastSetPromiscuousMode"); err != nil {
		// Some drivers - most notably virtio - don't support this setting.
		if err.(*zx.Error).Status != zx.ErrNotSupported {
			return nil, err
		}
		_ = syslog.WarnTf(tag, "%s", err)
	}
	info, err := device.GetInfo(fidl.Background())
	if err != nil {
		return nil, err
	}
	status, fifos, err := device.GetFifos(fidl.Background())
	if err != nil {
		return nil, err
	} else if err := checkStatus(status, "GetFifos"); err != nil {
		return nil, err
	}

	c := &Client{
		Info:     info,
		device:   device,
		fifos:    *fifos,
		topopath: topopath,
		filepath: filepath,
	}

	c.rx.init(fifos.RxDepth)
	c.tx.mu.entries.init(fifos.TxDepth)

	{
		vmo, err := zx.NewVMO(bufferSize*uint64(len(c.rx.storage)+len(c.tx.mu.entries.storage)), 0)
		if err != nil {
			_ = c.Close()
			return nil, fmt.Errorf("eth: cannot allocate VMO: %w", err)
		}
		if err := vmo.Handle().SetProperty(zx.PropName, []byte(fmt.Sprintf("ethernet.Device.IoBuffer: %s", topopath))); err != nil {
			_ = vmo.Close()
			_ = c.Close()
			return nil, err
		}
		iob, err := MakeIOBuffer(vmo)
		if err != nil {
			_ = vmo.Close()
			_ = c.Close()
			return nil, fmt.Errorf("eth: make IO buffer: %w", err)
		}
		c.iob = iob
		if status, err := device.SetIoBuffer(fidl.Background(), vmo); err != nil {
			_ = c.Close()
			return nil, fmt.Errorf("eth: cannot set IO VMO: %w", err)
		} else if err := checkStatus(status, "SetIoBuffer"); err != nil {
			_ = c.Close()
			return nil, fmt.Errorf("eth: cannot set IO VMO: %w", err)
		}
	}

	var bufferIndex int32
	for i := range c.rx.storage {
		b := c.iob.buffer(bufferIndex)
		bufferIndex++
		c.rx.storage[i] = c.iob.entry(b)
		c.rx.incrementReadied(1)
		c.rx.incrementQueued(1)
	}
	for i := range c.tx.mu.entries.storage {
		b := c.iob.buffer(bufferIndex)
		bufferIndex++
		c.tx.mu.entries.storage[i] = c.iob.entry(b)
		c.tx.mu.entries.incrementReadied(1)
	}
	c.tx.cond.L = &c.tx.mu.Mutex

	c.Stats.Tx = makeFifoStats(fifos.TxDepth)
	c.Stats.Rx = makeFifoStats(fifos.RxDepth)

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

func (c *Client) write(pkts []stack.PacketBuffer) (int, *tcpip.Error) {
	for i := 0; i < len(pkts); {
		c.tx.mu.Lock()
		for {
			if c.tx.mu.detached {
				c.tx.mu.Unlock()
				return i, tcpip.ErrClosedForSend
			}

			if c.tx.mu.entries.haveReadied() {
				break
			}

			c.tx.mu.waiters++
			c.tx.cond.Wait()
			c.tx.mu.waiters--
		}

		// Queue as many remaining packets as possible; if we run out of space, we'll return to the
		// waiting state in the outer loop.
		for {
			pkt := pkts[i]
			i++

			// This is being reused, reset its length to get an appropriately sized buffer.
			entry := c.tx.mu.entries.getReadied()
			entry.SetLength(bufferSize)
			b := c.iob.BufferFromEntry(*entry)
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
			*entry = c.iob.entry(b[:used])
			c.tx.mu.entries.incrementQueued(1)

			if i == len(pkts) || !c.tx.mu.entries.haveReadied() {
				break
			}
		}
		c.tx.mu.Unlock()
		c.tx.cond.Broadcast()
	}

	return len(pkts), nil
}

func (c *Client) WritePacket(_ *stack.Route, _ *stack.GSO, _ tcpip.NetworkProtocolNumber, pkt stack.PacketBuffer) *tcpip.Error {
	_, err := c.write([]stack.PacketBuffer{pkt})
	return err
}

func (c *Client) WritePackets(_ *stack.Route, _ *stack.GSO, pkts []stack.PacketBuffer, _ tcpip.NetworkProtocolNumber) (int, *tcpip.Error) {
	return c.write(pkts)
}

func (c *Client) WriteRawPacket(vv buffer.VectorisedView) *tcpip.Error {
	_, err := c.write([]stack.PacketBuffer{{
		Data: vv,
	}})
	return err
}

func (c *Client) Attach(dispatcher stack.NetworkDispatcher) {
	c.dispatcher = dispatcher

	// dispatcher may be nil when the NIC in stack.Stack is being removed.
	if dispatcher == nil {
		return
	}

	c.wg.Add(1)
	go func() {
		defer c.wg.Done()
		if err := func() error {
			scratch := make([]FifoEntry, c.fifos.TxDepth)
			for {
				c.tx.mu.Lock()
				detached := c.tx.mu.detached
				c.tx.mu.Unlock()
				if detached {
					return nil
				}

				if _, err := zxwait.Wait(c.fifos.Tx, zx.SignalFIFOReadable|zx.SignalFIFOPeerClosed, zx.TimensecInfinite); err != nil {
					return err
				}

				switch status, count := FifoRead(c.fifos.Tx, scratch); status {
				case zx.ErrOk:
					c.Stats.Tx.Reads(count).Increment()
					c.tx.mu.Lock()
					n := c.tx.mu.entries.addReadied(scratch[:count])
					c.tx.mu.entries.incrementReadied(uint16(n))
					c.tx.mu.Unlock()
					c.tx.cond.Broadcast()

					if n := uint32(n); count != n {
						return fmt.Errorf("fifoRead(TX): tx_depth invariant violation; observed=%d expected=%d", c.fifos.TxDepth-n+count, c.fifos.TxDepth)
					}
				default:
					return &zx.Error{Status: status, Text: "FifoRead(TX)"}
				}
			}
		}(); err != nil {
			c.mu.Lock()
			state := c.state
			c.mu.Unlock()
			if state != link.StateClosed {
				_ = syslog.WarnTf(tag, "TX read loop: %s", err)
			}
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
			scratch := make([]FifoEntry, c.fifos.TxDepth)
			for {
				var batch []FifoEntry
				c.tx.mu.Lock()
				for {
					if c.tx.mu.detached {
						c.tx.mu.Unlock()
						return nil
					}
					if batchSize := len(scratch) - int(c.tx.mu.entries.inFlight()); batchSize != 0 {
						if c.tx.mu.entries.haveQueued() {
							if c.tx.mu.waiters == 0 || !c.tx.mu.entries.haveReadied() {
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
				n := c.tx.mu.entries.getQueued(batch)
				c.tx.mu.entries.incrementSent(uint16(n))
				c.tx.mu.Unlock()

				switch status, count := FifoWrite(c.fifos.Tx, batch[:n]); status {
				case zx.ErrOk:
					if n := uint32(n); count != n {
						return fmt.Errorf("fifoWrite(TX): tx_depth invariant violation; observed=%d expected=%d", c.fifos.TxDepth-n+count, c.fifos.TxDepth)
					}
					c.Stats.Tx.Writes(count).Increment()
				default:
					return &zx.Error{Status: status, Text: "fifoWrite(TX)"}
				}
			}
		}(); err != nil {
			c.mu.Lock()
			state := c.state
			c.mu.Unlock()
			if state != link.StateClosed {
				_ = syslog.WarnTf(tag, "TX write loop: %s", err)
			}
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
			scratch := make([]FifoEntry, c.fifos.RxDepth)
			for {
				if batchSize := len(scratch) - int(c.rx.inFlight()); batchSize != 0 && c.rx.haveQueued() {
					n := c.rx.getQueued(scratch[:batchSize])
					c.rx.incrementSent(uint16(n))

					status, count := FifoWrite(c.fifos.Rx, scratch[:n])
					switch status {
					case zx.ErrOk:
						c.Stats.Rx.Writes(count).Increment()
						if n := uint32(n); count != n {
							return fmt.Errorf("fifoWrite(RX): tx_depth invariant violation; observed=%d expected=%d", c.fifos.RxDepth-n+count, c.fifos.RxDepth)
						}
					default:
						return &zx.Error{Status: status, Text: "fifoWrite(RX)"}
					}
				}

				for c.rx.haveReadied() {
					entry := c.rx.getReadied()

					var emptyLinkAddress tcpip.LinkAddress
					dispatcher.DeliverNetworkPacket(c, emptyLinkAddress, emptyLinkAddress, 0, stack.PacketBuffer{
						Data: append(buffer.View(nil), c.iob.BufferFromEntry(*entry)...).ToVectorisedView(),
					})

					// This entry is going back to the driver; it can be reused.
					entry.SetLength(bufferSize)
					c.rx.incrementQueued(1)
				}

				for {
					signals := zx.Signals(zx.SignalFIFOReadable | zx.SignalFIFOPeerClosed | ethernet.SignalStatus)
					if int(c.rx.inFlight()) != len(scratch) && c.rx.haveQueued() {
						signals |= zx.SignalFIFOWritable
					}
					obs, err := zxwait.Wait(c.fifos.Rx, signals, zx.TimensecInfinite)
					if err != nil {
						return err
					}
					if obs&zx.Signals(ethernet.SignalStatus) != 0 {
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
					if obs&zx.SignalFIFOReadable != 0 {
						switch status, count := FifoRead(c.fifos.Rx, scratch); status {
						case zx.ErrOk:
							c.Stats.Rx.Reads(count).Increment()
							n := c.rx.addReadied(scratch[:count])
							c.rx.incrementReadied(uint16(n))

							if n := uint32(n); count != n {
								return fmt.Errorf("fifoRead(RX): tx_depth invariant violation; observed=%d expected=%d", c.fifos.RxDepth-n+count, c.fifos.RxDepth)
							}
						default:
							return &zx.Error{Status: status, Text: "FifoRead(RX)"}
						}
						break
					}
					if obs&zx.SignalFIFOWritable != 0 {
						break
					}
				}
			}
		}(); err != nil {
			c.mu.Lock()
			state := c.state
			c.mu.Unlock()
			if state != link.StateClosed {
				_ = syslog.WarnTf(tag, "RX loop: %s", err)
			}
		}
	}()
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

func (c *Client) Topopath() string {
	return c.topopath
}

func (c *Client) Filepath() string {
	return c.filepath
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
		if status, err := c.device.Start(fidl.Background()); err != nil {
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
		if err := c.device.Stop(fidl.Background()); err != nil {
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
	err := c.device.Stop(fidl.Background())
	if err != nil {
		err = fmt.Errorf("fuchsia.hardware.ethernet.Device.Stop() for path %q failed: %s", c.topopath, err)
	}

	if err := c.fifos.Tx.Close(); err != nil {
		_ = syslog.WarnTf(tag, "failed to close tx fifo: %s", err)
	}
	if err := c.fifos.Rx.Close(); err != nil {
		_ = syslog.WarnTf(tag, "failed to close rx fifo: %s", err)
	}
	if err := c.iob.Close(); err != nil {
		_ = syslog.WarnTf(tag, "failed to close IO buffer: %s", err)
	}
	c.changeStateLocked(link.StateClosed)

	return err
}

func (c *Client) SetPromiscuousMode(enabled bool) error {
	c.mu.Lock()
	defer c.mu.Unlock()

	if status, err := c.device.SetPromiscuousMode(fidl.Background(), enabled); err != nil {
		return err
	} else if err := checkStatus(status, "SetPromiscuousMode"); err != nil {
		return err
	}
	return nil
}

func FifoWrite(handle zx.Handle, b []FifoEntry) (zx.Status, uint32) {
	var actual uint
	data := unsafe.Pointer((*reflect.SliceHeader)(unsafe.Pointer(&b)).Data)
	status := zx.Sys_fifo_write(handle, uint(unsafe.Sizeof(FifoEntry{})), data, uint(len(b)), &actual)
	return status, uint32(actual)
}

func FifoRead(handle zx.Handle, b []FifoEntry) (zx.Status, uint32) {
	var actual uint
	data := unsafe.Pointer((*reflect.SliceHeader)(unsafe.Pointer(&b)).Data)
	status := zx.Sys_fifo_read(handle, uint(unsafe.Sizeof(FifoEntry{})), data, uint(len(b)), &actual)
	return status, uint32(actual)
}

// ListenTX tells the ethernet driver to reflect all transmitted
// packets back to this ethernet client.
func (c *Client) ListenTX() error {
	if status, err := c.device.ListenStart(fidl.Background()); err != nil {
		return err
	} else if err := checkStatus(status, "ListenStart"); err != nil {
		return err
	}
	return nil
}

type LinkStatus int

const (
	LinkDown LinkStatus = iota
	LinkUp
)

// GetStatus returns the underlying device's status.
func (c *Client) GetStatus() (LinkStatus, error) {
	status, err := c.device.GetStatus(fidl.Background())
	linkStatus := LinkStatus(status & ethernet.DeviceStatusOnline)
	syslog.InfoTf(tag, "fuchsia.hardware.ethernet.Device.GetStatus() = %s", linkStatus)
	return linkStatus, err
}
