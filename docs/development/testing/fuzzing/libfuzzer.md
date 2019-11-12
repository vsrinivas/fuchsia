# Fuzz testing in Fuchsia with LibFuzzer

Fuzzing is a testing technique that feeds auto-generated inputs to a piece of
target code in an attempt to crash the code. This technique finds security
vulnerabilities and stability bugs that other testing might miss. You can see
Fuchsia fuzzing trophies in Monorail by using the
[`component:Security>clusterfuzz
reporter:clusterfuzz@chromium.org`](https://bugs.fuchsia.dev/p/fuchsia/issues/list?colspec=ID%20jira_id%20Component%20Type%20Pri%20Status%20Owner%20Summary%20Modified&q=component%3ASecurity%3Eclusterfuzz%20reporter%3Aclusterfuzz%40chromium.org&can=2)
filter.

This guide focuses on [LibFuzzer](#q-what-is-libfuzzer), an in-process fuzzing
engine.

## Quick-start guide

1. Pass fuzzed inputs to your library by implementing
[LLVMFuzzerTestOneInput](#q-what-do-i-need-to-write-to-create-a-fuzzer).
1. Add a fuzzer build rule:
  * Add a [`fuzzer`][gn fuzzer] to the appropriate BUILD.gn.
  * Fuchsia: Create or extend a [`fuzzers_package`][gn fuzzers package] in an appropriate BUILD.gn.
  * Ensure there's a path from  a top-level target, e.g. `//bundles:tests`, to your fuzzers package.
1. Configure, build, and boot (with networking), e.g.:
  * _if a Fuchsia instance is not already running:_ `fx qemu -N`
  * `fx set core.x64 --fuzz-with asan --with //bundles:tests --with //garnet/packages/products:devtools`
  * `fx build`
  * `fx serve`
1. Use the fuzzer tool:
  * To display fuzzers:
    `$ fx fuzz list`
  * To start a fuzzer.
    `$ fx fuzz <fuzzer>`
  * To see if the fuzzer found crashes.
    `$ fx fuzz check <fuzzer>`
  * To replay a crash.
    `$ fx fuzz repro <fuzzer> [crash]`
1. File bug using the following labels:
  * `found-by-fuzzing`
  * `Sec-TriageMe`
  * `libfuzzer`

[TOC]

## Q: What is fuzzing? {#q-what-is-fuzzing}

A: Fuzzing or fuzz testing is style of testing that stochastically generates inputs to targeted
interfaces in order to automatically find defects and/or vulnerabilities.  In this document,
a distinction will be made between two components of a fuzzer: the fuzzing engine, which produces
context-free inputs, and the fuzz target function, which submits those inputs to a specific
interface.

Among the various styles of fuzzing, coverage-based fuzzing has been shown to yield a particularly
high number of bugs for the effort involved.  In coverage-based fuzzing, the code under test is
instrumented for coverage. The fuzzing engine can observe when inputs increase the overall code
coverage and use those inputs as the basis for generating further inputs.  This group of "seed"
inputs is collectively referred to as a corpus.

## Q: What is libFuzzer? {#q-what-is-libfuzzer}

A: [LibFuzzer] is an in-process fuzzing engine integrated within LLVM as a compiler runtime.
[Compiler runtimes][compiler-rt] are libraries that are invoked by hooks that compiler adds to the
code it builds.  Other examples include [sanitizers] such as [ASan], which detects certain overflows
and memory corruptions. LibFuzzer uses these sanitizers both for [coverage data][sancov] provided by
sanitizer-common, as well as to detect when inputs trigger a defect.

## Q: What do I need to write to create a fuzzer? {#q-what-do-i-need-to-write-to-create-a-fuzzer}

A: LibFuzzer can be used to make a coverage-based fuzzer binary by combining it with a sanitized
library and the implementation of the [fuzz target] function:

```cpp
extern "C" int LLVMFuzzerTestOneInput(const uint8_t *Data, size_t Size) {
  // Use the data to do something interesting with your API
  return 0;
}
```

Optionally, you can also add an initial [corpus].  Without it, libFuzzer will start from an empty
fuzzer and will (eventually) learn how to make appropriate inputs [on its own][thin-air].

LibFuzzer then be able to generate, submit, and monitor inputs to the library code:
![Coverage guided fuzzing](/docs/images/fuzzing/coverage-guided.png)

Developer-provided components are in green.

## Q: What should I fuzz with libFuzzer? {#q-what-should-i-fuzz-with-libfuzzer}

A: Coverage based fuzzing works best when fuzzing targets resemble [unit tests][fuzzer scope].  If
your code is already organized to make it easy to unit test, you can add targets for each of the
interfaces being tested., e.g. something like:

```cpp
  // std::string json = ...;
  Metadata metadata;
  EXPECT_TRUE(metadata.Parse(json));
```

becomes:

```cpp
extern "C" int LLVMFuzzerTestOneInput(const uint8_t *Data, size_t Size) {
  std::string json(static_cast<const char *>(Data), Size);
  metadata.Parse(json);
  return 0;
}
```

With a corpus of JSON inputs, `Data` may be close to what the `Metadata` object expects to parse.
If not, the fuzzer will eventually discover what inputs are meaningful to it through random
mutations, trial and error, and code coverage data.

### Q: How do I fuzz more complex interfaces?  {#q-how-do-i-fuzz-more-complex-interfaces}

A: The [`FuzzedDataProvider`][fuzzed-data-provider] library helps you map portions of the provided
`Data` to ["plain old data" (POD)][pod] types. More complex objects can almost
always be (eventually) built out of POD types and variable arrays.

```cpp
  #include <fuzzer/FuzzedDataProvider.h>

  extern "C" int LLVMFuzzerTestOneInput(const uint8_t *Data, size_t Size) {
    FuzzedDataProvider fuzzed_data(Data, Size);

    auto flags = fuzzed_data.ConsumeIntegral<uint32_t>();
    auto name_len =
         fuzzed_data.ConsumeIntegralInRange<size_t>(0, MAX_NAME_LEN - 1);

    std::string name = fuzzed_data.ConsumeBytesAsString(name_len);

    Parser parser(name.c_str(), flags);

    auto remaining = fuzzed_data.ConsumeRemainingBytes<char>();
    parser.Parse(remaining.data(), remaining.size());
    return 0;
  }
```

Note that using this library for splitting your data might make it harder for
you to provide a corpus for your fuzzer, as the splitting happens dynamically.
Other alternatives are explored in the [split inputs] documentation.


In some cases, you may have expensive set-up operations that you would like to do once.  The
libFuzzer documentation has tips on how to do [startup initialization].  Be aware though that such
state will be carried over from iteration to iteration.  This can be useful as it may expose new
bugs that depend on the library's persisted state, but it may also make bugs harder to reproduce
when they depend on a sequence of inputs rather than a single one.

### Q: What if my object expects more `Data` than what libfuzzer provides? {#q-what-if-size-is-too-small}

If `Size` isn't long enough for your needs, you can simply `return 0;`. The
fuzzer will quickly learn that inputs below that length aren't interesting and
will stop generating them.

By default, libfuzzer generates inputs with a maximum size of 4096. If you
need to generate larger inputs, you can provide the `-max_len` flag to `fx fuzz
start`. If you provide a corpus input large enough, libfuzzer will increase the
maximum size to that corpus size.

### Q: How should I scope my fuzzer? {#q-how-should-i-scope-my-fuzzer}

A: In general, an in-process coverage-based fuzzer, iterations should be __short__ and __focused__.
The more focused a [fuzz target] is, the faster libFuzzer will be able to find "interesting" inputs
that increase code coverage.

At the same time, becoming __too__ focused can lead to a proliferation of fuzz targets.  Consider
the example of a routine that parses incoming requests.  The parser may recognize dozens of
different request types, so developing a separate fuzz target for each may be cumbersome.  An
alternative in this case may be to develop a single fuzzer, and include examples of the different
requests in the initial [corpus].  In this way the single fuzz target can still bypass a large
amount of shallow fuzzing by being guided towards the interesting inputs.

Note: Currently, libFuzzer can be used in Fuchsia to fuzz C/C++ code. Additional language
support is [planned][todo].

## Q: LibFuzzer isn't quite right; what else could I use? {#q-libfuzzer-isnt-quite-right-what-else-could-i-use}

A: There's many other fuzzing engines out there:

* If the code you want to fuzz isn't a library with linkable interfaces, but instead a standalone
binary, then [AFL] may be a be better suited.

Note: AFL support on Fuchsia is [not yet supported][todo].

* If you want to fuzz a service through [FIDL] calls in the style of an integration
  test, see [Fuzzing FIDL Servers with LibFuzzer on Fuchsia][fidl_fuzzing].

* If none of these options fit your needs, you can still write a custom fuzzer and have it run
continuously under [ClusterFuzz].

## Q: How do I create a Fuchsia fuzzer? {#q-how-do-i-create-a-fuchsia-fuzzer}

A: First, create your [fuzz target] function.  It's recommended that the fuzzer's target is clear
from file name.  If the library code already has a directory for unit tests, you should use a
similar directory for your fuzzing targets.  If not, make sure the file's name clearly reflects it
is a fuzzer binary.  In general, use naming and location to make the fuzzer easy to find and its
purpose clear.

_Example:_ A fuzzer for `//src/lib/cmx` might be located at
`//src/lib/cmx/cmx_fuzzer.cc`, to match `//src/lib/cmx/cmx_unittest.cc`.

Libfuzzer already [provides tips][fuzz target] on writing the fuzz target function itself.

Next, add the build instructions to the library's BUILD.gn file.  Adding an import to
[//build/fuzzing/fuzzer.gni][fuzzer.gni] will provide two templates:

### The fuzzer GN template {#the-fuzzer-gn-template}

The `fuzzer` [template][fuzzer.gni] is used to build the fuzzer executable.  Given a fuzz target
function in a source file and the library under test as a dependency, it will provided the correct
[compiler flags] to link against the fuzzing engine:

```python
import("//build/fuzzing/fuzzer.gni")

fuzzer("cowsay_simple_fuzzer") {
  sources = [ "cowsay_fuzzer.cpp" ]
  deps = [ ":cowsay_sources" ]
}
```

It also enables the  `FUZZING_BUILD_MODE_UNSAFE_FOR_PRODUCTION` [build macro].  If the software
under test needs fuzzing-specific modifications, they can be wrapped in a preprocessor conditional
on this macro, e.g.:

```cpp
#ifdef FUZZING_BUILD_MODE_UNSAFE_FOR_PRODUCTION
  srand(++global_counter);
  rand_int = rand();
#else
  zx_cprng_draw(&rand_int, size_of(rand_int));
#endif
```

This can be useful to allow either more deterministic fuzzing and/or deeper coverage.

The fuzzer template also allows you include additional inputs to control the fuzzer:

* [Dictionaries] are files with tokens, one per line, that commonly appear in the target's input,
e.g. "GET" and "POST" for HTTP.
* An options file, made up a series of key-value pairs, one per line, of libFuzzer command line
[options].

```python
import("//build/fuzzing/fuzzer.gni")

fuzzer("cowsay_simple_fuzzer") {
  sources = [ "cowsay_fuzztest.cpp" ]
  deps = [ ":cowsay_sources" ]
  dictionary = "test_data/various_moos.dict"
  options = "test_data/fuzzer.opts"
}
```

When you use the [fx fuzz tool], libFuzzer's `merge`, `jobs`, `dict`, and `artifact_prefix` options
are set automatically. You do not need to specify these options unless they differ from the default
values.

### The fuzzers_package GN template {#the-fuzzers-package-gn-template}

The `fuzzers_package` [template][fuzzer.gni] bundles fuzzers into a Fuchsia package in the same way
that a normal
package bundles binaries.

```python
fuzzers_package("cowsay_fuzzers") {
  fuzzers = [ ":cowsay_simple_fuzzer" ]
}
```

By default, the package will support all sanitizers. This can be restricted by providing an optional
"sanitizers" list, e.g. `sanitizers = [ "asan", "ubsan" ]`

Once defined, a package needs to be included in the build dependency graph like any other test
package.  This typically means adding it to a group of tests, e.g. a `group("tests")` target.

__IMPORTANT__: The Fuchsia build system will build the fuzzers __only__ if it is explicitly told to
instrument them for fuzzing with an appropriate sanitizer.  The easiest way to achieve this is using
the `--fuzz-with <sanitizer>` flag with `fx set`, e.g:

```
$ fx set core.x64 --fuzz-with asan --with //bundles:tests --with //garnet/packages/products:devtools
$ fx build
```

## Q: How do I create a Zircon fuzzer? {#q-how-do-i-create-a-zircon-fuzzer}

Zircon has a different [fuzzer.gni template][fuzzer.gni] from the rest of Fuchsia, but is used similarly:

```python
import("$zx/public/gn/fuzzer.gni")

fuzzer("zx_fuzzer") {
  sources = [ "zx_fuzzer.cpp" ]
  deps = [ ":zx_sources" ]
}
```

Zircon fuzzers will be built with all supported sanitizers automatically. These fuzzers can be included in a
Fuchsia instance by including the `zircon_fuzzers` package, e.g.:

```
$ fx set core.x64 --with //garnet/tests/zircon:zircon_fuzzers --with //garnet/packages/products:devtools
$ fx build
```

Note that Zircon fuzzers *must* have names that end in "-fuzzer".

## Q: How do I run a fuzzer? {#q-how-do-i-run-a-fuzzer}

A: Use the `fx fuzz` tool which knows how to find fuzzing related files and various common options.


The fuzzer binary can be started directly, using the normal libFuzzer options, if you prefer.
However, it is easier to use the `fx fuzz` devshell tool, which understands where to look for
fuzzing related files and knows various common options.  Try one or more of the following:

* To see available commands and options:
  `$ fx fuzz help`
* To see available fuzzers:
  `$ fx fuzz list`
* To start a fuzzer:
  `fx fuzz [package]/[fuzzer]`

(Ignore errors of the form `Error: no such package.` These come from CIPD and should not affect the fuzzer!)

`package` and `fuzzer` match those reported by `fx fuzz list`, and may be abbreviated.  For commands
that accept a single fuzzer, e.g. `check`, the abbreviated name must uniquely identify exactly one
fuzzer.

When starting a fuzzer, the tool will echo the command it is invoking, prefixed by `+`.  This can be
useful if you need to manually reproduce the bug with modified libFuzzer [options].

## Q: How can I reproduce crashes found by the fuzzer? {#q-how-can-i-reproduce-crashes-found-by-the-fuzzer}

A: Use the [fx fuzz tool]:

* To check on the fuzzer and list found artifacts:
  `fx fuzz check [package]/[fuzzer]`
* To run the fuzzer on just the found artifacts:
  `fx fuzz repro [package]/[fuzzer]`

The test artifact are also copied to `//test_data/fuzzing/<package>/<fuzzer>/<timestamp>`. The most
  recent fuzzer run is symbolically linked to `//test_data/fuzzing/<package>/<fuzzer>/latest`.

As with `fx fuzz start`, the fuzzer will echo the command it is invoking, prefixed by `+`.  This can
be useful if you need to manually reproduce the bug with modified parameters.

## Q: What should I do with these bugs? {#q-what-should-i-do-with-these-bugs}

A: File them, then fix them!

Note: The bug tracker is currently only open to Googlers.

When filing bugs, __please__ use the following custom labels: `found-by-fuzzing`, `libfuzzer`
and `Sec-TriageMe`. This will help the security team see where fuzzers are being used and stay
aware of any critical issues they are finding.

As with other potential security issues, bugs should be filed __in the component of the code under
test__ (and __not__ in the [security component]).  Conversely, if you encounter problems or
shortcomings in the fuzzing framework _itself_, please __do__ open bugs or feature requests in the
[security component] with the label `libFuzzer`.

As with all potential security issues, don't wait for triage to begin fixing the bug!  Once fixed,
don't forget to link to the bug in the commit message.  This may also be a good time to consider
minimizing and uploading your corpus at the same time (see the next section).

## Q: How do I manage my corpus? {#q-how-do-i-manage-my-corpus}

A: When you first begin fuzzing a new target, the fuzzer may crash very quickly.  Typically, fuzzing
has a large initial spike of defects found followed by a long tail.  Fixing these initial, shallow
defects will allow your fuzzer to reach deeper and deeper into the code. Eventually your fuzzer will
run for several hours (e.g. overnight) without crashing.  At this point, you will want to save the
[corpus].

To do this, use the [fx fuzz tool]:
  `fx fuzz merge <package>/<fuzzer>`

This will pull down the current corpus from [CIPD], merge it with your corpus on the device,
minimize it, and upload it to [CIPD] as the *new* latest corpus.

When uploaded, the corpus is tagged with the current revision of the integration branch.  If needed,
you can retrieve older versions of the corpus relating to a specific version of the code:
  `fx fuzz fetch <package>/<fuzzer> <integration-revision>`

## Q: Can I use an existing third party corpus?  {#q-can-i-use-an-existing-third-party-corpus}

A: Yes! by fetching the corpus, and then performing a normal corpus update:

1. Fetch from a directory rather than CIPD:
* `fx fuzz fetch --no-cipd --staging /path/to/third/party/corpus [package]/[fuzzer]`
1. Upload the corpus to [CIPD].
* `fx fuzz merge [package]/[fuzzer]`

## Q: Can I run my fuzzer on host? {#q-can-i-run-my-fuzzer-on-host}

A: Yes, although the extra tooling of `fx fuzz` is not currently supported.  This means you can
build host fuzzers with the GN templates, but you'll need to manually run them, reproduce the bugs
they find, and manage their corpus data.

If your fuzzers don't have Fuchsia dependencies, you can build host versions simply by setting
`fuzz_host=true` in the `fuzzers_package`[gn fuzzers package]:

```python
fuzzers_package("overnet_fuzzers") {
  fuzzers = [ "packet_protocol:packet_protocol_fuzzer" ]
  fuzz_host = true
}
```

Upon building, the host fuzzers with can be found in in the host variant output directory, e.g.
`//out/default/host_x64-asan-fuzzer`.

## Q: How do I make my fuzzer better? {#q-how-do-i-make-my-fuzzer-better}

A: Once crashes begin to become infrequent, it may be because almost all the bugs have been
fixed, but it may also be because the fuzzer isn't reaching new code that still has bugs.  Code
coverage information is needed to determine the quality of the fuzzer.  Use
[source-based code coverage] to see what your current corpus reaches.

Note: Source-based code coverage is under [active development][todo].

If coverage in a certain area is low, there are a few options:

  * Improve the [corpus].  If there are types of inputs that aren't represented well, add some
  manually.  For code dealing with large inputs with complex types (e.g. X.509 certificates), you
  probably want to provide an initial corpus from the start.
  * Add a [dictionary][dictionaries].  If the code deals with data that has a certain grammar (e.g.
  HTML), adding that grammar in a dictionary allows the fuzzer to produce more meaningful inputs
  faster.
  * Disable uninteresting shallow checks.  A function that verifies a checksum before proceeding is
  hard to fuzz, even though a maliciously crafted input may be easy enough to construct.  You can
  disable such checks by wrapping them in the `FUZZING_BUILD_MODE_UNSAFE_FOR_PRODUCTION`
  [build macro] described [above][gn fuzzer].

The "run, merge, measure, improve" steps can be repeated for as many iterations as you feel are
needed to create a quality fuzzer.  Once ready, you'll need to upload your corpus and update the
[GN fuzzer] in the appropriate project.  At this point, others will be able use your fuzzer.
This includes [ClusterFuzz] which will automatically find new fuzzers and continuously fuzz them,
updating their corpora, filing bugs for crashes, and closing them when fixed.

Note: ClusterFuzz integration is in [development][todo].

## Q: What can I expect in the future for fuzzing in Fuchsia? {#q-what-can-i-expect-in-the-future-for-fuzzing-in-fuchsia}

A: As you can see from the various notes in this document, there's still plenty more to do!

* Add additional language support, e.g for [Rust][rust-fuzzing] and [Go][go-fuzzing].
* Add support for [AFL]  on Fuchsia.  Some design questions need to be worked out, as processes will
not typically be run executed from the shell in the long term.
* Continue to improve FIDL fuzzing, through both [libFuzzer][fidl_fuzzing] and [syzkaller].
* *Maybe* extend [Clusterfuzz] work to include [OSS-Fuzz] as well.
* Provide source-based code coverage.

We will continue to work on these features and others, and update this document accordingly as they
become available.

[3p-corpus]: #q-can-i-use-an-existing-third-party-corpus
[afl]: http://lcamtuf.coredump.cx/afl/
[asan]: https://clang.llvm.org/docs/AddressSanitizer.html
[build macro]: https://llvm.org/docs/LibFuzzer.html#fuzzer-friendly-build-mode
[cipd]: https://chrome-infra-packages.appspot.com/p/fuchsia/test_data/fuzzing
[clusterfuzz]: https://github.com/google/oss-fuzz/blob/master/docs/further-reading/clusterfuzz.md
[compiler flags]: /build/config/sanitizers/BUILD.gn
[compiler-rt]: https://compiler-rt.llvm.org/
[corpus]: https://llvm.org/docs/LibFuzzer.html#corpus
[dictionaries]: https://llvm.org/docs/LibFuzzer.html#dictionaries
[FIDL]: /docs/development/testing/fuzzing/libfuzzer_fidl.md
[fidl_fuzzing]: /docs/development/testing/fuzzing/libfuzzer_fidl.md
[fuzz target]: https://llvm.org/docs/LibFuzzer.html#fuzz-target
[fuzz tool]: #q-how-do-i-run-a-fuzzer
[fuzzed-data-provider]: https://github.com/llvm/llvm-project/blob/master/compiler-rt/include/fuzzer/FuzzedDataProvider.h
[fuzzer scope]: #q-how-should-i-scope-my-fuzzer
[fuzzer.gni]: /build/fuzzing/fuzzer.gni
[fx fuzz tool]: #q-how-do-i-run-a-fuzzer
[gn fuzzer]: #the-fuzzer-gn-template
[gn fuzzers package]: #the-fuzzers-package-gn-template
[go-fuzzing]: https://github.com/dvyukov/go-fuzz
[issue 24866]: https://bugs.fuchsia.dev/p/fuchsia/issues/detail?id=24866
[issue 27001]: https://bugs.fuchsia.dev/p/fuchsia/issues/detail?id=27001
[latest corpus]: #q-how-do-i-manage-my-corpus
[libFuzzer]: https://llvm.org/docs/LibFuzzer.html
[pod]: http://www.cplusplus.com/reference/type_traits/is_pod/
[options]: https://llvm.org/docs/LibFuzzer.html#options
[oss-fuzz]: https://github.com/google/oss-fuzz
[rust-fuzzing]: https://github.com/rust-fuzz/libFuzzer-sys
[sancov]: https://clang.llvm.org/docs/SanitizerCoverage.html
[sanitizers]: https://github.com/google/sanitizers
[security component]: https://bugs.fuchsia.dev/p/fuchsia/issues/list?q=component%3ASecurity
[source-based code coverage]: https://clang.llvm.org/docs/SourceBasedCodeCoverage.html
[split inputs]: https://github.com/google/fuzzing/blob/master/docs/split-inputs.md
[startup initialization]: https://llvm.org/docs/LibFuzzer.html#startup-initialization
[syzkaller]: https://github.com/google/syzkaller
[thin-air]: https://lcamtuf.blogspot.com/2014/11/pulling-jpegs-out-of-thin-air.html
[todo]: #q-what-can-i-expect-in-the-future-for-fuzzing-in-fuchsia
[ubsan]: https://clang.llvm.org/docs/UndefinedBehaviorSanitizer.html
[zircon_fuzzer.gni]: /zircon/public/gn/fuzzer.gni
