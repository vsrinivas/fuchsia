package urlscope

import (
	"fmt"
	"net/url"
	"testing"
)

func TestRescope(t *testing.T) {
	type example struct {
		local  string
		remote string
		want   string
	}
	examples := []example{
		{"http://example.org", "http://example.org", ""},
		{"http://example.org:1234/", "http://example.org:1234/", ""},
		{"http://[2001:4860:4860::8888]", "http://[2001:4860:4860::8888]", ""},
		{"http://[2001:4860:4860::8888]:1234", "http://[2001:4860:4860::8888]:1234", ""},
		{"http://[2001:4860:4860::8888]/abc", "http://[2001:4860:4860::8888]/abc", ""},
		{"http://[2001:4860:4860::8888]:1234/abc", "http://[2001:4860:4860::8888]:1234/abc", ""},
		{"http://[2001:4860:4860::8888%25eth0]/abc", "http://[2001:4860:4860::8888]/abc", "http://[2001:4860:4860::8888%25eth0]/abc"},
		{"http://[2001:4860:4860::8888%25eth0]:1234/abc", "http://[2001:4860:4860::8888]:1234/abc", "http://[2001:4860:4860::8888%25eth0]:1234/abc"},
		{"http://[2001:4860:4860::8888%25eth0]:1234/abc", "http://u:p@[2001:4860:4860::8888]:1234/abc", "http://u:p@[2001:4860:4860::8888%25eth0]:1234/abc"},
		{"http://[fe80::1a60:24ff:fe89:3f16%25ethx78]:8083/config.json", "http://[fe80::1a60:24ff:fe89:3f16]:8083", "http://[fe80::1a60:24ff:fe89:3f16%25ethx78]:8083"},
	}

	for _, ex := range examples {
		t.Run(fmt.Sprintf("Rescope(%q, %q)", ex.local, ex.remote), func(t *testing.T) {
			local, err := url.Parse(ex.local)
			if err != nil {
				t.Fatal(err)
			}
			remote, err := url.Parse(ex.remote)
			if err != nil {
				t.Fatal(err)
			}

			got := Rescope(local, remote)

			if got == nil {
				if ex.want == "" {
					return
				}

				t.Fatalf("got %v, want %v", got, ex.want)
			}

			if got.String() != ex.want {
				t.Errorf("got %v, want %v", got, ex.want)
			}
		})
	}
}
