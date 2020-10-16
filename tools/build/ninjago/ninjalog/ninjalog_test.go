// Copyright 2014 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package ninjalog

import (
	"bytes"
	"container/heap"
	"io/ioutil"
	"reflect"
	"sort"
	"strings"
	"testing"
	"time"

	"github.com/google/go-cmp/cmp"
	"github.com/google/go-cmp/cmp/cmpopts"
	"go.fuchsia.dev/fuchsia/tools/build/ninjago/compdb"
)

var (
	logTestCase = `# ninja log v5
76	187	0	resources/inspector/devtools_extension_api.js	75430546595be7c2
80	284	0	gen/autofill_regex_constants.cc	fa33c8d7ce1d8791
78	286	0	gen/angle/commit_id.py	4ede38e2c1617d8c
79	287	0	gen/angle/copy_compiler_dll.bat	9fb635ad5d2c1109
141	287	0	PepperFlash/manifest.json	324f0a0b77c37ef
142	288	0	PepperFlash/libpepflashplayer.so	1e2c2b7845a4d4fe
287	290	0	obj/third_party/angle/src/copy_scripts.actions_rules_copies.stamp	b211d373de72f455
`

	stepsTestCase = []Step{
		{
			Start:   76 * time.Millisecond,
			End:     187 * time.Millisecond,
			Out:     "resources/inspector/devtools_extension_api.js",
			CmdHash: 0x75430546595be7c2,
		},
		{
			Start:   80 * time.Millisecond,
			End:     284 * time.Millisecond,
			Out:     "gen/autofill_regex_constants.cc",
			CmdHash: 0xfa33c8d7ce1d8791,
		},
		{
			Start:   78 * time.Millisecond,
			End:     286 * time.Millisecond,
			Out:     "gen/angle/commit_id.py",
			CmdHash: 0x4ede38e2c1617d8c,
		},
		{
			Start:   79 * time.Millisecond,
			End:     287 * time.Millisecond,
			Out:     "gen/angle/copy_compiler_dll.bat",
			CmdHash: 0x9fb635ad5d2c1109,
		},
		{
			Start:   141 * time.Millisecond,
			End:     287 * time.Millisecond,
			Out:     "PepperFlash/manifest.json",
			CmdHash: 0x324f0a0b77c37ef,
		},
		{
			Start:   142 * time.Millisecond,
			End:     288 * time.Millisecond,
			Out:     "PepperFlash/libpepflashplayer.so",
			CmdHash: 0x1e2c2b7845a4d4fe,
		},
		{
			Start:   287 * time.Millisecond,
			End:     290 * time.Millisecond,
			Out:     "obj/third_party/angle/src/copy_scripts.actions_rules_copies.stamp",
			CmdHash: 0xb211d373de72f455,
		},
	}

	stepsSorted = []Step{
		{
			Start:   76 * time.Millisecond,
			End:     187 * time.Millisecond,
			Out:     "resources/inspector/devtools_extension_api.js",
			CmdHash: 0x75430546595be7c2,
		},
		{
			Start:   78 * time.Millisecond,
			End:     286 * time.Millisecond,
			Out:     "gen/angle/commit_id.py",
			CmdHash: 0x4ede38e2c1617d8c,
		},
		{
			Start:   79 * time.Millisecond,
			End:     287 * time.Millisecond,
			Out:     "gen/angle/copy_compiler_dll.bat",
			CmdHash: 0x9fb635ad5d2c1109,
		},
		{
			Start:   80 * time.Millisecond,
			End:     284 * time.Millisecond,
			Out:     "gen/autofill_regex_constants.cc",
			CmdHash: 0xfa33c8d7ce1d8791,
		},
		{
			Start:   141 * time.Millisecond,
			End:     287 * time.Millisecond,
			Out:     "PepperFlash/manifest.json",
			CmdHash: 0x324f0a0b77c37ef,
		},
		{
			Start:   142 * time.Millisecond,
			End:     288 * time.Millisecond,
			Out:     "PepperFlash/libpepflashplayer.so",
			CmdHash: 0x1e2c2b7845a4d4fe,
		},
		{
			Start:   287 * time.Millisecond,
			End:     290 * time.Millisecond,
			Out:     "obj/third_party/angle/src/copy_scripts.actions_rules_copies.stamp",
			CmdHash: 0xb211d373de72f455,
		},
	}

	metadataTestCase = Metadata{
		Platform: "linux",
		Argv:     []string{"../../../scripts/slave/compile.py", "--target", "Release", "--clobber", "--compiler=goma", "--", "all"},
		Cwd:      "/b/build/slave/Linux_x64/build/src",
		Compiler: "goma",
		Cmdline:  []string{"ninja", "-C", "/b/build/slave/Linux_x64/build/src/out/Release", "all", "-j50"},
		Exit:     0,
		Env: map[string]string{
			"LANG":    "en_US.UTF-8",
			"SHELL":   "/bin/bash",
			"HOME":    "/home/chrome-bot",
			"PWD":     "/b/build/slave/Linux_x64/build",
			"LOGNAME": "chrome-bot",
			"USER":    "chrome-bot",
			"PATH":    "/home/chrome-bot/slavebin:/b/depot_tools:/usr/bin:/usr/bin:/bin:/usr/sbin:/sbin:/usr/local/bin",
		},
		CompilerProxyInfo: "/tmp/compiler_proxy.build48-m1.chrome-bot.log.INFO.20140907-203827.14676",
	}
)

