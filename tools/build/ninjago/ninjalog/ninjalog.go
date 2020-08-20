// Copyright 2014 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package ninjalog

import (
	"bufio"
	"container/heap"
	"encoding/binary"
	"encoding/json"
	"fmt"
	"io"
	"path"
	"sort"
	"strconv"
	"strings"
	"time"

	"go.fuchsia.dev/fuchsia/tools/build/ninjago/compdb"
)

// Step is one step in ninja_log file.
// time is measured from ninja start time.
type Step struct {
	Start time.Duration
	End   time.Duration
	// modification time, but not convertable to absolute real time.
	// on POSIX, time_t is used, but on Windows different type is used.
	// htts://github.com/martine/ninja/blob/master/src/timestamp.h
	Restat  int
	Out     string
	CmdHash uint64

	// other outs for the same CmdHash if dedup'ed.
	Outs []string
	// compilation database command
	Command *compdb.Command
}

// Duration reports step's duration.
func (s Step) Duration() time.Duration {
	return s.End - s.Start
}

var toSkip = map[string]bool{
	"bin": true,
	"env": true,
	// This is a binary wrapper for invoking host binaries produced by the build.
	"gn_run_binary.sh": true,
}

// category returns a comma separate list of executed commands.
func (s Step) Category() string {
	if s.Command == nil {
		return "unknown"
	}
	var categories []string
	// TODO(jayzhuang): use github.com/google/shlex instead.
	for _, command := range strings.Split(strings.Replace(s.Command.Command, " || ", " && ", -1), " && ") {
		var cmd string
		for _, token := range strings.Split(strings.TrimSpace(command), " ") {
			cmd = strings.Trim(path.Base(token), "()")
			if !toSkip[cmd] {
				break
			}
		}
		if cmd != "" {
			categories = append(categories, cmd)
		}
	}
	return strings.Join(categories, ",")
}

// Steps is a list of Step.
// It could be used to sort by start time.
type Steps []Step

func (s Steps) Len() int      { return len(s) }
func (s Steps) Swap(i, j int) { s[i], s[j] = s[j], s[i] }
func (s Steps) Less(i, j int) bool {
	if s[i].Start != s[j].Start {
		return s[i].Start < s[j].Start
	}
	if s[i].End != s[j].End {
		return s[i].End < s[j].End
	}
	return s[i].Out < s[j].Out
}
func (s *Steps) Push(x interface{}) { *s = append(*s, x.(Step)) }
func (s *Steps) Pop() interface{} {
	l := len(*s)
	if l == 0 {
		return Step{}
	}
	step := (*s)[l-1]
	*s = (*s)[:l-1]
	return step
}

// Reverse reverses steps.
// It would be more efficient if steps is already sorted than using sort.Reverse.
func (s Steps) Reverse() {
	for i, j := 0, len(s)-1; i < j; i, j = i+1, j-1 {
		s[i], s[j] = s[j], s[i]
	}
}

// ByEnd is used to sort by end time.
type ByEnd struct{ Steps }

func (s ByEnd) Less(i, j int) bool { return s.Steps[i].End < s.Steps[j].End }

// ByDuration is used to sort by duration.
type ByDuration struct{ Steps }

func (s ByDuration) Less(i, j int) bool { return s.Steps[i].Duration() < s.Steps[j].Duration() }

// ByWeightedTime is used to sort by weighted time.
type ByWeightedTime struct {
	Weighted map[string]time.Duration
	Steps
}

func (s ByWeightedTime) Less(i, j int) bool {
	return s.Weighted[s.Steps[i].Out] < s.Weighted[s.Steps[j].Out]
}

// Metadata is data added by compile.py.
type Metadata struct {
	// Platform is platform of buildbot.
	Platform string `json:"platform"`

	// Argv is argv of compile.py
	Argv []string `json:"argv"`

	// Cwd is current working directory of compile.py
	Cwd string `json:"cwd"`

	// Compiler is compiler used.
	Compiler string `json:"compiler"`

	// Cmdline is command line of ninja.
	Cmdline []string `json:"cmdline"`

	// Exit is exit status of ninja.
	Exit int `json:"exit"`

	// Env is environment variables.
	Env map[string]string `json:"env"`

	// CompilerProxyInfo is a path name of associated compiler_proxy.INFO log.
	CompilerProxyInfo string `json:"compiler_proxy_info"`

	// Raw is raw string for metadata.
	Raw string
	// Error is error message of parsing metadata.
	Error string
}

