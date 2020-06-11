package bridge

import (
	"sync"

	"gvisor.dev/gvisor/pkg/tcpip"
	"gvisor.dev/gvisor/pkg/tcpip/stack"
)

var _ stack.LinkEndpoint = (*BridgeableEndpoint)(nil)
var _ stack.GSOEndpoint = (*BridgeableEndpoint)(nil)
var _ stack.NetworkDispatcher = (*BridgeableEndpoint)(nil)

type BridgeableEndpoint struct {
	stack.LinkEndpoint
	mu struct {
		sync.RWMutex
		bridge     *Endpoint
		dispatcher stack.NetworkDispatcher
	}
}

func NewEndpoint(lower stack.LinkEndpoint) *BridgeableEndpoint {
	return &BridgeableEndpoint{
		LinkEndpoint: lower,
	}
}

func (e *BridgeableEndpoint) IsAttached() bool {
	e.mu.RLock()
	d := e.mu.dispatcher != nil
	e.mu.RUnlock()
	return d
}

func (e *BridgeableEndpoint) SetBridge(b *Endpoint) {
	e.mu.Lock()
	e.mu.bridge = b
	e.mu.Unlock()
}

func (e *BridgeableEndpoint) Attach(d stack.NetworkDispatcher) {
	e.mu.Lock()
	e.mu.dispatcher = d
	e.mu.Unlock()
	if d != nil {
		// Only pass down the BridgeableEndpoint as a dispatcher if we're not
		// attaching to nil dispatcher.
		d = e
	}
	e.LinkEndpoint.Attach(d)
}

func (e *BridgeableEndpoint) Dispatcher() stack.NetworkDispatcher {
	e.mu.RLock()
	d := e.mu.dispatcher
	e.mu.RUnlock()
	return d
}

func (e *BridgeableEndpoint) DeliverNetworkPacket(src, dst tcpip.LinkAddress, protocol tcpip.NetworkProtocolNumber, pkt *stack.PacketBuffer) {
	e.mu.RLock()
	d := e.mu.dispatcher
	b := e.mu.bridge
	e.mu.RUnlock()

	if b != nil {
		b.DeliverNetworkPacketToBridge(e, src, dst, protocol, pkt)
		return
	}

	if d != nil {
		d.DeliverNetworkPacket(src, dst, protocol, pkt)
	}
}

func (e *BridgeableEndpoint) GSOMaxSize() uint32 {
	if e, ok := e.LinkEndpoint.(stack.GSOEndpoint); ok {
		return e.GSOMaxSize()
	}
	return 0
}
