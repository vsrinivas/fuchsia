// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package main

import (
	"flag"
	"fmt"
	"io"
	"os"
	"path"
	"path/filepath"
	"regexp"
	"sort"
	"strings"
)

const noExtraSpaceOnRightName = "no-extra-space-on-right"

type noExtraSpaceOnRight struct {
	defaultLintRuleOverTokens
	reporter Reporter

	lastTok token
}

var _ lintRuleOverTokens = (*noExtraSpaceOnRight)(nil)

func (rule *noExtraSpaceOnRight) onNext(tok token) {
	if rule.lastTok.kind == tSpace && tok.kind == tNewline {
		rule.reporter.Warnf(rule.lastTok, "extra whitespace")
	}
	rule.lastTok = tok
}

const casingOfAnchorsName = "casing-of-anchors"

type casingOfAnchors struct {
	defaultLintRuleOverTokens
	reporter Reporter
}

var _ lintRuleOverTokens = (*casingOfAnchors)(nil)

var casingOfAnchorsRe = regexp.MustCompile("{#[a-zA-Z0-9]+([a-zA-Z0-9-]*[a-zA-Z0-9])?}")

func (rule *casingOfAnchors) onNext(tok token) {
	if tok.kind == tAnchor {
		if !casingOfAnchorsRe.MatchString(tok.content) {
			rule.reporter.Warnf(tok, "poorly formated anchor")
		}
	}
}

const verifyInternalLinksName = "verify-internal-links"

type verifyInternalLinks struct {
	defaultLintRuleOverEvents

	reporter Reporter

	byXref []token
	xrefs  map[string]string
}

var _ lintRuleOverEvents = (*verifyInternalLinks)(nil)

func (rule *verifyInternalLinks) onDocStart(doc *doc) {
	rule.byXref = nil
	rule.xrefs = make(map[string]string)
}

func (rule *verifyInternalLinks) onLinkByXref(xref token) {
	rule.byXref = append(rule.byXref, xref)
}

func (rule *verifyInternalLinks) onXrefDefinition(xref, url token) {
	// TODO(fxbug.dev/62964): internal or external? if external, need to check presence
	normalized := normalizeLinkLabel(xref.content)
	if _, ok := rule.xrefs[normalized]; ok {
		rule.reporter.Warnf(xref, "internal reference defined multiple times")
	} else {
		rule.xrefs[normalized] = url.content
	}
}

func (rule *verifyInternalLinks) onDocEnd() {
	for _, xref := range rule.byXref {
		if _, ok := rule.xrefs[normalizeLinkLabel(xref.content)]; !ok {
			rule.reporter.Warnf(xref, "internal reference not defined")
		}
	}
	rule.byXref = nil
	rule.xrefs = nil
}

const badListsName = "bad-lists"

type badLists struct {
	defaultLintRuleOverTokens
	reporter Reporter

	buf    [2]token
	i      int
	inList bool
}

var _ lintRuleOverTokens = (*badLists)(nil)

func (rule *badLists) onDocStart(_ *doc) {
	var defaultTok token
	rule.buf[0] = defaultTok
	rule.buf[1] = defaultTok
	rule.inList = false
}

func (rule *badLists) onNext(tok token) {
	switch tok.kind {
	case tSpace:
		return // ignore
	case tList:
		if !rule.inList && !(rule.buf[0].kind == tNewline && rule.buf[1].kind == tNewline) {
			rule.reporter.Warnf(tok, "must have extra new line before a list")
			var defaultTok token
			rule.buf[0] = defaultTok
			rule.buf[1] = defaultTok
		}
		rule.inList = true
	case tNewline:
		if rule.inList && rule.buf[rule.i].kind == tNewline {
			rule.inList = false
		}
	}
	rule.buf[rule.i] = tok
	rule.i = (rule.i + 1) % 2
}

func processOnDoc(filename string, stream io.Reader, rule lintRuleOverTokens) error {
	doc := newDoc(filename, stream)
	rule.onDocStart(doc)
	defer rule.onDocEnd()

	tokenizer := newTokenizer(doc)
	for {
		tok, err := tokenizer.next()
		if err != nil {
			return err
		}
		rule.onNext(tok)
		if tok.kind == tEOF {
			return nil
		}
	}
}

