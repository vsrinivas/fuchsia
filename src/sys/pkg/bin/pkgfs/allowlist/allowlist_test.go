package allowlist

import (
	"strings"
	"testing"
)

type allowListCheckAndResult struct {
	item          string
	shouldBeFound bool
}

var allowlistTests = []struct {
	allowlistText             string
	allowlistChecksAndResults []allowListCheckAndResult
	desiredAllowListLength    int
}{
	{
		// Empty allowlist
		"#Comment",
		[]allowListCheckAndResult{
			{"test", false},
			{"#Comment", false},
		},
		0,
	},
	{
		// Empty allowlist with a newline at EOF
		"#Comment\n",
		[]allowListCheckAndResult{
			{"test", false},
			{"#Comment", false},
		},
		0,
	},
	{
		// Allowlist with a comment
		"#Test\nls\ncurl\n",
		[]allowListCheckAndResult{
			{"ls", true},
			{"curl", true},
			{"iquery", false},
		},
		2,
	},
	{
		// Allowlist without a newline at EOF
		"#Test\nls\ncurl",
		[]allowListCheckAndResult{
			{"ls", true},
			{"curl", true},
			{"iquery", false},
		},
		2,
	},
}

func TestAllowlist(t *testing.T) {
	for _, test := range allowlistTests {
		t.Run(test.allowlistText, func(t *testing.T) {
			reader := strings.NewReader(test.allowlistText)
			allowlist, err := LoadFrom(reader)
			if err != nil {
				t.Fatal(err)
			}
			for _, check := range test.allowlistChecksAndResults {
				found := allowlist.Contains(check.item)
				if found != check.shouldBeFound {
					t.Errorf("Expected item %s to be found in allowlist: %t. Got: %t", check.item, check.shouldBeFound, found)
				}
			}

			if len(allowlist.allowed) != test.desiredAllowListLength {
				t.Errorf("Expected allowlist to have %d entries, found %d", len(allowlist.allowed), test.desiredAllowListLength)
			}
		})
	}
}
