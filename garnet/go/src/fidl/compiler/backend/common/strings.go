package common

import (
	"fmt"
	"strings"
)

func SingleQuote(s string) string {
	s = strings.ReplaceAll(s, `\`, `\\`)
	s = strings.ReplaceAll(s, `'`, `\'`)
	return fmt.Sprintf(`'%s'`, s)
}
