// link/bridge implements a bridging LinkEndpoint
// It can be writable.
package bridge

import (
	"fmt"
	"hash/fnv"
	"math"
	"sort"
	"strings"
	"sync"

	"netstack/link"

	"github.com/google/netstack/tcpip"
	"github.com/google/netstack/tcpip/buffer"
	"github.com/google/netstack/tcpip/stack"
)

var _ stack.LinkEndpoint = (*Endpoint)(nil)
var _ stack.NetworkDispatcher = (*Endpoint)(nil)
var _ link.Controller = (*Endpoint)(nil)

type Endpoint struct {
	links           map[tcpip.LinkAddress]*BridgeableEndpoint
	dispatcher      stack.NetworkDispatcher
	mtu             uint32
	capabilities    stack.LinkEndpointCapabilities
	maxHeaderLength uint16
	linkAddress     tcpip.LinkAddress
	mu              struct {
		sync.Mutex
		onStateChange func(link.State)
	}
}

// New creates a new link from a list of BridgeableEndpoints that bridges
// packets written to it and received from any of its constituent links.
//
// The new link will have the minumum of the MTUs, the maximum of the max
// header lengths, and minimum set of the capabilities. This function takes
// ownership of `links`.
func New(links []*BridgeableEndpoint) *Endpoint {
	sort.Slice(links, func(i, j int) bool {
		return strings.Compare(string(links[i].LinkAddress()), string(links[j].LinkAddress())) > 0
	})
	ep := &Endpoint{
		links: make(map[tcpip.LinkAddress]*BridgeableEndpoint),
		mtu:   math.MaxUint32,
	}
	h := fnv.New64()
	for _, l := range links {
		linkAddress := l.LinkAddress()
		ep.links[linkAddress] = l

		// mtu is the maximum write size, which is the minimum of any link's mtu.
		if mtu := l.MTU(); mtu < ep.mtu {
			ep.mtu = mtu
		}

		// Resolution is required if any link requires it.
		ep.capabilities |= l.Capabilities() & stack.CapabilityResolutionRequired

		// maxHeaderLength is the space to reserve for possible addition
		// headers. We want to reserve enough to suffice for all links.
		if maxHeaderLength := l.MaxHeaderLength(); maxHeaderLength > ep.maxHeaderLength {
			ep.maxHeaderLength = maxHeaderLength
		}

		if _, err := h.Write([]byte(linkAddress)); err != nil {
			panic(err)
		}
	}
	b := h.Sum(nil)[:6]
	// The second bit of the first byte indicates "locally administered".
	b[0] |= 1 << 1
	ep.linkAddress = tcpip.LinkAddress(b)
	return ep
}

// Up calls SetBridge(bridge) on all the constituent links of a bridge.
//
// This causes each constituent link to delegate dispatch to the bridge,
// meaning that received packets will be written out of or dispatched back up
// the stack for another constituent link.
func (ep *Endpoint) Up() error {
	for _, l := range ep.links {
		l.SetBridge(ep)
	}

	ep.mu.Lock()
	onStateChange := ep.mu.onStateChange
	ep.mu.Unlock()

	if onStateChange != nil {
		onStateChange(link.StateStarted)
	}

	return nil
}

// Down calls SetBridge(nil) on all the constituent links of a bridge.
//
// This causes each bridgeable endpoint to go back to its state before
// bridging, dispatching up the stack to the default NetworkDispatcher
// implementation directly.
//
// Down and Close are the same, except they call the OnStateChange callback
// with link.StateDown and link.StateClose respectively.
func (ep *Endpoint) Down() error {
	for _, l := range ep.links {
		l.SetBridge(nil)
	}

	ep.mu.Lock()
	onStateChange := ep.mu.onStateChange
	ep.mu.Unlock()

	if onStateChange != nil {
		onStateChange(link.StateDown)
	}

	return nil
}

// Close calls SetBridge(nil) on all the constituent links of a bridge.
//
// This causes each bridgeable endpoint to go back to its state before
// bridging, dispatching up the stack to the default NetworkDispatcher
// implementation directly.
//
// Down and Close are the same, except they call the OnStateChange callback
// with link.StateDown and link.StateClose respectively.
func (ep *Endpoint) Close() error {
	for _, l := range ep.links {
		l.SetBridge(nil)
	}

	ep.mu.Lock()
	onStateChange := ep.mu.onStateChange
	ep.mu.Unlock()

	if onStateChange != nil {
		onStateChange(link.StateClosed)
	}

	return nil
}

func (ep *Endpoint) SetOnStateChange(f func(link.State)) {
	ep.mu.Lock()
	defer ep.mu.Unlock()

	ep.mu.onStateChange = f
}

func (ep *Endpoint) Path() string {
	return "bridge"
}

// SetPromiscuousMode on a bridge is a no-op, since all of the constituent
// links on a bridge need to already be in promiscuous mode for bridging to
// work.
func (ep *Endpoint) SetPromiscuousMode(bool) error {
	return nil
}

func (ep *Endpoint) MTU() uint32 {
	return ep.mtu
}

