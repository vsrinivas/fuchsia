// Copyright 2015 The Go Authors. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

// Minimal RFC 6724 address selection.

package dns

import (
	"context"
	"sort"
	"time"

	"github.com/google/netstack/tcpip"
	"github.com/google/netstack/tcpip/transport/udp"
)

func sortByRFC6724(c *Client, addrs []tcpip.Address) {
	if len(addrs) < 2 {
		return
	}
	sortByRFC6724withSrcs(addrs, srcAddrs(c, addrs))
}

func sortByRFC6724withSrcs(addrs []tcpip.Address, srcs []tcpip.Address) {
	if len(addrs) != len(srcs) {
		panic("internal error")
	}
	addrAttr := make([]ipAttr, len(addrs))
	srcAttr := make([]ipAttr, len(srcs))
	for i, v := range addrs {
		addrAttr[i] = ipAttrOf(v)
		srcAttr[i] = ipAttrOf(srcs[i])
	}
	sort.Stable(&byRFC6724{
		addrs:    addrs,
		addrAttr: addrAttr,
		srcs:     srcs,
		srcAttr:  srcAttr,
	})
}

// srcsAddrs tries to UDP-connect to each address to see if it has a
// route. (This doesn't send any packets). The destination port
// number is irrelevant.
func srcAddrs(c *Client, addrs []tcpip.Address) []tcpip.Address {
	srcs := make([]tcpip.Address, len(addrs))
	dst := tcpip.FullAddress{Port: 9}
	ctx, cancel := context.WithTimeout(context.Background(), time.Second*1)
	for i := range addrs {
		dst.Addr = addrs[i]
		ep, _, err := c.connect(ctx, udp.ProtocolNumber, dst)
		if err == nil {
			if src, err := ep.GetLocalAddress(); err == nil {
				srcs[i] = src.Addr
			}
			ep.Close()
		}
	}
	cancel()
	return srcs
}

type ipSubnet struct {
	Addr tcpip.Address
	Mask tcpip.Address
}

type ipAttr struct {
	Scope      scope
	Precedence uint8
	Label      uint8
}

func ipAttrOf(ip tcpip.Address) ipAttr {
	if ip == "" {
		return ipAttr{}
	}
	match := rfc6724policyTable.Classify(ip)
	return ipAttr{
		Scope:      classifyScope(ip),
		Precedence: match.Precedence,
		Label:      match.Label,
	}
}

type byRFC6724 struct {
	addrs    []tcpip.Address // addrs to sort
	addrAttr []ipAttr
	srcs     []tcpip.Address // or nil if unreachable
	srcAttr  []ipAttr
}

func (s *byRFC6724) Len() int { return len(s.addrs) }

func (s *byRFC6724) Swap(i, j int) {
	s.addrs[i], s.addrs[j] = s.addrs[j], s.addrs[i]
	s.srcs[i], s.srcs[j] = s.srcs[j], s.srcs[i]
	s.addrAttr[i], s.addrAttr[j] = s.addrAttr[j], s.addrAttr[i]
	s.srcAttr[i], s.srcAttr[j] = s.srcAttr[j], s.srcAttr[i]
}

