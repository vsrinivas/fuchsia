// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package eth

import (
	"context"
	"fmt"
	"math"
	"reflect"
	"sync"
	"syscall/zx"
	"unsafe"

	"gen/netstack/link/eth"

	"go.fuchsia.dev/fuchsia/src/connectivity/network/netstack/link"
	"go.fuchsia.dev/fuchsia/src/connectivity/network/netstack/link/fifo"
	syslog "go.fuchsia.dev/fuchsia/src/lib/syslog/go"

	"fidl/fuchsia/hardware/ethernet"
	"fidl/fuchsia/hardware/network"

	"gvisor.dev/gvisor/pkg/tcpip"
	"gvisor.dev/gvisor/pkg/tcpip/buffer"
	"gvisor.dev/gvisor/pkg/tcpip/header"
	"gvisor.dev/gvisor/pkg/tcpip/stack"
)

// #include <zircon/device/ethernet.h>
// #include <zircon/types.h>
import "C"

const tag = "eth"

const bufferSize = 2048

// Buffer is a segment of memory backed by a mapped VMO.
//
// A Buffer must not outlive its VMO's mapping.
// A Buffer's head must not change (by slicing or appending).
type Buffer []byte

type IOBuffer struct {
	fifo.MappedVMO
}

func (iob *IOBuffer) buffer(i int32) Buffer {
	return iob.GetData(uint64(i*bufferSize), bufferSize)
}

func (iob *IOBuffer) index(b Buffer) int {
	return int((*(*reflect.SliceHeader)(unsafe.Pointer(&b))).Data-uintptr(iob.GetPointer(0))) / bufferSize
}

func (iob *IOBuffer) entry(b Buffer) eth.FifoEntry {
	i := iob.index(b)

	return eth.NewFifoEntry(uint32(i*bufferSize), uint16(len(b)), int32(i))
}

func (iob *IOBuffer) BufferFromEntry(e eth.FifoEntry) Buffer {
	return iob.buffer(e.Index())[:e.Length()]
}

var _ link.Controller = (*Client)(nil)
var _ link.Observer = (*Client)(nil)

var _ stack.LinkEndpoint = (*Client)(nil)
var _ stack.GSOEndpoint = (*Client)(nil)

// Client is an ethernet client.
//
// It connects to a zircon ethernet driver using a FIFO-based protocol.
// The protocol is described in system/fidl/fuchsia-hardware-ethernet/ethernet.fidl.
type Client struct {
	dispatcher stack.NetworkDispatcher
	wg         sync.WaitGroup

	Info ethernet.Info

	device ethernet.DeviceWithCtx

	iob IOBuffer

	topopath, filepath string

	mu struct {
		sync.Mutex
		closed              bool
		onLinkClosed        func()
		onLinkOnlineChanged func(bool)
	}

	handler eth.Handler
}

