// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package testsharder

import (
	"container/heap"
	"encoding/json"
	"fmt"
	"math"
	"os"
	"path/filepath"
	"regexp"
	"sort"
	"strings"
	"time"
)

const (
	// The maximum number of runs that testsharder will calculate for a multiplied
	// test if totalRuns is unset.
	// TODO(olivernewman): Apply a maximum to user-specified values too, but
	// probably not in testsharder since we'll want to get feedback to users ASAP if
	// validation fails.
	multipliedTestMaxRuns = 2000

	// The maximum number of tests that a multiplier can match. testsharder will
	// fail if this is exceeded.
	maxMatchesPerMultiplier = 5

	// The prefix added to the names of shards that run affected tests.
	affectedShardPrefix = "affected:"

	// The prefix added to the names of shards that run multiplied tests.
	multipliedShardPrefix = "multiplied:"

	// When multiplying a test to fill a shard, multiply the target duration by this.
	multiplyShardTargetDurationFactor = 0.75
)

// MultiplyShards will return an error that unwraps to this if a multiplier's
// "name" field does not compile to a valid regex.
var errInvalidMultiplierRegex = fmt.Errorf("invalid multiplier regex")

// MultiplyShards will return an error that unwraps to this if a multiplier
// matches too many tests.
var errTooManyMultiplierMatches = fmt.Errorf("a multiplier cannot match more than %d tests", maxMatchesPerMultiplier)

func ExtractDeps(shards []*Shard, fuchsiaBuildDir string) error {
	for _, shard := range shards {
		if err := extractDepsFromShard(shard, fuchsiaBuildDir); err != nil {
			return err
		}
	}
	return nil
}

func extractDepsFromShard(shard *Shard, fuchsiaBuildDir string) error {
	var shardDeps []string
	for i := range shard.Tests {
		test, deps, err := extractDepsFromTest(shard.Tests[i], fuchsiaBuildDir)
		if err != nil {
			return err
		}
		// extractDepsFromTest may modify the test, so we need to overwrite the
		// entry.
		shard.Tests[i] = test
		// Any test that doesn't run on Fuchsia is invoked via an executable in
		// the build out directory. The executable itself needs to be copied to
		// the testing bot along with the test's deps.
		if test.OS != "fuchsia" && test.Path != "" {
			deps = append(deps, test.Path)
		}
		shardDeps = append(shardDeps, deps...)
	}
	shardDeps = dedupe(shardDeps)
	sort.Strings(shardDeps)
	shard.Deps = shardDeps
	return nil
}

func extractDepsFromTest(test Test, fuchsiaBuildDir string) (Test, []string, error) {
	if test.RuntimeDepsFile == "" {
		return test, nil, nil
	}
	path := filepath.Join(fuchsiaBuildDir, test.RuntimeDepsFile)
	f, err := os.Open(path)
	if err != nil {
		return test, nil, err
	}
	defer f.Close()
	// RuntimeDepsFile is no longer needed at this point and clutters the output.
	test.RuntimeDepsFile = ""
	var deps []string
	err = json.NewDecoder(f).Decode(&deps)
	return test, deps, err
}

func dedupe(l []string) []string {
	var deduped []string
	m := make(map[string]struct{})
	for _, s := range l {
		m[s] = struct{}{}
	}
	for s := range m {
		deduped = append(deduped, s)
	}
	return deduped
}

// MultiplyShards appends new shards to shards where each new shard contains one test
// repeated multiple times according to the specifications in multipliers.
func MultiplyShards(
	shards []*Shard,
	multipliers []TestModifier,
	testDurations TestDurationsMap,
	// TODO(olivernewman): Use the adjusted target duration calculated by
	// WithTargetDuration instead of the original target duration.
	targetDuration time.Duration,
	targetTestCount int,
) ([]*Shard, error) {
	for _, multiplier := range multipliers {
		if multiplier.TotalRuns < 0 {
			continue
		}
		type multiplierMatch struct {
			shard *Shard
			test  Test
		}
		var exactMatches []*multiplierMatch
		var regexMatches []*multiplierMatch

		nameRegex, err := regexp.Compile(multiplier.Name)
		if err != nil {
			return nil, fmt.Errorf("%w %q: %s", errInvalidMultiplierRegex, multiplier.Name, err)
		}

		safeTargetDuration := float64(targetDuration) * multiplyShardTargetDurationFactor
		for _, shard := range shards {
			for _, test := range shard.Tests {
				// An empty OS matches all OSes.
				if multiplier.OS != "" && multiplier.OS != test.OS {
					continue
				}

				match := &multiplierMatch{shard: shard, test: test}
				if multiplier.Name == test.Name {
					exactMatches = append(exactMatches, match)
				} else if nameRegex.FindString(test.Name) != "" {
					regexMatches = append(regexMatches, match)
				} else {
					continue
				}

				if multiplier.TotalRuns > 0 {
					match.test.Runs = multiplier.TotalRuns
				} else if targetDuration > 0 {
					// We both cap the number of runs and apply a safety factor because
					// we want to keep the total runs to a reasonable number
					// in case the test takes longer than expected. We're conservative because
					// if a shard exceeds its timeout, it's really painful for users.
					expectedDuration := testDurations.Get(test).MedianDuration
					match.test.Runs = min(
						int(safeTargetDuration/float64(expectedDuration)),
						multipliedTestMaxRuns,
					)
				} else if targetTestCount > 0 {
					match.test.Runs = targetTestCount
				} else {
					match.test.Runs = 1
				}
				match.test.RunAlgorithm = KeepGoing
			}
		}

		// We'll only consider partial regex matches if we have no exact
		// matches.
		matches := exactMatches
		if len(matches) == 0 {
			matches = regexMatches
		}

		if len(matches) > maxMatchesPerMultiplier {
			return nil, fmt.Errorf(
				"multiplier %q matches too many tests (%d): %w",
				multiplier.Name, len(matches), errTooManyMultiplierMatches,
			)
		}

		for _, m := range matches {
			// If a test is multiplied, it doesn't matter if it was originally
			// in an affected shard or not and it's confusing for it to have
			// two prefixes. So don't include the "affected" prefix.
			shardName := strings.TrimPrefix(m.shard.Name, affectedShardPrefix)
			shards = append(shards, &Shard{
				Name:  multipliedShardPrefix + shardName + "-" + normalizeTestName(m.test.Name),
				Tests: []Test{m.test},
				Env:   m.shard.Env,
			})
		}
	}

	return shards, nil
}

