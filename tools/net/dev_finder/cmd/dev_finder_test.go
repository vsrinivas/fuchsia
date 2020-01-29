// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package main

import (
	"bytes"
	"context"
	"encoding/json"
	"fmt"
	"net"
	"strconv"
	"strings"
	"sync"
	"testing"
	"time"
	"unsafe"

	"github.com/google/go-cmp/cmp"

	"go.fuchsia.dev/fuchsia/tools/net/mdns"
	"go.fuchsia.dev/fuchsia/tools/net/netboot"
)

const defaultIPResponseNAT = "192.168.0.42"
const defaultIPTarget = "192.168.1.92"
const defaultIPv6ResponseNAT = "fe80::ae68:3cff:3e9f:7317"
const defaultIPv6Target = "fe80::ae68:3cff:3e9f:beef"
const defaultIPv6MDNSZone = "mdnsIface0"

const fuchsiaMDNSNodename1 = "fuchsia-domain-name-1"
const fuchsiaMDNSNodename2 = "fuchsia-domain-name-2"

const defaultIPv6NetbootAddr = "fe80::ae68:3cff:3e9f:7319"
const defaultIPv6NetbootZone = "netbootIface0"

const defaultNetbootNodename = "this-is-a-netboot-device-1"

type nbDiscoverFunc func(chan<- *netboot.Target, string, bool) (func() error, error)

type fakeNetbootClient struct {
	discover nbDiscoverFunc
}

func (m *fakeNetbootClient) StartDiscover(t chan<- *netboot.Target, nodename string, fuchsia bool) (func() error, error) {
	return m.discover(t, nodename, fuchsia)
}

// fakeMDNS is a fake implementation of MDNS for testing.
type fakeMDNS struct {
	answer           *fakeAnswer
	handlers         []func(net.Interface, net.Addr, mdns.Packet)
	sendEmptyData    bool
	sendTooShortData bool
}

type fakeAnswer struct {
	ip             string
	ipTargetAddr   string
	ipv6           string
	ipv6TargetAddr string
	zone           string
	domains        []string
}