// NewClient creates a new ethernet Client.
func NewClient(clientName string, topopath, filepath string, device ethernet.DeviceWithCtx) (*Client, error) {
	if status, err := device.SetClientName(context.Background(), clientName); err != nil {
		return nil, err
	} else if err := checkStatus(status, "SetClientName"); err != nil {
		return nil, err
	}
	// TODO(NET-57): once we support IGMP, don't automatically set multicast promisc true
	if status, err := device.ConfigMulticastSetPromiscuousMode(context.Background(), true); err != nil {
		return nil, err
	} else if err := checkStatus(status, "ConfigMulticastSetPromiscuousMode"); err != nil {
		// Some drivers - most notably virtio - don't support this setting.
		if err.(*zx.Error).Status != zx.ErrNotSupported {
			return nil, err
		}
		_ = syslog.WarnTf(tag, "%s", err)
	}
	info, err := device.GetInfo(context.Background())
	if err != nil {
		return nil, err
	}
	status, fifos, err := device.GetFifos(context.Background())
	if err != nil {
		return nil, err
	} else if err := checkStatus(status, "GetFifos"); err != nil {
		return nil, err
	}

	c := &Client{
		Info:     info,
		device:   device,
		topopath: topopath,
		filepath: filepath,
		handler: eth.Handler{
			TxDepth: fifos.TxDepth,
			RxDepth: fifos.RxDepth,
			RxFifo:  fifos.Rx,
			TxFifo:  fifos.Tx,
		},
	}

	rxStorage := int(c.handler.InitRx(uint16(fifos.RxDepth * 2)))
	txStorage := int(c.handler.InitTx(uint16(fifos.TxDepth * 2)))

	{
		mappedVmo, vmo, err := fifo.NewMappedVMO(bufferSize*uint64(rxStorage+txStorage), fmt.Sprintf("ethernet.Device.IoBuffer: %s", topopath))
		if err != nil {
			_ = c.Close()
			return nil, fmt.Errorf("eth: NewMappedVMO: %w", err)
		}
		c.iob = IOBuffer{MappedVMO: mappedVmo}
		if status, err := device.SetIoBuffer(context.Background(), vmo); err != nil {
			_ = c.Close()
			return nil, fmt.Errorf("eth: cannot set IO VMO: %w", err)
		} else if err := checkStatus(status, "SetIoBuffer"); err != nil {
			_ = c.Close()
			return nil, fmt.Errorf("eth: cannot set IO VMO: %w", err)
		}
	}

	var bufferIndex int32
	for i := 0; i < rxStorage; i++ {
		b := c.iob.buffer(bufferIndex)
		bufferIndex++
		c.handler.PushInitialRx(c.iob.entry(b))
	}
	for i := 0; i < txStorage; i++ {
		b := c.iob.buffer(bufferIndex)
		bufferIndex++
		c.handler.PushInitialTx(c.iob.entry(b))
	}

	c.handler.Stats.Rx.FifoStats = fifo.MakeFifoStats(fifos.RxDepth)
	c.handler.Stats.Tx.FifoStats = fifo.MakeFifoStats(fifos.TxDepth)

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

func (c *Client) write(pkts stack.PacketBufferList) (int, *tcpip.Error) {
	return c.handler.ProcessWrite(pkts, func(entry *eth.FifoEntry, pkt *stack.PacketBuffer) {
		entry.SetLength(bufferSize)
		b := c.iob.BufferFromEntry(*entry)
		used := copy(b, pkt.Header.View())
		for _, v := range pkt.Data.Views() {
			used += copy(b[used:], v)
		}
		*entry = c.iob.entry(b[:used])
	})
}

func (c *Client) WritePacket(_ *stack.Route, _ *stack.GSO, _ tcpip.NetworkProtocolNumber, pkt *stack.PacketBuffer) *tcpip.Error {
	var pkts stack.PacketBufferList
	pkts.PushBack(pkt)
	_, err := c.write(pkts)
	return err
}

func (c *Client) WritePackets(_ *stack.Route, _ *stack.GSO, pkts stack.PacketBufferList, _ tcpip.NetworkProtocolNumber) (int, *tcpip.Error) {
	return c.write(pkts)
}

func (c *Client) WriteRawPacket(vv buffer.VectorisedView) *tcpip.Error {
	var pkts stack.PacketBufferList
	pkts.PushBack(&stack.PacketBuffer{
		Data: vv,
	})
	_, err := c.write(pkts)
	return err
}

func (c *Client) Attach(dispatcher stack.NetworkDispatcher) {
	c.dispatcher = dispatcher

	detachWithError := func(cause error) {
		c.mu.Lock()
		closed := c.mu.closed
		c.mu.Unlock()
		if closed {
			return
		}
		if err := c.Close(); err != nil {
			_ = syslog.WarnTf(tag, "error closing device on detach (caused by %s): %s", cause, err)
		} else {
			_ = syslog.WarnTf(tag, "closed device due to %s", cause)
		}
	}

	// dispatcher may be nil when the NIC in stack.Stack is being removed.
	if dispatcher == nil {
		detachWithError(fmt.Errorf("RemoveNIC"))
		return
	}

	c.wg.Add(1)
	go func() {
		defer c.wg.Done()
		if err := c.handler.TxReceiverLoop(func(entry *eth.FifoEntry) bool {
			return entry.Flags()&eth.FifoTXOK != 0
		}); err != nil {
			detachWithError(fmt.Errorf("TX read loop error: %w", err))
		}
	}()

	c.wg.Add(1)
	go func() {
		defer c.wg.Done()
		if err := c.handler.TxSenderLoop(); err != nil {
			detachWithError(fmt.Errorf("TX write loop error: %w", err))
		}
	}()

	c.wg.Add(1)
	go func() {
		defer c.wg.Done()
		if err := c.handler.RxLoop(func(entry *eth.FifoEntry) {
			// Process inbound packet.
			var emptyLinkAddress tcpip.LinkAddress
			dispatcher.DeliverNetworkPacket(emptyLinkAddress, emptyLinkAddress, 0, &stack.PacketBuffer{
				Data: append(buffer.View(nil), c.iob.BufferFromEntry(*entry)...).ToVectorisedView(),
			})

			// This entry is going back to the driver; it can be reused.
			entry.SetLength(bufferSize)
		}, zx.Signals(ethernet.SignalStatus), func() {
			// Process ethernetStatusSignal.
			status, err := c.device.GetStatus(context.Background())
			if err != nil {
				_ = syslog.InfoTf(tag, "fuchsia.hardware.ethernet.Device.GetStatus() error = %s", err)
				return
			}
			_ = syslog.InfoTf(tag, "fuchsia.hardware.ethernet.Device.GetStatus() = %s", status)
			c.mu.Lock()
			fn := c.mu.onLinkOnlineChanged
			c.mu.Unlock()
			fn(status&ethernet.DeviceStatusOnline != 0)
		}); err != nil {
			detachWithError(fmt.Errorf("RX loop error: %w", err))
		}
	}()

	// Spawn a goroutine to clean up the mapped memory once all the handler
	// loops are done.
	go func() {
		c.wg.Wait()
		if err := c.iob.Close(); err != nil {
			_ = syslog.WarnTf(tag, "failed to close IO buffer: %s", err)
		}
		c.mu.Lock()
		fn := c.mu.onLinkClosed
		c.mu.Unlock()
		fn()
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

// ARPHardwareType implements stack.LinkEndpoint.
func (*Client) ARPHardwareType() header.ARPHardwareType {
	return header.ARPHardwareNone
}

// AddHeader implements stack.LinkEndpoint.
func (*Client) AddHeader(_, _ tcpip.LinkAddress, _ tcpip.NetworkProtocolNumber, _ *stack.PacketBuffer) {
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

func (c *Client) SetOnLinkClosed(f func()) {
	c.mu.Lock()
	c.mu.onLinkClosed = f
	c.mu.Unlock()
}

func (c *Client) SetOnLinkOnlineChanged(f func(bool)) {
	c.mu.Lock()
	c.mu.onLinkOnlineChanged = f
	c.mu.Unlock()
}

func (c *Client) Topopath() string {
	return c.topopath
}

func (c *Client) Filepath() string {
	return c.filepath
}

// Up enables the interface.
func (c *Client) Up() error {
	if status, err := c.device.Start(context.Background()); err != nil {
		return err
	} else if err := checkStatus(status, "Start"); err != nil {
		return err
	}
	return nil
}

// Down disables the interface.
func (c *Client) Down() error {
	return c.device.Stop(context.Background())
}

// Close closes a Client, releasing any held resources.
func (c *Client) Close() error {
	c.mu.Lock()
	closed := c.mu.closed
	c.mu.closed = true
	c.mu.Unlock()
	if closed {
		return nil
	}
	c.handler.DetachTx()
	if err := c.device.Stop(context.Background()); err != nil {
		_ = syslog.WarnTf(tag, "fuchsia.hardware.ethernet.Device.Stop() for path %q failed: %s", c.topopath, err)
	}

	if iface, ok := c.device.(*ethernet.DeviceWithCtxInterface); ok {
		if err := iface.Close(); err != nil {
			_ = syslog.WarnTf(tag, "failed to close device handle: %s", err)
		}
	} else {
		_ = syslog.WarnTf(tag, "can't close device interface of type %T", c.device)
	}

	if err := c.handler.TxFifo.Close(); err != nil {
		_ = syslog.WarnTf(tag, "failed to close tx fifo: %s", err)
	}
	if err := c.handler.RxFifo.Close(); err != nil {
		_ = syslog.WarnTf(tag, "failed to close rx fifo: %s", err)
	}

	// Additional cleanup is performed by the watcher goroutine spawned in
	// Attach once all the io loops are done.

	return nil
}

func (c *Client) SetPromiscuousMode(enabled bool) error {
	c.mu.Lock()
	defer c.mu.Unlock()

	if status, err := c.device.SetPromiscuousMode(context.Background(), enabled); err != nil {
		return err
	} else if err := checkStatus(status, "SetPromiscuousMode"); err != nil {
		return err
	}
	return nil
}

// ListenTX tells the ethernet driver to reflect all transmitted
// packets back to this ethernet client.
func (c *Client) ListenTX() error {
	if status, err := c.device.ListenStart(context.Background()); err != nil {
		return err
	} else if err := checkStatus(status, "ListenStart"); err != nil {
		return err
	}
	return nil
}

func (*Client) DeviceClass() network.DeviceClass {
	return network.DeviceClassEthernet
}

func (c *Client) RxStats() *fifo.RxStats {
	return &c.handler.Stats.Rx
}

func (c *Client) TxStats() *fifo.TxStats {
	return &c.handler.Stats.Tx
}