func TestStepsSort(t *testing.T) {
	steps := append([]Step{}, stepsTestCase...)
	sort.Sort(Steps(steps))
	if !reflect.DeepEqual(steps, stepsSorted) {
		t.Errorf("sort Steps=%v; want=%v", steps, stepsSorted)
	}
}

func TestStepsReverse(t *testing.T) {
	steps := []Step{
		{Out: "0"},
		{Out: "1"},
		{Out: "2"},
		{Out: "3"},
	}
	Steps(steps).Reverse()
	want := []Step{
		{Out: "3"},
		{Out: "2"},
		{Out: "1"},
		{Out: "0"},
	}
	if !reflect.DeepEqual(steps, want) {
		t.Errorf("steps.Reverse=%v; want=%v", steps, want)
	}
}

func TestParseBadVersion(t *testing.T) {
	_, err := Parse(".ninja_log", strings.NewReader(`# ninja log v4
0	1	0	foo	touch foo
`))
	if err == nil {
		t.Error("Parse()=_, <nil>; want=_, error")
	}
}

func TestParseSimple(t *testing.T) {
	njl, err := Parse(".ninja_log", strings.NewReader(logTestCase))
	if err != nil {
		t.Errorf(`Parse()=_, %v; want=_, <nil>`, err)
	}

	want := &NinjaLog{
		Filename: ".ninja_log",
		Start:    1,
		Steps:    stepsTestCase,
	}
	if !reflect.DeepEqual(njl, want) {
		t.Errorf("Parse()=%v; want=%v", njl, want)
	}
}

func TestParseEmptyLine(t *testing.T) {
	njl, err := Parse(".ninja_log", strings.NewReader(logTestCase+"\n"))
	if err != nil {
		t.Errorf(`Parse()=_, %v; want=_, <nil>`, err)
	}
	want := &NinjaLog{
		Filename: ".ninja_log",
		Start:    1,
		Steps:    stepsTestCase,
	}
	if !reflect.DeepEqual(njl, want) {
		t.Errorf("Parse()=%v; want=%v", njl, want)
	}
}

func TestParsePopulate(t *testing.T) {
	const testCase = `# ninja log v5
0	10	0	file.stamp	42bba4133f0fb12c
`

	njl, err := Parse(".ninja_log", strings.NewReader(testCase))
	if err != nil {
		t.Errorf(`Parse(%q)=_, %v; want=_, <nil>`, testCase, err)
	}

	steps := Populate(njl.Steps, []compdb.Command{
		{
			Directory: "out",
			Command:   "touch file.stamp",
			File:      "file",
			Output:    "file.stamp",
		},
	})

	want := []Step{
		{
			Start:   0 * time.Millisecond,
			End:     10 * time.Millisecond,
			Out:     "file.stamp",
			CmdHash: 0x42bba4133f0fb12c,
			Command: &compdb.Command{
				Directory: "out",
				Command:   "touch file.stamp",
				File:      "file",
				Output:    "file.stamp",
			},
		},
	}
	if !reflect.DeepEqual(steps, want) {
		t.Errorf("Parse(%q)=%v; want=%v", testCase, njl, want)
	}
}

