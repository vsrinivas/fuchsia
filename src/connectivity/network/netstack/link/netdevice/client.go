// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//go:build !build_with_native_toolchain
// +build !build_with_native_toolchain

package netdevice

import (
	"context"
	"fmt"
	"math"
	"math/bits"
	"sync"
	"syscall/zx"
	"syscall/zx/fidl"

	"gen/netstack/link/netdevice"

	"go.fuchsia.dev/fuchsia/src/connectivity/network/netstack/link"
	"go.fuchsia.dev/fuchsia/src/connectivity/network/netstack/link/fifo"
	syslog "go.fuchsia.dev/fuchsia/src/lib/syslog/go"

	"fidl/fuchsia/hardware/network"

	"go.uber.org/multierr"
	"gvisor.dev/gvisor/pkg/tcpip"
	"gvisor.dev/gvisor/pkg/tcpip/buffer"
	"gvisor.dev/gvisor/pkg/tcpip/header"
	"gvisor.dev/gvisor/pkg/tcpip/stack"
)

// #include <zircon/device/network.h>
import "C"

const descriptorLength uint64 = C.sizeof_buffer_descriptor_t
const tag = "netdevice"
const emptyLinkAddress tcpip.LinkAddress = ""

// TODO(https://fxbug.dev/64310): Remove port 0 assumptions once netstack FIDL
// supports ports.
const port0 = 0

type bufferDescriptor = C.buffer_descriptor_t

var _ link.Controller = (*Client)(nil)
var _ link.Observer = (*Client)(nil)

var _ stack.LinkEndpoint = (*Client)(nil)
var _ stack.GSOEndpoint = (*Client)(nil)

// InfoProvider abstracts a common information interface for different Clients.
type InfoProvider interface {
	RxStats() *fifo.RxStats
	TxStats() *fifo.TxStats
	Class() network.DeviceClass
}

var _ InfoProvider = (*Client)(nil)

// A client for a network device that implements the
// fuchsia.hardware.network.Device protocol.
type Client struct {
	dispatcher stack.NetworkDispatcher
	// WaitGroup on all the go routines associated with the Client. See Attach
	// and Wait.
	dispatcherWg sync.WaitGroup

	device     *network.DeviceWithCtxInterface
	port       *network.PortWithCtxInterface
	session    *network.SessionWithCtxInterface
	deviceInfo network.DeviceInfo
	portInfo   network.PortInfo
	config     SessionConfig
	watcher    *network.StatusWatcherWithCtxInterface

	data        fifo.MappedVMO
	descriptors fifo.MappedVMO

	handler netdevice.Handler

	state struct {
		mu struct {
			sync.Mutex
			closed              bool
			onLinkClosed        func()
			onLinkOnlineChanged func(bool)
		}
	}

	mtu struct {
		mu struct {
			sync.Mutex
			value uint32
		}
	}
}

func (c *Client) MTU() uint32 {
	c.mtu.mu.Lock()
	mtu := c.mtu.mu.value
	c.mtu.mu.Unlock()
	return mtu
}

func (*Client) Capabilities() stack.LinkEndpointCapabilities {
	return 0
}

func (c *Client) MaxHeaderLength() uint16 {
	return 0
}

func (c *Client) LinkAddress() tcpip.LinkAddress {
	// NOTE: Plain Client does not have a link address. Only MacAddressingClient
	// does.
	return emptyLinkAddress
}

// write writes a list of packets to the device.
func (c *Client) write(pkts stack.PacketBufferList, protocol tcpip.NetworkProtocolNumber) (int, tcpip.Error) {
	return c.handler.ProcessWrite(pkts, func(descriptorIndex *uint16, pkt *stack.PacketBuffer) {
		descriptor := c.getDescriptor(*descriptorIndex)
		// Reset descriptor to default values before filling it.
		c.resetTxDescriptor(descriptor)

		data := c.getDescriptorData(descriptor)
		n := 0
		for _, v := range pkt.Views() {
			if w := copy(data[n:], v); w != len(v) {
				panic(fmt.Sprintf("failed to copy packet data to descriptor %d, want %d got %d bytes", descriptorIndex, len(v), w))
			} else {
				n += w
			}
		}

		var frameType network.FrameType
		if len(pkt.LinkHeader().View()) != 0 {
			frameType = network.FrameTypeEthernet
		} else {
			switch protocol {
			case header.IPv4ProtocolNumber:
				frameType = network.FrameTypeIpv4
			case header.IPv6ProtocolNumber:
				frameType = network.FrameTypeIpv6
			default:
				_ = syslog.ErrorTf(tag, "can't identify outgoing packet type")
			}
		}
		// Pad tx frame to device requirements.
		for ; n < int(c.deviceInfo.MinTxBufferLength); n++ {
			data[n] = 0
		}

		descriptor.info_type = C.uint(network.InfoTypeNoInfo)
		descriptor.frame_type = C.uchar(frameType)
		descriptor.data_length = C.uint(n)
	})
}