// Less reports whether i is a better destination address for this
// host than j.
//
// The algorithm and variable names comes from RFC 6724 section 6.
func (s *byRFC6724) Less(i, j int) bool {
	DA := s.addrs[i]
	DB := s.addrs[j]
	SourceDA := s.srcs[i]
	SourceDB := s.srcs[j]
	attrDA := &s.addrAttr[i]
	attrDB := &s.addrAttr[j]
	attrSourceDA := &s.srcAttr[i]
	attrSourceDB := &s.srcAttr[j]

	const preferDA = true
	const preferDB = false

	// Rule 1: Avoid unusable destinations.
	// If DB is known to be unreachable or if Source(DB) is undefined, then
	// prefer DA.  Similarly, if DA is known to be unreachable or if
	// Source(DA) is undefined, then prefer DB.
	if SourceDA == "" && SourceDB == "" {
		return false // "equal"
	}
	if SourceDB == "" {
		return preferDA
	}
	if SourceDA == "" {
		return preferDB
	}

	// Rule 2: Prefer matching scope.
	// If Scope(DA) = Scope(Source(DA)) and Scope(DB) <> Scope(Source(DB)),
	// then prefer DA.  Similarly, if Scope(DA) <> Scope(Source(DA)) and
	// Scope(DB) = Scope(Source(DB)), then prefer DB.
	if attrDA.Scope == attrSourceDA.Scope && attrDB.Scope != attrSourceDB.Scope {
		return preferDA
	}
	if attrDA.Scope != attrSourceDA.Scope && attrDB.Scope == attrSourceDB.Scope {
		return preferDB
	}

	// Rule 3: Avoid deprecated addresses.
	// If Source(DA) is deprecated and Source(DB) is not, then prefer DB.
	// Similarly, if Source(DA) is not deprecated and Source(DB) is
	// deprecated, then prefer DA.

	// TODO(bradfitz): implement? low priority for now.

	// Rule 4: Prefer home addresses.
	// If Source(DA) is simultaneously a home address and care-of address
	// and Source(DB) is not, then prefer DA.  Similarly, if Source(DB) is
	// simultaneously a home address and care-of address and Source(DA) is
	// not, then prefer DB.

	// TODO(bradfitz): implement? low priority for now.

	// Rule 5: Prefer matching label.
	// If Label(Source(DA)) = Label(DA) and Label(Source(DB)) <> Label(DB),
	// then prefer DA.  Similarly, if Label(Source(DA)) <> Label(DA) and
	// Label(Source(DB)) = Label(DB), then prefer DB.
	if attrSourceDA.Label == attrDA.Label &&
		attrSourceDB.Label != attrDB.Label {
		return preferDA
	}
	if attrSourceDA.Label != attrDA.Label &&
		attrSourceDB.Label == attrDB.Label {
		return preferDB
	}

	// Rule 6: Prefer higher precedence.
	// If Precedence(DA) > Precedence(DB), then prefer DA.  Similarly, if
	// Precedence(DA) < Precedence(DB), then prefer DB.
	if attrDA.Precedence > attrDB.Precedence {
		return preferDA
	}
	if attrDA.Precedence < attrDB.Precedence {
		return preferDB
	}

	// Rule 7: Prefer native transport.
	// If DA is reached via an encapsulating transition mechanism (e.g.,
	// IPv6 in IPv4) and DB is not, then prefer DB.  Similarly, if DB is
	// reached via encapsulation and DA is not, then prefer DA.

	// TODO(bradfitz): implement? low priority for now.

	// Rule 8: Prefer smaller scope.
	// If Scope(DA) < Scope(DB), then prefer DA.  Similarly, if Scope(DA) >
	// Scope(DB), then prefer DB.
	if attrDA.Scope < attrDB.Scope {
		return preferDA
	}
	if attrDA.Scope > attrDB.Scope {
		return preferDB
	}

	// Rule 9: Use longest matching prefix.
	// When DA and DB belong to the same address family (both are IPv6 or
	// both are IPv4): If CommonPrefixLen(Source(DA), DA) >
	// CommonPrefixLen(Source(DB), DB), then prefer DA.  Similarly, if
	// CommonPrefixLen(Source(DA), DA) < CommonPrefixLen(Source(DB), DB),
	// then prefer DB.
	da4 := DA.To4() != ""
	db4 := DB.To4() != ""
	if da4 == db4 {
		commonA := commonPrefixLen(SourceDA, DA)
		commonB := commonPrefixLen(SourceDB, DB)

		// CommonPrefixLen doesn't really make sense for IPv4, and even
		// causes problems for common load balancing practices
		// (e.g., https://golang.org/issue/13283).  Glibc instead only
		// uses CommonPrefixLen for IPv4 when the source and destination
		// addresses are on the same subnet, but that requires extra
		// work to find the netmask for our source addresses. As a
		// simpler heuristic, we limit its use to when the source and
		// destination belong to the same special purpose block.
		if da4 {
			if !sameIPv4SpecialPurposeBlock(SourceDA, DA) {
				commonA = 0
			}
			if !sameIPv4SpecialPurposeBlock(SourceDB, DB) {
				commonB = 0
			}
		}

		if commonA > commonB {
			return preferDA
		}
		if commonA < commonB {
			return preferDB
		}
	}

	// Rule 10: Otherwise, leave the order unchanged.
	// If DA preceded DB in the original list, prefer DA.
	// Otherwise, prefer DB.
	return false // "equal"
}