// ShardAffected separates the affected tests into separate shards.
func ShardAffected(shards []*Shard, modTests []TestModifier) ([]*Shard, error) {
	var newShards []*Shard
	defaultModTest := TestModifier{}
	foundDefault := false
	for _, modTest := range modTests {
		if modTest.Name == "*" {
			if foundDefault {
				return nil, fmt.Errorf("too many default modifiers, only one is allowed")
			}
			defaultModTest = modTest
			foundDefault = true
		}
	}
	for _, shard := range shards {
		var affected []Test
		var unaffected []Test
		for _, test := range shard.Tests {
			isAffected := false
			test.applyModifier(defaultModTest)
			for _, modTest := range modTests {
				if modTest.Name != test.Name {
					continue
				}
				test.applyModifier(modTest)

				if modTest.Affected {
					isAffected = true
					affected = append(affected, test)
				}
				break
			}
			if !isAffected {
				unaffected = append(unaffected, test)
			}
		}
		if len(affected) > 0 {
			newShards = append(newShards, &Shard{
				Name:  affectedShardPrefix + shard.Name,
				Tests: affected,
				Env:   shard.Env,
			})
		}
		if len(unaffected) > 0 {
			shard.Tests = unaffected
			newShards = append(newShards, shard)
		}
	}
	return newShards, nil
}

func min(a, b int) int {
	if a < b {
		return a
	}
	return b
}

func divRoundUp(a, b int) int {
	if a%b == 0 {
		return a / b
	}
	return (a / b) + 1
}

// WithTargetDuration returns a list of shards such that all shards are expected
// to complete in approximately `targetDuration` time.
// If targetDuration <= 0, just returns its input.
// Alternatively, accepts a `targetTestCount` argument for backwards compatibility.
func WithTargetDuration(
	shards []*Shard,
	targetDuration time.Duration,
	targetTestCount,
	maxShardsPerEnvironment int,
	testDurations TestDurationsMap,
) []*Shard {
	if targetDuration <= 0 && targetTestCount <= 0 {
		return shards
	}
	if maxShardsPerEnvironment <= 0 {
		maxShardsPerEnvironment = math.MaxInt64
	}

	if targetDuration > 0 {
		for _, shard := range shards {
			var shardDuration time.Duration
			// If any single test is expected to take longer than `targetDuration`,
			// it's no use creating shards whose entire expected runtimes are
			// shorter than that one test. So in that case we use the longest test's
			// expected duration as the target duration.
			for _, t := range shard.Tests {
				duration := testDurations.Get(t).MedianDuration
				if duration > targetDuration {
					targetDuration = duration
				}
				shardDuration += duration * time.Duration(t.minRequiredRuns())
			}
			// If any environment would exceed the maximum shard count, then its
			// shard durations will exceed the specified target duration. So
			// increase the target duration accordingly for the other
			// environments.
			subShardCount := divRoundUp(int(shardDuration), int(targetDuration))
			if subShardCount > maxShardsPerEnvironment {
				targetDuration = time.Duration(divRoundUp(int(shardDuration), maxShardsPerEnvironment))
			}
		}
	}

	output := make([]*Shard, 0, len(shards))
	for _, shard := range shards {
		numNewShards := 0
		if targetDuration > 0 {
			var total time.Duration
			for _, t := range shard.Tests {
				total += testDurations.Get(t).MedianDuration * time.Duration(t.minRequiredRuns())
			}
			numNewShards = divRoundUp(int(total), int(targetDuration))
		} else {
			var total int
			for _, t := range shard.Tests {
				total += t.minRequiredRuns()
			}
			numNewShards = divRoundUp(total, targetTestCount)
		}
		if numNewShards == 0 {
			// If targetDuration is set but all durations are zero, we'll
			// determine that we need zero new shards. In this case, we'll be
			// careful and assume that we need the maximum allowed shards to be
			// able to fit all tests or one shard per test if the number of tests
			// is less than the maximum allowed shards.
			numNewShards = min(len(shard.Tests), maxShardsPerEnvironment)
		}
		numNewShards = min(numNewShards, maxShardsPerEnvironment)

		newShards := shardByTime(shard, testDurations, numNewShards)
		output = append(output, newShards...)
	}
	return output
}

