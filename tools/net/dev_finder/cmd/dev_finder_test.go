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
	"strings"
	"testing"
	"time"

	"github.com/google/go-cmp/cmp"

	"go.fuchsia.dev/fuchsia/tools/net/mdns"
	"go.fuchsia.dev/fuchsia/tools/net/netboot"
)

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
	ip      string
	domains []string
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
			ifc := net.Interface{}
			ip := net.IPAddr{IP: net.ParseIP(m.answer.ip).To4()}
			for _, q := range packet.Questions {
				switch {
				case q.Type == mdns.PTR && q.Class == mdns.IN:
					// 'list' command
					answers := make([]mdns.Record, len(m.answer.domains))
					for _, d := range m.answer.domains {
						// Cases for malformed response.
						if m.sendEmptyData {
							answers = append(answers, mdns.Record{
								Class: mdns.IN,
								Type:  mdns.PTR,
								Data:  nil, // Empty data
							})
							continue
						}
						if m.sendTooShortData {
							data := make([]byte, len(d)) // One byte shorter
							data[0] = byte(len(d))
							copy(data[1:], []byte(d[1:]))
							answers = append(answers, mdns.Record{
								Class: mdns.IN,
								Type:  mdns.PTR,
								Data:  data,
							})
							continue
						}

						data := make([]byte, len(d)+1)
						data[0] = byte(len(d))
						copy(data[1:], []byte(d))
						answers = append(answers, mdns.Record{
							Class: mdns.IN,
							Type:  mdns.PTR,
							Data:  data,
						})
					}
					pkt := mdns.Packet{Answers: answers}
					for _, h := range m.handlers {
						h(ifc, &ip, pkt)
					}
				case q.Type == mdns.A && q.Class == mdns.IN:
					// 'resolve' command
					answers := make([]mdns.Record, len(m.answer.domains))
					for _, d := range m.answer.domains {
						answers = append(answers, mdns.Record{
							Class:  mdns.IN,
							Type:   mdns.A,
							Data:   net.ParseIP(m.answer.ip).To4(),
							Domain: d,
						})
					}
					pkt := mdns.Packet{Answers: answers}
					for _, h := range m.handlers {
						h(ifc, &ip, pkt)
					}
				}
			}
		}()
	}
	return nil
}
func (m *fakeMDNS) Start(context.Context, int) error { return nil }

func newDevFinderCmd(handler mDNSHandler, answerDomains []string, sendEmptyData bool, sendTooShortData bool, nbDiscover nbDiscoverFunc) devFinderCmd {
	return devFinderCmd{
		mdnsHandler: handler,
		mdnsAddrs:   "224.0.0.251",
		mdnsPorts:   "5353",
		timeout:     10,
		netboot:     true,
		mdns:        true,
		newMDNSFunc: func(addr string) mdnsInterface {
			return &fakeMDNS{
				answer: &fakeAnswer{
					ip:      "192.168.0.42",
					domains: answerDomains,
				},
				sendEmptyData:    sendEmptyData,
				sendTooShortData: sendTooShortData,
			}
		},
		newNetbootFunc: func(_ time.Duration) netbootClientInterface {
			return &fakeNetbootClient{nbDiscover}
		},
	}
}

func compareFuchsiaDevices(d1, d2 *fuchsiaDevice) bool {
	return cmp.Equal(d1.addr, d2.addr) && cmp.Equal(d1.domain, d2.domain) && cmp.Equal(d1.zone, d2.zone)
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
				TargetAddress: net.ParseIP("192.168.1.2").To4(),
				Nodename:      "this-is-a-netboot-device",
			}
			target <- &netboot.Target{
				TargetAddress: net.ParseIP("192.168.0.42").To4(),
				Nodename:      "some.domain",
			}
		}()
		return func() error { return nil }, nil

	}
	cmd := listCmd{
		devFinderCmd: newDevFinderCmd(
			listMDNSHandler,
			[]string{
				"some.domain",
				"another.domain",
			}, false, false, nbDiscover),
	}

	got, err := cmd.listDevices(context.Background())
	if err != nil {
		t.Fatalf("listDevices: %v", err)
	}
	want := []*fuchsiaDevice{
		{
			addr:   net.ParseIP("192.168.0.42").To4(),
			domain: "another.domain",
		},
		{
			addr:   net.ParseIP("192.168.0.42").To4(),
			domain: "some.domain",
		},
		{
			addr:   net.ParseIP("192.168.1.2").To4(),
			domain: "this-is-a-netboot-device",
		},
	}
	if d := cmp.Diff(want, got, cmp.Comparer(compareFuchsiaDevices)); d != "" {
		t.Errorf("listDevices mismatch: (-want +got):\n%s", d)
	}
}

