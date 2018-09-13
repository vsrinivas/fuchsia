package bridge

import (
	"sync"

	"gvisor.dev/gvisor/pkg/tcpip"
	"gvisor.dev/gvisor/pkg/tcpip/stack"
)

var _ stack.LinkEndpoint = (*BridgeableEndpoint)(nil)
var _ stack.NetworkDispatcher = (*Endpoint)(nil)

type BridgeableEndpoint struct {
	stack.LinkEndpoint
	dispatcher stack.NetworkDispatcher
	mu         struct {
		sync.RWMutex
		bridge *Endpoint
	}
}

func NewEndpoint(lower stack.LinkEndpoint) *BridgeableEndpoint {
	return &BridgeableEndpoint{
		LinkEndpoint: lower,
	}
}

func (e *BridgeableEndpoint) IsAttached() bool {
	return e.dispatcher != nil
}

func (e *BridgeableEndpoint) SetBridge(b *Endpoint) {
	e.mu.Lock()
	e.mu.bridge = b
	e.mu.Unlock()
}

func (e *BridgeableEndpoint) Attach(d stack.NetworkDispatcher) {
	e.dispatcher = d
	e.LinkEndpoint.Attach(e)
}

func (e *BridgeableEndpoint) Dispatcher() stack.NetworkDispatcher {
	return e.dispatcher
}

func (e *BridgeableEndpoint) DeliverNetworkPacket(ep stack.LinkEndpoint, src, dst tcpip.LinkAddress, protocol tcpip.NetworkProtocolNumber, pkt tcpip.PacketBuffer) {
	d := e.dispatcher

	e.mu.RLock()
	b := e.mu.bridge
	e.mu.RUnlock()

	if b != nil {
		d = b
	}

	d.DeliverNetworkPacket(ep, src, dst, protocol, pkt)
}