func (m *fakeMDNS) AddHandler(f func(net.Interface, net.Addr, mdns.Packet)) {
	m.handlers = append(m.handlers, f)
}
func (m *fakeMDNS) AddWarningHandler(func(net.Addr, error)) {}
func (m *fakeMDNS) AddErrorHandler(func(error))             {}
func (m *fakeMDNS) SendTo(mdns.Packet, *net.UDPAddr) error  { return nil }
func (m *fakeMDNS) Send(packet mdns.Packet) error {
	if m.answer != nil {
		go func() {
			ifc := net.Interface{Name: defaultIPv6MDNSZone}
			ip := net.IPAddr{IP: net.ParseIP(m.answer.ip).To4()}
			ipv6 := net.IPAddr{IP: net.ParseIP(m.answer.ipv6).To16(), Zone: m.answer.zone}
			for _, q := range packet.Questions {
				switch {
				case q.Type == mdns.PTR && q.Class == mdns.IN:
					// 'list' command
					for _, domain := range m.answer.domains {
						additionalRecords := []mdns.Record{}
						additionalRecords = append(additionalRecords, mdns.Record{
							Class:  mdns.IN,
							Type:   mdns.A,
							Domain: fmt.Sprintf("%s.local", domain),
							Data:   net.ParseIP(m.answer.ipTargetAddr).To4(),
						})
						additionalRecords = append(additionalRecords, mdns.Record{
							Class:  mdns.IN,
							Type:   mdns.AAAA,
							Domain: fmt.Sprintf("%s.local", domain),
							Data:   net.ParseIP(m.answer.ipv6TargetAddr).To16(),
						})
						var answer mdns.Record
						// Cases for malformed response.
						if m.sendEmptyData {
							answer = mdns.Record{
								Class: mdns.IN,
								Type:  mdns.PTR,
								Data:  nil, // Empty data
							}
						} else if m.sendTooShortData {
							data := make([]byte, len(domain)) // One byte shorter
							data[0] = byte(len(domain))
							copy(data[1:], []byte(domain[1:]))
							answer = mdns.Record{
								Class: mdns.IN,
								Type:  mdns.PTR,
								Data:  data,
							}
						} else { // Normal response.
							data := make([]byte, len(domain)+1)
							data[0] = byte(len(domain))
							copy(data[1:], []byte(domain))
							answer = mdns.Record{
								Class:  mdns.IN,
								Type:   mdns.PTR,
								Data:   data,
								Domain: fuchsiaMDNSService,
							}
						}
						pkt := mdns.Packet{
							Answers:    []mdns.Record{answer},
							Additional: additionalRecords,
						}
						for _, h := range m.handlers {
							// Important: changing the order of these function calls will likely
							// cause failures for tests that are looking for both IPv4 and IPv6
							// addresses simultaneously.
							h(ifc, &ip, pkt)
							h(ifc, &ipv6, pkt)
						}
					}
				case q.Type == mdns.A && q.Class == mdns.IN:
				case q.Type == mdns.AAAA && q.Class == mdns.IN:
					// 'resolve' command
					answers := make([]mdns.Record, len(m.answer.domains))
					for _, d := range m.answer.domains {
						answers = append(answers, mdns.Record{
							Class:  mdns.IN,
							Type:   mdns.A,
							Data:   net.ParseIP(m.answer.ipTargetAddr).To4(),
							Domain: d,
						})
						answers = append(answers, mdns.Record{
							Class:  mdns.IN,
							Type:   mdns.AAAA,
							Data:   net.ParseIP(m.answer.ipv6TargetAddr).To16(),
							Domain: d,
						})
					}
					pkt := mdns.Packet{Answers: answers}
					for _, h := range m.handlers {
						// Important: changing the order of these function calls will likely
						// cause failures for tests that are looking for both IPv4 and IPv6
						// addresses simultaneously.
						h(ifc, &ip, pkt)
						h(ifc, &ipv6, pkt)
					}
				}
			}
		}()
	}
	return nil
}
func (m *fakeMDNS) Start(context.Context, int) error { return nil }

func newDevFinderCmd(
	handler mDNSHandler,
	answerDomains []string,
	sendEmptyData bool,
	sendTooShortData bool,
	st subtest,
	nbDiscover nbDiscoverFunc) devFinderCmd {
	cmd := devFinderCmd{
		mdnsHandler: handler,
		mdnsAddrs:   "ff02::fb,224.0.0.251",
		mdnsPorts:   "5353",
		timeout:     10 * time.Millisecond,
		netboot:     true,
		mdns:        true,
		ipv6:        st.ipv6,
		ipv4:        st.ipv4,
		ignoreNAT:   st.ignoreNAT,
		newMDNSFunc: func(addr string) mdnsInterface {
			return &fakeMDNS{
				// Every device is behind a NAT, so the target
				// address (the address the target sees) is
				// different from the SRC address (the address
				// we see when the device responds).
				answer: &fakeAnswer{
					ip:             defaultIPResponseNAT,
					ipTargetAddr:   defaultIPTarget,
					ipv6:           defaultIPv6ResponseNAT,
					ipv6TargetAddr: defaultIPv6Target,
					zone:           defaultIPv6MDNSZone,
					domains:        answerDomains,
				},
				sendEmptyData:    sendEmptyData,
				sendTooShortData: sendTooShortData,
			}
		},
		newNetbootFunc: func(_ time.Duration) netbootClientInterface {
			return &fakeNetbootClient{nbDiscover}
		},
	}
	cmd.finders = append(
		cmd.finders,
		&mdnsFinder{deviceFinderBase{cmd: &cmd}})
	if st.ipv6 {
		cmd.finders = append(
			cmd.finders,
			&netbootFinder{deviceFinderBase{cmd: &cmd}})
	}
	return cmd
}

func compareFuchsiaDevices(d1, d2 *fuchsiaDevice) bool {
	return cmp.Equal(d1.addr, d2.addr) && cmp.Equal(d1.domain, d2.domain) && cmp.Equal(d1.zone, d2.zone)
}