type policyTableEntry struct {
	Prefix     *tcpip.Subnet
	Precedence uint8
	Label      uint8
}

type policyTable []policyTableEntry

// RFC 6724 section 2.1.
var rfc6724policyTable = policyTable{
	{
		// ::1/128
		Prefix:     makeSubnet(tcpip.Address("\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x01"), 128),
		Precedence: 50,
		Label:      0,
	},
	{
		// ::/0
		Prefix:     makeSubnet(tcpip.Address("\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00"), 0),
		Precedence: 40,
		Label:      1,
	},
	{
		// ::ffff:0:0/96
		Prefix:     makeSubnet(tcpip.Address("\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\xff\xff\x00\x00\x00\x00"), 96),
		Precedence: 35,
		Label:      4,
	},
	{
		// 2002::/16
		Prefix:     makeSubnet(tcpip.Address("\x20\x02\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00"), 16),
		Precedence: 30,
		Label:      2,
	},
	{
		// 2001::/32
		Prefix:     makeSubnet(tcpip.Address("\x20\x01\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00"), 32),
		Precedence: 5,
		Label:      5,
	},
	{
		// fc00::/7
		Prefix:     makeSubnet(tcpip.Address("\xfc\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00"), 7),
		Precedence: 3,
		Label:      13,
	},
	{
		// ::/96
		Prefix:     makeSubnet(tcpip.Address("\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00"), 96),
		Precedence: 1,
		Label:      3,
	},
	{
		// fec0::/10
		Prefix:     makeSubnet(tcpip.Address("\xfe\xc0\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00"), 10),
		Precedence: 1,
		Label:      11,
	},
	{
		// 3ffe::/16
		Prefix:     makeSubnet(tcpip.Address("\x3f\xfe\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00"), 16),
		Precedence: 1,
		Label:      12,
	},
}

func init() {
	sort.Sort(sort.Reverse(byMaskLength(rfc6724policyTable)))
}

// byMaskLength sorts policyTableEntry by the size of their Prefix.Mask.Size,
// from smallest mask, to largest.
type byMaskLength []policyTableEntry

func (s byMaskLength) Len() int      { return len(s) }
func (s byMaskLength) Swap(i, j int) { s[i], s[j] = s[j], s[i] }
func (s byMaskLength) Less(i, j int) bool {
	isize, _ := s[i].Prefix.Bits()
	jsize, _ := s[j].Prefix.Bits()
	return isize < jsize
}

// Classify returns the policyTableEntry of the entry with the longest
// matching prefix that contains ip.
// The table t must be sorted from largest mask size to smallest.
func (t policyTable) Classify(ip tcpip.Address) policyTableEntry {
	for _, ent := range t {
		if ent.Prefix.Contains(ip) {
			return ent
		}
	}
	return policyTableEntry{}
}

// RFC 6724 section 3.1.
type scope uint8

const (
	scopeInterfaceLocal scope = 0x1
	scopeLinkLocal      scope = 0x2
	scopeAdminLocal     scope = 0x4
	scopeSiteLocal      scope = 0x5
	scopeOrgLocal       scope = 0x8
	scopeGlobal         scope = 0xe
)

