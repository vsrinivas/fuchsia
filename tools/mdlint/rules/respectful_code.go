// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package rules

import (
	"bytes"
	"regexp"
	"strings"

	"go.fuchsia.dev/fuchsia/tools/mdlint/core"
)

var matchAvoidWords *regexp.Regexp

func init() {
	var (
		buf  bytes.Buffer
		keys []string
	)
	buf.WriteString("(?im)^[^a-zA-Z0-9_]*(")
	for avoidWord := range avoidWords {
		keys = append(keys, avoidWord)
	}
	buf.WriteString(strings.Join(keys, "|"))
	buf.WriteString(")[^a-zA-Z0-9_]*$")
	matchAvoidWords = regexp.MustCompile(buf.String())

	core.RegisterLintRuleOverTokens(respectfulCodeName, newRespectfulCode)
}

const respectfulCodeName = "respectful-code"

type respectfulCode struct {
	core.DefaultLintRuleOverTokens
	reporter core.Reporter
}

var _ core.LintRuleOverTokens = (*respectfulCode)(nil)

func newRespectfulCode(reporter core.Reporter) core.LintRuleOverTokens {
	return &respectfulCode{reporter: reporter}
}

var avoidWords = map[string]string{
	"master":    "primary, controller, leader, host",
	"slave":     "replica, subordinate, secondary, follower, device, peripheral",
	"whitelist": "allowlist, exception list, inclusion list",
	"blacklist": "denylist, blocklist, exclusion list",
	"insane":    "unexpected, catastrophic, incoherent",
	"sane":      "expected, appropriate, sensible, valid",
	"crazy":     "unexpected, catastrophic, incoherent",
	"redline":   "priority line, limit, soft limit",
}

func (rule *respectfulCode) OnNext(tok core.Token) {
	if tok.Kind == core.Text {
		match := matchAvoidWords.FindStringSubmatch(tok.Content)

		if match != nil && len(match) > 1 {
			match_lower := strings.ToLower(match[1])
			alternatives := avoidWords[match_lower]
			rule.reporter.Warnf(tok, "avoid using the term \"%s\", consider %s", match_lower, alternatives)
		}
	}
}
