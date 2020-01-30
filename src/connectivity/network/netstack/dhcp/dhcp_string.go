// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package dhcp

import (
	"fmt"
	"strings"

	"gvisor.dev/gvisor/pkg/tcpip"
)

func (h hdr) String() string {
	var buf strings.Builder
	if _, err := fmt.Fprintf(&buf, "len=%d", len(h)); err != nil {
		panic(err)
	}
	if !h.isValid() {
		return fmt.Sprintf("DHCP invalid; %s; %x", buf.String(), []byte(h))
	}
	opts, err := h.options()
	if err != nil {
		return fmt.Sprintf("DHCP options=%s; %s; %x", err, buf.String(), []byte(h))
	}
	buf.WriteString(";options=")
	for i, opt := range opts {
		if i > 0 {
			buf.WriteByte(',')
		}
		buf.WriteString(opt.String())
	}
	msgType, err := opts.dhcpMsgType()
	if err != nil {
		return fmt.Sprintf("DHCP type=%s; %s; %x", err, buf.String(), []byte(h))
	}
	if _, err := fmt.Fprintf(
		&buf,
		"type=%s;ciaddr=%s;yiaddr=%s;siaddr=%s;giaddr=%s;chaddr=%x",
		msgType,
		tcpip.Address(h.ciaddr()),
		tcpip.Address(h.yiaddr()),
		tcpip.Address(h.siaddr()),
		tcpip.Address(h.giaddr()),
		h.chaddr(),
	); err != nil {
		panic(err)
	}
	return buf.String()
}

func (opt option) String() string {
	return fmt.Sprintf("%s: %x", opt.code, opt.body)
}

func (code optionCode) String() string {
	switch code {
	case optSubnetMask:
		return "option(subnet-mask)"
	case optDefaultGateway:
		return "option(default-gateway)"
	case optDomainNameServer:
		return "option(dns)"
	case optDomainName:
		return "option(domain-name)"
	case optReqIPAddr:
		return "option(request-ip-address)"
	case optLeaseTime:
		return "option(lease-time)"
	case optDHCPMsgType:
		return "option(message-type)"
	case optDHCPServer:
		return "option(server)"
	case optParamReq:
		return "option(parameter-request)"
	case optMessage:
		return "option(message)"
	case optClientID:
		return "option(client-id)"
	case optRenewalTime:
		return "option(renewal-time)"
	case optRebindingTime:
		return "option(rebinding-time)"
	default:
		return fmt.Sprintf("option(%d)", code)
	}
}

func (o op) String() string {
	switch o {
	case opRequest:
		return "op(request)"
	case opReply:
		return "op(reply)"
	}
	return fmt.Sprintf("op(UNKNOWN:%d)", int(o))
}

func (t dhcpMsgType) String() string {
	switch t {
	case dhcpDISCOVER:
		return "DHCPDISCOVER"
	case dhcpOFFER:
		return "DHCPOFFER"
	case dhcpREQUEST:
		return "DHCPREQUEST"
	case dhcpDECLINE:
		return "DHCPDECLINE"
	case dhcpACK:
		return "DHCPACK"
	case dhcpNAK:
		return "DHCPNAK"
	case dhcpRELEASE:
		return "DHCPRELEASE"
	}
	return fmt.Sprintf("DHCP(%d)", int(t))
}

func (v xid) String() string {
	return fmt.Sprintf("xid:%x", uint32(v))
}

func (s dhcpClientState) String() string {
	switch s {
	case initSelecting:
		return "INIT-SELECTING"
	case rebinding:
		return "REBINDING"
	case renewing:
		return "RENEWING"
	default:
		return fmt.Sprintf("UNKNOWN(%d)", s)
	}
}