func classifyScope(ip tcpip.Address) scope {
	// TODO(mpcomplete): implement
	// if ip.IsLoopback() || ip.IsLinkLocalUnicast() {
	// 	return scopeLinkLocal
	// }
	ipv6 := len(ip) == 16 && ip.To4() == ""
	// if ipv6 && ip.IsMulticast() {
	// 	return scope(ip[1] & 0xf)
	// }

	// Site-local addresses are defined in RFC 3513 section 2.5.6
	// (and deprecated in RFC 3879).
	if ipv6 && ip[0] == 0xfe && ip[1]&0xc0 == 0xc0 {
		return scopeSiteLocal
	}
	return scopeGlobal
}

// commonPrefixLen reports the length of the longest prefix (looking
// at the most significant, or leftmost, bits) that the
// two addresses have in common, up to the length of a's prefix (i.e.,
// the portion of the address not including the interface ID).
//
// If a or b is an IPv4 address as an IPv6 address, the IPv4 addresses
// are compared (with max common prefix length of 32).
// If a and b are different IP versions, 0 is returned.
//
// See https://tools.ietf.org/html/rfc6724#section-2.2
func commonPrefixLen(a, b tcpip.Address) (cpl int) {
	if a4 := a.To4(); a4 != "" {
		a = a4
	}
	if b4 := b.To4(); b4 != "" {
		b = b4
	}
	if len(a) != len(b) {
		return 0
	}
	// If IPv6, only up to the prefix (first 64 bits)
	if len(a) > 8 {
		a = a[:8]
		b = b[:8]
	}
	for len(a) > 0 {
		if a[0] == b[0] {
			cpl += 8
			a = a[1:]
			b = b[1:]
			continue
		}
		bits := 8
		ab, bb := a[0], b[0]
		for {
			ab >>= 1
			bb >>= 1
			bits--
			if ab == bb {
				cpl += bits
				return
			}
		}
	}
	return
}

// sameIPv4SpecialPurposeBlock reports whether a and b belong to the same
// address block reserved by the IANA IPv4 Special-Purpose Address Registry:
// http://www.iana.org/assignments/iana-ipv4-special-registry/iana-ipv4-special-registry.xhtml
func sameIPv4SpecialPurposeBlock(a, b tcpip.Address) bool {
	a, b = a.To4(), b.To4()
	if a == "" || b == "" || a[0] != b[0] {
		return false
	}
	// IANA defines more special-purpose blocks, but these are the only
	// ones likely to be relevant to typical Go systems.
	switch a[0] {
	case 10: // 10.0.0.0/8: Private-Use
		return true
	case 127: // 127.0.0.0/8: Loopback
		return true
	case 169: // 169.254.0.0/16: Link Local
		return a[1] == 254 && b[1] == 254
	case 172: // 172.16.0.0/12: Private-Use
		return a[1]&0xf0 == 16 && b[1]&0xf0 == 16
	case 192: // 192.168.0.0/16: Private-Use
		return a[1] == 168 && b[1] == 168
	}
	return false
}

// makeSubnet returns a tcpip.Subnet from a valid IPv6 address and bit prefix.
// It panics on failure.
func makeSubnet(addr tcpip.Address, bits int) *tcpip.Subnet {
	subnet, err := tcpip.NewSubnet(addr, makeMask(bits, 8*16))
	if err != nil {
		panic(err.Error())
	}
	return &subnet
}

// makeMask returns an AddressMask consisting of `ones' 1 bits
// followed by 0s up to a total length of `bits' bits.
// For a mask of this form, makeMask is the inverse of tcpip.Subnet.Bits.
func makeMask(ones, bits int) tcpip.AddressMask {
	if bits != 8*4 && bits != 8*16 {
		return ""
	}
	if ones < 0 || ones > bits {
		return ""
	}
	l := bits / 8
	m := make([]byte, l)
	n := uint(ones)
	for i := 0; i < l; i++ {
		if n >= 8 {
			m[i] = 0xff
			n -= 8
			continue
		}
		m[i] = ^byte(0xff >> n)
		n = 0
	}
	return tcpip.AddressMask(m)
}