func TestParseLast(t *testing.T) {
	njl, err := Parse(".ninja_log", strings.NewReader(`# ninja log v5
1020807	1020916	0	chrome.1	e101fd46be020cfc
84	9489	0	gen/libraries.cc	9001f3182fa8210e
1024369	1041522	0	chrome	aee9d497d56c9637
76	187	0	resources/inspector/devtools_extension_api.js	75430546595be7c2
80	284	0	gen/autofill_regex_constants.cc	fa33c8d7ce1d8791
78	286	0	gen/angle/commit_id.py	4ede38e2c1617d8c
79	287	0	gen/angle/copy_compiler_dll.bat	9fb635ad5d2c1109
141	287	0	PepperFlash/manifest.json	324f0a0b77c37ef
142	288	0	PepperFlash/libpepflashplayer.so	1e2c2b7845a4d4fe
287	290	0	obj/third_party/angle/src/copy_scripts.actions_rules_copies.stamp	b211d373de72f455
`))
	if err != nil {
		t.Errorf(`Parse()=_, %v; want=_, <nil>`, err)
	}

	want := &NinjaLog{
		Filename: ".ninja_log",
		Start:    4,
		Steps:    stepsTestCase,
	}
	if !reflect.DeepEqual(njl, want) {
		t.Errorf("Parse()=%v; want=%v", njl, want)
	}
}

func TestParseMetadata(t *testing.T) {
	njl, err := Parse(".ninja_log", strings.NewReader(`# ninja log v5
1020807	1020916	0	chrome.1	e101fd46be020cfc
84	9489	0	gen/libraries.cc	9001f3182fa8210e
1024369	1041522	0	chrome	aee9d497d56c9637
76	187	0	resources/inspector/devtools_extension_api.js	75430546595be7c2
80	284	0	gen/autofill_regex_constants.cc	fa33c8d7ce1d8791
78	286	0	gen/angle/commit_id.py	4ede38e2c1617d8c
79	287	0	gen/angle/copy_compiler_dll.bat	9fb635ad5d2c1109
141	287	0	PepperFlash/manifest.json	324f0a0b77c37ef
142	288	0	PepperFlash/libpepflashplayer.so	1e2c2b7845a4d4fe
287	290	0	obj/third_party/angle/src/copy_scripts.actions_rules_copies.stamp	b211d373de72f455

# end of ninja log
{"platform": "linux", "argv": ["../../../scripts/slave/compile.py", "--target", "Release", "--clobber", "--compiler=goma", "--", "all"], "cmdline": ["ninja", "-C", "/b/build/slave/Linux_x64/build/src/out/Release", "all", "-j50"], "exit": 0, "env": {"LANG": "en_US.UTF-8", "SHELL": "/bin/bash", "HOME": "/home/chrome-bot", "PWD": "/b/build/slave/Linux_x64/build", "LOGNAME": "chrome-bot", "USER": "chrome-bot", "PATH": "/home/chrome-bot/slavebin:/b/depot_tools:/usr/bin:/usr/bin:/bin:/usr/sbin:/sbin:/usr/local/bin" }, "compiler_proxy_info": "/tmp/compiler_proxy.build48-m1.chrome-bot.log.INFO.20140907-203827.14676", "cwd": "/b/build/slave/Linux_x64/build/src", "compiler": "goma"}
`))
	if err != nil {
		t.Errorf(`Parse()=_, %#v; want=_, <nil>`, err)
	}

	want := &NinjaLog{
		Filename: ".ninja_log",
		Start:    4,
		Steps:    stepsTestCase,
		Metadata: metadataTestCase,
	}
	njl.Metadata.Raw = ""
	if !reflect.DeepEqual(njl, want) {
		t.Errorf("Parse()=%#v; want=%#v", njl, want)
	}
}

