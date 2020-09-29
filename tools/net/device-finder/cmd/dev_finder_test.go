// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package main

import (
	"bytes"
	"context"
	"encoding/json"
	"errors"
	"fmt"
	"net"
	"strconv"
	"strings"
	"sync"
	"syscall"
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

// TODO(awdavies): Induce an error in a way that is more predictable, and uses
// the available API. This is a hack that makes errors occur when `Start()` is
// called with this port to the `fakeMDNS` struct.
const failurePort = 999999

const pollTestTimeout = time.Second

type nbDiscoverFunc func(chan<- *netboot.Target, string) (func() error, error)

type fakeNetbootClient struct {
	discover nbDiscoverFunc
}

func nilNBDiscoverFunc(chan<- *netboot.Target, string) (func() error, error) {
	return func() error { return nil }, nil
}

func (m *fakeNetbootClient) StartDiscover(t chan<- *netboot.Target, nodename string) (func() error, error) {
	return m.discover(t, nodename)
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

var badPortTestError = errors.New("test failure caused by passing failure port")

func (m *fakeMDNS) Start(_ context.Context, port int) error {
	if port == failurePort {
		return badPortTestError
	}
	return nil
}

func newDevFinderCmd(
	handler mDNSHandler,
	answerDomains []string,
	sendEmptyData bool,
	sendTooShortData bool,
	st subtest,
	nbDiscover nbDiscoverFunc,
) devFinderCmd {
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

// This test drops in a device on the incoming channel that has a domain that
// doesn't match the desired one while device-limit is set to 1. This should
// not affect the device-limit (when the first inbound device is the wrong one).
func TestFilterDevices(t *testing.T) {
	nbDiscover := nilNBDiscoverFunc
	cmd := newDevFinderCmd(
		resolveMDNSHandler,
		[]string{},
		false,
		false,
		subtest{ipv4: true},
		nbDiscover,
	)
	cmd.deviceLimit = 1
	f := make(chan *fuchsiaDevice, 1024)
	f <- &fuchsiaDevice{
		addr:   net.ParseIP(defaultIPTarget),
		domain: fuchsiaMDNSNodename1,
	}
	want := &fuchsiaDevice{
		addr:   net.ParseIP(defaultIPTarget),
		domain: fuchsiaMDNSNodename2,
	}
	f <- want
	got, err := cmd.filterInboundDevices(context.Background(), f, fuchsiaMDNSNodename2)
	if err != nil {
		t.Error(err)
	}
	if d := cmp.Diff([]*fuchsiaDevice{want}, got, cmp.Comparer(compareFuchsiaDevices)); d != "" {
		t.Errorf("listDevices mismatch: (-want +got):\n%s", d)
	}
}

//// Tests for the `list` command.

func TestListDevices(t *testing.T) {
	nbDiscover := func(target chan<- *netboot.Target, nodename string) (func() error, error) {
		t.Helper()
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
				nbDiscover,
			),
		}
		got, err := cmd.listDevices(context.Background())
		if err != nil {
			t.Fatalf("listDevices: %s", err)
		}
		want := []*fuchsiaDevice{
			{
				addr:   s.defaultMDNSIP(),
				zone:   s.defaultMDNSZone(),
				domain: fuchsiaMDNSNodename1,
			},
			{
				addr:   s.defaultMDNSIP(),
				zone:   s.defaultMDNSZone(),
				domain: fuchsiaMDNSNodename2,
			},
		}
		if s.ipv6 {
			want = append(want, s.defaultNetbootDevice())
		}
		if d := cmp.Diff(want, got, cmp.Comparer(compareFuchsiaDevices)); d != "" {
			t.Errorf("listDevices mismatch: (-want +got):\n%s", d)
		}
	})
}

func TestListDevice_allProtocolsDisabled(t *testing.T) {
	nbDiscover := nilNBDiscoverFunc
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
	nbDiscover := nilNBDiscoverFunc
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

	_, err := cmd.listDevices(context.Background())
	if err != nil {
		t.Fatalf("listDevices: %s", err)
	}
}