// NinjaLog is parsed data of ninja_log file.
type NinjaLog struct {
	// Filename is a filename of ninja_log.
	Filename string

	// Start is start line of the last build in ninja_log file.
	Start int

	// Steps contains steps in the last build in ninja_log file.
	Steps []Step

	// Metadata is additional data found in ninja_log file.
	Metadata Metadata
}

// Parse parses .ninja_log file, with chromium's compile.py metadata.
func Parse(fname string, r io.Reader) (*NinjaLog, error) {
	b := bufio.NewReader(r)
	scanner := bufio.NewScanner(b)
	nlog := &NinjaLog{Filename: fname}
	lineno := 0
	if !scanner.Scan() {
		if err := scanner.Err(); err != nil {
			return nil, err
		}
		return nil, fmt.Errorf("empty file?")
	}
	lineno++
	line := scanner.Text()
	if line != "# ninja log v5" {
		return nil, fmt.Errorf("unexpected format: %s", line)
	}
	nlog.Start = lineno
	var lastStep Step
	for scanner.Scan() {
		line := scanner.Text()
		if line == "# end of ninja log" {
			break
		}
		if line == "" {
			continue
		}
		step, err := lineToStep(line)
		if err != nil {
			return nil, fmt.Errorf("error at %d: %w", lineno, err)
		}
		if step.End < lastStep.End {
			nlog.Start = lineno
			nlog.Steps = nil
		}
		nlog.Steps = append(nlog.Steps, step)
		lastStep = step
		lineno++
	}
	if err := scanner.Err(); err != nil {
		return nil, fmt.Errorf("error at %d: %w", lineno, err)
	}
	if !scanner.Scan() {
		if err := scanner.Err(); err != nil {
			return nil, fmt.Errorf("error at %d: %w", lineno, err)
		}
		// missing metadata?
		return nlog, nil
	}
	lineno++
	nlog.Metadata.Raw = scanner.Text()
	if err := parseMetadata([]byte(nlog.Metadata.Raw), &nlog.Metadata); err != nil {
		nlog.Metadata.Error = fmt.Sprintf("error at %d: %v", lineno, err)
	}
	return nlog, nil
}

func lineToStep(line string) (Step, error) {
	var step Step

	// Due to slowness of strings.Split in App Engine Go,
	// we use more faster implementation.
	fields := make([]string, 0, 5)
	for i := 0; i < 5; i += 1 {
		m := strings.IndexByte(line, '\t')
		if m < 0 {
			m = len(line)
		}
		fields = append(fields, line[:m])
		if m < len(line) {
			line = line[m+1:]
		}
	}

	if len(fields) < 5 {
		return step, fmt.Errorf("few fields:%d", len(fields))
	}
	s, err := strconv.ParseUint(fields[0], 10, 0)
	if err != nil {
		return step, fmt.Errorf("bad start %s:%w", fields[0], err)
	}
	e, err := strconv.ParseUint(fields[1], 10, 0)
	if err != nil {
		return step, fmt.Errorf("bad end %s:%w", fields[1], err)
	}
	rs, err := strconv.ParseUint(fields[2], 10, 0)
	if err != nil {
		return step, fmt.Errorf("bad restat %s:%w", fields[2], err)
	}
	step.Start = time.Duration(s) * time.Millisecond
	step.End = time.Duration(e) * time.Millisecond
	step.Restat = int(rs)
	step.Out = fields[3]
	ch, err := strconv.ParseUint(fields[4], 16, 64)
	if err != nil {
		return step, fmt.Errorf("bad hash %s:%w", fields[4], err)
	}
	step.CmdHash = ch
	return step, nil
}

func stepToLine(s Step) string {
	return fmt.Sprintf("%d\t%d\t%d\t%s\t%x",
		s.Start.Nanoseconds()/int64(time.Millisecond),
		s.End.Nanoseconds()/int64(time.Millisecond),
		s.Restat,
		s.Out,
		s.CmdHash)
}