func TestParseBadMetadata(t *testing.T) {
	// https://bugs.chromium.org/p/chromium/issues/detail?id=667571
	njl, err := Parse(".ninja_log", strings.NewReader(`# ninja log v5
1020807	1020916	0	chrome.1	e101fd46be020cfc
84	9489	0	gen/libraries.cc	9001f3182fa8210e
1024369	1041522	0	chrome	aee9d497d56c9637
76	187	0	resources/inspector/devtools_extension_api.js	75430546595be7c2
80	284	0	gen/autofill_regex_constants.cc	fa33c8d7ce1d8791
78	286	0	gen/angle/commit_id.py	4ede38e2c1617d8c
79	287	0	gen/angle/copy_compiler_dll.bat	9fb635ad5d2c1109
141	287	0	PepperFlash/manifest.json	324f0a0b77c37ef
142	288	0	PepperFlash/libpepflashplayer.so	1e2c2b7845a4d4fe
287	290	0	obj/third_party/angle/src/copy_scripts.actions_rules_copies.stamp	b211d373de72f455
# end of ninja log
{"platform": "linux", "argv": ["/b/build/scripts/slave/upload_goma_logs.py", "--upload-compiler-proxy-info", "--json-status", "/b/build/slave/cache/cipd/goma/jsonstatus", "--ninja-log-outdir", "/b/build/slave/pdfium/build/pdfium/out/debug_xfa_v8", "--ninja-log-compiler", "unknown", "--ninja-log-command", "['ninja', '-C', Path('checkout', 'out','debug_xfa_v8'), '-j', 80]", "--ninja-log-exit-status", "0", "--goma-stats-file", "/b/build/slave/pdfium/.recipe_runtime/tmpOgwx97/build_data/goma_stats_proto", "--goma-crash-report-id-file", "/b/build/slave/pdfium/.recipe_runtime/tmpOgwx97/build_data/crash_report_id_file", "--build-data-dir", "/b/build/slave/pdfium/.recipe_runtime/tmpOgwx97/build_data", "--buildbot-buildername", "linux_xfa", "--buildbot-mastername", "tryserver.client.pdfium", "--buildbot-slavename", "slave1386-c4"], "cmdline": "['ninja', '-C', Path('checkout','out','debug_xfa_v8'), '-j', 80]", "exit": 0, "env": {"GOMA_SERVICE_ACCOUNT_JSON_FILE": "/creds/service_accounts/service-account-goma-client.json", "BUILDBOT_BUILDERNAME": "linux_xfa", "USER": "chrome-bot", "HOME": "/home/chrome-bot", "BOTO_CONFIG": "/b/build/scripts/slave/../../site_config/.boto", "PATH": "/home/chrome-bot/slavebin:/b/depot_tools:/usr/bin:/usr/bin:/bin:/usr/sbin:/sbin:/usr/local/bin", "PYTHONUNBUFFERED": "1", "BUILDBOT_BUILDBOTURL": "https://build.chromium.org/p/tryserver.client.pdfium/", "DISPLAY": ":0.0", "LANG": "en_US.UTF-8", "BUILDBOT_BLAMELIST": "[u'dsinclair@chromium.org']", "BUILDBOT_MASTERNAME": "tryserver.client.pdfium", "GOMACTL_CRASH_REPORT_ID_FILE": "/b/build/slave/pdfium/.recipe_runtime/tmpOgwx97/build_data/crash_report_id_file", "USERNAME": "chrome-bot", "BUILDBOT_GOT_REVISION": "None", "PYTHONPATH": "/b/build/site_config:/b/build/scripts:/b/build/scripts/release:/b/build/third_party:/b/build/third_party/requests_2_10_0:/b/build_internal/site_config:/b/build_internal/symsrc:/b/build/slave:/b/build/third_party/buildbot_slave_8_4:/b/build/third_party/twisted_10_2:", "BUILDBOT_SCHEDULER": "None", "BUILDBOT_REVISION": "", "AWS_CREDENTIAL_FILE": "/b/build/scripts/slave/../../site_config/.boto", "CHROME_HEADLESS": "1", "BUILDBOT_BRANCH": "", "GIT_USER_AGENT": "linux2 git/2.10.2 slave1386-c4.c.chromecompute.google.com.internal", "TESTING_SLAVENAME": "slave1386-c4", "GOMA_DUMP_STATS_FILE": "/b/build/slave/pdfium/.recipe_runtime/tmpOgwx97/build_data/goma_stats_proto", "BUILDBOT_BUILDNUMBER": "2937", "PWD": "/b/build/slave/pdfium/build", "BUILDBOT_SLAVENAME": "slave1386-c4", "BUILDBOT_CLOBBER": "", "PAGER": "cat"}, "compiler_proxy_info": "/tmp/compiler_proxy.slave1386-c4.chrome-bot.log.INFO.20161121-165459.5790", "cwd": "/b/build/slave/pdfium/build", "compiler": "unknown"}
`))
	if err != nil {
		t.Errorf(`Parse()=_, %#v; want=_, <nil>`, err)
	}

	if njl.Metadata.Error == "" {
		t.Errorf("Parse().Metadata.Error='', want some error")
	}
	njl.Metadata = Metadata{}

	want := &NinjaLog{
		Filename: ".ninja_log",
		Start:    4,
		Steps:    stepsTestCase,
	}
	if !reflect.DeepEqual(njl, want) {
		t.Errorf("Parse()=%#v; want=%#v", njl, want)
	}
}

