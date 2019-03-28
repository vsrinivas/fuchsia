package bridge

import (
	"sync"

	"github.com/google/netstack/tcpip"
	"github.com/google/netstack/tcpip/buffer"
	"github.com/google/netstack/tcpip/stack"
)

type BridgeableEndpoint struct {
	stack.LinkEndpoint
	dispatcher stack.NetworkDispatcher
	mu         struct {
		sync.RWMutex
		bridge *Endpoint
	}
}

func NewEndpoint(lower tcpip.LinkEndpointID) (tcpip.LinkEndpointID, *BridgeableEndpoint) {
	e := &BridgeableEndpoint{
		LinkEndpoint: stack.FindLinkEndpoint(lower),
	}
	return stack.RegisterLinkEndpoint(e), e
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

func (e *BridgeableEndpoint) DeliverNetworkPacket(ep stack.LinkEndpoint, src, dst tcpip.LinkAddress, p tcpip.NetworkProtocolNumber, vv buffer.VectorisedView) {
	e.mu.RLock()
	b := e.mu.bridge
	e.mu.RUnlock()

	if b != nil {
		b.DeliverNetworkPacket(ep, src, dst, p, vv)
	} else {
		e.dispatcher.DeliverNetworkPacket(ep, src, dst, p, vv)
	}
}