func (c *Client) WritePacket(_ stack.RouteInfo, proto tcpip.NetworkProtocolNumber, pkt *stack.PacketBuffer) tcpip.Error {
	var pkts stack.PacketBufferList
	pkts.PushBack(pkt)
	_, err := c.write(pkts, proto)
	return err
}

func (c *Client) WritePackets(_ stack.RouteInfo, pkts stack.PacketBufferList, proto tcpip.NetworkProtocolNumber) (int, tcpip.Error) {
	return c.write(pkts, proto)
}

func (*Client) WriteRawPacket(*stack.PacketBuffer) tcpip.Error { return &tcpip.ErrNotSupported{} }

func (c *Client) Attach(dispatcher stack.NetworkDispatcher) {
	c.dispatcher = dispatcher

	detachWithError := func(reason error) {
		c.state.mu.Lock()
		closed := c.state.mu.closed
		c.state.mu.Unlock()
		if closed {
			return
		}

		c.handler.DetachTx()
		if err := c.Close(); err != nil {
			_ = syslog.WarnTf(tag, "error closing device on detach (%s): %s", reason, err)
		} else {
			_ = syslog.WarnTf(tag, "closed device: %s", reason)
		}
	}

	// dispatcher may be nil when the NIC in stack.Stack is being removed.
	if dispatcher == nil {
		detachWithError(fmt.Errorf("RemoveNIC"))
		return
	}

	c.dispatcherWg.Add(1)
	go func() {
		defer c.dispatcherWg.Done()
		for {
			status, err := c.watcher.WatchStatus(context.Background())
			if err != nil {
				detachWithError(fmt.Errorf("watcher loop: %w", err))
				return
			}
			if status.HasMtu() {
				c.mtu.mu.Lock()
				c.mtu.mu.value = status.GetMtu()
				c.mtu.mu.Unlock()
			}
			c.state.mu.Lock()
			fn := c.state.mu.onLinkOnlineChanged
			c.state.mu.Unlock()
			fn(status.HasFlags() && status.GetFlags()&network.StatusFlagsOnline != 0)
		}
	}()

	c.dispatcherWg.Add(1)
	go func() {
		defer c.dispatcherWg.Done()
		if err := c.handler.TxReceiverLoop(func(descriptorIndex *uint16) bool {
			descriptor := c.getDescriptor(*descriptorIndex)
			return network.TxReturnFlags(descriptor.return_flags)&network.TxReturnFlagsTxRetError == 0
		}); err != nil {
			detachWithError(fmt.Errorf("TX read loop: %w", err))
		}
		_ = syslog.VLogTf(syslog.DebugVerbosity, tag, "TX read loop finished")
	}()

	c.dispatcherWg.Add(1)
	go func() {
		defer c.dispatcherWg.Done()
		if err := c.handler.TxSenderLoop(); err != nil {
			detachWithError(fmt.Errorf("TX write loop: %w", err))
		}
		_ = syslog.VLogTf(syslog.DebugVerbosity, tag, "TX write loop finished")
	}()

	c.dispatcherWg.Add(1)
	go func() {
		defer c.dispatcherWg.Done()
		if err := c.handler.RxLoop(func(descriptorIndex *uint16) {
			descriptor := c.getDescriptor(*descriptorIndex)
			data := c.getDescriptorData(descriptor)
			view := buffer.NewView(len(data))
			view = view[:copy(view, data)]

			var protocolNumber tcpip.NetworkProtocolNumber
			switch network.FrameType(descriptor.frame_type) {
			case network.FrameTypeIpv4:
				protocolNumber = header.IPv4ProtocolNumber
			case network.FrameTypeIpv6:
				protocolNumber = header.IPv6ProtocolNumber
			}

			dispatcher.DeliverNetworkPacket(emptyLinkAddress, emptyLinkAddress, protocolNumber, stack.NewPacketBuffer(stack.PacketBufferOptions{
				Data: view.ToVectorisedView(),
			}))

			// This entry is going back to the driver; it can be reused.
			c.resetRxDescriptor(descriptor)
		}); err != nil {
			detachWithError(fmt.Errorf("RX loop: %w", err))
		}
		_ = syslog.VLogTf(syslog.DebugVerbosity, tag, "Rx loop finished")
	}()

	// Spawn a goroutine to clean up the mapped memory once all the handler
	// loops are done.
	go func() {
		c.dispatcherWg.Wait()
		if err := multierr.Combine(c.data.Close(), c.descriptors.Close()); err != nil {
			_ = syslog.WarnTf(tag, "failed to close mapped VMOs: %s", err)
		}
		c.state.mu.Lock()
		fn := c.state.mu.onLinkClosed
		c.state.mu.Unlock()
		fn()
	}()
}