func (ep *Endpoint) Capabilities() stack.LinkEndpointCapabilities {
	return ep.capabilities
}

func (ep *Endpoint) MaxHeaderLength() uint16 {
	return ep.maxHeaderLength
}

func (ep *Endpoint) LinkAddress() tcpip.LinkAddress {
	return ep.linkAddress
}

func (ep *Endpoint) WritePacket(r *stack.Route, gso *stack.GSO, hdr buffer.Prependable, payload buffer.VectorisedView, protocol tcpip.NetworkProtocolNumber) *tcpip.Error {
	for _, l := range ep.links {
		if err := l.WritePacket(r, gso, hdr, payload, protocol); err != nil {
			return err
		}
	}
	return nil
}

// WritePackets returns the number of packets in hdrs that were successfully
// written to all links.
func (ep *Endpoint) WritePackets(r *stack.Route, gso *stack.GSO, hdrs []stack.PacketDescriptor, payload buffer.VectorisedView, protocol tcpip.NetworkProtocolNumber) (int, *tcpip.Error) {
	n := len(hdrs)
	for _, l := range ep.links {
		i, err := l.WritePackets(r, gso, hdrs, payload, protocol)
		if err != nil {
			return 0, err
		}

		if i < n {
			n = i
		}
	}
	return n, nil
}

func (ep *Endpoint) WriteRawPacket(packet buffer.VectorisedView) *tcpip.Error {
	for _, l := range ep.links {
		if err := l.WriteRawPacket(packet); err != nil {
			return err
		}
	}
	return nil
}

func (ep *Endpoint) Attach(d stack.NetworkDispatcher) {
	ep.dispatcher = d
}

func (ep *Endpoint) IsAttached() bool {
	return ep.dispatcher != nil
}

func (ep *Endpoint) DeliverNetworkPacket(rxEP stack.LinkEndpoint, srcLinkAddr, dstLinkAddr tcpip.LinkAddress, p tcpip.NetworkProtocolNumber, vv buffer.VectorisedView, linkHeader buffer.View) {
	broadcast := false

	switch dstLinkAddr {
	case tcpip.LinkAddress([]byte{0xff, 0xff, 0xff, 0xff, 0xff, 0xff}):
		broadcast = true
	case ep.linkAddress:
		ep.dispatcher.DeliverNetworkPacket(ep, srcLinkAddr, dstLinkAddr, p, vv, linkHeader)
		return
	default:
		if l, ok := ep.links[dstLinkAddr]; ok {
			l.Dispatcher().DeliverNetworkPacket(l, srcLinkAddr, dstLinkAddr, p, vv, linkHeader)
			return
		}
	}

	// The bridge `ep` isn't included in ep.links below and we don't want to write
	// out of rxEP, otherwise the rest of this function would just be
	// "ep.WritePacket and if broadcast, also deliver to ep.links."
	if broadcast {
		ep.dispatcher.DeliverNetworkPacket(ep, srcLinkAddr, dstLinkAddr, p, vv.Clone(nil), linkHeader)
	}

	// NB: This isn't really a valid Route; Route is a public type but cannot
	// be instantiated fully outside of the stack package, because its
	// underlying referencedNetworkEndpoint cannot be accessed.
	// This means that methods on Route that depend on accessing the
	// underlying LinkEndpoint like MTU() will panic, but it would be
	// extremely strange for the LinkEndpoint we're calling WritePacket on to
	// access itself so indirectly.
	r := stack.Route{LocalLinkAddress: srcLinkAddr, RemoteLinkAddress: dstLinkAddr, NetProto: p}

	// WritePacket implementations assume that `hdr` contains all packet headers. We need to bridge
	// the gap between this API and DeliverNetworkPacket which has all packet headers in the first
	// payload view.
	//
	// TODO(tamird): this recently changed upstream such that both APIs use the same type; this code
	// should be removed once that change is imported.
	payload := vv
	firstView := payload.First()
	payload.RemoveFirst() // doesn't mutate vv
	hdr := buffer.NewPrependable(int(ep.MaxHeaderLength()) + len(firstView))
	{
		reserved := hdr.Prepend(len(firstView))
		if n := copy(reserved, firstView); n != len(firstView) {
			panic(fmt.Sprintf("copied %d/%d bytes", n, len(firstView)))
		}
	}

	// TODO(NET-690): Learn which destinations are on which links and restrict transmission, like a bridge.
	rxaddr := rxEP.LinkAddress()
	for linkaddr, l := range ep.links {
		if broadcast {
			l.Dispatcher().DeliverNetworkPacket(l, srcLinkAddr, dstLinkAddr, p, vv.Clone(nil), linkHeader)
		}
		// Don't write back out interface from which the frame arrived
		// because that causes interoperability issues with a router.
		if linkaddr != rxaddr {
			l.WritePacket(&r, nil, hdr, payload, p)
		}
	}
}

// Wait implements stack.LinkEndpoint.
func (ep *Endpoint) Wait() {
	for _, e := range ep.links {
		e.Wait()
	}
}
