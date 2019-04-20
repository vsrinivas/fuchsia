package main

import (
	"bytes"
	"context"
	"encoding/json"
	"net"
	"strings"
	"testing"

	"github.com/google/go-cmp/cmp"

	"fuchsia.googlesource.com/tools/mdns"
)

// fakeMDNS is a fake implementation of MDNS for testing.
type fakeMDNS struct {
	answer   *fakeAnswer
	handlers []func(net.Interface, net.Addr, mdns.Packet)
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
func (m *fakeMDNS) Send(mdns.Packet) error                  { return nil }
func (m *fakeMDNS) Start(context.Context, int) error {
	if m.answer != nil {
		ifc := net.Interface{}
		ip := net.IPAddr{IP: net.ParseIP(m.answer.ip)}
		answers := make([]mdns.Record, len(m.answer.domains))
		for _, d := range m.answer.domains {
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
		go func() {
			for _, h := range m.handlers {
				h(ifc, &ip, pkt)
			}
		}()
	}
	return nil
}

func compareFuchsiaDevices(d1, d2 *fuchsiaDevice) bool {
	return cmp.Equal(d1.addr, d2.addr) && cmp.Equal(d1.domain, d2.domain)
}

func TestListFindDevices(t *testing.T) {
	// Because mdnsPorts have two ports specified, two MDNS objects are
	// created. To emulate the case where only one port responds, create
	// only one fake MDNS object with answers. The other one wouldn't
	// respond at all. See the Start() method above.
	mdnsCount := 0
	cmd := listCmd{
		devFinderCmd: devFinderCmd{
			mdnsHandler: listMDNSHandler,
			mdnsPorts:   "5353,5356",
			timeout:     2000,
			newMDNSFunc: func() mdnsInterface {
				mdnsCount++
				switch mdnsCount {
				case 1:
					return &fakeMDNS{
						answer: &fakeAnswer{
							ip: "192.168.0.42",
							domains: []string{
								"some.domain",
								"another.domain",
							},
						},
					}
				default:
					return &fakeMDNS{}
				}
			},
		},
	}

	got, err := cmd.findDevices(context.Background())
	if err != nil {
		t.Fatalf("findDevices: %v", err)
	}
	want := []*fuchsiaDevice{
		{
			addr:   net.ParseIP("192.168.0.42"),
			domain: "some.domain",
		},
		{
			addr:   net.ParseIP("192.168.0.42"),
			domain: "another.domain",
		},
	}
	if d := cmp.Diff(want, got, cmp.Comparer(compareFuchsiaDevices)); d != "" {
		t.Errorf("findDevices mismatch: (-want +got):\n%s", d)
	}

}

func TestListFindDevices_domainFilter(t *testing.T) {
	// Because mdnsPorts have two ports specified, two MDNS objects are
	// created. To emulate the case where only one port responds, create
	// only one fake MDNS object with answers. The other one wouldn't
	// respond at all. See the Start() method above.
	mdnsCount := 0
	cmd := listCmd{
		devFinderCmd: devFinderCmd{
			mdnsHandler: listMDNSHandler,
			mdnsPorts:   "5353,5356",
			timeout:     2000,
			newMDNSFunc: func() mdnsInterface {
				mdnsCount++
				switch mdnsCount {
				case 1:
					return &fakeMDNS{
						answer: &fakeAnswer{
							ip: "192.168.0.42",
							domains: []string{
								"some.domain",
								"another.domain",
							},
						},
					}
				default:
					return &fakeMDNS{}
				}
			},
		},
		domainFilter: "some",
	}

	got, err := cmd.findDevices(context.Background())
	if err != nil {
		t.Fatalf("findDevices: %v", err)
	}
	want := []*fuchsiaDevice{
		{
			addr:   net.ParseIP("192.168.0.42"),
			domain: "some.domain",
		},
	}
	if d := cmp.Diff(want, got, cmp.Comparer(compareFuchsiaDevices)); d != "" {
		t.Errorf("findDevices mismatch: (-want +got):\n%s", d)
	}

}

func TestListOutputNormal(t *testing.T) {
	devs := []*fuchsiaDevice{
		{
			addr: net.ParseIP("123.12.234.23"),
		},
		{
			addr: net.ParseIP("11.22.33.44"),
		},
	}
	var buf strings.Builder
	cmd := listCmd{
		devFinderCmd: devFinderCmd{output: &buf},
	}
	cmd.outputNormal(devs)

	got := buf.String()
	want := `123.12.234.23
11.22.33.44
`
	if d := cmp.Diff(want, got); d != "" {
		t.Errorf("outputNormal mismatch: (-want +got):\n%s", d)
	}
}

func TestListOutputNormal_fullInfo(t *testing.T) {
	devs := []*fuchsiaDevice{
		{
			addr:   net.ParseIP("123.12.234.23"),
			domain: "hello.world",
		},
		{
			addr:   net.ParseIP("11.22.33.44"),
			domain: "fuchsia.rocks",
		},
	}
	var buf strings.Builder
	cmd := listCmd{
		devFinderCmd: devFinderCmd{output: &buf},
		fullInfo:     true,
	}
	cmd.outputNormal(devs)

	got := buf.String()
	want := `123.12.234.23 hello.world
11.22.33.44 fuchsia.rocks
`
	if d := cmp.Diff(want, got); d != "" {
		t.Errorf("outputNormal mismatch: (-want +got):\n%s", d)
	}
}

func TestListOutputJSON(t *testing.T) {
	devs := []*fuchsiaDevice{
		{
			addr: net.ParseIP("123.12.234.23"),
		},
		{
			addr: net.ParseIP("11.22.33.44"),
		},
	}
	var buf bytes.Buffer
	cmd := listCmd{
		devFinderCmd: devFinderCmd{
			json:   true,
			output: &buf,
		},
	}
	cmd.outputJSON(devs)

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

func TestListOutputJSON_fullInfo(t *testing.T) {
	devs := []*fuchsiaDevice{
		{
			addr:   net.ParseIP("123.12.234.23"),
			domain: "hello.world",
		},
		{
			addr:   net.ParseIP("11.22.33.44"),
			domain: "fuchsia.rocks",
		},
	}
	var buf bytes.Buffer
	cmd := listCmd{
		devFinderCmd: devFinderCmd{
			json:   true,
			output: &buf,
		},
		fullInfo: true,
	}
	cmd.outputJSON(devs)

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
		t.Errorf("outputNormal mismatch: (-want +got):\n%s", d)
	}
}
