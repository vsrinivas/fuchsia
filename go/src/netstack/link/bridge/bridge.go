// link/bridge implements a bridging LinkEndpoint
// It can be writable.
package bridge

import (
	"hash/fnv"
	"sort"
	"strings"

	"github.com/google/netstack/tcpip"
	"github.com/google/netstack/tcpip/buffer"
	"github.com/google/netstack/tcpip/stack"
)

var _ stack.LinkEndpoint = (*endpoint)(nil)
var _ stack.NetworkDispatcher = (*endpoint)(nil)

type endpoint struct {
	links           []stack.LinkEndpoint
	dispatcher      stack.NetworkDispatcher
	mtu             uint32
	capabilities    stack.LinkEndpointCapabilities
	maxHeaderLength uint16
	linkAddress     tcpip.LinkAddress
}

// New creates a new link from a list of links.  The new link will
// have the minumum of the MTUs, the maximum of the max header
// lengths, and minimum set of the capabilities.  This function takes
// ownership of the argument.
func New(links []stack.LinkEndpoint) stack.LinkEndpoint {
	sort.Slice(links, func(i, j int) bool {
		return strings.Compare(string(links[i].LinkAddress()), string(links[j].LinkAddress())) > 0
	})
	ep := &endpoint{links: links}
	h := fnv.New64()
	if len(links) > 0 {
		l := links[0]
		ep.capabilities = l.Capabilities()
		ep.mtu = l.MTU()
		ep.maxHeaderLength = l.MaxHeaderLength()
	}
	for _, l := range links {
		// Only capabilities that exist on all the link endpoints should be reported.
		ep.capabilities = CombineCapabilities(ep.capabilities, l.Capabilities())
		if mtu := l.MTU(); mtu < ep.mtu {
			ep.mtu = mtu
		}
		// maxHeaderLength is the space to reserve for possible addition
		// headers.  We want to reserve enough to suffice for all links.
		if maxHeaderLength := l.MaxHeaderLength(); maxHeaderLength > ep.maxHeaderLength {
			ep.maxHeaderLength = maxHeaderLength
		}
		if _, err := h.Write([]byte(l.LinkAddress())); err != nil {
			panic(err)
		}
	}
	b := h.Sum(nil)[:6]
	// The second bit of the first byte indicates "locally administered".
	b[0] |= 1 << 1
	ep.linkAddress = tcpip.LinkAddress(b)
	return ep
}

// CombineCapabilities returns the capabilities restricted by the most
// restrictive of the inputs.
func CombineCapabilities(a, b stack.LinkEndpointCapabilities) stack.LinkEndpointCapabilities {
	newCapabilities := a
	// Take the minimum of CapabilityChecksumOffload and CapabilityLoopback.
	newCapabilities &= b | ^(stack.CapabilityChecksumOffload | stack.CapabilityLoopback)
	// Take the maximum of CapabilityResolutionRequired.
	newCapabilities |= b & stack.CapabilityResolutionRequired
	return newCapabilities
}

func (ep *endpoint) MTU() uint32 {
	return ep.mtu
}

func (ep *endpoint) Capabilities() stack.LinkEndpointCapabilities {
	return ep.capabilities
}

func (ep *endpoint) MaxHeaderLength() uint16 {
	return ep.maxHeaderLength
}

func (ep *endpoint) LinkAddress() tcpip.LinkAddress {
	return ep.linkAddress
}

func (ep *endpoint) WritePacket(r *stack.Route, hdr buffer.Prependable, payload buffer.VectorisedView, protocol tcpip.NetworkProtocolNumber) *tcpip.Error {
	for _, l := range ep.links {
		if err := l.WritePacket(r, hdr, payload, protocol); err != nil {
			return err
		}
	}
	return nil
}

func (ep *endpoint) Attach(d stack.NetworkDispatcher) {
	ep.dispatcher = d
	for _, l := range ep.links {
		l.Attach(ep)
	}
}

func (ep *endpoint) IsAttached() bool {
	return ep.dispatcher != nil
}

func (ep *endpoint) DeliverNetworkPacket(rxEP stack.LinkEndpoint, dstLinkAddr, srcLinkAddr tcpip.LinkAddress, p tcpip.NetworkProtocolNumber, vv buffer.VectorisedView) {
	payload := vv
	hdr := buffer.NewPrependableFromView(payload.First())
	payload.RemoveFirst()

	for _, l := range ep.links {
		if dstLinkAddr == l.LinkAddress() {
			// The destination of the packet is this port on the bridge.  We
			// assume that the MAC address is unique.
			ep.dispatcher.DeliverNetworkPacket(l, dstLinkAddr, srcLinkAddr, p, vv)
			return
		}
	}
	// None of the links is the destination so forward to all links, like a hub.
	// TODO(NET-690): Learn which destinations are on which links and restrict transmission, like a bridge.
	for _, l := range ep.links {
		// NB: This isn't really a valid Route; Route is a public type but cannot
		// be instantiated fully outside of the stack package, because its
		// underlying referencedNetworkEndpoint cannot be accessed.
		// This means that methods on Route that depend on accessing the
		// underlying LinkEndpoint like MTU() will panic, but it would be
		// extremely strange for the LinkEndpoint we're calling WritePacket on to
		// access itself so indirectly.
		r := stack.Route{LocalLinkAddress: srcLinkAddr, RemoteLinkAddress: dstLinkAddr, NetProto: p}

		l.WritePacket(&r, hdr, payload, p)
	}
}