// makes a dns-sd finder with a single result in it (for storage/lookup of
// dnsSDContext)
func makeDNSSDFinderForTest(nodename string) *dnsSDFinder {
	c := make(chan *fuchsiaDevice, 1)
	c <- &fuchsiaDevice{domain: nodename}
	f := &dnsSDFinder{
		deviceChannel: c,
	}
	return f
}

type subtest struct {
	ipv4      bool
	ipv6      bool
	ignoreNAT bool
	node      string
}

func (s *subtest) defaultMDNSIP() net.IP {
	if s.ignoreNAT {
		// When parsing the additional records (for getting the target
		// address), the last record is IPv6. If the order of outputs
		// changes for the fake MDNS implementation it may break this
		// code, as having IPv4 and IPv6 enabled could return a v4
		// address in some cases. This also applies to the lower
		// `if s.ipv6` statement.
		if s.ipv6 {
			return net.ParseIP(defaultIPv6Target).To16()
		}
		if s.ipv4 {
			return net.ParseIP(defaultIPTarget).To4()
		}
	}

	if s.ipv6 {
		return net.ParseIP(defaultIPv6ResponseNAT).To16()
	}
	if s.ipv4 {
		return net.ParseIP(defaultIPResponseNAT).To4()
	}
	return nil
}

func (s *subtest) defaultNetbootDevice() *fuchsiaDevice {
	if !s.ipv6 {
		return nil
	}
	return &fuchsiaDevice{
		addr:   net.ParseIP(defaultIPv6NetbootAddr).To16(),
		domain: defaultNetbootNodename,
		zone:   defaultIPv6NetbootZone,
	}
}

func (s *subtest) defaultMDNSZone() string {
	if !s.ipv6 {
		return ""
	}
	return defaultIPv6MDNSZone
}

func (s *subtest) String() string {
	b := strings.Builder{}
	if len(s.node) > 0 {
		b.WriteString("node=\"")
		b.WriteString(s.node)
		b.WriteByte('"')
		b.WriteByte('_')
	}
	b.WriteString("ipv4=")
	b.WriteString(strconv.FormatBool(s.ipv4))
	b.WriteString("_ipv6=")
	b.WriteString(strconv.FormatBool(s.ipv6))
	b.WriteString("_ignore-nat=")
	b.WriteString(strconv.FormatBool(s.ignoreNAT))
	return b.String()
}

func runSubTests(t *testing.T, node string, f func(*testing.T, subtest)) {
	for _, ipv6 := range []bool{true, false} {
		for _, ipv4 := range []bool{true, false} {
			if !ipv4 && !ipv6 {
				continue
			}
			for _, nat := range []bool{true, false} {
				s := subtest{
					ipv4:      ipv4,
					ipv6:      ipv6,
					ignoreNAT: nat,
					node:      node,
				}
				t.Run(s.String(), func(t *testing.T) {
					f(t, s)
				})
			}
		}
	}
}

//// Tests for the `list` command.