func TestDump(t *testing.T) {
	var b bytes.Buffer
	err := Dump(&b, stepsTestCase)
	if err != nil {
		t.Errorf("Dump()=%v; want=<nil>", err)
	}
	if b.String() != logTestCase {
		t.Errorf("Dump %q; want %q", b.String(), logTestCase)
	}
}

func TestDedup(t *testing.T) {
	steps := append([]Step{}, stepsTestCase...)
	for _, out := range []string{
		"gen/ui/keyboard/webui/keyboard.mojom.cc",
		"gen/ui/keyboard/webui/keyboard.mojom.h",
		"gen/ui/keyboard/webui/keyboard.mojom.js",
		"gen/ui/keyboard/webui/keyboard.mojom-internal.h",
	} {
		steps = append(steps, Step{
			Start:   302 * time.Millisecond,
			End:     5764 * time.Millisecond,
			Out:     out,
			CmdHash: 0xa551cc46f8c21e5a,
		})
	}
	got := Dedup(steps)
	want := append([]Step{}, stepsSorted...)
	want = append(want, Step{
		Start: 302 * time.Millisecond,
		End:   5764 * time.Millisecond,
		Out:   "gen/ui/keyboard/webui/keyboard.mojom-internal.h",
		Outs: []string{
			"gen/ui/keyboard/webui/keyboard.mojom.cc",
			"gen/ui/keyboard/webui/keyboard.mojom.h",
			"gen/ui/keyboard/webui/keyboard.mojom.js",
		},
		CmdHash: 0xa551cc46f8c21e5a,
	})
	if !reflect.DeepEqual(got, want) {
		t.Errorf("Dedup=%v; want=%v", got, want)
	}
}

func TestFlow(t *testing.T) {
	steps := append([]Step{}, stepsTestCase...)
	steps = append(steps, Step{
		Start:   187 * time.Millisecond,
		End:     21304 * time.Millisecond,
		Out:     "obj/third_party/pdfium/core/src/fpdfdoc/fpdfdoc.doc_formfield.o",
		CmdHash: 0x2ac7111aa1ae86af,
	})

	flow := Flow(steps)

	want := [][]Step{
		{
			{
				Start:   76 * time.Millisecond,
				End:     187 * time.Millisecond,
				Out:     "resources/inspector/devtools_extension_api.js",
				CmdHash: 0x75430546595be7c2,
			},
			{
				Start:   187 * time.Millisecond,
				End:     21304 * time.Millisecond,
				Out:     "obj/third_party/pdfium/core/src/fpdfdoc/fpdfdoc.doc_formfield.o",
				CmdHash: 0x2ac7111aa1ae86af,
			},
		},
		{
			{
				Start:   78 * time.Millisecond,
				End:     286 * time.Millisecond,
				Out:     "gen/angle/commit_id.py",
				CmdHash: 0x4ede38e2c1617d8c,
			},
			{
				Start:   287 * time.Millisecond,
				End:     290 * time.Millisecond,
				Out:     "obj/third_party/angle/src/copy_scripts.actions_rules_copies.stamp",
				CmdHash: 0xb211d373de72f455,
			},
		},
		{
			{
				Start:   79 * time.Millisecond,
				End:     287 * time.Millisecond,
				Out:     "gen/angle/copy_compiler_dll.bat",
				CmdHash: 0x9fb635ad5d2c1109,
			},
		},
		{
			{
				Start:   80 * time.Millisecond,
				End:     284 * time.Millisecond,
				Out:     "gen/autofill_regex_constants.cc",
				CmdHash: 0xfa33c8d7ce1d8791,
			},
		},
		{
			{
				Start:   141 * time.Millisecond,
				End:     287 * time.Millisecond,
				Out:     "PepperFlash/manifest.json",
				CmdHash: 0x324f0a0b77c37ef,
			},
		},
		{
			{
				Start:   142 * time.Millisecond,
				End:     288 * time.Millisecond,
				Out:     "PepperFlash/libpepflashplayer.so",
				CmdHash: 0x1e2c2b7845a4d4fe,
			},
		},
	}

	if !reflect.DeepEqual(flow, want) {
		t.Errorf("Flow()=%v; want=%v", flow, want)
	}
}

