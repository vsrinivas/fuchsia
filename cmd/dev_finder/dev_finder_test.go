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

func newDevFinderCmd(handler mDNSHandler, answerDomains []string) devFinderCmd {
	// Because mdnsAddrs have two addresses specified and mdnsPorts have
	// two ports specified, four MDNS objects are created. To emulate the
	// case where only one of them responds, create only one fake MDNS
	// object with answers. The others wouldn't respond at all. See the
	// Send() method above.
	mdnsCount := 0
	return devFinderCmd{
		mdnsHandler: handler,
		mdnsAddrs:   "224.0.0.251,224.0.0.250",
		mdnsPorts:   "5353,5356",
		timeout:     10,
		newMDNSFunc: func(addr string) mdnsInterface {
			mdnsCount++
			switch mdnsCount {
			case 1:
				return &fakeMDNS{
					answer: &fakeAnswer{
						ip:      "192.168.0.42",
						domains: answerDomains,
					},
				}
			default:
				return &fakeMDNS{}
			}
		},
	}
}

func compareFuchsiaDevices(d1, d2 *fuchsiaDevice) bool {
	return cmp.Equal(d1.addr, d2.addr) && cmp.Equal(d1.domain, d2.domain)
}

//// Tests for the `list` command.

func TestListDevices(t *testing.T) {
	cmd := listCmd{
		devFinderCmd: newDevFinderCmd(
			listMDNSHandler,
			[]string{
				"some.domain",
				"another.domain",
			}),
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
			addr:   net.ParseIP("192.168.0.42").To4(),
			domain: "another.domain",
		},
	}
	if d := cmp.Diff(want, got, cmp.Comparer(compareFuchsiaDevices)); d != "" {
		t.Errorf("listDevices mismatch: (-want +got):\n%s", d)
	}

}

func TestListDevices_domainFilter(t *testing.T) {
	cmd := listCmd{
		devFinderCmd: newDevFinderCmd(
			listMDNSHandler,
			[]string{
				"some.domain",
				"another.domain",
			}),
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
	}
	if d := cmp.Diff(want, got, cmp.Comparer(compareFuchsiaDevices)); d != "" {
		t.Errorf("listDevices mismatch: (-want +got):\n%s", d)
	}

}

//// Tests for the `resolve` command.

func TestResolveDevices(t *testing.T) {
	cmd := resolveCmd{
		devFinderCmd: newDevFinderCmd(
			resolveMDNSHandler,
			[]string{
				"some.domain.local",
				"another.domain.local",
			}),
	}

	got, err := cmd.resolveDevices(context.Background(), "some.domain")
	if err != nil {
		t.Fatalf("resolveDevices: %v", err)
	}
	want := []*fuchsiaDevice{
		{
			addr:   net.ParseIP("192.168.0.42").To4(),
			domain: "some.domain.local",
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