func (c *Client) IsAttached() bool {
	return c.dispatcher != nil
}

func (c *Client) Wait() {
	c.dispatcherWg.Wait()
}

func (*Client) ARPHardwareType() header.ARPHardwareType {
	return header.ARPHardwareNone
}

func (*Client) AddHeader(_, _ tcpip.LinkAddress, _ tcpip.NetworkProtocolNumber, _ *stack.PacketBuffer) {
}

// GSOMaxSize implements stack.GSOEndpoint.
func (*Client) GSOMaxSize() uint32 {
	// There's no limit on how much data we can take in a single software GSO write.
	return math.MaxUint32
}

// SupportedGSO implements stack.GSOEndpoint.
func (*Client) SupportedGSO() stack.SupportedGSO {
	// TODO(https://fxbug.dev/76010): expose hardware offloading capabilities.
	return stack.SWGSOSupported
}

func (c *Client) Up() error {
	result, err := c.session.Attach(context.Background(), port0, c.config.RxFrames)
	if err != nil {
		return err
	}
	switch w := result.Which(); w {
	case network.SessionAttachResultResponse:
		return nil
	case network.SessionAttachResultErr:
		return &zx.Error{
			Status: zx.Status(result.Err),
			Text:   "failed to attach session",
		}
	default:
		panic(fmt.Sprintf("unexpected session.Attach variant %d", w))
	}
}

func (c *Client) Down() error {
	result, err := c.session.Detach(context.Background(), port0)
	if err != nil {
		return err
	}
	switch w := result.Which(); w {
	case network.SessionDetachResultResponse:
		return nil
	case network.SessionDetachResultErr:
		return &zx.Error{
			Status: zx.Status(result.Err),
			Text:   "failed to detach session",
		}
	default:
		panic(fmt.Sprintf("unexpected session.Detach variant %d", w))
	}
}

func (c *Client) SetOnLinkClosed(f func()) {
	c.state.mu.Lock()
	c.state.mu.onLinkClosed = f
	c.state.mu.Unlock()
}

func (c *Client) SetOnLinkOnlineChanged(f func(bool)) {
	c.state.mu.Lock()
	c.state.mu.onLinkOnlineChanged = f
	c.state.mu.Unlock()
}

func (c *Client) SetPromiscuousMode(bool) error {
	return fmt.Errorf("promiscuous mode not supported on device")
}

func (c *Client) DeviceClass() network.DeviceClass {
	return c.portInfo.Class
}

// Closes the client and disposes of all its resources.
func (c *Client) Close() error {
	c.state.mu.Lock()
	defer c.state.mu.Unlock()
	if c.state.mu.closed {
		return nil
	}
	c.state.mu.closed = true
	c.handler.DetachTx()

	return multierr.Combine(
		c.device.Close(),
		// Session also has a Close method, make sure we're calling the ChannelProxy
		// one.
		((*fidl.ChannelProxy)(c.session)).Close(),
		c.handler.RxFifo.Close(),
		c.handler.TxFifo.Close(),
		c.watcher.Close(),
		// Additional cleanup is performed by the watcher goroutine spawned in
		// Attach once all the io loops are done.
	)
}

// getDescriptor returns the shared memory representing the descriptor indexed
// by d.
func (c *Client) getDescriptor(d uint16) *bufferDescriptor {
	offset := uint64(d) * c.config.DescriptorLength
	return (*bufferDescriptor)(c.descriptors.GetPointer(offset))
}

// getDescriptorData returns the shared contiguous memory for a descriptor.
func (c *Client) getDescriptorData(desc *bufferDescriptor) []byte {
	if desc.chain_length != 0 {
		panic(fmt.Sprintf("descriptor chaining not implemented, descriptor requested chain of length %d", desc.chain_length))
	}
	offset := uint64(desc.offset) + uint64(desc.head_length)
	return c.data.GetData(offset, uint64(desc.data_length))
}