func TestWeightedTime(t *testing.T) {
	steps := []Step{
		{
			Start:   0 * time.Millisecond,
			End:     3 * time.Millisecond,
			Out:     "target-a",
			CmdHash: 0xa,
		},
		{
			Start:   2 * time.Millisecond,
			End:     5 * time.Millisecond,
			Out:     "target-b",
			CmdHash: 0xb,
		},
		{
			Start:   2 * time.Millisecond,
			End:     8 * time.Millisecond,
			Out:     "target-c",
			CmdHash: 0xc,
		},
		{
			Start:   2 * time.Millisecond,
			End:     3 * time.Millisecond,
			Out:     "target-d",
			CmdHash: 0xd,
		},
	}

	// 0 1 2 3 4 5 6 7 8
	// +-+-+-+-+-+-+-+-+
	// <--A-->
	//     <--B-->
	//     <------C---->
	//     <D>
	got := WeightedTime(steps)
	want := map[string]time.Duration{
		"target-a": 2*time.Millisecond + 1*time.Millisecond/4,
		"target-b": 1*time.Millisecond/4 + 2*time.Millisecond/2,
		"target-c": 1*time.Millisecond/4 + 2*time.Millisecond/2 + 3*time.Millisecond,
		"target-d": 1 * time.Millisecond / 4,
	}
	if !reflect.DeepEqual(got, want) {
		t.Errorf("WeightedTime(%v)=%v; want=%v", steps, got, want)
	}
}

func BenchmarkParse(b *testing.B) {
	data, err := ioutil.ReadFile("testdata/ninja_log")
	if err != nil {
		b.Errorf(`ReadFile("testdata/ninja_log")=_, %v; want_, <nil>`, err)
	}

	for i := 0; i < b.N; i++ {
		_, err := Parse(".ninja_log", bytes.NewReader(data))
		if err != nil {
			b.Errorf(`Parse()=_, %v; want=_, <nil>`, err)
		}
	}
}

func BenchmarkDedup(b *testing.B) {
	data, err := ioutil.ReadFile("testdata/ninja_log")
	if err != nil {
		b.Errorf(`ReadFile("testdata/ninja_log")=_, %v; want_, <nil>`, err)
	}

	njl, err := Parse(".ninja_log", bytes.NewReader(data))
	if err != nil {
		b.Errorf(`Parse()=_, %v; want=_, <nil>`, err)
	}
	b.ResetTimer()
	for i := 0; i < b.N; i++ {
		steps := make([]Step, len(njl.Steps))
		copy(steps, njl.Steps)
		Dedup(steps)
	}
}

func BenchmarkFlow(b *testing.B) {
	data, err := ioutil.ReadFile("testdata/ninja_log")
	if err != nil {
		b.Errorf(`ReadFile("testdata/ninja_log")=_, %v; want_, <nil>`, err)
	}

	njl, err := Parse(".ninja_log", bytes.NewReader(data))
	if err != nil {
		b.Errorf(`Parse()=_, %v; want=_, <nil>`, err)
	}
	steps := Dedup(njl.Steps)
	b.ResetTimer()
	for i := 0; i < b.N; i++ {
		flowInput := make([]Step, len(steps))
		copy(flowInput, steps)
		Flow(flowInput)
	}
}

func BenchmarkToTraces(b *testing.B) {
	data, err := ioutil.ReadFile("testdata/ninja_log")
	if err != nil {
		b.Errorf(`ReadFile("testdata/ninja_log")=_, %v; want_, <nil>`, err)
	}

	njl, err := Parse(".ninja_log", bytes.NewReader(data))
	if err != nil {
		b.Errorf(`Parse()=_, %v; want=_, <nil>`, err)
	}
	steps := Dedup(njl.Steps)
	flow := Flow(steps)
	b.ResetTimer()
	for i := 0; i < b.N; i++ {
		ToTraces(flow, 1, nil)
	}
}