var allRules = map[string]func(rootReporter *RootReporter) (lintRuleOverTokens, lintRuleOverEvents){
	noExtraSpaceOnRightName: func(rootReporter *RootReporter) (lintRuleOverTokens, lintRuleOverEvents) {
		return &noExtraSpaceOnRight{reporter: rootReporter.ForRule(noExtraSpaceOnRightName)}, nil
	},
	casingOfAnchorsName: func(rootReporter *RootReporter) (lintRuleOverTokens, lintRuleOverEvents) {
		return &casingOfAnchors{reporter: rootReporter.ForRule(casingOfAnchorsName)}, nil
	},
	badListsName: func(rootReporter *RootReporter) (lintRuleOverTokens, lintRuleOverEvents) {
		return &badLists{reporter: rootReporter.ForRule(badListsName)}, nil
	},
	verifyInternalLinksName: func(rootReporter *RootReporter) (lintRuleOverTokens, lintRuleOverEvents) {
		return nil, &verifyInternalLinks{reporter: rootReporter.ForRule(verifyInternalLinksName)}
	},
}

func newLintRule(rootReporter *RootReporter) lintRuleOverTokens {
	var (
		rulesOverTokens []lintRuleOverTokens
		rulesOverEvents []lintRuleOverEvents
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

	// combining all rules into a single rule over tokens
	//
	// - oneToManyOverTokens rule over
	//   - all rules over tokens
	//   - a recognizer bridging to a
	//     - oneToManyOverEvents rule over
	//       - all rules over events
	rulesOverTokens = append(rulesOverTokens, &recognizer{rule: oneToManyOverEvents(rulesOverEvents)})
	return oneToManyOverTokens(rulesOverTokens)
}

type dirFlag string

func (flag *dirFlag) String() string {
	return "dir"
}

func (flag *dirFlag) Set(dir string) error {
	dirFs, err := os.Stat(dir)
	if err != nil {
		return err
	}
	if !dirFs.Mode().IsDir() {
		return fmt.Errorf("%s: not a directory", dir)
	}
	*flag = dirFlag(dir)
	return nil
}

type enabledRulesFlag []string

func (flag *enabledRulesFlag) String() string {
	return "name(s)"
}

func (flag *enabledRulesFlag) Set(name string) error {
	if _, ok := allRules[name]; !ok {
		return fmt.Errorf("unknown rule")
	}
	*flag = append(*flag, name)
	return nil
}

var (
	rootDir      dirFlag
	jsonOutput   bool
	enabledRules enabledRulesFlag
)

func init() {
	flag.Var(&rootDir, "root-dir", "Path to root directory containing Mardown files.")
	flag.BoolVar(&jsonOutput, "json", false, "Enable JSON output")

	var names []string
	for name := range allRules {
		names = append(names, fmt.Sprintf("'%s'", name))
	}
	sort.Strings(names)
	flag.Var(&enabledRules, "enable", fmt.Sprintf("Enable a rule. Valid rules are %s", strings.Join(names, ", ")))
}

func printUsage() {
	program := path.Base(os.Args[0])
	message := `Usage: ` + program + ` [flags]

Markdown linter.

Flags:
`
	fmt.Fprint(flag.CommandLine.Output(), message)
	flag.PrintDefaults()
}

const (
	exitOnSuccess = 0
	exitOnError   = 1
)

func main() {
	flag.Usage = printUsage
	flag.Parse()
	if !flag.Parsed() {
		printUsage()
		os.Exit(exitOnError)
	}

	reporter := RootReporter{
		JSONOutput: jsonOutput,
	}
	rules := newLintRule(&reporter)
	filenames, err := filepath.Glob(filepath.Join(string(rootDir), "*/*/*.md"))
	if err != nil {
		fmt.Fprintf(os.Stderr, "%s\n", err)
		os.Exit(exitOnError)
	}
	for _, filename := range filenames {
		if err := func() error {
			file, err := os.Open(filename)
			if err != nil {
				return err
			}
			defer file.Close()
			return processOnDoc(filename, file, rules)
		}(); err != nil {
			fmt.Fprintf(os.Stderr, "%s\n", err)
			os.Exit(exitOnError)
		}
	}

	if reporter.HasMessages() {
		reporter.Print(os.Stderr)
		os.Exit(exitOnError)
	}

	os.Exit(exitOnSuccess)
}