type subshard struct {
	duration time.Duration
	tests    []Test
}

// A subshardHeap is a min heap of subshards, using subshard duration as the key
// to sort by.
// It implements heap.Interface.
type subshardHeap []subshard

func (h subshardHeap) Len() int {
	return len(h)
}

func (h subshardHeap) Less(i, j int) bool {
	if h[i].duration != h[j].duration {
		return h[i].duration < h[j].duration
	}
	// All durations being equal, fall back to comparing test counts. This
	// ensures that even if all expected durations are zero (which generally
	// shouldn't happen, but is possible), we'll still divide tests evenly by
	// test count across shards.
	return len(h[i].tests) < len(h[j].tests)
}

func (h subshardHeap) Swap(i, j int) {
	h[i], h[j] = h[j], h[i]
}

func (h *subshardHeap) Push(s interface{}) {
	*h = append(*h, s.(subshard))
}

func (h *subshardHeap) Pop() interface{} {
	old := *h
	n := len(old)
	s := old[n-1]
	*h = old[0 : n-1]
	return s
}

// shardByTime breaks a single original shard into numNewShards subshards such
// that each subshard has approximately the same expected total duration.
//
// It does this using a greedy approximation algorithm for static multiprocessor
// scheduling (https://en.wikipedia.org/wiki/Multiprocessor_scheduling). It
// first sorts the tests in descending order by expected duration and then
// successively allocates each test to the subshard with the lowest total
// expected duration so far.
func shardByTime(shard *Shard, testDurations TestDurationsMap, numNewShards int) []*Shard {
	sort.Slice(shard.Tests, func(index1, index2 int) bool {
		test1, test2 := shard.Tests[index1], shard.Tests[index2]
		duration1 := testDurations.Get(test1).MedianDuration
		duration2 := testDurations.Get(test2).MedianDuration
		if duration1 == duration2 {
			// Sort by name for tests of equal duration to ensure deterministic
			// ordering.
			return test1.Name < test2.Name
		}
		// "greater than" instead of "less than" to achieve descending ordering
		return duration1 > duration2
	})

	h := (subshardHeap)(make([]subshard, numNewShards))

	for _, test := range shard.Tests {
		runsPerShard := divRoundUp(test.minRequiredRuns(), numNewShards)
		extra := runsPerShard*numNewShards - test.minRequiredRuns()
		for i := 0; i < numNewShards; i++ {
			testCopy := test
			var runs int
			if i < numNewShards-extra {
				runs = runsPerShard
			} else {
				runs = runsPerShard - 1
			}
			if runs == 0 {
				break
			}
			// Only if the test must be run more than once should we split it
			// across multiple shards.
			if test.minRequiredRuns() > 1 {
				testCopy.Runs = runs
			}
			// Assign this test to the subshard with the lowest total expected
			// duration at this iteration of the for loop.
			ss := heap.Pop(&h).(subshard)
			ss.duration += testDurations.Get(test).MedianDuration * time.Duration(testCopy.minRequiredRuns())
			ss.tests = append(ss.tests, testCopy)
			heap.Push(&h, ss)
		}
	}

	// Sort the resulting shards by the basename of the first test. Otherwise,
	// changes to the input set of tests (adding, removing or renaming a test)
	// result in confusing reordering of the shard names. This ensures that a
	// given named shard (e.g. "QEMU-(1)") will contain roughly the same set of
	// longer-running tests across multiple builds, even if the input set of
	// tests changes. Shorter tests are more likely to be switched between
	// shards because we're sorting by the name of the longest test.
	sort.Slice(h, func(i, j int) bool {
		return h[i].tests[0].Name < h[j].tests[0].Name
	})

	newShards := make([]*Shard, 0, numNewShards)
	for i, subshard := range h {
		name := shard.Name
		if numNewShards > 1 {
			name = fmt.Sprintf("%s-(%d)", shard.Name, i+1)
		}
		newShards = append(newShards, &Shard{
			Name:  name,
			Tests: subshard.tests,
			Env:   shard.Env,
		})
	}
	return newShards
}

// Removes leading slashes and replaces all other `/` with `_`. This allows the
// shard name to appear in filepaths.
func normalizeTestName(name string) string {
	trimmedName := strings.TrimLeft(name, "/")
	return strings.ReplaceAll(trimmedName, "/", "_")
}