func TestListDevices_duplicateDevices(t *testing.T) {
	runSubTests(t, "", func(t *testing.T, s subtest) {
		nbDiscover := nilNBDiscoverFunc
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
			t.Fatalf("listDevices: %s", err)
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

func TestStartMDNSHandlers(t *testing.T) {
	runSubTests(t, "", func(t *testing.T, s subtest) {
		nbDiscover := nilNBDiscoverFunc
		cmd := newDevFinderCmd(
			listMDNSHandler,
			[]string{
				fuchsiaMDNSNodename1,
				fuchsiaMDNSNodename2,
			},
			false,
			false,
			s,
			nbDiscover,
		)
		for _, test := range []struct {
			name      string
			addrs     []string
			ports     []int
			expectErr error
		}{
			{
				name:      "one ipv4 addr one port",
				addrs:     []string{"192.168.1.2"},
				ports:     []int{1234},
				expectErr: nil,
			},
			{
				name:      "one ipv6 addr two ports",
				addrs:     []string{"fe80::1234:1234:1234:1234"},
				ports:     []int{1234, 4567},
				expectErr: nil,
			},
			{
				name:      "one ipv6 addr one ipv4 two ports one failure",
				addrs:     []string{"fe80::1234:1234:1234:1234", "192.168.1.2"},
				ports:     []int{1234, failurePort, 4567},
				expectErr: nil,
			},
			{
				name:      "one ipv6 addr one ipv4 one port one failure",
				addrs:     []string{"fe80::1234:1234:1234:1234", "192.168.1.2"},
				ports:     []int{failurePort, 4567},
				expectErr: nil,
			},
			{
				name:      "no addrs one port",
				addrs:     []string{},
				ports:     []int{1234},
				expectErr: noConnectableAddressError{},
			},
			{
				name:      "one ipv6 addr all fail ports",
				addrs:     []string{"fe80::1234:1234:1234:1234"},
				ports:     []int{failurePort, failurePort, failurePort},
				expectErr: noConnectableAddressError{badPortTestError},
			},
		} {
			t.Run(fmt.Sprintf("%s expect error %t", test.name, test.expectErr != nil), func(t *testing.T) {
				f := make(chan *fuchsiaDevice)
				packet := mdns.Packet{
					Header: mdns.Header{QDCount: 1},
					Questions: []mdns.Question{
						{
							Domain:  fuchsiaMDNSService,
							Type:    mdns.PTR,
							Class:   mdns.IN,
							Unicast: true,
						},
					},
				}
				expectedErr := test.expectErr
				if err := startMDNSHandlers(context.Background(), &cmd, packet, test.addrs, test.ports, f); !errors.Is(err, expectedErr) {
					t.Errorf("unexpected error. got %v expected %v", err, test.expectErr)
				}
			})
		}
	})
}

func TestListDevices_tooShortData(t *testing.T) {
	nbDiscover := nilNBDiscoverFunc
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
	nbDiscover := func(target chan<- *netboot.Target, nodename string) (func() error, error) {
		t.Helper()
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
			t.Fatalf("listDevices: %s", err)
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
	nbDiscover := nilNBDiscoverFunc
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
			t.Fatalf("json.Unmarshal: %s", err)
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
			t.Fatalf("json.Unmarshal: %s", err)
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
			t.Errorf("Address %v returns %t for link local, expect %t", test.bytes, got, test.want)
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
			t.Errorf("unexpected error: %s", target.err)
		}
		domainWant := fuchsiaMDNSNodename1
		addrWant := s.defaultMDNSIP()
		if domainWant != target.domain {
			t.Errorf("expected domain %q, got %q", domainWant, target.domain)
		}
		if addrWant.String() != target.addr.String() {
			t.Errorf("expected addr %s, got %s", addrWant, target.addr)
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
			want := fmt.Sprintf("%d", i)
			ctx := newDNSSDContext(makeDNSSDFinderForTest(want), func(unsafe.Pointer) dnsSDError { return 0 })
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
	ctx := newDNSSDContext(f, func(unsafe.Pointer) dnsSDError { return -1 })
	if ctx != nil {
		t.Errorf("expecting nil context")
	}
	if target := <-f.deviceChannel; target.err == nil {
		t.Errorf("expecting no error from channel")
	}
}

func TestDNSSDErrorMessages(t *testing.T) {
	for _, test := range []struct {
		err      dnsSDError
		expected string
	}{
		{
			dnsSDNoError,
			"NoError",
		},
		{
			dnsSDUnknown,
			"Unknown",
		},
		{
			dnsSDNoSuchName,
			"NoSuchName",
		},
		{
			dnsSDNoMemory,
			"NoMemory",
		},
		{
			dnsSDBadParam,
			"BadParam",
		},
		{
			dnsSDBadReference,
			"BadReference",
		},
		{
			dnsSDBadState,
			"BadState",
		},
		{
			dnsSDBadFlags,
			"BadFlags",
		},
		{
			dnsSDUnsupported,
			"Unsupported",
		},
		{
			dnsSDNotInitialized,
			"NotInitialized",
		},
		{
			dnsSDAlreadyRegistered,
			"AlreadyRegistered",
		},
		{
			dnsSDNameConflict,
			"NameConflict",
		},
		{
			dnsSDInvalid,
			"Invalid",
		},
		{
			dnsSDFirewall,
			"Firewall",
		},
		{
			dnsSDIncompatible,
			"Incompatible",
		},
		{
			dnsSDBadInterfaceIndex,
			"BadInterfaceIndex",
		},
		{
			dnsSDRefused,
			"Refused",
		},
		{
			dnsSDNoSuchRecord,
			"NoSuchRecord",
		},
		{
			dnsSDNoAuth,
			"NoAuth",
		},
		{
			dnsSDNoSuchKey,
			"NoSuchKey",
		},
		{
			dnsSDNATTraversal,
			"NATTraversal",
		},
		{
			dnsSDDoubleNAT,
			"DoubleNAT",
		},
		{
			dnsSDBadTime,
			"BadTime",
		},
		{
			dnsSDBadSig,
			"BadSig",
		},
		{
			dnsSDBadKey,
			"BadKey",
		},
		{
			dnsSDTransient,
			"Transient",
		},
		{
			dnsSDServiceNotRunning,
			"ServiceNotRunning",
		},
		{
			dnsSDNATPortMappingUnsupported,
			"NATPortMappingUnsupported",
		},
		{
			dnsSDNATPortMappingDisabled,
			"NATPortMappingDisabled",
		},
		{
			dnsSDNoRouter,
			"NoRouter",
		},
		{
			dnsSDPollingMode,
			"PollingMode",
		},
		{
			dnsSDTimeout,
			"Timeout",
		},
		{
			dnsSDError(-3),
			"Unrecognized Error Code: -3",
		},
		{
			dnsSDError(5),
			"Unrecognized Error Code: 5",
		},
	} {
		if test.err.Error() != test.expected {
			t.Errorf("expected %q for error code %d, got %q", test.expected, int32(test.err), test.err.Error())
		}
	}
}

func TestDNSSDPollErrors(t *testing.T) {
	for _, test := range []struct {
		name        string
		results     []syscall.Errno
		expectedErr syscall.Errno
	}{
		{
			name: "eintr and eagain",
			results: []syscall.Errno{
				syscall.EINTR,
				syscall.EINTR,
				syscall.EINTR,
				syscall.EAGAIN,
			},
			expectedErr: 0,
		},
		{
			name: "eintr to non-err",
			results: []syscall.Errno{
				syscall.EINTR,
				0,
			},
			expectedErr: 0,
		},
		{
			name: "eagain",
			results: []syscall.Errno{
				syscall.EAGAIN,
			},
			expectedErr: 0,
		},
		{
			name: "eintr to fatal error",
			results: []syscall.Errno{
				syscall.EINTR,
				syscall.EINVAL,
			},
			expectedErr: syscall.EINVAL,
		},
		{
			name: "badf",
			results: []syscall.Errno{
				syscall.EBADF,
			},
			expectedErr: syscall.EBADF,
		},
	} {
		t.Run(test.name, func(t *testing.T) {
			f := makeDNSSDFinderForTest(fuchsiaMDNSNodename1)
			// This value is unused outside of verifying that storage/lookup of a
			// DNS-SD context is working.
			<-f.deviceChannel
			dctx := newDNSSDContext(f, func(unsafe.Pointer) dnsSDError { return 0 })
			resultIdx := 0
			dctx.pollingFunc = func(d *dnsSDContext, timeout int) (bool, syscall.Errno) {
				res := test.results[resultIdx]
				resultIdx += 1
				// Don't return ready as we're not intending to process results ever.
				return false, res
			}
			// This can crash if one of the above test cases isn't setup properly, e.g.
			// an error intended to return from the polling function (EAGAIN) causing
			// the polling function to contine looping would create an index out of
			// bounds error.
			dctx.poll()
			if test.expectedErr != 0 {
				select {
				case <-time.After(pollTestTimeout):
					t.Fatal("timeout")
				case chErr := <-f.deviceChannel:
					if err := chErr.err; !errors.Is(err, test.expectedErr) {
						t.Fatalf("expected error %q received %q", test.expectedErr, err)
					}
				}
			}
		})
	}
}