func parseMetadata(buf []byte, metadata *Metadata) error {
	return json.Unmarshal(buf, metadata)
}

// Dump dumps steps as ninja log v5 format in w.
func Dump(w io.Writer, steps []Step) error {
	_, err := fmt.Fprintf(w, "# ninja log v5\n")
	if err != nil {
		return err
	}
	for _, s := range steps {
		_, err = fmt.Fprintln(w, stepToLine(s))
		if err != nil {
			return err
		}
	}
	return nil
}

// Dedup dedupes steps. step may have the same cmd hash.
// Dedup only returns the first step for these steps.
// steps will be sorted by start time.
func Dedup(steps []Step) []Step {
	m := make(map[uint64]*Step)
	sort.Sort(Steps(steps))
	var dedup []Step
	for _, s := range steps {
		if os := m[s.CmdHash]; os != nil {
			os.Outs = append(os.Outs, s.Out)
			continue
		}
		dedup = append(dedup, s)
		m[s.CmdHash] = &dedup[len(dedup)-1]
	}
	return dedup
}

// MurmurHash64A computes Murmur hash using the same exact
// algorithm and seed that is used by Ninja in .ninja_log.
// See https://github.com/ninja-build/ninja/blob/cc79afb/src/build_log.cc#L64
func MurmurHash64A(data []byte) uint64 {
	const seed = uint64(0xDECAFBADDECAFBAD)
	const m = uint64(0xc6a4a7935bd1e995)
	const r = 47
	var h = seed ^ (uint64(len(data)) * m)
	for len(data) >= 8 {
		k := binary.LittleEndian.Uint64(data[0:])
		k *= m
		k ^= k >> r
		k *= m
		h ^= k
		h *= m
		data = data[8:]
	}
	switch len(data) & 7 {
	case 7:
		h ^= uint64(data[6]) << 48
		fallthrough
	case 6:
		h ^= uint64(data[5]) << 40
		fallthrough
	case 5:
		h ^= uint64(data[4]) << 32
		fallthrough
	case 4:
		h ^= uint64(data[3]) << 24
		fallthrough
	case 3:
		h ^= uint64(data[2]) << 16
		fallthrough
	case 2:
		h ^= uint64(data[1]) << 8
		fallthrough
	case 1:
		h ^= uint64(data[0])
		h *= m
	}
	h ^= h >> r
	h *= m
	h ^= h >> r
	return h
}

// Populate the steps with information from the compilation database.
func Populate(steps []Step, commands []compdb.Command) []Step {
	m := make(map[uint64]compdb.Command)
	// TODO(phosek): consider computing hashes in parallel
	for _, c := range commands {
		m[MurmurHash64A([]byte(c.Command))] = c
	}
	var populated []Step
	for _, s := range steps {
		if c, ok := m[s.CmdHash]; ok {
			s.Command = &c
		}
		populated = append(populated, s)
	}
	return populated
}

// TotalTime returns startup time and end time of ninja, and accumulated time
// of all tasks.
func TotalTime(steps []Step) (startupTime, endTime, cpuTime time.Duration) {
	if len(steps) == 0 {
		return 0, 0, 0
	}
	steps = Dedup(steps)
	startup := steps[0].Start
	var end time.Duration
	for _, s := range steps {
		if s.Start < startup {
			startup = s.Start
		}
		if s.End > end {
			end = s.End
		}
		cpuTime += s.Duration()
	}
	return startup, end, cpuTime
}

// Flow returns concurrent steps by time.
// steps in every []Step will not have time overlap.
// steps will be sorted by start time.
func Flow(steps []Step) [][]Step {
	sort.Sort(Steps(steps))
	var threads [][]Step

	for _, s := range steps {
		tid := -1
		for i, th := range threads {
			if len(th) == 0 {
				panic(fmt.Errorf("thread %d has no entry", i))
			}
			if th[len(th)-1].End <= s.Start {
				tid = i
				break
			}
		}
		if tid == -1 {
			threads = append(threads, nil)
			tid = len(threads) - 1
		}
		threads[tid] = append(threads[tid], s)
	}
	return threads
}

// action represents an event's action. "start" or "end".
type action string

