package allowlist

import (
	"bufio"
	"io"
	"log"
	"strings"
)

// Allowlist is a generic allowlist for strings. Currently used to allow/disallow
// non-static packages from the /packages subdir
type Allowlist struct {
	// Note to future maintainers:
	// If you're going to do updates to this at runtime
	// (i.e. after pkgfs startup when there's only one goroutine),
	// you MUST mediate access to this map with a synchronization mechanism.
	allowed map[string]struct{}
}

// LoadFrom takes a file in the form of an io.Reader and returns a built AllowList
func LoadFrom(f io.Reader) (*Allowlist, error) {
	allowed := map[string]struct{}{}

	reader := bufio.NewReader(f)
	for {
		l, err := reader.ReadString('\n')
		l = strings.TrimSpace(l)
		if err != nil {
			if err == io.EOF {
				if l == "" {
					// We're done
					break
				} else {
					// Keep going for one more record
				}
			} else {
				log.Printf("pkgfs: couldn't parse allowlist file: %v", err)
				return nil, err
			}
		}
		if strings.HasPrefix(l, "#") {
			// This is a comment line in the allowlist
			continue
		}
		allowed[l] = struct{}{}
	}

	return &Allowlist{allowed: allowed}, nil
}

// Contains returns whether an Allowlist contains a given string
func (a *Allowlist) Contains(entry string) bool {
	_, found := a.allowed[entry]
	return found
}