func TestListDevices(t *testing.T) {
	nbDiscover := func(target chan<- *netboot.Target, nodename string, fuchsia bool) (func() error, error) {
		t.Helper()
		if !fuchsia {
			t.Fatalf("fuchsia set to false")
		}
		nodenameWant := netboot.NodenameWildcard
		if nodename != nodenameWant {
			t.Fatalf("nodename set incorrectly: want %q got %q", nodenameWant, nodename)
		}
		go func() {
			target <- &netboot.Target{
				TargetAddress: net.ParseIP(defaultIPv6NetbootAddr).To16(),
				Nodename:      defaultNetbootNodename,
				Interface:     &net.Interface{Name: defaultIPv6NetbootZone},
			}
		}()
		return func() error { return nil }, nil
	}

	for _, filter := range []string{"", "-1"} {
		filter := filter
		t.Run(fmt.Sprintf("domainFilter=%q", filter), func(t *testing.T) {
			runSubTests(t, "", func(t *testing.T, s subtest) {
				cmd := listCmd{
					devFinderCmd: newDevFinderCmd(
						listMDNSHandler,
						[]string{
							fuchsiaMDNSNodename1,
							fuchsiaMDNSNodename2,
						},
						false,
						false,
						s,
						nbDiscover),
					domainFilter: filter,
				}
				got, err := cmd.listDevices(context.Background())
				if err != nil {
					t.Fatalf("listDevices: %v", err)
				}
				want := []*fuchsiaDevice{
					{
						addr:   s.defaultMDNSIP(),
						zone:   s.defaultMDNSZone(),
						domain: fuchsiaMDNSNodename1,
					},
				}
				// If not filtering, add the remaining MDNS device that
				// would not have been filtered.
				if len(filter) == 0 {
					want = append(want,
						&fuchsiaDevice{
							addr:   s.defaultMDNSIP(),
							zone:   s.defaultMDNSZone(),
							domain: fuchsiaMDNSNodename2,
						},
					)
				}
				if s.ipv6 {
					want = append(want, s.defaultNetbootDevice())
				}
				if d := cmp.Diff(want, got, cmp.Comparer(compareFuchsiaDevices)); d != "" {
					t.Errorf("listDevices mismatch: (-want +got):\n%s", d)
				}
			})
		})
	}
}

func TestListDevice_allProtocolsDisabled(t *testing.T) {
	nbDiscover := func(_ chan<- *netboot.Target, _ string, _ bool) (func() error, error) {
		return func() error { return nil }, nil
	}
	cmd := listCmd{
		devFinderCmd: newDevFinderCmd(
			listMDNSHandler,
			[]string{
				fuchsiaMDNSNodename1,
				fuchsiaMDNSNodename2,
			},
			false,
			false,
			subtest{},
			nbDiscover,
		),
	}
	_, err := cmd.listDevices(context.Background())
	if err == nil {
		t.Error("listDevice error expected")
	}
}

func TestListDevices_emptyData(t *testing.T) {
	nbDiscover := func(_ chan<- *netboot.Target, _ string, _ bool) (func() error, error) {
		return func() error { return nil }, nil
	}
	cmd := listCmd{
		devFinderCmd: newDevFinderCmd(
			listMDNSHandler,
			[]string{
				fuchsiaMDNSNodename1,
				fuchsiaMDNSNodename2,
			},
			true, // sendEmptyData
			false,
			subtest{ipv4: true},
			nbDiscover),
	}
	// Must not crash.
	cmd.listDevices(context.Background())
}

func TestListDevices_duplicateDevices(t *testing.T) {
	runSubTests(t, "", func(t *testing.T, s subtest) {
		nbDiscover := func(_ chan<- *netboot.Target, _ string, _ bool) (func() error, error) {
			return func() error { return nil }, nil
		}
		cmd := listCmd{
			devFinderCmd: newDevFinderCmd(
				listMDNSHandler,
				[]string{
					fuchsiaMDNSNodename1,
					fuchsiaMDNSNodename1,
					fuchsiaMDNSNodename1,
					fuchsiaMDNSNodename1,
					fuchsiaMDNSNodename1,
					fuchsiaMDNSNodename2,
				},
				false,
				false,
				s,
				nbDiscover),
		}
		got, err := cmd.listDevices(context.Background())
		if err != nil {
			t.Fatalf("listDevices: %v", err)
		}
		want := []*fuchsiaDevice{
			{
				addr:   s.defaultMDNSIP(),
				domain: fuchsiaMDNSNodename1,
				zone:   s.defaultMDNSZone(),
			},
			{
				addr:   s.defaultMDNSIP(),
				domain: fuchsiaMDNSNodename2,
				zone:   s.defaultMDNSZone(),
			},
		}
		if d := cmp.Diff(want, got, cmp.Comparer(compareFuchsiaDevices)); d != "" {
			t.Errorf("listDevices mismatch: (-want +got):\n%s", d)
		}
	})
}

