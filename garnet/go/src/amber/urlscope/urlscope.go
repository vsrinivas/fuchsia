package urlscope

import (
	"fmt"
	"net"
	"net/url"
	"strings"
)

// Rescope compares the local and remote URLs. If the remote url is a reference
// to the same host and port as the local URL with the IPv6 scope removed, it
// returns a pointer to the remote URL with the locally scoped host address.
func Rescope(local, remote *url.URL) *url.URL {
	// Here be dragons. There's no really clean way to do this in the Go stdlib, as
	// the exposed parsers do not compose together for a stable solution, nor is
	// there one in x/net. The stdlib relies on the resolver for this task, but we
	// don't actually want the resolver, we want to match something that appears to
	// be be a scoped ipv6 url. Warnings stated, lets go.

	if local.Host[0] != '[' || remote.Host[0] != '[' {
		return nil
	}

	if strings.Count(local.Hostname(), "%") != 1 {
		return nil
	}

	parts := strings.SplitN(local.Hostname(), "%", 2)

	ip := net.ParseIP(parts[0])
	if ip == nil {
		return nil
	}

	port := local.Port()
	if port != "" {
		port = ":" + port
	}

	localWithoutScope := fmt.Sprintf("[%s]%s", ip, port)

	if remote.Host != localWithoutScope {
		return nil
	}

	// Userinfo in the url is unexported, so this copy is unfortunately expensive.
	newURL, err := url.Parse(remote.String())
	if err != nil {
		// If we reached this path, the caller messed with something, or there's a
		// stdlib bug.
		return nil
	}

	newURL.Host = local.Host

	return newURL
}
