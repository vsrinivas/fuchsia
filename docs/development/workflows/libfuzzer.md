# LibFuzzer in Fuchsia

## Quick-start guide

1. Pass fuzzed inputs to your library by implementing
[LLVMFuzzerTestOneInput](#q-what-do-i-need-to-write-to-create-a-fuzzer-).
1. Add a fuzzer build rule:
  * Add a [`fuzzer`][gn fuzzer] to the appropriate BUILD.gn.
  * Fuchsia: Create or extend a [`fuzzers_package`][gn fuzzers package] in an appropriate BUILD.gn.
  * Ensure there's a path from  a top-level target, e.g. `//bundles:tests`, to your fuzzers package.
1. Configure, build, and boot (with networking), e.g.:
  * `fx set core.x64 --fuzz-with asan --with //bundles:tests`
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
  * `found-by-libfuzzer`
  * `SecTriageMe`

[TOC]

## Q: What is fuzzing?

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

## Q: What is libFuzzer?

A: [LibFuzzer] is an in-process fuzzing engine integrated within LLVM as a compiler runtime.
[Compiler runtimes][compiler-rt] are libraries that are invoked by hooks that compiler adds to the
code it builds.  Other examples include [sanitizers] such as [ASan], which detects certain overflows
and memory corruptions. LibFuzzer uses these sanitizers both for [coverage data][sancov] provided by
sanitizer-common, as well as to detect when inputs trigger a defect.

## Q: What do I need to write to create a fuzzer?

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
![Coverage guided fuzzing](../../images/fuzzing/coverage-guided.png)

Developer-provided components are in green.

## Q: What should I fuzz with libFuzzer?

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

### Q: How do I fuzz more complex interfaces?

A: It is easy to map portions of the provided `Data` to ["plain old data" (POD)][pod] types.  The
data can also be sliced into variable length arrays. More complex objects can almost always be
(eventually) built out of POD types and variable arrays. If `Size` isn't long enough for your needs,
you can simply return `0`.  The fuzzer will quickly learn that inputs below that length aren't
interesting and will stop generating them.

```cpp
  uint32_t flags;
  char name[MAX_NAME_LEN];
  if (Size < sizeof(flags)) {
    return 0;
  }
  memcpy(&flags, Data, sizeof(flags));
  Data += sizeof(flags);
  Size -= sizeof(flags);

  size_t name_len;
  if (Size < sizeof(name_len)) {
    return 0;
  }
  memcpy(&name_len, Data, sizeof(name_len));
  Data += sizeof(name_len);
  Size -= sizeof(name_len);
  name_len %= sizeof(name_len) - 1;

  if (Size < name_len) {
    return 0;
  }
  memcpy(name, Data, name_len);
  Data += name_len;
  Size -= name_len;
  name[name_len] = '\0';

  Parser parser(name, flags);
  parser.Parse(Data, Size);
```

*__NOTE__: A small library to make this easier is under development.*

In some cases, you may have expensive set-up operations that you would like to do once.  The
libFuzzer documentation has tips on how to do [startup initialization].  Be aware though that such
state will be carried over from iteration to iteration.  This can be useful as it may expose new
bugs that depend on the library's persisted state, but it may also make bugs harder to reproduce
when they depend on a sequence of inputs rather than a single one.

### Q: How should I scope my fuzzer?

A: In general, an in-process coverage-based fuzzer, iterations should be __short__ and __focused__.
The more focused a [fuzz target] is, the faster libFuzzer will be able to find "interesting" inputs
that increase code coverage.

At the same time, becoming __too__ focused can lead to a proliferation of fuzz targets.  Consider
the example of a routine that parses incoming requests.  The parser may recognize dozens of
different request types, so developing a separate fuzz target for each may be cumbersome.  An
alternative in this case may be to develop a single fuzzer, and include examples of the different
requests in the initial [corpus].  In this way the single fuzz target can still bypass a large
amount of shallow fuzzing by being guided towards the interesting inputs.

*__NOTE:__ Currently, libFuzzer can be used in Fuchsia to fuzz C/C++ code.   Additional language
support is [planned][todo].*

## Q: LibFuzzer isn't quite right; what else could I use?

A: There's many other fuzzing engines out there:
* If the code you want to fuzz isn't a library with linkable interfaces, but instead a standalone
binary, then [AFL] may be a be better suited.
  * *__NOTE:__ AFL support on Fuchsia is [not yest supported][todo].*
* If you want to fuzz a service via [FIDL] calls in the style of an integration test, consider using
[syzkaller]'s FIDL support.
  * *__NOTE:__ FIDL support is in [development][todo].*
* If none of these options fit your needs, you can still write a custom fuzzer and have it run
continuously under [ClusterFuzz].
  * *__NOTE:__ ClusterFuzz integration is in [development][todo].*

## Q: How do I create a Fuchsia fuzzer?

A: First, create your [fuzz target] function.  It's recommended that the fuzzer's target is clear
from file name.  If the library code already has a directory for unit tests, you should use a
similar directory for your fuzzing targets.  If not, make sure the file's name clearly reflects it
is a fuzzer binary.  In general, use naming and location to make the fuzzer easy to find and its
purpose clear.

_Example:_ A fuzzer for `//garnet/lib/cmx` might be located at
`//garnet/lib/cmx/cmx_fuzzer.cc`, to match `//garnet/lib/cmx/cmx_unittest.cc`.

Libfuzzer already [provides tips][fuzz target] on writing the fuzz target function itself.

Next, add the build instructions to the library's BUILD.gn file.  Adding an import to
[//build/fuzzing/fuzzer.gni][fuzzer.gni] will provide two templates:

### The fuzzer GN template

The `fuzzer` [template][fuzzer.gni] is used to build the fuzzer executable.  Given a fuzz target
function in a source file and a the library under test as a dependency, it will provided the correct
[compiler flags] to link against the fuzzing engine:

```python
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

### The fuzzers_package GN template

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
$ fx set core.x64 --fuzz-with asan --with //bundles:tests
$ fx build
```

Zircon fuzzers will be built with all supported sanitizers automatically.

## Q: How do I run a fuzzer?

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

`package` and `fuzzer` match those reported by `fx fuzz list`, and may be abbreviated.  For commands
that accept a single fuzzer, e.g. `check`, the abbreviated name must uniquely identify exactly one
fuzzer.

When starting a fuzzer, the tool will echo the command it is invoking, prefixed by `+`.  This can be
useful if you need to manually reproduce the bug with modified libFuzzer [options].

## Q: How can I reproduce crashes found by the fuzzer?

A: Use the [fx fuzz tool]:
* To check on the fuzzer and list found artifacts:
  `fx fuzz check [package]/[fuzzer]`
* To run the fuzzer on just the found artifacts:
  `fx fuzz repro [package]/[fuzzer]`

The test artifact are also copied to `//test_data/fuzzing/<package>/<fuzzer>/<timestamp>`. The most
  recent fuzzer run is symbolically linked to `//test_data/fuzzing/<package>/<fuzzer>/latest`.

As with `fx fuzz start`, the fuzzer will echo the command it is invoking, prefixed by `+`.  This can
be useful if you need to manually reproduce the bug with modified parameters.

## Q: What should I do with these bugs?

A: File them, then fix them!

*__NOTE:__ The bug tracker is currently only open to Googlers.*

When filing bugs, __please__ use both the custom `found-by-libfuzzer` label, as well as the custom
`Sec-TriageMe` label.  This will help the security team see where fuzzers are being used and stay
aware of any critical issues they are finding.

As with other potential security issues, bugs should be filed __in the project of the code under
test__ (and __not__ in the [security project]).  Conversely, if you encounter problems or
shortcomings in the fuzzing framework _itself_, please __do__ open bugs or feature requests in the
[security project] with the label `libFuzzer`.

As with all potential security issues, don't wait for triage to begin fixing the bug!  Once fixed,
don't forget to link to the bug in the commit message.  This may also be a good time to consider
minimizing and uploading your corpus at the same time (see the next section).

## Q: How do I manage my corpus?

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

## Q: Can I use an existing third party corpus?

A: Yes! by , and then performing a normal corpus update:
1. Fetch from a directory rather than CIPD:
* `fx fuzz fetch [package]/[fuzzer] /path/to/third/party/corpus`
1. Upload the corpus to [CIPD].
* `fx fuzz merge [package]/[fuzzer]`

## Q: Can I run my fuzzer on host?

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

## Q: How do make my fuzzer better?

A: Once crashes begin to become infrequent, it may be because almost all the bugs have been
fixed, but it may also be because the fuzzer isn't reaching new code that still has bugs.  Code
coverage information is needed to determine the quality of the fuzzer.  Use
[source-based code coverage] to see what your current corpus reaches.
  * *__NOTE:__ Source-based code coverage is under [active development][todo].*

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
[GN fuzzer] in the appropriate project.  At this point, others will be able use your fuzzer,
This includes [ClusterFuzz] which will automatically find new fuzzers and continuously fuzz them,
updating their corpora, filing bugs for crashes, and closing them when fixed.
  * *__NOTE:__ ClusterFuzz integration is in [development][todo].*

## Q: What can I expect in the future for fuzzing in Fuchsia?

A: As you can see from the various notes in this document, there's still plenty more to do!
* Add additional language support, e.g for [Rust][rust-fuzzing] and [Go][go-fuzzing].
* Add support for [AFL]  on Fuchsia.  Some design questions need to be worked out, as processes will
not typically be run executed from the shell in the long term.
* Continue work on fuzzing FIDL via [syzkaller] and other efforts.
* Integrate with [ClusterFuzz].  Eventually this *may* be extended to include [OSS-Fuzz] as well.
* Provide source-based code coverage.

We will continue to work on these features and others, and update this document accordingly as they
become available.

[libFuzzer]: https://llvm.org/docs/LibFuzzer.html
[compiler-rt]: https://compiler-rt.llvm.org/
[sanitizers]: https://github.com/google/sanitizers
[asan]: https://clang.llvm.org/docs/AddressSanitizer.html
[sancov]: https://clang.llvm.org/docs/SanitizerCoverage.html
[fuzz target]: https://llvm.org/docs/LibFuzzer.html#fuzz-target
[afl]: http://lcamtuf.coredump.cx/afl/
[FIDL]: ../languages/fidl/README.md
[syzkaller]: https://github.com/google/syzkaller
[todo]: #q-what-can-i-expect-in-the-future-for-fuzzing-in-fuchsia-
[thin-air]: https://lcamtuf.blogspot.com/2014/11/pulling-jpegs-out-of-thin-air.html
[startup initialization]: https://llvm.org/docs/LibFuzzer.html#startup-initialization
[fuzzer.gni]: /build/fuzzing/fuzzer.gni
[build macro]: https://llvm.org/docs/LibFuzzer.html#fuzzer-friendly-build-mode
[compiler flags]: /build/config/sanitizers/BUILD.gn
[corpus]: https://llvm.org/docs/LibFuzzer.html#corpus
[3p-corpus]: #q-can-i-use-an-existing-third-party-corpus-
[dictionaries]: https://llvm.org/docs/LibFuzzer.html#dictionaries
[options]: https://llvm.org/docs/LibFuzzer.html#options
[fuzz tool]: #q-how-do-i-run-a-fuzzer-
[gn fuzzer]: #the-fuzzer-gn-template
[gn fuzzers package]: #the-fuzzers_package-gn-template
[source-based code coverage]: https://clang.llvm.org/docs/SourceBasedCodeCoverage.html
[clusterfuzz]: https://github.com/google/oss-fuzz/blob/master/docs/clusterfuzz.md
[rust-fuzzing]: https://github.com/rust-fuzz/libFuzzer-sys
[go-fuzzing]: https://github.com/dvyukov/go-fuzz
[ubsan]: https://clang.llvm.org/docs/UndefinedBehaviorSanitizer.html
[oss-fuzz]: https://github.com/google/oss-fuzz
[cipd]: https://chrome-infra-packages.appspot.com/p/fuchsia/test_data/fuzzing
[sec-144]: https://fuchsia.atlassian.net/browse/SEC-144
[tc-241]: https://fuchsia.atlassian.net/browse/TC-241
[pod]: http://www.cplusplus.com/reference/type_traits/is_pod/
[security project]: https://fuchsia.atlassian.net/browse/SEC
[latest corpus]: #q-how-do-i-manage-my-corpus-
[fx fuzz tool]: #q-how-do-i-run-a-fuzzer-
[fuzzer scope]: #q-how-should-i-scope-my-fuzzer?