func TestListDevices_tooShortData(t *testing.T) {
	nbDiscover := func(_ chan<- *netboot.Target, _ string, _ bool) (func() error, error) {
		return func() error { return nil }, nil
	}
	cmd := listCmd{
		devFinderCmd: newDevFinderCmd(
			listMDNSHandler,
			[]string{
				fuchsiaMDNSNodename1,
				fuchsiaMDNSNodename2,
			},
			false,
			true, // sendTooShortData
			subtest{ipv4: true},
			nbDiscover,
		),
	}

	// Must not crash.
	cmd.listDevices(context.Background())
}

//// Tests for the `resolve` command.

func TestResolveDevices(t *testing.T) {
	node := fuchsiaMDNSNodename1
	nbDiscover := func(target chan<- *netboot.Target, nodename string, fuchsia bool) (func() error, error) {
		t.Helper()
		if !fuchsia {
			t.Fatalf("fuchsia set to false")
		}
		nodenameWant := node
		if nodename != nodenameWant {
			t.Fatalf("nodename set incorrectly: want %q got %q", nodenameWant, nodename)
		}
		go func() {
			target <- &netboot.Target{
				TargetAddress: net.ParseIP("192.168.1.2").To4(),
				Nodename:      "this-is-some-netboot-device",
			}
		}()
		return func() error { return nil }, nil
	}
	runSubTests(t, node, func(t *testing.T, s subtest) {
		cmd := resolveCmd{
			devFinderCmd: newDevFinderCmd(
				resolveMDNSHandler,
				[]string{
					fmt.Sprintf("%s.local", fuchsiaMDNSNodename1),
					fmt.Sprintf("%s.local", fuchsiaMDNSNodename2),
				},
				false,
				false,
				s,
				nbDiscover),
		}
		got, err := cmd.resolveDevices(context.Background(), s.node)
		if err != nil {
			t.Fatalf("listDevices: %v", err)
		}
		want := []*fuchsiaDevice{
			{
				addr:   s.defaultMDNSIP(),
				zone:   s.defaultMDNSZone(),
				domain: s.node,
			},
		}
		if d := cmp.Diff(want, got, cmp.Comparer(compareFuchsiaDevices)); d != "" {
			t.Errorf("listDevices mismatch: (-want +got):\n%s", d)
		}
	})
}

func TestResolveDevices_allProtocolsDisabled(t *testing.T) {
	nbDiscover := func(_ chan<- *netboot.Target, _ string, _ bool) (func() error, error) {
		return func() error { return nil }, nil
	}
	cmd := resolveCmd{
		devFinderCmd: newDevFinderCmd(
			resolveMDNSHandler,
			[]string{
				fmt.Sprintf("%s.local", fuchsiaMDNSNodename1),
			},
			false,
			false,
			subtest{},
			nbDiscover,
		),
	}
	_, err := cmd.resolveDevices(context.Background(), fuchsiaMDNSNodename1)
	if err == nil {
		t.Error("resolveDevice error expected")
	}
}

//// Tests for output functions.

func TestOutputNormal(t *testing.T) {
	devs := []*fuchsiaDevice{
		{
			addr:   net.ParseIP("123.12.234.23").To4(),
			domain: "hello.world",
		},
		{
			addr:   net.ParseIP("11.22.33.44").To4(),
			domain: "fuchsia.rocks",
		},
	}

	{
		var buf strings.Builder
		cmd := devFinderCmd{output: &buf}

		cmd.outputNormal(devs, false)

		got := buf.String()
		want := `123.12.234.23
11.22.33.44
`
		if d := cmp.Diff(want, got); d != "" {
			t.Errorf("outputNormal mismatch: (-want +got):\n%s", d)
		}
	}

	{
		var buf strings.Builder
		cmd := devFinderCmd{output: &buf}
		cmd.outputNormal(devs, true)

		got := buf.String()
		want := `123.12.234.23 hello.world
11.22.33.44 fuchsia.rocks
`
		if d := cmp.Diff(want, got); d != "" {
			t.Errorf("outputNormal(includeDomain) mismatch: (-want +got):\n%s", d)
		}
	}
}