func BenchmarkDedupFlowToTraces(b *testing.B) {
	data, err := ioutil.ReadFile("testdata/ninja_log")
	if err != nil {
		b.Errorf(`ReadFile("testdata/ninja_log")=_, %v; want_, <nil>`, err)
	}

	for i := 0; i < b.N; i++ {
		njl, err := Parse(".ninja_log", bytes.NewReader(data))
		if err != nil {
			b.Errorf(`Parse()=_, %v; want=_, <nil>`, err)
		}

		steps := Dedup(njl.Steps)
		flow := Flow(steps)
		ToTraces(flow, 1, nil)
	}
}

func TestCategory(t *testing.T) {
	for _, tc := range []struct {
		step Step
		want string
	}{
		{
			step: Step{},
			want: "unknown",
		},
		{
			step: Step{
				Command: &compdb.Command{
					Command: "touch obj/third_party/cobalt/src/lib/client/rust/cobalt-client.inputs.stamp",
				},
			},
			want: "touch",
		},
		{
			step: Step{
				Command: &compdb.Command{
					Command: "ln -f ../../zircon/third_party/ulib/musl/include/libgen.h zircon_toolchain/obj/zircon/public/sysroot/sysroot/include/libgen.h 2>/dev/null || (rm -rf zircon_toolchain/obj/zircon/public/sysroot/sysroot/include/libgen.h && cp -af ../../zircon/third_party/ulib/musl/include/libgen.h zircon_toolchain/obj/zircon/public/sysroot/sysroot/include/libgen.h)",
				},
			},
			want: "ln,rm,cp",
		},
		{
			step: Step{
				Command: &compdb.Command{
					Command: "/usr/bin/env ../../build/gn_run_binary.sh ../../prebuilt/third_party/clang/linux-x64/bin host_x64/fidlgen_rust --json fidling/gen/sdk/fidl/fuchsia.wlan.product.deprecatedconfiguration/fuchsia.wlan.product.deprecatedconfiguration.fidl.json --output-filename fidling/gen/sdk/fidl/fuchsia.wlan.product.deprecatedconfiguration/fidl_fuchsia_wlan_product_deprecatedconfiguration.rs --rustfmt /home/jayzhuang/fuchsia/prebuilt/third_party/rust_tools/linux-x64/bin/rustfmt",
				},
			},
			want: "fidlgen_rust",
		},
	} {
		if got := tc.step.Category(); got != tc.want {
			t.Errorf("Category() = %s, want: %s, step: %#v", got, tc.want, tc.step)
		}
	}
}

func TestStatsByType(t *testing.T) {
	for _, tc := range []struct {
		desc     string
		steps    []Step
		weighted map[string]time.Duration
		want     []Stat
	}{
		{
			desc: "empty",
		},
		{
			desc: "group by category",
			steps: []Step{
				{
					Start:   time.Second,
					End:     11 * time.Second,
					Out:     "cc1",
					CmdHash: 1,
					Command: &compdb.Command{Command: "gomacc"},
				},
				{
					Start:   time.Second,
					End:     2 * time.Second,
					Out:     "rust1",
					CmdHash: 2,
					Command: &compdb.Command{Command: "rustc"},
				},
				{
					Start:   10 * time.Second,
					End:     21 * time.Second,
					Out:     "rust2",
					CmdHash: 3,
					Command: &compdb.Command{Command: "rustc"},
				},
				{
					End:     5 * time.Second,
					Out:     "cc2",
					CmdHash: 4,
					Command: &compdb.Command{Command: "gomacc"},
				},
				{
					End:     100 * time.Millisecond,
					CmdHash: 5,
					Out:     "unknown",
				},
				{
					End:     42 * time.Second,
					Out:     "cc3",
					CmdHash: 6,
					Command: &compdb.Command{Command: "gomacc"},
				},
			},
			weighted: map[string]time.Duration{
				"cc1":     time.Second,
				"cc2":     2 * time.Second,
				"cc3":     3 * time.Second,
				"rust1":   4 * time.Second,
				"rust2":   5 * time.Second,
				"unknown": 6 * time.Second,
			},
			want: []Stat{
				{
					Type:     "gomacc",
					Count:    3,
					Time:     57 * time.Second,
					Weighted: 6 * time.Second,
					Times:    []time.Duration{5 * time.Second, 10 * time.Second, 42 * time.Second},
				},
				{
					Type:     "rustc",
					Count:    2,
					Time:     12 * time.Second,
					Weighted: 9 * time.Second,
					Times:    []time.Duration{time.Second, 11 * time.Second},
				},
				{
					Type:     "unknown",
					Count:    1,
					Time:     100 * time.Millisecond,
					Weighted: 6 * time.Second,
					Times:    []time.Duration{100 * time.Millisecond},
				},
			},
		},
	} {
		t.Run(tc.desc, func(t *testing.T) {
			got := StatsByType(tc.steps, tc.weighted, func(s Step) string { return s.Category() })
			orderByType := cmpopts.SortSlices(func(x, y Stat) bool { return x.Type < y.Type })
			orderByDuration := cmpopts.SortSlices(func(x, y time.Duration) bool { return x < y })
			if diff := cmp.Diff(tc.want, got, orderByType, orderByDuration); diff != "" {
				t.Errorf("StatsByType(%#v, %#v, 'step.Category()') = %#v\nwant:\n%#v\ndiff(-want, +got):\n%s", tc.steps, tc.weighted, got, tc.want, diff)
			}
		})
	}
}

