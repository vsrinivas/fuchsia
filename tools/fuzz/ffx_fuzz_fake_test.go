// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package fuzz

import (
	"math/rand"
	"strconv"
	"strings"
	"testing"
)

// Simulate output of running a libFuzzer fuzzer through ffx. Validates
// arguments and returns the contents of stdout and the path of the output
// directory for artifacts, etc.
//
// The output is based on example-fuzzers/crash_fuzzer.
func getFfxFuzzOutput(t *testing.T, args []string) (string, string) {
	if len(args) < 1 {
		t.Fatalf("not enough args")
	}

	command := args[0]
	args = args[1:]

	// TODO: Simulate the effect of `ffx set` commands (requires sharing state
	// between processes...)
	seed := rand.Int()

	// Separate out the "-o" and "-j" flags, if any
	var outputDir string
	var testsJson string
	var otherArgs []string
	i := 0
	for i < len(args) {
		if args[i] == "-o" {
			if i == len(args)-1 {
				t.Fatalf("-o flag missing value")
			}
			outputDir = args[i+1]
			i += 2
		} else if args[i] == "-j" {
			if i == len(args)-1 {
				t.Fatalf("-j flag missing value")
			}
			testsJson = args[i+1]
			i += 2
		} else {
			otherArgs = append(otherArgs, args[i])
			i++
		}
	}

	var fuzzerUrl string
	if command == "list" {
		if testsJson != "/path/to/tests.json" {
			t.Fatalf("tests.json not passed correctly: %q", args)
		}
	} else {
		if outputDir == "" {
			t.Fatalf("output flag not detected: %q", args)
		}
		if len(otherArgs) < 1 {
			t.Fatalf("fuzzer url required")
		}
		fuzzerUrl = otherArgs[0]

		if fuzzerUrl == "fuchsia-pkg://fuchsia.com/cff#meta/broken.cm" {
			// Simulate fuzzer error by having the subprocess test exit with
			// error.
			// TODO(fxbug.dev/112048): We are running inside a `go test`
			// environment which, unintuitively, outputs everything over stdout
			// even if we write to stderr.
			t.Fatalf("internal error")
		}
	}

	var output string

	switch command {
	case "list":
		output = `[
  "fuchsia-pkg://fuchsia.com/ffx-fuzzers#meta/one.cm",
  "fuchsia-pkg://fuchsia.com/ffx-fuzzers#meta/two.cm"
]
`
	case "run":
		output = `Attaching to '{{fuzzerUrl}}'...
Attached; fuzzer is idle.
Configuring fuzzer...
Running fuzzer...
/pkg/test/fuzzer -seed={{seed}} -exact_artifact_path=/tmp/result_input /tmp/live_corpus /tmp/seed_corpus 
INFO: Running with entropic power schedule (0xFF, 100).
INFO: Seed: {{seed}}
INFO: Loaded 3 modules   (185 inline 8-bit counters): 2 [0x22618921a000, 0x22618921a002), 32 [0x2123764ee000, 0x2123764ee020), 151 [0x22e15c36a000, 0x22e15c36a097), 
INFO: Loaded 3 PC tables (185 PCs): 2 [0x22618921a008,0x22618921a028), 32 [0x2123764ee020,0x2123764ee220), 151 [0x22e15c36a098,0x22e15c36aa08), 
=={{pid}}== INFO: libFuzzer starting.
INFO:        4 files found in /tmp/live_corpus
INFO:        0 files found in /tmp/seed_corpus
INFO: -max_len is not provided; libFuzzer will not generate inputs larger than 4096 bytes
INFO: seed corpus: files: 4 min: 1b max: 3b total: 8b rss: 35Mb
#5      INITED cov: 6 ft: 6 corp: 4/8b exec/s: 0 rss: 35Mb
#6      NEW    cov: 7 ft: 7 corp: 5/9b lim: 4 exec/s: 0 rss: 35Mb L: 1/3 MS: 1 ChangeBit-
=={{pid}}== ERROR: libFuzzer: deadly signal
NOTE: libFuzzer has rudimentary signal handlers.
      Combine libFuzzer with AddressSanitizer or similar for better crash reports.
SUMMARY: libFuzzer: deadly signal
MS: 1 ChangeByte-; base unit: 42f40f77fc43a11a989b1c0ef558cd7089075b1a
0x48,0x49,0x21,
HI!
artifact_prefix='./'; Test unit written to /tmp/result_input
Base64: SEkh


An input to the fuzzer caused a process to crash.
Input saved to '{{outputDir}}/artifacts/crash-1312'
Exiting...
Note: fuzzer is idle but still alive.
To reconnect later, use the 'attach' command.
To stop this fuzzer, use 'stop'. command
`
	case "try":
		if len(otherArgs) < 2 {
			t.Fatalf("`ffx fuzz try` needs an input")
		}

		output = `Attaching to '{{fuzzerUrl}}'...
Attached; fuzzer is idle.
Trying an input of 4 bytes...
/pkg/test/fuzzer -seed={{seed}} -exact_artifact_path=/tmp/result_input /tmp/temp_corpus/ba704764884fede203157d29e1958ed09135ef07 
INFO: Running with entropic power schedule (0xFF, 100).
INFO: Seed: {{seed}}
INFO: Loaded 3 modules   (185 inline 8-bit counters): 2 [0x232442e92000, 0x232442e92002), 32 [0x23fc54e44000, 0x23fc54e44020), 151 [0x21aec36f4000, 0x21aec36f4097), 
INFO: Loaded 3 PC tables (185 PCs): 2 [0x232442e92008,0x232442e92028), 32 [0x23fc54e44020,0x23fc54e44220), 151 [0x21aec36f4098,0x21aec36f4a08), 
=={{pid}}== INFO: libFuzzer starting.
/pkg/test/fuzzer: Running 1 inputs 1 time(s) each.
Running: /tmp/temp_corpus/ba704764884fede203157d29e1958ed09135ef07
=={{pid}}== ERROR: libFuzzer: deadly signal
NOTE: libFuzzer has rudimentary signal handlers.
      Combine libFuzzer with AddressSanitizer or similar for better crash reports.
SUMMARY: libFuzzer: deadly signal


The input caused a process to crash.
Exiting...
Note: fuzzer is idle but still alive.
To reconnect later, use the 'attach' command.
To stop this fuzzer, use 'stop'. command
`
	case "merge":
		output = `Attaching to '{{fuzzerUrl}}'...
Attached; fuzzer is idle.
Compacting fuzzer corpus...
/pkg/test/fuzzer -seed={{seed}} -exact_artifact_path=/tmp/result_input -merge=1 /tmp/temp_corpus /tmp/seed_corpus /tmp/live_corpus 
INFO: Running with entropic power schedule (0xFF, 100).
INFO: Seed: {{seed}}
INFO: Loaded 3 modules   (185 inline 8-bit counters): 2 [0x224d07e24000, 0x224d07e24002), 32 [0x22b8dba0c000, 0x22b8dba0c020), 151 [0x23102d3ef000, 0x23102d3ef097), 
INFO: Loaded 3 PC tables (185 PCs): 2 [0x224d07e24008,0x224d07e24028), 32 [0x22b8dba0c020,0x22b8dba0c220), 151 [0x23102d3ef098,0x23102d3efa08), 
=={{pid}}== INFO: libFuzzer starting.
MERGE-OUTER: 6 files, 1 in the initial corpus, 0 processed earlier
MERGE-OUTER: attempt 1
INFO: Running with entropic power schedule (0xFF, 100).
INFO: Seed: {{seed}}
INFO: Loaded 3 modules   (185 inline 8-bit counters): 2 [0x23b4ee15e000, 0x23b4ee15e002), 32 [0x235aa2715000, 0x235aa2715020), 151 [0x212a17555000, 0x212a17555097), 
INFO: Loaded 3 PC tables (185 PCs): 2 [0x23b4ee15e008,0x23b4ee15e028), 32 [0x235aa2715020,0x235aa2715220), 151 [0x212a17555098,0x212a17555a08), 
=={{pid}}1== INFO: libFuzzer starting.
INFO: -max_len is not provided; libFuzzer will not generate inputs larger than 1048576 bytes
MERGE-INNER: using the control file '/tmp/libFuzzerTemp.Merge1379908.txt'
MERGE-INNER: 6 total files; 0 processed earlier; will process 6 files now
=={{pid}}1== ERROR: libFuzzer: deadly signal
NOTE: libFuzzer has rudimentary signal handlers.
      Combine libFuzzer with AddressSanitizer or similar for better crash reports.
SUMMARY: libFuzzer: deadly signal
MS: 0 ; base unit: 0000000000000000000000000000000000000000
0x48,0x49,0x21,0x21,
HI!!
artifact_prefix='./'; Test unit written to /tmp/result_input
Base64: SEkhIQ==
MERGE-OUTER: attempt 2
INFO: Running with entropic power schedule (0xFF, 100).
INFO: Seed: 3241147828
INFO: Loaded 3 modules   (185 inline 8-bit counters): 2 [0x229b87499000, 0x229b87499002), 32 [0x23085bb81000, 0x23085bb81020), 151 [0x207e41de4000, 0x207e41de4097), 
INFO: Loaded 3 PC tables (185 PCs): 2 [0x229b87499008,0x229b87499028), 32 [0x23085bb81020,0x23085bb81220), 151 [0x207e41de4098,0x207e41de4a08), 
=={{pid}}2== INFO: libFuzzer starting.
INFO: -max_len is not provided; libFuzzer will not generate inputs larger than 1048576 bytes
MERGE-INNER: using the control file '/tmp/libFuzzerTemp.Merge1379908.txt'
MERGE-INNER: '/tmp/temp_corpus/ba704764884fede203157d29e1958ed09135ef07' caused a failure at the previous merge step
MERGE-INNER: 6 total files; 1 processed earlier; will process 5 files now
#1      pulse  cov: 3 ft: 3 exec/s: 0 rss: 35Mb
#1      LOADED cov: 3 ft: 3 exec/s: 0 rss: 35Mb
#2      pulse  cov: 4 ft: 4 exec/s: 0 rss: 35Mb
#4      pulse  cov: 6 ft: 6 exec/s: 0 rss: 35Mb
#5      DONE   cov: 7 ft: 7 exec/s: 0 rss: 35Mb
MERGE-OUTER: successful in 2 attempt(s)
MERGE-OUTER: the control file has 528 bytes
MERGE-OUTER: consumed 0Mb (7Mb rss) to parse the control file
MERGE-OUTER: 5 new files with 7 new features added; 7 new coverage edges


Retrieving fuzzer corpus...
Retrieved 7 inputs totaling 13 bytes from the live corpus.
Exiting...
Note: fuzzer is idle but still alive.
To reconnect later, use the 'attach' command.
To stop this fuzzer, use 'stop'. command
`
	case "minimize":
		output = `Attaching to '{{fuzzerUrl}}'...
Attached; fuzzer is idle.
Configuring fuzzer...
Attempting to minimize an input of 4 bytes...
/pkg/test/fuzzer -seed={{seed}} -exact_artifact_path=/tmp/result_input -minimize_crash=1 /tmp/test_input 
INFO: Running with entropic power schedule (0xFF, 100).
INFO: Seed: {{seed}}
INFO: Loaded 3 modules   (185 inline 8-bit counters): 2 [0x23f4b206d000, 0x23f4b206d002), 32 [0x227fa504a000, 0x227fa504a020), 151 [0x218914fd3000, 0x218914fd3097), 
INFO: Loaded 3 PC tables (185 PCs): 2 [0x23f4b206d008,0x23f4b206d028), 32 [0x227fa504a020,0x227fa504a220), 151 [0x218914fd3098,0x218914fd3a08), 
=={{pid}}== INFO: libFuzzer starting.
CRASH_MIN: minimizing crash input: '/tmp/test_input' (4 bytes)
CRASH_MIN: executing: /pkg/test/fuzzer -seed={{seed}} /tmp/test_input 2>&1
CRASH_MIN: '/tmp/test_input' (4 bytes) caused a crash. Will try to minimize it further
CRASH_MIN: executing: /pkg/test/fuzzer -seed={{seed}} /tmp/test_input -minimize_crash_internal_step=1 -exact_artifact_path=/tmp/result_input 2>&1
INFO: Running with entropic power schedule (0xFF, 100).
INFO: Seed: {{seed}}
INFO: Loaded 3 modules   (185 inline 8-bit counters): 2 [0x20cf66ebe000, 0x20cf66ebe002), 32 [0x2133edc2e000, 0x2133edc2e020), 151 [0x208f62943000, 0x208f62943097), 
INFO: Loaded 3 PC tables (185 PCs): 2 [0x20cf66ebe008,0x20cf66ebe028), 32 [0x2133edc2e020,0x2133edc2e220), 151 [0x208f62943098,0x208f62943a08), 
=={{pid}}1== INFO: libFuzzer starting.
INFO: Starting MinimizeCrashInputInternalStep: 4
INFO: -max_len is not provided; libFuzzer will not generate inputs larger than 4 bytes
=={{pid}}1== ERROR: libFuzzer: deadly signal
NOTE: libFuzzer has rudimentary signal handlers.
      Combine libFuzzer with AddressSanitizer or similar for better crash reports.
SUMMARY: libFuzzer: deadly signal
MS: 1 EraseBytes-; base unit: 0000000000000000000000000000000000000000
0x48,0x49,0x21,
HI!
artifact_prefix='./'; Test unit written to /tmp/result_input
Base64: SEkh
*********************************
CRASH_MIN: minimizing crash input: '/tmp/result_input' (3 bytes)
CRASH_MIN: executing: /pkg/test/fuzzer -seed={{seed}} /tmp/result_input 2>&1
CRASH_MIN: '/tmp/result_input' (3 bytes) caused a crash. Will try to minimize it further
CRASH_MIN: executing: /pkg/test/fuzzer -seed={{seed}} /tmp/result_input -minimize_crash_internal_step=1 -exact_artifact_path=/tmp/result_input 2>&1
INFO: Running with entropic power schedule (0xFF, 100).
INFO: Seed: {{seed}}
INFO: Loaded 3 modules   (185 inline 8-bit counters): 2 [0x20a9e421b000, 0x20a9e421b002), 32 [0x2385a10ab000, 0x2385a10ab020), 151 [0x204ab3617000, 0x204ab3617097), 
INFO: Loaded 3 PC tables (185 PCs): 2 [0x20a9e421b008,0x20a9e421b028), 32 [0x2385a10ab020,0x2385a10ab220), 151 [0x204ab3617098,0x204ab3617a08), 
=={{pid}}2== INFO: libFuzzer starting.
INFO: Starting MinimizeCrashInputInternalStep: 3
INFO: -max_len is not provided; libFuzzer will not generate inputs larger than 3 bytes
#1048576        pulse  exec/s: 524288 rss: 50Mb
#2097152        pulse  exec/s: 524288 rss: 95Mb
INFO: Done MinimizeCrashInputInternalStep, no crashes found
CRASH_MIN: failed to minimize beyond /tmp/result_input (3 bytes), exiting


Minimized input written to '{{outputDir}}/artifacts/crash-1312'
Exiting...
Note: fuzzer is idle but still alive.
To reconnect later, use the 'attach' command.
To stop this fuzzer, use 'stop'. command
`
	case "fetch":
		output = `Attaching to '{{fuzzerUrl}}'...
Attached; fuzzer is idle.
Retrieving fuzzer corpus...
Retrieved 2 input totaling 32 bytes from the live corpus.
Exiting...
Note: fuzzer is idle but still alive.
To reconnect later, use the 'attach' command.
To stop this fuzzer, use 'stop'. command
`
	default:
		t.Fatalf("unknown ffx fuzz command: %s", command)
	}

	// Extremely basic templating
	output = strings.ReplaceAll(output, "{{fuzzerUrl}}", fuzzerUrl)
	output = strings.ReplaceAll(output, "{{seed}}", strconv.Itoa(seed))
	output = strings.ReplaceAll(output, "{{outputDir}}", outputDir)
	// Note: the single digits following the PIDs in some parts above are not
	// mistakes, just a way to make the subprocesses have distinct PIDs
	output = strings.ReplaceAll(output, "{{pid}}", strconv.Itoa(mockFuzzerPid))

	return output, outputDir
}