func TestListDevices_domainFilter(t *testing.T) {
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
				TargetAddress: net.ParseIP("192.168.1.2").To4(),
				Nodename:      "this-is-some-netboot-device",
			}
		}()
		return func() error { return nil }, nil
	}
	cmd := listCmd{
		devFinderCmd: newDevFinderCmd(
			listMDNSHandler,
			[]string{
				"some.domain",
				"another.domain",
			}, false, false, nbDiscover),
		domainFilter: "some",
	}

	got, err := cmd.listDevices(context.Background())
	if err != nil {
		t.Fatalf("listDevices: %v", err)
	}
	want := []*fuchsiaDevice{
		{
			addr:   net.ParseIP("192.168.0.42").To4(),
			domain: "some.domain",
		},
		{
			addr:   net.ParseIP("192.168.1.2").To4(),
			domain: "this-is-some-netboot-device",
		},
	}
	if d := cmp.Diff(want, got, cmp.Comparer(compareFuchsiaDevices)); d != "" {
		t.Errorf("listDevices mismatch: (-want +got):\n%s", d)
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
				"some.domain",
				"another.domain",
			},
			true, // sendEmptyData
			false, nbDiscover),
	}

	// Must not crash.
	cmd.listDevices(context.Background())
}

func TestListDevices_duplicateDevices(t *testing.T) {
	nbDiscover := func(_ chan<- *netboot.Target, _ string, _ bool) (func() error, error) {
		return func() error { return nil }, nil
	}
	cmd := listCmd{
		devFinderCmd: newDevFinderCmd(
			listMDNSHandler,
			[]string{
				"some.domain",
				"some.domain",
				"some.domain",
				"some.domain",
				"some.domain",
				"another.domain",
			},
			false,
			false,
			nbDiscover),
	}
	got, err := cmd.listDevices(context.Background())
	if err != nil {
		t.Fatalf("listDevices: %v", err)
	}
	want := []*fuchsiaDevice{
		{
			addr:   net.ParseIP("192.168.0.42").To4(),
			domain: "another.domain",
		},
		{
			addr:   net.ParseIP("192.168.0.42").To4(),
			domain: "some.domain",
		},
	}
	if d := cmp.Diff(want, got, cmp.Comparer(compareFuchsiaDevices)); d != "" {
		t.Errorf("listDevices mismatch: (-want +got):\n%s", d)
	}
}

func TestListDevices_tooShortData(t *testing.T) {
	nbDiscover := func(_ chan<- *netboot.Target, _ string, _ bool) (func() error, error) {
		return func() error { return nil }, nil
	}
	cmd := listCmd{
		devFinderCmd: newDevFinderCmd(
			listMDNSHandler,
			[]string{
				"some.domain",
				"another.domain",
			},
			false,
			true, // sendTooShortData
			nbDiscover,
		),
	}

	// Must not crash.
	cmd.listDevices(context.Background())
}

//// Tests for the `resolve` command.

func TestResolveDevices(t *testing.T) {
	resolveNode := "some.domain"
	nbDiscover := func(target chan<- *netboot.Target, nodename string, fuchsia bool) (func() error, error) {
		t.Helper()
		if !fuchsia {
			t.Fatalf("fuchsia set to false")
		}
		nodenameWant := resolveNode
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
	cmd := resolveCmd{
		devFinderCmd: newDevFinderCmd(
			resolveMDNSHandler,
			[]string{
				"some.domain.local",
				"another.domain.local",
			}, false, false, nbDiscover),
	}

	got, err := cmd.resolveDevices(context.Background(), "some.domain")
	if err != nil {
		t.Fatalf("resolveDevices: %v", err)
	}
	want := []*fuchsiaDevice{
		{
			addr:   net.ParseIP("192.168.0.42").To4(),
			domain: "some.domain",
		},
	}
	if d := cmp.Diff(want, got, cmp.Comparer(compareFuchsiaDevices)); d != "" {
		t.Errorf("resolveDevices mismatch: (-want +got):\n%s", d)
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