func TestMinHeap(t *testing.T) {
	steps := []Step{
		{End: 5 * time.Second},                              // Duration = 5s
		{End: 15 * time.Second},                             // Duratoin = 15s
		{Start: 10 * time.Second, End: 20 * time.Second},    // Duration = 10s
		{Start: 199 * time.Second, End: 200 * time.Second},  // Duration = 1s
		{Start: 100 * time.Second, End: 1000 * time.Second}, // Duration = 900s
	}

	mh := new(minHeap)
	for _, step := range steps {
		heap.Push(mh, step)
	}
	var gotHeapSortedSteps []Step
	for mh.Len() > 0 {
		gotHeapSortedSteps = append(gotHeapSortedSteps, heap.Pop(mh).(Step))
	}

	wantSteps := []Step{
		{Start: 199 * time.Second, End: 200 * time.Second},  // Duration = 1s
		{End: 5 * time.Second},                              // Duration = 5s
		{Start: 10 * time.Second, End: 20 * time.Second},    // Duration = 10s
		{End: 15 * time.Second},                             // Duration = 15s
		{Start: 100 * time.Second, End: 1000 * time.Second}, // Duration = 900s
	}
	if !cmp.Equal(gotHeapSortedSteps, wantSteps) {
		t.Errorf("Got heap sorted steps: %#v, want: %#v", gotHeapSortedSteps, wantSteps)
	}
}

func TestSlowestSteps(t *testing.T) {
	for _, tc := range []struct {
		desc  string
		steps []Step
		n     int
		want  []Step
	}{
		{
			desc: "empty",
		},
		{
			desc:  "n is 0",
			steps: []Step{{End: time.Second}},
			n:     0,
		},
		{
			desc: "top 1",
			steps: []Step{
				{End: time.Second},
				{End: 20 * time.Second},
				{End: 3 * time.Second},
				{Start: 30 * time.Second, End: 40 * time.Second},
				{End: 5 * time.Second},
			},
			n:    1,
			want: []Step{{End: 20 * time.Second}},
		},
		{
			desc: "top 3",
			steps: []Step{
				{End: time.Second},
				{Start: 19 * time.Second, End: 20 * time.Second},
				{End: 3 * time.Second},
				{Start: 10 * time.Second, End: 40 * time.Second},
				{End: 5 * time.Second},
			},
			n: 3,
			want: []Step{
				{Start: 10 * time.Second, End: 40 * time.Second},
				{End: 5 * time.Second},
				{End: 3 * time.Second},
			},
		},
		{
			desc:  "steps less than n",
			steps: []Step{{End: time.Second}},
			n:     100,
			want:  []Step{{End: time.Second}},
		},
	} {
		t.Run(tc.desc, func(t *testing.T) {
			got := SlowestSteps(tc.steps, tc.n)
			if diff := cmp.Diff(tc.want, got); diff != "" {
				t.Errorf("SlowestSteps(%#v, %d) = %v\nwant: %v\ndiff(-want +got):\n%s", tc.steps, tc.n, got, tc.want, diff)
			}
		})
	}
}
