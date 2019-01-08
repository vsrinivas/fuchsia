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
		{"1.2.3.4", tcpip.Address("\x01\x02\x03\x04")},
		{"1.2.3.255", tcpip.Address("\x01\x02\x03\xff")},
		{"1.2.333.1", tcpip.Address("")},
		{"1.2.3", tcpip.Address("")},
		{"1.2.3.4a", tcpip.Address("")},
		{"a1.2.3.4", tcpip.Address("")},
		{"::FFFF:1.2.3.4", tcpip.Address("\x01\x02\x03\x04")},
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
			t.Errorf("got util.Parse(%q) = %q, want %q", test.txt, got, test.addr)
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
				t.Errorf("tcpip.NewSubnet('%v', '%v') failed: %v", want, tt.netmask, err)
			} else if want != subnet {
				t.Errorf("ParseCIDR('%s') = (_, %+v); want (_, %+v)", tt.in, subnet, want)
			}
		}
	}
}

func TestIsAny(t *testing.T) {
	for _, tc := range []struct {
		name string
		addr tcpip.Address
		want bool
	}{
		{"IPv4-Empty", "", false},
		{"IPv4-Zero", "\x00\x00\x00\x00", true},
		{"IPv4-Loopback", "\x7f\x00\x00\x01", false},
		{"IPv4-Broadcast", "\xff\xff\xff\xff", false},
		{"IPv4-Regular1", "\x00\x00\x00\x01", false},
		{"IPv4-Regular2", "\x00\x00\x01\x00", false},
		{"IPv4-Regular3", "\x00\x01\x00\x00", false},
		{"IPv4-Regular4", "\x01\x00\x00\x00", false},
		{"IPv4-Regular5", "\x01\x01\x01\x01", false},
		{"IPv4-Regular6", "\x11\x22\x33\x44", false},
		{"IPv6-Empty", "", false},
		{"IPv6-Zero", "\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00", true},
		{"IPv6-Loopback", "\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x01", false},
		{"IPv6-Broadcast", "\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff", false},
		{"IPv6-Regular1", "\x00\x00\x00\x00\x00\x01\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00", false},
		{"IPv6-Regular2", "\x00\x00\x00\x00\x00\x00\x00\x00\x01\x00\x00\x00\x00\x00\x00\x00", false},
		{"IPv6-Regular3", "\x00\x00\x00\x10\x00\x00\x00\x00\x00\x00\x00\x00\x10\x00\x00\x00", false},
		{"IPv6-Regular4", "\x00\xaa\x00\x01\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00", false},
		{"IPv6-Regular5", "\x01\x01\x01\x01\x01\x01\x01\x01\x01\x01\x01\x01\x01\x01\x01\x01", false},
	} {
		t.Run(tc.name, func(t *testing.T) {
			if got := util.IsAny(tc.addr); got != tc.want {
				t.Fatalf("IsAny(%v) = %v, want = %v", tc.addr, got, tc.want)
			}
		})
	}
}

func TestPrefixLength(t *testing.T) {
	for _, tc := range []struct {
		name string
		mask tcpip.AddressMask
		want int
	}{
		{"IPv4-Empty", "", 0},
		{"IPv4-0", "\x00\x00\x00\x00", 0},
		{"IPv4-3", "\xe0\x00\x00\x00", 3},
		{"IPv4-7", "\xfe\x00\x00\x00", 7},
		{"IPv4-8", "\xff\x00\x00\x00", 8},
		{"IPv4-12", "\xff\xf0\x00\x00", 12},
		{"IPv4-16", "\xff\xff\x00\x00", 16},
		{"IPv4-24", "\xff\xff\xff\x00", 24},
		{"IPv4-29", "\xff\xff\xff\xfc", 30},
		{"IPv4-32", "\xff\xff\xff\xff", 32},
		{"IPv6-Empty", "", 0},
		{"IPv6-0", "\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00", 0},
		{"IPv6-5", "\xf8\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00", 5},
		{"IPv6-8", "\xff\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00", 8},
		{"IPv6-22", "\xff\xff\xfc\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00", 22},
		{"IPv6-73", "\xff\xff\xff\xff\xff\xff\xff\xff\xff\x80\x00\x00\x00\x00\x00\x00", 73},
		{"IPv6-123", "\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xe0", 123},
		{"IPv6-128", "\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff", 128},
	} {
		t.Run(tc.name, func(t *testing.T) {
			if got := util.PrefixLength(tc.mask); got != tc.want {
				t.Fatalf("PrefixLength(%v) = %v, want = %v", tc.mask, got, tc.want)
			}
		})
	}
}