const (
	unknownAction action = ""
	startAction   action = "start"
	stopAction    action = "stop"
)

// event is an event of steps.
type event struct {
	time   time.Duration
	action action
	target string
}

// toEvent converts steps into events.
// events are sorted by its time.
func toEvent(steps []Step) []event {
	var events []event
	for _, s := range steps {
		events = append(events,
			event{
				time:   s.Start,
				action: startAction,
				target: s.Out,
			},
			event{
				time:   s.End,
				action: stopAction,
				target: s.Out,
			},
		)
	}
	sort.Slice(events, func(i, j int) bool {
		if events[i].time == events[j].time {
			// If a task starts and stops on the same time stamp
			// then the start will come first.
			return events[i].action < events[j].action
		}
		return events[i].time < events[j].time
	})
	return events
}

// WeightedTime calculates weighted time, which is elapsed time with
// each segment divided by the number of tasks that were running in paralle.
// This makes it a much better approximation of how "important" a slow step was.
// For example, A link that is entirely or mostly serialized will have a
// weighted time that is the same or similar to its elapsed time.
// A compile that runs in parallel with 999 other compiles will have a weighted
// time that is tiny.
func WeightedTime(steps []Step) map[string]time.Duration {
	if len(steps) == 0 {
		return nil
	}
	steps = Dedup(steps)
	events := toEvent(steps)
	weightedDuration := make(map[string]time.Duration)

	// Track the tasks which are currently running.
	runningTasks := make(map[string]time.Duration)

	// Record the time we have processed up to so we know how to calculate
	// time deltas.
	lastTime := events[0].time

	// Track the accumulated weighted time so that it can efficiently be
	// added to individual tasks.
	var lastWeightedTime time.Duration

	for _, event := range events {
		numRunning := len(runningTasks)
		if numRunning > 0 {
			// Update the total weighted time up to this moment.
			lastWeightedTime += (event.time - lastTime) / time.Duration(numRunning)
		}
		switch event.action {
		case startAction:
			// Record the total weighted task time when this task starts.
			runningTasks[event.target] = lastWeightedTime
		case stopAction:
			// Record the change in the total weighted task time while this task ran.
			weightedDuration[event.target] = lastWeightedTime - runningTasks[event.target]
			delete(runningTasks, event.target)
		}
		lastTime = event.time
	}
	return weightedDuration
}

// Stat represents statistics for build step.
type Stat struct {
	// Type used to group this stat, this is determined by the grouping function
	// provided by the caller when this stat is calculated.
	Type string
	// Count of builds for this type.
	Count int
	// Accumulative build time for this type.
	Time time.Duration
	// Accumulative weighted build time for this time.
	Weighted time.Duration
}

// StatsByType summarizes build step statistics with weighted and typeOf.
// Order of the returned slice is undefined.
func StatsByType(steps []Step, weighted map[string]time.Duration, typeOf func(Step) string) []Stat {
	if len(steps) == 0 {
		return nil
	}
	steps = Dedup(steps)
	m := make(map[string]int) // type to index of stats.
	var stats []Stat
	for _, step := range steps {
		t := typeOf(step)
		if i, ok := m[t]; ok {
			stats[i].Count++
			stats[i].Time += step.Duration()
			stats[i].Weighted += weighted[step.Out]
			continue
		}
		stats = append(stats, Stat{
			Type:     t,
			Count:    1,
			Time:     step.Duration(),
			Weighted: weighted[step.Out],
		})
		m[t] = len(stats) - 1
	}
	return stats
}

// SlowestSteps returns the `n` steps that took the longest time to finish.
//
// Returned steps are sorted on build time in descending order (step takes the
// longest time to build is the first element).
func SlowestSteps(steps []Step, n int) []Step {
	minHeap := new(Steps)
	for _, step := range steps {
		heap.Push(minHeap, step)
		if minHeap.Len() > n {
			heap.Pop(minHeap)
		}
	}
	var res []Step
	for minHeap.Len() > 0 {
		res = append(res, heap.Pop(minHeap).(Step))
	}
	for left, right := 0, len(res)-1; left < right; left, right = left+1, right-1 {
		res[left], res[right] = res[right], res[left]
	}
	return res
}
