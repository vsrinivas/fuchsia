// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//go:build !build_with_native_toolchain

package netdevice

import (
	"context"
	"fmt"
	"math"
	"math/bits"
	"syscall/zx"
	"syscall/zx/fidl"
	"unsafe"

	"gen/netstack/link/netdevice"

	"go.fuchsia.dev/fuchsia/src/connectivity/network/netstack/link"
	"go.fuchsia.dev/fuchsia/src/connectivity/network/netstack/link/fifo"
	"go.fuchsia.dev/fuchsia/src/connectivity/network/netstack/sync"
	"go.fuchsia.dev/fuchsia/src/connectivity/network/netstack/tracing/trace"
	syslog "go.fuchsia.dev/fuchsia/src/lib/syslog/go"

	"fidl/fuchsia/hardware/network"

	"go.uber.org/multierr"
	"gvisor.dev/gvisor/pkg/bufferv2"
	"gvisor.dev/gvisor/pkg/tcpip"
	"gvisor.dev/gvisor/pkg/tcpip/header"
	"gvisor.dev/gvisor/pkg/tcpip/stack"
)

// #include <zircon/device/network.h>
import "C"

const tag = "netdevice"
const emptyLinkAddress tcpip.LinkAddress = ""

type bufferDescriptor = C.buffer_descriptor_t
type basePortId = uint8

type PortMode int

const (
	_ PortMode = iota
	PortModeEthernet
	PortModeIp
)

const DescriptorVersion uint8 = C.NETWORK_DEVICE_DESCRIPTOR_VERSION
const DescriptorLength uint64 = C.sizeof_buffer_descriptor_t

// Client is a client for a network device that implements the
// fuchsia.hardware.network.Device protocol.
type Client struct {
	device     *network.DeviceWithCtxInterface
	session    *network.SessionWithCtxInterface
	deviceInfo network.DeviceInfo
	config     SessionConfig

	data        fifo.MappedVMO
	descriptors fifo.MappedVMO

	handler netdevice.Handler

	mu struct {
		sync.RWMutex
		closed      bool
		runningChan chan struct{}
		ports       map[basePortId]*Port
	}
}

var _ link.Controller = (*Port)(nil)
var _ link.Observer = (*Port)(nil)

var _ stack.LinkEndpoint = (*Port)(nil)
var _ stack.GSOEndpoint = (*Port)(nil)