// resetTxDescriptor resets the the descriptor's fields that a device
// implementation could have changed. It should only be used for
// Tx buffers.
func (c *Client) resetTxDescriptor(descriptor *bufferDescriptor) {
	*descriptor = bufferDescriptor{
		info_type:   C.uint(network.InfoTypeNoInfo),
		offset:      descriptor.offset,
		head_length: C.ushort(c.config.TxHeaderLength),
		// Note: we assert that BufferLength > TxHeaderLength + TxTailLength when
		// the session config is created, so we don't have to worry about overflow
		// here.
		data_length: C.uint(c.config.BufferLength - uint32(c.config.TxHeaderLength) - uint32(c.config.TxTailLength)),
		tail_length: C.ushort(c.config.TxTailLength),
	}
}

// resetRxDescriptor resets the the descriptor's fields that a device
// implementation could have changed. It should only be used for
// Rx buffers.
func (c *Client) resetRxDescriptor(descriptor *bufferDescriptor) {
	*descriptor = bufferDescriptor{
		info_type:   C.uint(network.InfoTypeNoInfo),
		offset:      descriptor.offset,
		data_length: C.uint(c.config.BufferLength),
	}
}

// NewClient creates a new client from a provided network device interface.
func NewClient(ctx context.Context, dev *network.DeviceWithCtxInterface, sessionConfigFactory SessionConfigFactory) (*Client, error) {
	_ = syslog.VLogTf(syslog.DebugVerbosity, tag, "creating network device client")

	deviceInfo, err := dev.GetInfo(ctx)
	if err != nil {
		return nil, fmt.Errorf("failed to get device information: %w", err)
	}
	if !(deviceInfo.HasMinDescriptorLength() &&
		deviceInfo.HasDescriptorVersion() &&
		deviceInfo.HasRxDepth() &&
		deviceInfo.HasTxDepth() &&
		deviceInfo.HasBufferAlignment() &&
		deviceInfo.HasMaxBufferLength() &&
		deviceInfo.HasMinRxBufferLength() &&
		deviceInfo.HasMinTxBufferLength() &&
		deviceInfo.HasMinTxBufferHead() &&
		deviceInfo.HasMinTxBufferTail() &&
		deviceInfo.HasMaxBufferParts()) {
		return nil, fmt.Errorf("incomplete DeviceInfo: %#v", deviceInfo)
	}

	portRequest, port, err := network.NewPortWithCtxInterfaceRequest()
	if err != nil {
		return nil, fmt.Errorf("failed to create port request: %w", err)
	}
	if err := dev.GetPort(ctx, port0, portRequest); err != nil {
		return nil, fmt.Errorf("failed to get port %d: %w", port0, err)
	}

	portInfo, err := port.GetInfo(ctx)
	if err != nil {
		return nil, fmt.Errorf("failed to get port info: %w", err)
	}
	if !(portInfo.HasId() && portInfo.HasClass() && portInfo.HasRxTypes() && portInfo.HasTxTypes()) {
		return nil, fmt.Errorf("incomplete PortInfo: %#v", portInfo)
	}

	portStatus, err := port.GetStatus(ctx)
	if err != nil {
		return nil, fmt.Errorf("failed to create get port status: %w", err)
	}

	config, err := sessionConfigFactory.MakeSessionConfig(deviceInfo, portStatus)
	if err != nil {
		return nil, fmt.Errorf("session configuration factory failed: %w", err)
	}

	if len(config.RxFrames) == 0 {
		config.RxFrames = portInfo.RxTypes
	} else {
		// Verify that requested frame types are valid.
		isValidRxFrame := func(frameType network.FrameType) bool {
			for _, f := range portInfo.RxTypes {
				if f == frameType {
					return true
				}
			}
			return false
		}
		for _, requested := range config.RxFrames {
			if !isValidRxFrame(requested) {
				// NB: Rx types is formatted with %v below so it's printed as a slice of
				// frame types correctly. %s compiles correctly but is interpreted as
				// []uint8, which is itself printed as a string backed by that slice
				// when using %s.
				return nil, fmt.Errorf("requested unsupported frame type: %s. (should be in %v)", requested, portInfo.RxTypes)
			}
		}
	}

	// Descriptor count must be a power of 2.
	config.TxDescriptorCount = 1 << bits.Len16(config.TxDescriptorCount-1)
	config.RxDescriptorCount = 1 << bits.Len16(config.RxDescriptorCount-1)

	totalDescriptors := uint64(config.TxDescriptorCount + config.RxDescriptorCount)
	mappedDataVmo, dataVmo, err := fifo.NewMappedVMO(totalDescriptors*uint64(config.BufferStride), "fuchsia.hardware.network.Device/descriptors")
	if err != nil {
		return nil, fmt.Errorf("failed to create data VMO: %w", err)
	}

	mappedDescVmo, descVmo, err := fifo.NewMappedVMO(totalDescriptors*config.DescriptorLength, "fuchsia.hardware.network.Device/data")
	if err != nil {
		_ = mappedDataVmo.Close()
		return nil, fmt.Errorf("failed to create descriptors VMO: %w", err)
	}

	var sessionInfo network.SessionInfo
	sessionInfo.SetDescriptors(descVmo)
	sessionInfo.SetData(dataVmo)
	sessionInfo.SetDescriptorVersion(C.NETWORK_DEVICE_DESCRIPTOR_VERSION)
	sessionInfo.SetDescriptorLength(uint8(config.DescriptorLength / 8))
	sessionInfo.SetDescriptorCount(config.RxDescriptorCount + config.TxDescriptorCount)
	sessionInfo.SetOptions(config.Options)

	sessionResult, err := dev.OpenSession(ctx, "netstack", sessionInfo)
	if err != nil {
		_ = mappedDataVmo.Close()
		_ = mappedDescVmo.Close()
		return nil, fmt.Errorf("failed to open device session: %w", err)
	}
	if sessionResult.Which() == network.DeviceOpenSessionResultErr {
		_ = mappedDataVmo.Close()
		_ = mappedDescVmo.Close()
		return nil, &zx.Error{Status: zx.Status(sessionResult.Err), Text: "fuchsia.hardware.network/Device.OpenSession"}
	}

	req, watcher, err := network.NewStatusWatcherWithCtxInterfaceRequest()
	if err != nil {
		_ = mappedDataVmo.Close()
		_ = mappedDescVmo.Close()
		return nil, fmt.Errorf("failed to create status watcher request: %w", err)
	}
	if err := port.GetStatusWatcher(ctx, req, network.MaxStatusBuffer); err != nil {
		_ = mappedDataVmo.Close()
		_ = mappedDescVmo.Close()
		_ = watcher.Close()
		return nil, fmt.Errorf("failed to create get status watcher: %w", err)
	}

	c := &Client{
		device:      dev,
		port:        port,
		session:     &sessionResult.Response.Session,
		deviceInfo:  deviceInfo,
		portInfo:    portInfo,
		config:      config,
		watcher:     watcher,
		data:        mappedDataVmo,
		descriptors: mappedDescVmo,
		handler: netdevice.Handler{
			TxDepth: uint32(deviceInfo.TxDepth),
			RxDepth: uint32(deviceInfo.RxDepth),
			RxFifo:  sessionResult.Response.Fifos.Rx,
			TxFifo:  sessionResult.Response.Fifos.Tx,
		},
	}

	if entries := c.handler.InitRx(c.config.RxDescriptorCount); entries != c.config.RxDescriptorCount {
		panic(fmt.Sprintf("Bad handler rx queue size: %d, expected %d", entries, c.config.RxDescriptorCount))
	}
	if entries := c.handler.InitTx(c.config.TxDescriptorCount); entries != c.config.TxDescriptorCount {
		panic(fmt.Sprintf("Bad handler tx queue size: %d, expected %d", entries, c.config.RxDescriptorCount))
	}

	c.handler.Stats.Tx.FifoStats = fifo.MakeFifoStats(uint32(c.deviceInfo.TxDepth))
	c.handler.Stats.Rx.FifoStats = fifo.MakeFifoStats(uint32(c.deviceInfo.RxDepth))

	descriptorIndex := uint16(0)
	vmoOffset := uint64(0)
	for i := uint16(0); i < c.config.RxDescriptorCount; i++ {
		descriptor := c.getDescriptor(descriptorIndex)
		*descriptor = bufferDescriptor{
			offset: C.ulong(vmoOffset),
		}
		c.resetRxDescriptor(descriptor)
		c.handler.PushInitialRx(descriptorIndex)
		vmoOffset += uint64(c.config.BufferStride)
		descriptorIndex++
	}
	for i := uint16(0); i < c.config.TxDescriptorCount; i++ {
		descriptor := c.getDescriptor(descriptorIndex)
		*descriptor = bufferDescriptor{
			offset: C.ulong(vmoOffset),
		}
		c.resetTxDescriptor(descriptor)
		c.handler.PushInitialTx(descriptorIndex)
		vmoOffset += uint64(c.config.BufferStride)
		descriptorIndex++
	}

	return c, nil
}

// RxStats implements InfoProvider.
func (c *Client) RxStats() *fifo.RxStats {
	return &c.handler.Stats.Rx
}

// TxStats implements InfoProvider.
func (c *Client) TxStats() *fifo.TxStats {
	return &c.handler.Stats.Tx
}

// Class implements InfoProvider.
func (c *Client) Class() network.DeviceClass {
	return c.portInfo.Class
}
