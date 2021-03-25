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
var allRules = make(map[string]func(rootReporter *RootReporter) (LintRuleOverTokens, LintRuleOverEvents))

func HasRule(name string) bool {
	_, ok := allRules[name]
	return ok
}

func AllRuleNames() []string {
	var names []string
	for name := range allRules {
		names = append(names, name)
	}
	return names
}

// CombineRules combines rules over tokens and rules over events into a single
// rule over tokens.
//
// To accomplish this combination, dedicated one-to-many rules bridge between
// the returned rule and the provided rules. Additionally, the token stream is
// recognized and turned into an event stream so as to drive all rules over
// events.
func CombineRules(rulesOverTokens []LintRuleOverTokens, rulesOverEvents []LintRuleOverEvents) LintRuleOverTokens {
	// combining all rules into a single rule over tokens
	//
	// - oneToManyOverTokens rule over
	//   - all rules over tokens
	//   - a recognizer bridging to a
	//     - oneToManyOverEvents rule over
	//       - all rules over events
	return oneToManyOverTokens(append(
		rulesOverTokens,
		&recognizer{
			rule: oneToManyOverEvents(
				rulesOverEvents),
		}))
}

// InstantiateRules instantiates all `enabledRules`.
func InstantiateRules(rootReporter *RootReporter, enabledRules []string) LintRuleOverTokens {
	var (
		rulesOverTokens []LintRuleOverTokens
		rulesOverEvents []LintRuleOverEvents
	)
	for _, name := range enabledRules {
		instantiator, ok := allRules[name]
		if !ok {
			panic(fmt.Sprintf("unknown rule '%s', should not happen", name))
		}
		overTokens, overEvents := instantiator(rootReporter)
		if overTokens != nil {
			rulesOverTokens = append(rulesOverTokens, overTokens)
		}
		if overEvents != nil {
			rulesOverEvents = append(rulesOverEvents, overEvents)
		}
	}
	return CombineRules(rulesOverTokens, rulesOverEvents)
}

// RegisterLintRuleOverTokens registers a lint rule over tokens. This meant to
// be called from an `init` block.
func RegisterLintRuleOverTokens(name string, instantiator func(Reporter) LintRuleOverTokens) {
	checkRegisteredName(name)
	allRules[name] = func(rootReporter *RootReporter) (LintRuleOverTokens, LintRuleOverEvents) {
		return instantiator(rootReporter.ForRule(name)), nil
	}
}

// RegisterLintRuleOverEvents registers a lint rule over events. This meant to
// be called from an `init` block.
func RegisterLintRuleOverEvents(name string, instantiator func(Reporter) LintRuleOverEvents) {
	checkRegisteredName(name)
	allRules[name] = func(rootReporter *RootReporter) (LintRuleOverTokens, LintRuleOverEvents) {
		return nil, instantiator(rootReporter.ForRule(name))
	}
}

const generalName = "general"

func checkRegisteredName(name string) {
	if name == generalName {
		panic(fmt.Sprintf("rule name '%s' is reserved", generalName))
	}
	if _, ok := allRules[name]; ok {
		panic(fmt.Sprintf("rule name '%s' is being registered more than once", name))
	}
}