func TestOutputJSON(t *testing.T) {
	devs := []*fuchsiaDevice{
		{
			addr:   net.ParseIP("123.12.234.23").To4(),
			domain: "hello.world",
		},
		{
			addr:   net.ParseIP("11.22.33.44").To4(),
			domain: "fuchsia.rocks",
		},
	}

	{
		var buf bytes.Buffer
		cmd := devFinderCmd{
			json:   true,
			output: &buf,
		}

		cmd.outputJSON(devs, false)

		var got jsonOutput
		if err := json.Unmarshal(buf.Bytes(), &got); err != nil {
			t.Fatalf("json.Unmarshal: %v", err)
		}
		want := jsonOutput{
			Devices: []jsonDevice{
				{Addr: "123.12.234.23"},
				{Addr: "11.22.33.44"},
			},
		}
		if d := cmp.Diff(want, got); d != "" {
			t.Errorf("outputNormal mismatch: (-want +got):\n%s", d)
		}
	}

	{
		var buf bytes.Buffer
		cmd := devFinderCmd{
			json:   true,
			output: &buf,
		}

		cmd.outputJSON(devs, true)

		var got jsonOutput
		if err := json.Unmarshal(buf.Bytes(), &got); err != nil {
			t.Fatalf("json.Unmarshal: %v", err)
		}

		want := jsonOutput{
			Devices: []jsonDevice{
				{
					Addr:   "123.12.234.23",
					Domain: "hello.world",
				},
				{
					Addr:   "11.22.33.44",
					Domain: "fuchsia.rocks",
				},
			},
		}
		if d := cmp.Diff(want, got); d != "" {
			t.Errorf("outputNormal(includeDomain) mismatch: (-want +got):\n%s", d)
		}
	}
}

type linkLocalTest struct {
	bytes []byte
	want  bool
}

func TestIsIPv6LinkLocal(t *testing.T) {
	tests := []linkLocalTest{
		{
			bytes: []byte{0xfe, 0x80, 0x90, 0x90, 0x9, 0x9, 0x1, 0x1, 0x1, 0x1, 0x1, 0x1, 0x2, 0x1, 0x4, 0x6},
			want:  true,
		},
		{
			bytes: []byte{0xfe, 0x80},
			want:  false,
		},
		{
			bytes: nil,
			want:  false,
		},
		{
			bytes: []byte{0xfe, 0x80, 0xfe, 0x80},
			want:  false,
		},
	}
	for _, test := range tests {
		if got := isIPv6LinkLocal(test.bytes); got != test.want {
			t.Errorf("Address %v returns %v for link local, expect %v", test.bytes, got, test.want)
		}
	}
}

func TestAddrToIP(t *testing.T) {
	want := "::1"
	ipNet := &net.IPNet{IP: net.ParseIP(want)}
	ip, zone, err := addrToIP(ipNet)
	if err != nil {
		t.Fatal(err)
	}
	got := ip.String()
	if d := cmp.Diff(want, got); d != "" {
		t.Errorf("addrToIP(ipNet) mismatch: (-want +got):\n%s", d)
	}

	ipAddr := &net.IPAddr{IP: net.ParseIP("::2"), Zone: "eno1"}
	want = ipAddr.String()
	ip, zone, err = addrToIP(ipAddr)
	if err != nil {
		t.Fatal(err)
	}
	got = fmt.Sprintf("%s%%%s", ip.String(), zone)
	if d := cmp.Diff(want, got); d != "" {
		t.Errorf("addrToIP(ipAddr) mismatch: (-want +got):\n%s", d)
	}

	udpAddr := &net.IPAddr{IP: net.ParseIP("::3"), Zone: "eno1"}
	want = fmt.Sprintf("%s%%%s", udpAddr.IP.String(), udpAddr.Zone)
	ip, zone, err = addrToIP(udpAddr)
	if err != nil {
		t.Fatal(err)
	}
	got = fmt.Sprintf("%s%%%s", ip.String(), zone)
	if d := cmp.Diff(want, got); d != "" {
		t.Errorf("addrToIP(ipAddr) mismatch: (-want +got):\n%s", d)
	}
}