// Port is the instantiation of a network interface backed by a
// netdevice port.
type Port struct {
	// Used to wait for goroutine teardown. See Attach and Wait.
	dispatcherWg   sync.WaitGroup
	cancelDispatch context.CancelFunc

	client        *Client
	port          *network.PortWithCtxInterface
	portInfo      network.PortInfo
	watcher       *network.StatusWatcherWithCtxInterface
	linkAddress   tcpip.LinkAddress
	macAddressing *network.MacAddressingWithCtxInterface
	mode          PortMode

	state struct {
		mu struct {
			sync.Mutex
			dispatcher          stack.NetworkDispatcher
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

func (p *Port) TxDepth() uint32 {
	return p.client.handler.TxDepth
}

func (p *Port) MTU() uint32 {
	p.mtu.mu.Lock()
	mtu := p.mtu.mu.value
	p.mtu.mu.Unlock()
	return mtu
}

func (*Port) Capabilities() stack.LinkEndpointCapabilities {
	return 0
}

func (*Port) MaxHeaderLength() uint16 {
	return 0
}

func (p *Port) LinkAddress() tcpip.LinkAddress {
	return p.linkAddress
}

// write writes a list of packets to the device.
func (c *Client) write(port network.PortId, pkts stack.PacketBufferList) (int, tcpip.Error) {
	trace.AsyncBegin("net", "netdevice.Client.write", trace.AsyncID(uintptr(unsafe.Pointer(c))))
	defer trace.AsyncEnd("net", "netdevice.Client.write", trace.AsyncID(uintptr(unsafe.Pointer(c))))
	c.mu.RLock()
	defer c.mu.RUnlock()
	if c.mu.closed {
		return 0, &tcpip.ErrClosedForSend{}
	}
	return c.handler.ProcessWrite(pkts, func(descriptorIndex *uint16, pkt stack.PacketBufferPtr) {
		descriptor := c.getDescriptor(*descriptorIndex)
		// Reset descriptor to default values before filling it.
		c.resetTxDescriptor(descriptor)

		data := c.getDescriptorData(descriptor)
		n := 0
		for _, v := range pkt.AsSlices() {
			if w := copy(data[n:], v); w != len(v) {
				panic(fmt.Sprintf("failed to copy packet data to descriptor %d, want %d got %d bytes", descriptorIndex, len(v), w))
			} else {
				n += w
			}
		}

		var frameType network.FrameType
		if len(pkt.LinkHeader().Slice()) != 0 {
			frameType = network.FrameTypeEthernet
		} else {
			switch pkt.NetworkProtocolNumber {
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
		descriptor.port_id.base = C.uchar(port.Base)
		descriptor.port_id.salt = C.uchar(port.Salt)
		descriptor.info_type = C.uint(network.InfoTypeNoInfo)
		descriptor.frame_type = C.uchar(frameType)
		descriptor.data_length = C.uint(n)
	})
}

func (p *Port) WritePackets(pkts stack.PacketBufferList) (int, tcpip.Error) {
	return p.client.write(p.portInfo.Id, pkts)
}

func (p *Port) WriteRawPacket(pkt stack.PacketBufferPtr) tcpip.Error {
	var pkts stack.PacketBufferList
	pkts.PushBack(pkt)
	// TODO(https://fxbug.dev/86725): Frame type detection may not work for implementing
	// packet sockets.
	_, err := p.client.write(p.portInfo.Id, pkts)
	return err
}

func (p *Port) Attach(dispatcher stack.NetworkDispatcher) {
	p.state.mu.Lock()
	p.state.mu.dispatcher = dispatcher
	onLinkClosed := p.state.mu.onLinkClosed
	p.state.mu.Unlock()

	closeWithError := func(reason error) {
		if err := p.Close(); err != nil {
			_ = syslog.WarnTf(tag, "error closing port endpoint on detach (%s): %s", reason, err)
		} else {
			_ = syslog.WarnTf(tag, "closed port endpoint: %s", reason)
		}
	}

	// dispatcher may be nil when the NIC in stack.Stack is being removed.
	if dispatcher == nil {
		closeWithError(fmt.Errorf("RemoveNIC"))
		return
	}
	ctx, cancel := context.WithCancel(context.Background())
	p.cancelDispatch = cancel

	p.dispatcherWg.Add(1)
	go func() {
		defer p.dispatcherWg.Done()
		if err := func() error {
			for {
				status, err := p.watcher.WatchStatus(ctx)
				if err != nil {
					return fmt.Errorf("WatchStatus failed: %w", err)
				}
				if err := p.client.config.checkValidityForPort(status); err != nil {
					return fmt.Errorf("invalid port status for session: %w", err)
				}

				if status.HasMtu() {
					p.mtu.mu.Lock()
					p.mtu.mu.value = status.GetMtu()
					p.mtu.mu.Unlock()
				}
				p.state.mu.Lock()
				fn := p.state.mu.onLinkOnlineChanged
				p.state.mu.Unlock()
				fn(status.HasFlags() && status.GetFlags()&network.StatusFlagsOnline != 0)
			}
		}(); err != nil {
			closeWithError(err)
		}
	}()

	// Spawn a goroutine to notify link closure once all the routines are done.
	go func() {
		p.dispatcherWg.Wait()
		onLinkClosed()
		cancel()
	}()
}

func (p *Port) Close() error {
	p.client.mu.Lock()
	// Remove from parent.
	portId := p.portInfo.GetId()
	// Check that we are removing the right port. Note that because Close
	// can be called multiple times, for example, once when port watcher
	// returns an error because a port goes away and once when the netstack
	// finally removes the port. The race condition could cause the port
	// to be unexpectedly removed, so only remove when the salt agrees.
	if toRemove, ok := p.client.mu.ports[portId.Base]; ok {
		if toRemove.portInfo.GetId().Salt == portId.Salt {
			delete(p.client.mu.ports, portId.Base)
		}
	}
	deviceClosed := p.client.mu.closed
	p.client.mu.Unlock()

	p.state.mu.Lock()
	wasClosed := p.state.mu.closed
	p.state.mu.closed = true
	p.state.mu.Unlock()
	if wasClosed {
		return nil
	}

	if p.cancelDispatch != nil {
		p.cancelDispatch()
	}
	var err error
	if p.macAddressing != nil {
		err = p.macAddressing.Close()
	}

	// Detach from the session if the device isn't already closed.
	detachErr := func() error {
		if deviceClosed {
			return nil
		}
		result, err := p.client.session.Detach(context.Background(), p.portInfo.GetId())
		if err != nil {
			return err
		}
		switch w := result.Which(); w {
		case network.SessionDetachResultResponse:
			return nil
		case network.SessionDetachResultErr:
			switch status := zx.Status(result.Err); status {
			case zx.ErrNotFound:
				// Wasn't attached.
				return nil
			default:
				return &zx.Error{Status: status, Text: "detaching from session"}
			}
			return nil
		default:
			panic(fmt.Sprintf("unexpected result %d", w))
		}
	}()

	return multierr.Combine(p.port.Close(), p.watcher.Close(), err, detachErr)
}

func (c *Client) Run(ctx context.Context) {
	c.mu.Lock()
	closed := c.mu.closed
	oldRunningChan := c.mu.runningChan
	runningChan := make(chan struct{})
	c.mu.runningChan = runningChan
	defer func() {
		close(runningChan)
	}()
	c.mu.Unlock()
	if oldRunningChan != nil {
		panic("can't call Run twice on the same client")
	}
	if closed {
		panic("can't call Run on a client that is already closed")
	}

	ctx, cancel := context.WithCancel(ctx)
	defer cancel()

	detachWithError := func(reason error) {
		cancel()
		c.handler.DetachTx()
		if _, err := c.closeInner(); err != nil {
			_ = syslog.WarnTf(tag, "error closing device on detach (%s): %s", reason, err)
		} else {
			_ = syslog.WarnTf(tag, "closed device: %s", reason)
		}
	}

	var wg sync.WaitGroup
	wg.Add(1)
	go func() {
		defer wg.Done()
		<-ctx.Done()
		detachWithError(fmt.Errorf("context done %w", ctx.Err()))
	}()

	wg.Add(1)
	go func() {
		defer wg.Done()
		if err := c.handler.TxReceiverLoop(func(descriptorIndex *uint16) bool {
			descriptor := c.getDescriptor(*descriptorIndex)
			return network.TxReturnFlags(descriptor.return_flags)&network.TxReturnFlagsTxRetError == 0
		}); err != nil {
			detachWithError(fmt.Errorf("TX read loop: %w", err))
		}
		_ = syslog.VLogTf(syslog.DebugVerbosity, tag, "TX read loop finished")
	}()

	wg.Add(1)
	go func() {
		defer wg.Done()
		if err := c.handler.RxLoop(func(descriptorIndex *uint16) {
			trace.AsyncBegin("net", "netdevice.RxLoop.Handler", trace.AsyncID(uintptr(unsafe.Pointer(c))))
			defer trace.AsyncEnd("net", "netdevice.RxLoop.Handler", trace.AsyncID(uintptr(unsafe.Pointer(c))))
			descriptor := c.getDescriptor(*descriptorIndex)
			data := c.getDescriptorData(descriptor)
			view := make([]byte, len(data))
			view = view[:copy(view, data)]

			var protocolNumber tcpip.NetworkProtocolNumber
			switch network.FrameType(descriptor.frame_type) {
			case network.FrameTypeIpv4:
				protocolNumber = header.IPv4ProtocolNumber
			case network.FrameTypeIpv6:
				protocolNumber = header.IPv6ProtocolNumber
			}
			portId := basePortId(descriptor.port_id.base)

			c.mu.RLock()
			port, ok := c.mu.ports[portId]
			c.mu.RUnlock()
			if ok {
				if want, got := port.portInfo.GetId().Salt, uint8(descriptor.port_id.salt); want == got {
					port.state.mu.Lock()
					dispatcher := port.state.mu.dispatcher
					port.state.mu.Unlock()
					if dispatcher != nil {
						func() {
							pkt := stack.NewPacketBuffer(stack.PacketBufferOptions{
								Payload: bufferv2.MakeWithData(view),
							})
							defer pkt.DecRef()

							id := trace.AsyncID(pkt.ID())
							trace.AsyncBegin("net", "netdevice.DeliverNetworkPacket", id)
							dispatcher.DeliverNetworkPacket(protocolNumber, pkt)
							trace.AsyncEnd("net", "netdevice.DeliverNetworkPacket", id)
						}()
					}
				} else {
					// This can happen if the port flaps on the device while frames
					// are propagating in the FIFO.
					_ = syslog.WarnTf(tag, "received frame on port %d with bad salt %d, want %d", portId, got, want)
				}
			} else {
				// This can happen if the port is detached from the client while frames
				// are propagating in the FIFO.
				_ = syslog.WarnTf(tag, "received frame for unknown port: %d", portId)
			}

			// This entry is going back to the driver; it can be reused.
			c.resetRxDescriptor(descriptor)
		}); err != nil {
			detachWithError(fmt.Errorf("RX loop: %w", err))
		}
		_ = syslog.VLogTf(syslog.DebugVerbosity, tag, "Rx loop finished")
	}()

	wg.Wait()
}

func (p *Port) IsAttached() bool {
	p.state.mu.Lock()
	attached := p.state.mu.dispatcher != nil
	p.state.mu.Unlock()
	return attached
}

func (p *Port) Wait() {
	p.dispatcherWg.Wait()
}

func (*Port) ARPHardwareType() header.ARPHardwareType {
	return header.ARPHardwareNone
}

func (*Port) AddHeader(stack.PacketBufferPtr) {}

// GSOMaxSize implements stack.GSOEndpoint.
func (*Port) GSOMaxSize() uint32 {
	// There's no limit on how much data we can take in a single software GSO write.
	return math.MaxUint32
}

// SupportedGSO implements stack.GSOEndpoint.
func (*Port) SupportedGSO() stack.SupportedGSO {
	// TODO(https://fxbug.dev/76010): expose hardware offloading capabilities.
	return stack.GvisorGSOSupported
}

func (p *Port) Up() error {
	result, err := p.client.session.Attach(context.Background(), p.portInfo.GetId(), p.subscriptionFrameTypes())
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

func (p *Port) Down() error {
	result, err := p.client.session.Detach(context.Background(), p.portInfo.GetId())
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

func (p *Port) SetOnLinkClosed(f func()) {
	p.state.mu.Lock()
	p.state.mu.onLinkClosed = f
	p.state.mu.Unlock()
}

func (p *Port) SetOnLinkOnlineChanged(f func(bool)) {
	p.state.mu.Lock()
	p.state.mu.onLinkOnlineChanged = f
	p.state.mu.Unlock()
}

func (p *Port) SetPromiscuousMode(enabled bool) error {
	if p.macAddressing == nil {
		return fmt.Errorf("promiscuous mode not supported on device")
	}

	var mode network.MacFilterMode
	if enabled {
		mode = network.MacFilterModePromiscuous
	} else {
		// NOTE: Netstack currently is not capable of handling multicast
		// filters, promiscuous mode = false means receive all multicasts still.
		mode = network.MacFilterModeMulticastPromiscuous
	}
	if status, err := p.macAddressing.SetMode(context.Background(), mode); err != nil {
		return err
	} else if zx.Status(status) != zx.ErrOk {
		return &zx.Error{
			Status: zx.Status(status),
			Text:   "fuchsia.hardware.network/MacAddressing.SetMode",
		}
	}
	return nil
}

func (p *Port) DeviceClass() network.DeviceClass {
	return p.portInfo.Class
}

func (p *Port) ConnectPort(port network.PortWithCtxInterfaceRequest) {
	if err := p.port.Clone(context.Background(), port); err != nil {
		_ = syslog.WarnTf(tag, "ConnectPort: port.Clone() = %s", err)
	}
}

// Close closes the client and disposes of all its resources.
func (c *Client) Close() error {
	running, err := c.closeInner()
	if running != nil {
		<-running
	}

	// NB: descriptors and data VMOs are closed only after all the goroutines
	// in Run are closed to prevent data races.
	return multierr.Combine(err, c.data.Close(), c.descriptors.Close())
}

func (c *Client) closeInner() (chan struct{}, error) {
	runningChan, ports, err := func() (chan struct{}, map[basePortId]*Port, error) {
		c.mu.Lock()
		defer c.mu.Unlock()
		if c.mu.closed {
			return c.mu.runningChan, nil, nil
		}
		c.mu.closed = true
		ports := c.mu.ports
		c.mu.ports = nil

		return c.mu.runningChan, ports, multierr.Combine(
			c.device.Close(),
			// Session also has a Close method, make sure we're calling the ChannelProxy
			// one.
			((*fidl.ChannelProxy)(c.session)).Close(),
			c.handler.RxFifo.Close(),
			c.handler.TxFifo.Close(),
		)
	}()

	for _, port := range ports {
		err = multierr.Append(err, port.Close())
	}

	return runningChan, err
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

// NewPort creates a new port client for this device.
func (c *Client) NewPort(ctx context.Context, portId network.PortId) (*Port, error) {
	portRequest, port, err := network.NewPortWithCtxInterfaceRequest()
	if err != nil {
		return nil, fmt.Errorf("failed to create port request: %w", err)
	}
	defer func() {
		if port != nil {
			_ = port.Close()
		}
	}()

	if err := c.device.GetPort(ctx, portId, portRequest); err != nil {
		return nil, fmt.Errorf("failed to get port %d: %w", portId, err)
	}

	portInfo, err := port.GetInfo(ctx)
	if err != nil {
		return nil, fmt.Errorf("failed to get port info: %w", err)
	}
	if !(portInfo.HasId() && portInfo.HasClass() && portInfo.HasRxTypes() && portInfo.HasTxTypes()) {
		return nil, fmt.Errorf("incomplete PortInfo: %#v", portInfo)
	}

	portMode, err := selectPortOperatingMode(portInfo.GetRxTypes())
	if err != nil {
		return nil, err
	}

	macRequest, mac, err := network.NewMacAddressingWithCtxInterfaceRequest()
	if err != nil {
		return nil, fmt.Errorf("failed to create mac request: %w", err)
	}
	defer func() {
		if mac != nil {
			_ = mac.Close()
		}
	}()
	if err := port.GetMac(ctx, macRequest); err != nil {
		return nil, fmt.Errorf("failed to get mac: %w", err)
	}

	linkAddress, mac := func() (tcpip.LinkAddress, *network.MacAddressingWithCtxInterface) {
		macAddr, err := mac.GetUnicastAddress(ctx)
		if err != nil {
			// MAC is not supported.
			_ = mac.Close()
			return emptyLinkAddress, nil
		} else {
			return tcpip.LinkAddress(macAddr.Octets[:]), mac
		}
	}()

	if mac != nil {
		// Set device to multicast promiscuous to match current behavior. When
		// Netstack controls multicast filters this can be removed.
		if status, err := mac.SetMode(ctx, network.MacFilterModeMulticastPromiscuous); err != nil {
			return nil, fmt.Errorf("failed to set multicast promiscuous: %w", err)
		} else if zx.Status(status) != zx.ErrOk {
			return nil, &zx.Error{
				Status: zx.Status(status),
				Text:   "fuchsia.hardware.network/MacAddressing.SetMode",
			}
		}
	}

	portStatus, err := port.GetStatus(ctx)
	if err != nil {
		return nil, fmt.Errorf("failed to create get port status: %w", err)
	}
	if !portStatus.HasMtu() {
		return nil, fmt.Errorf("missing MTU in port status")
	}

	if err := c.config.checkValidityForPort(portStatus); err != nil {
		return nil, err
	}

	watcherRequest, watcher, err := network.NewStatusWatcherWithCtxInterfaceRequest()
	if err != nil {
		return nil, fmt.Errorf("failed to create status watcher request: %w", err)
	}
	defer func() {
		if watcher != nil {
			_ = watcher.Close()
		}
	}()
	if err := port.GetStatusWatcher(ctx, watcherRequest, network.MaxStatusBuffer); err != nil {
		return nil, fmt.Errorf("failed to create get status watcher: %w", err)
	}

	portEndpoint := &Port{
		client:        c,
		port:          port,
		portInfo:      portInfo,
		watcher:       watcher,
		linkAddress:   linkAddress,
		macAddressing: mac,
		mode:          portMode,
	}
	portEndpoint.mtu.mu.value = portStatus.GetMtu()

	c.mu.Lock()
	defer c.mu.Unlock()
	baseId := basePortId(portId.Base)
	if _, ok := c.mu.ports[baseId]; ok {
		return nil, &PortAlreadyBoundError{id: portId}
	}
	c.mu.ports[baseId] = portEndpoint

	// Prevent deferred functions from cleaning up.
	mac = nil
	port = nil
	watcher = nil

	return portEndpoint, nil
}

// Mode returns the port's operating mode.
func (p *Port) Mode() PortMode {
	return p.mode
}

// subscriptionFrameTypes returns the frame types to use when attaching this
// port to a session, based on the port's operating mode.
func (p *Port) subscriptionFrameTypes() []network.FrameType {
	switch mode := p.mode; mode {
	case PortModeEthernet:
		return []network.FrameType{network.FrameTypeEthernet}
	case PortModeIp:
		return []network.FrameType{network.FrameTypeIpv4, network.FrameTypeIpv6}
	default:
		panic(fmt.Sprintf("invalid port mode %d", mode))
	}
}

// NewClient creates a new client from a provided network device interface.
func NewClient(ctx context.Context, dev *network.DeviceWithCtxInterface, sessionConfigFactory SessionConfigFactory) (*Client, error) {
	_ = syslog.VLogTf(syslog.DebugVerbosity, tag, "creating network device client")
	defer func() {
		// We always take ownership of the device, close it in case of errors.
		if dev != nil {
			_ = dev.Close()
		}
	}()

	deviceInfo, err := dev.GetInfo(ctx)
	if err != nil {
		return nil, fmt.Errorf("failed to get device information: %w", err)
	}
	if !(deviceInfo.HasMinDescriptorLength() &&
		deviceInfo.HasDescriptorVersion() &&
		deviceInfo.HasRxDepth() &&
		deviceInfo.HasTxDepth() &&
		deviceInfo.HasBufferAlignment() &&
		deviceInfo.HasMinRxBufferLength() &&
		deviceInfo.HasMinTxBufferLength() &&
		deviceInfo.HasMinTxBufferHead() &&
		deviceInfo.HasMinTxBufferTail() &&
		deviceInfo.HasMaxBufferParts()) {
		return nil, fmt.Errorf("incomplete DeviceInfo: %#v", deviceInfo)
	}
	if deviceInfo.HasMaxBufferLength() && deviceInfo.MaxBufferLength == 0 {
		return nil, fmt.Errorf("invalid MaxBufferLength: %d, expected != 0", deviceInfo.MaxBufferLength)
	}

	config, err := sessionConfigFactory.MakeSessionConfig(deviceInfo)
	if err != nil {
		return nil, fmt.Errorf("session configuration factory failed: %w", err)
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
	sessionInfo.SetDescriptorVersion(DescriptorVersion)
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

	c := &Client{
		device:      dev,
		session:     &sessionResult.Response.Session,
		deviceInfo:  deviceInfo,
		config:      config,
		data:        mappedDataVmo,
		descriptors: mappedDescVmo,
		handler: netdevice.Handler{
			TxDepth: uint32(deviceInfo.TxDepth),
			RxDepth: uint32(deviceInfo.RxDepth),
			RxFifo:  sessionResult.Response.Fifos.Rx,
			TxFifo:  sessionResult.Response.Fifos.Tx,
		},
	}
	c.mu.ports = make(map[basePortId]*Port)

	if entries := c.handler.InitRx(c.config.RxDescriptorCount); entries != c.config.RxDescriptorCount {
		panic(fmt.Sprintf("bad handler rx queue size: %d, expected %d", entries, c.config.RxDescriptorCount))
	}
	if entries := c.handler.InitTx(c.config.TxDescriptorCount); entries != c.config.TxDescriptorCount {
		panic(fmt.Sprintf("bad handler tx queue size: %d, expected %d", entries, c.config.RxDescriptorCount))
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

	// Prevent defer function from closing the device.
	dev = nil

	return c, nil
}

func (p *Port) RxStats() *fifo.RxStats {
	return &p.client.handler.Stats.Rx
}

func (p *Port) TxStats() *fifo.TxStats {
	return &p.client.handler.Stats.Tx
}

func (p *Port) Class() network.DeviceClass {
	return p.portInfo.Class
}

type InvalidPortOperatingModeError struct {
	rxTypes []network.FrameType
}

func (e *InvalidPortOperatingModeError) Error() string {
	return fmt.Sprintf("can't determine port operating mode for types '%v'", e.rxTypes)
}

type PortAlreadyBoundError struct {
	id network.PortId
}

func (e *PortAlreadyBoundError) Error() string {
	return fmt.Sprintf("port %d(salt=%d) is already bound", e.id.Base, e.id.Salt)
}

func selectPortOperatingMode(rxTypes []network.FrameType) (PortMode, error) {
	seenIpv4 := false
	seenIpv6 := false
	for _, frameType := range rxTypes {
		switch frameType {
		case network.FrameTypeEthernet:
			return PortModeEthernet, nil
		case network.FrameTypeIpv4:
			seenIpv4 = true
		case network.FrameTypeIpv6:
			seenIpv6 = true
		default:
			panic(fmt.Sprintf("unrecognized frame type %s", frameType))
		}
	}
	// We only support devices with dual IP mode for now.
	if seenIpv4 && seenIpv6 {
		return PortModeIp, nil
	}
	return 0, &InvalidPortOperatingModeError{rxTypes: rxTypes}
}
