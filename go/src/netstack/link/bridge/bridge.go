// link/bridge implements a bridging LinkEndpoint
// It can be writable.
package bridge

import (
	"bytes"
	"hash/fnv"
	"sort"

	"github.com/google/netstack/tcpip"
	"github.com/google/netstack/tcpip/buffer"
	"github.com/google/netstack/tcpip/stack"
)

var _ stack.LinkEndpoint = (*endpoint)(nil)
var _ stack.NetworkDispatcher = (*endpoint)(nil)

type endpoint struct {
	links       []stack.LinkEndpoint
	dispatcher  stack.NetworkDispatcher
	linkAddress tcpip.LinkAddress
}

func New(links []stack.LinkEndpoint) stack.LinkEndpoint {
	sort.Slice(links, func(i, j int) bool {
		return bytes.Compare([]byte(links[i].LinkAddress()), []byte(links[j].LinkAddress())) > 0
	})
	ep := &endpoint{links: links}
	h := fnv.New64()
	for _, l := range links {
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

func (ep *endpoint) MTU() uint32 {
	panic("unimplemented")
}

func (ep *endpoint) Capabilities() stack.LinkEndpointCapabilities {
	panic("unimplemented")
}

func (ep *endpoint) MaxHeaderLength() uint16 {
	panic("unimplemented")
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
			ep.dispatcher.DeliverNetworkPacket(l, dstLinkAddr, srcLinkAddr, p, vv)
			return
		}

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