func TestDNSSDFinder_browseCallbackError(t *testing.T) {
	c := make(chan *fuchsiaDevice)
	f := &dnsSDFinder{deviceChannel: c}
	ctx := &dnsSDContext{
		finder: f,
	}
	browseCallback(-2, "some-whatever-stuff", ctx)
	target := <-c
	if target.err == nil {
		t.Errorf("expected error from browse callback")
	}
}

func TestDNSSDFinder_resolveCallbackError(t *testing.T) {
	c := make(chan *fuchsiaDevice)
	f := &dnsSDFinder{deviceChannel: c}
	ctx := &dnsSDContext{
		finder: f,
	}
	resolveCallback(-2, "whatever-man", "222222", nil, ctx)
	target := <-c
	if target.err == nil {
		t.Errorf("expected error from resolve callback")
	}
}

func TestDNSSDFinder_resolveCallbackBadIP(t *testing.T) {
	c := make(chan *fuchsiaDevice)
	f := &dnsSDFinder{deviceChannel: c}
	ctx := &dnsSDContext{
		finder: f,
	}
	resolveCallback(0, "whatever-my-dude", "192.161.21.222222", nil, ctx)
	target := <-c
	if target.err == nil {
		t.Errorf("expected error from resolve callback for bad IP")
	}
}

func TestDNSSDFinder_resolveCallback(t *testing.T) {
	runSubTests(t, "", func(t *testing.T, s subtest) {
		c := make(chan *fuchsiaDevice)
		f := &dnsSDFinder{
			deviceFinderBase: deviceFinderBase{
				cmd: &devFinderCmd{},
			},
			deviceChannel: c,
		}
		ctx := &dnsSDContext{
			finder: f,
		}
		var fakeIface *net.Interface
		var zoneWant string
		if s.ipv6 {
			zoneWant = s.defaultMDNSZone()
			fakeIface = &net.Interface{
				Name: zoneWant,
			}
		}
		resolveCallback(0, fmt.Sprintf("%s.local.", fuchsiaMDNSNodename1), s.defaultMDNSIP().String(), fakeIface, ctx)
		target := <-c
		if target.err != nil {
			t.Errorf("unexpected error: %v", target.err)
		}
		domainWant := fuchsiaMDNSNodename1
		addrWant := s.defaultMDNSIP()
		if domainWant != target.domain {
			t.Errorf("expected domain %q, got %q", domainWant, target.domain)
		}
		if addrWant.String() != target.addr.String() {
			t.Errorf("expected addr %v, got %v", addrWant, target.addr)
		}
		if zoneWant != target.zone {
			t.Errorf("expected zone %q, got %q", zoneWant, target.zone)
		}
	})
}

func TestDNSContextStoreAndLookup(t *testing.T) {
	var w sync.WaitGroup
	for i := 1; i <= 1000; i++ {
		w.Add(1)
		i := i
		go func() {
			t.Helper()
			defer w.Done()
			want := fmt.Sprintf("%v", i)
			ctx := newDNSSDContext(makeDNSSDFinderForTest(want), func(_ unsafe.Pointer) int { return 0 })
			ctx = getDNSSDContext(ctx.idx)
			got := <-ctx.finder.deviceChannel
			if want != got.domain {
				t.Fatalf("unable to lookup context: want %q, got %q", want, got.domain)
			}
		}()
	}
	w.Wait()
}

func TestDNSContextStoreAndLookup_badAllocCall(t *testing.T) {
	f := makeDNSSDFinderForTest(fuchsiaMDNSNodename1)
	<-f.deviceChannel // flush out unused value.
	ctx := newDNSSDContext(f, func(_ unsafe.Pointer) int { return -1 })
	if ctx != nil {
		t.Errorf("expecting nil context")
	}
	if target := <-f.deviceChannel; target.err == nil {
		t.Errorf("expecting no error from channel")
	}
}
