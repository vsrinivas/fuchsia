// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package symbolize

import (
	"fmt"
	"regexp"
	"strings"
)

// TODO: Make this whole file private

type actionFunc func(...string)

type regexInfo struct {
	regex      *regexp.Regexp // the regex for this rule
	groupCount int            // the number of groups in this regex
	index      int            // the group index of this regex in the master regex
	action     actionFunc     // the action to execute if this rule succeeds
}

// RegexpTokenizer allows for the splitting of input into tokens based on a list
// of regexs a la (f)lex.
type regexpTokenizer struct {
	regexs        []regexInfo
	master        *regexp.Regexp
	defaultAction func(string)
}

type rule struct {
	regexStr string
	action   actionFunc
}

// RegexpTokenizerBuilder is the means by which a RegexpTokenizer can be constructed.
type regexpTokenizerBuilder struct {
	rules []rule
}

// TODO: Add a way to infer the automatic conversions that need to happen from
// a user supplied function's type via reflection.
// Rule adds a new regex to the builder
func (r *regexpTokenizerBuilder) addRule(regex string, action actionFunc) {
	r.rules = append(r.rules, rule{regex, action})
}

// End compiles the list of regular expressions and actions into a RegexpTokenizer
func (r *regexpTokenizerBuilder) compile(defaultAction func(string)) (*regexpTokenizer, error) {
	out := regexpTokenizer{defaultAction: defaultAction}
	// Start groupIndex at 1 to account for the master regexp
	groupIndex := 1
	regexStrs := []string{}
	for _, rule := range r.rules {
		regex, err := regexp.Compile(rule.regexStr)
		if err != nil {
			return nil, err
		}
		// Add all needed information to an regexInfo for this rule.
		toAdd := regexInfo{regex, len(regex.SubexpNames()), groupIndex, rule.action}
		// Advance the groupIndex by the subgroups of this regex plus the additional group we add for the whole thing.
		groupIndex += toAdd.groupCount
		regexStrs = append(regexStrs, fmt.Sprintf("(%s)", rule.regexStr))
		out.regexs = append(out.regexs, toAdd)
	}
	// Create the master regex
	masterRegexp, err := regexp.Compile(strings.Join(regexStrs, "|"))
	if err != nil {
		return nil, err
	}
	out.master = masterRegexp
	return &out, nil
}

// Run tokenizes 'input'
func (r *regexpTokenizer) run(input string) {
	for len(input) > 0 {
		locs := r.master.FindStringSubmatchIndex(input)
		if locs == nil {
			// There are no more matches so parse the rest of the input and return
			r.defaultAction(input)
			return
		}
		// If there is anything before the match we need to pass it to the default case.
		if locs[0] != 0 {
			r.defaultAction(input[:locs[0]])
		}
		// If we have a match however find which regex produced the match
		for _, regex := range r.regexs {
			if locs[2*regex.index] >= 0 {
				groups := []string{}
				for i := 0; i < regex.groupCount; i++ {
					groupBeginIdx := locs[2*(regex.index+i)]
					groupEndIdx := locs[2*(regex.index+i)+1]
					groups = append(groups, input[groupBeginIdx:groupEndIdx])
				}
				// Pass the regex's groups to it
				regex.action(groups...)
				break
			}
		}
		// Now we need to advance the input to not cover anything in the match.
		input = input[locs[1]:]
	}
}
