package util_test

import (
	"strings"
	"testing"

	"netstack/util"

	"github.com/google/netstack/tcpip"
)

func TestParse(t *testing.T) {
	tests := []struct {
		txt  string
		addr tcpip.Address
	}{
		{"::", tcpip.Address(strings.Repeat("\x00", 16))},
		{"8::", tcpip.Address("\x00\x08" + strings.Repeat("\x00", 14))},
		{"::8a", tcpip.Address(strings.Repeat("\x00", 14) + "\x00\x8a")},
		{"fe80::1234:5678", "\xfe\x80\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x12\x34\x56\x78"},
		{"fe80::b097:c9ff:fe02:477", "\xfe\x80\x00\x00\x00\x00\x00\x00\xb0\x97\xc9\xff\xfe\x02\x04\x77"},
		{"a:b:c:d:1:2:3:4", "\x00\x0a\x00\x0b\x00\x0c\x00\x0d\x00\x01\x00\x02\x00\x03\x00\x04"},
		{"a:b:c::2:3:4", "\x00\x0a\x00\x0b\x00\x0c\x00\x00\x00\x00\x00\x02\x00\x03\x00\x04"},
		{"000a:000b:000c::", "\x00\x0a\x00\x0b\x00\x0c\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00"},
		{"0000:0000:0000::0001", tcpip.Address(strings.Repeat("\x00", 15) + "\x01")},
		{"0:0::1", tcpip.Address(strings.Repeat("\x00", 15) + "\x01")},
	}

	for _, test := range tests {
		got := util.Parse(test.txt)
		if got != test.addr {
			t.Errorf("got util.Parse(%v) = %v, want %v", test.txt, got, test.addr)
		}
	}
}

// Copied from pkg net (ip_test.go).
func TestParseCIDR(t *testing.T) {
	for _, tt := range []struct {
		in      string
		address string
		netmask string
	}{
		{"135.104.0.0/32", "135.104.0.0", "255.255.255.255"},
		{"0.0.0.0/24", "0.0.0.0", "255.255.255.0"},
		{"135.104.0.0/24", "135.104.0.0", "255.255.255.0"},
		{"135.104.0.1/32", "135.104.0.1", "255.255.255.255"},
		{"135.104.0.1/24", "135.104.0.1", "255.255.255.0"},
	} {
		address, subnet, err := util.ParseCIDR(tt.in)
		if err != nil {
			t.Error(err)
		} else if want := util.Parse(tt.address); address != want {
			t.Errorf("ParseCIDR('%s') = ('%s', _); want ('%s', _)", tt.in, address, want)
		} else {
			netmask := tcpip.AddressMask(util.Parse(tt.netmask))
			if want, err := tcpip.NewSubnet(util.ApplyMask(want, netmask), netmask); err != nil {
				t.Errorf("tcpip.NewSubnet('%s', '%s') failed: %v", want, tt.netmask, err)
			} else if want != subnet {
				t.Errorf("ParseCIDR('%s') = (_, %+v); want (_, %+v)", tt.in, subnet, want)
			}
		}
	}
}
