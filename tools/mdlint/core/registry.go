// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package core

import (
	"fmt"
)

// allRules is evil global mutable state, but we're being super careful about
// how it is used. It can be mutated only through registration, all other uses
// are read only. We do not rely on the registry in tests.
var allRules = make(map[string]func(rootReporter *RootReporter) (LintRuleOverTokens, LintRuleOverPatterns))

// HasRule tests whether a given rule is registered.
func HasRule(name string) bool {
	_, ok := allRules[name]
	return ok
}

// AllRules returns the names of all the registered rules.
func AllRules() []string {
	var names []string
	for name := range allRules {
		names = append(names, name)
	}
	return names
}

// CombineRules combines rules over tokens and rules over patterns into a single
// rule over tokens.
//
// To accomplish this combination, dedicated one-to-many rules bridge between
// the returned rule and the provided rules. Additionally, the token stream is
// recognized and turned into a pattern stream so as to drive all rules over
// patterns.
func CombineRules(rulesOverTokens []LintRuleOverTokens, rulesOverPatterns []LintRuleOverPatterns) LintRuleOverTokens {
	// combining all rules into a single rule over tokens
	//
	// - oneToManyOverTokens rule over
	//   - all rules over tokens
	//   - a recognizer bridging to a
	//     - oneToManyOverPatterns rule over
	//       - all rules over patterns
	return oneToManyOverTokens(append(
		rulesOverTokens,
		&recognizer{
			rule: oneToManyOverPatterns(
				rulesOverPatterns),
		}))
}

// InstantiateRules instantiates all `enabledRules`.
func InstantiateRules(rootReporter *RootReporter, enabledRules []string) LintRuleOverTokens {
	var (
		rulesOverTokens   []LintRuleOverTokens
		rulesOverPatterns []LintRuleOverPatterns
	)
	if len(enabledRules) == 1 && enabledRules[0] == AllRulesName {
		enabledRules = nil
		for name := range allRules {
			enabledRules = append(enabledRules, name)
		}
	}
	for _, name := range enabledRules {
		instantiator, ok := allRules[name]
		if !ok {
			panic(fmt.Sprintf("unknown rule '%s', should not happen", name))
		}
		overTokens, overPatterns := instantiator(rootReporter)
		if overTokens != nil {
			rulesOverTokens = append(rulesOverTokens, overTokens)
		}
		if overPatterns != nil {
			rulesOverPatterns = append(rulesOverPatterns, overPatterns)
		}
	}
	return CombineRules(rulesOverTokens, rulesOverPatterns)
}

// RegisterLintRuleOverTokens registers a lint rule over tokens. This is meant
// to be called from an `init` block.
func RegisterLintRuleOverTokens(name string, instantiator func(Reporter) LintRuleOverTokens) {
	checkRegisteredName(name)
	allRules[name] = func(rootReporter *RootReporter) (LintRuleOverTokens, LintRuleOverPatterns) {
		return instantiator(rootReporter.ForRule(name)), nil
	}
}

// RegisterLintRuleOverPatterns registers a lint rule over patterns. This is
// meant to be called from an `init` block.
func RegisterLintRuleOverPatterns(name string, instantiator func(Reporter) LintRuleOverPatterns) {
	checkRegisteredName(name)
	allRules[name] = func(rootReporter *RootReporter) (LintRuleOverTokens, LintRuleOverPatterns) {
		return nil, instantiator(rootReporter.ForRule(name))
	}
}

// generalName is the category name reserved for warnings not issued by specific
// rules, but rather 'general' warnings.
const generalName = "general"

// AllRulesName is a reserved rule name used to indicate "all" rules.
const AllRulesName = "all"

func checkRegisteredName(name string) {
	if name == generalName || name == AllRulesName {
		panic(fmt.Sprintf("rule name '%s' is reserved", generalName))
	}
	if _, ok := allRules[name]; ok {
		panic(fmt.Sprintf("rule name '%s' is being registered more than once", name))
	}
}
