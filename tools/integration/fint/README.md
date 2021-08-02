# fint

`fint` provides a common high-level interface for continuous integration
infrastructure and local developer tools to build Fuchsia.

## CLI

fint has two subcommands, intended to be run in succession:

1. `set`, which runs `gn gen`. `fint set` shares code with `fx set` (see
   `//tools/build/fx-set/cmd/main.go`). `set` also does some GN analysis to
   determine whether the build graph is affected based on the files that are
   modified by a change under test. The caller will use that result to decide
   whether to run `fint build` afterward.
2. `build`, which runs `ninja` against a build directory that has
   already been configured by `fint set`. After running `ninja`, `build`
   validates the build outputs (e.g. checking that a subsequent Ninja run would
   be a no-op) and performs some build graph analysis to determine which tests
   are affected by a change, given a set of changed files.

The code for the CLI is nested under `cmd/fint` within the main `fint` directory
so that `go build` will output an executable named `fint` by default. If it were
directly within the `cmd` directory, then the default executable name would
confusingly be `cmd` rather than `fint`.

## Bootstrapping

fint is the entrypoint to the Fuchsia build system, so it's undesirable to build
fint using GN and Ninja, as that would mandate the existence of two separate GN
and Ninja codepaths – one to build fint, and another within fint to build
everything else.

Instead we use a bootstrapping script, `//tools/integration/bootstrap.sh`, to
build fint using the prebuilt Go toolchain, circumventing GN and Ninja entirely.
The infrastructure runs this script to produce the fint executable that it uses
to do the main Fuchsia build. (Building fint using the prebuilt Go toolchain is
also an order of magnitude faster than using GN and Ninja, so bootstrapping
introduces minimal latency to infrastructure builds.)

However, we also enforce that fint is buildable with Ninja as part of the normal
Fuchsia build system, making it possible to build and run fint's unit tests just
like any other host tests.

## Inputs

Aside from fint's command-line API, the interface between fint and the
infrastructure is entirely composed of a set of input and output files whose
schemas are defined by .proto files checked in under
`//tools/integration/fint/proto`.

`set` and `build` both take two protobufs as input. Generally the same two
protobuf files will be passed to `fint set` and `fint build`, although this is
not a hard requirement.

### static.proto

The `static` input proto contains all of the input parameters that can be known
ahead of time, outside the context of a specific machine or a specific
infrastructure build.

For example, parameters like `board` and `product` are static relative to a
given build configuration and don't change between builds.

Most of the `static.proto` fields correspond directly to specific GN arguments,
such as `base_packages`, or to GN files to include (for example, `board`
is a path to a file under `//boards` to import in the args passed to `gn gen`).
Other fields like `include_host_tests` determine which Ninja targets `fint
build` should build.

The `static` input is sometimes referred to as just the "fint parameters" or
"fint params" since it's the more prominent of the two input files: it's the
only one that's checked into version control, and it contains most of the inputs
that engineers tend to care about for debugging.

The static protobuf files are generated alongside the other infrastructure
configs in the internal integration repository, using [lucicfg][lucicfg], and
checked into version control. Then the infrastructure passes the static proto
into fint directly from the checkout.

### context.proto

Unfortunately, not all inputs to fint can be determined statically ahead of
time. Some parameters can only be computed in the context of a specific
infrastructure build, or on a specific machine.

For example, the absolute directory at which Fuchsia is checked out may vary
between infrastructure builds and different developers' workstations, so it must
be computed dynamically and passed into fint as the `checkout_dir` field.
Likewise, the client must be able to choose their own build directory path and
pass it in via the `build_dir` field.

When running in a presubmit infrastructure build, fint needs to know the set of
files affected by the change under test to determine whether the change actually
affects the build graph, and if so, which tests (if any) are affected by the
change. The set of changed files will vary depending on which change is being
tested (and there won't be any changed files in local runs or in post-submit
builds), so `changed_files` must also be passed in via the context proto.

**Before adding a new field to `context.proto`, thoroughly consider whether
it would be possible to configure the field statically instead. Any fint
behavior configured by fields in `context.proto` cannot be reproduced locally on
developer workstations, leading to divergence between local and infrastructure
workflows. So the contents of `context.proto` should be kept to a bare
minimum.**

### Input encoding

Inputs protos are encoded in [textproto form][textproto-spec].

When choosing an encoding for the inputs the primary consideration was
readability, because these files are checked into version control and are often
read by humans (for example, when reviewing changes to the files or debugging
the inputs to a build).

- Binary encoding is not an option because it's not human-readable.
- JSON is less concise than textproto, and changes tend to have noisier diffs
  due to extra punctuation and lack of trailing commas.
- Textproto is very concise and is also consistent with other files generated by
  lucicfg.

## Outputs

`fint set` and `fint build` each produce some data that needs to be passed back
to the caller. Of course, their most important byproducts are the files
contained in the build directory after running each command. But the
infrastructure also needs access to some information that's not encapsulated in
the build directory, so we need a dedicated output file for each command.

Each command writes its output data to a temporary file located outside the
build directory (specifically, in the `artifact_dir` specified by the context
proto), since the build directory should only ever contain files produced by GN
and Ninja.

### set_artifacts.proto

`fint set` emits a file called `set_artifacts.json` containing a message
formatted as [JSON][jsonpb-spec] that conforms to the schema of
`set_artifacts.proto`.

`set_artifacts.proto` contains information about what GN arguments were set, in
the `metadata` field. The infrastructure uses this information to tag tests with
information about the build configuration that produced them.

The `metadata` fields also inform how the infrastructure runs some tests – for
example, it allocates more memory to emulators that run tests produced by builds
that used `variants` known to produce memory-intensive binaries. This is
somewhat of a breach of the platform-infrastructure interface, and ideally the
infrastructure wouldn't have any knowledge of specific variants or any other GN
argument values, so additional infrastructure logic like that should be avoided
in favor of emitting higher-level flags from fint that determine how the
infrastructure should be have.

On that note, some fields of `set_artifacts.proto` also inform higher-level
decisions made by the infrastructure, e.g. `enable_rbe` determines whether the
infrastructure starts an RBE daemon prior to running `fint build`.

### build_artifacts.proto

`fint build` emits a file called `build_artifacts.json` containing a message
formatted as [JSON][jsonpb-spec] that conforms to the schema of
`build_artifacts.proto`.

`build_artifacts.proto` contains mostly diagnostic information that's helpful
for humans trying to understand a build, such as `log_files` and
`ninja_duration_seconds`.

Other fields feed back into later infrastructure actions, e.g. the
infrastructure may run tests reported by `affected_tests` many times to detect
introduction of new flakiness.

### Output encoding

Output protos are encoded as [JSON][jsonpb-spec].

Ideally fint could encode outputs as textproto for consistency with the inputs.
However, these messages pass data from fint back to the caller, but the source
of truth for the proto definitions is in fint. Proto libraries don't have good
support for deserializing unknown fields in textprotos, so if we used textproto
then introducing a new field to one of the output protos would require two
separate changes to fint: one to add the field to the .proto file, then another
to have fint actually set the field, which can only be landed after the updated
.proto definition has been propagated to all consumers.

On the other hand, all proto libraries support deserializing JSON messages that
contain unknown fields, so with JSON encoding it's possible to add a new .proto
field and start populating it in a single atomic fint change.

Readability is *useful* for outputs - so binary encoding is, again, not an
option – but it's not absolutely vital since the outputs are not checked into
version control and only need to be read very occasionally (and normally only by
people making changes to fint).

## Development

### Making proto file changes

#### Proto field numbers

The fint protobufs are only ever encoded as JSON or textproto, and neither
format is sensitive to proto field numbers or field ordering. However, the
encodings are sensitive to field names. (This is the complete opposite of binary
protobuf encoding, which keys by field number.)

Since we never encode or transmit fint protos as binary, the fields of all fint
protobuf can be renumbered arbitrarily, so we can prioritize readability and
cleanliness of the .proto files without worrying about backwards compatibility
of field numbers.

Follow these guidelines when modifying fint protos:

- When adding a new field that's closely related to an existing field, prefer to
  declare the new field close to the existing field and increment all later
  field numbers.
- When deleting a field, delete it entirely rather than marking it as
  `reserved`, and decrement all later field numbers.

#### Regenerating generated code

After changing a .proto file, run `fx build` (make sure the changed proto is
included by your `fx set`), and follow the prompt to update the corresponding
generated code.

Note that when adding a new field to a protobuf, you'll need to add the field
and then update the generated code *before* referencing the new field in fint's
code. This is because `fx set` uses (and builds) fint itself under the hood, and
if fint references a protobuf field that doesn't yet exist in the generated Go
code then it will fail to compile.

On the other hand, it *is* safe to delete a protobuf field and then regenerate
the generated files even while the field is still referenced in fint. But
afterwards, fint may fail to compile until you remove references to the deleted
field from fint's source code.

#### Copying proto files to downstream repositories

A Google-internal [Copybara][copybara] workflow automatically propagates fint
.proto file changes to the other repositories where they're used, so there's
generally no need to manually copy-paste the files. However, there's no harm in
manually copy-pasting if you wish to work on a corresponding downstream change
while waiting for a .proto change to land in fint (just make sure that the fint
change lands before the downstream change).

`context.proto`, `set_artifacts.proto`, and `build_artifacts.proto` are copied
into the [recipes][fuchsia-recipes] repository so recipes can construct
`context` protobufs to pass to fint, and read `set_artifacts` and
`build_artifacts` protobufs produced by fint.

`static.proto` (more specifically the generated binary description file,
`static.desc.pb`) is copied into the internal integration repository so that
[lucicfg][lucicfg] can generate per-builder static protobufs that are checked
into version control.

### Why Go?

At this point, you may be wondering why fint is implemented in a compiled
language like Go, since it requires a bootstrapping step.

Go was chosen as an implementation language for fint because:

- Go is well-supported in Fuchsia's build system.
- It enables sharing utility libraries with other Go tools like `testsharder`
  for tasks such as reading build API files (see `//tools/build/modules.go`).
- Go is easy to learn, read, and write, making it an excellent choice for a tool
  like fint that is central to the Fuchsia build system and is touched by many
  different teams.
- Go is statically typed, making it safer and (arguably) more maintainable than
  a dynamic language like Python. fint contains a non-trivial amount of logic
  that could easily become unmaintainable if it were written in a dynamically
  typed language, given that it's constantly being modified by people with
  varying levels of familiarity with the codebase.

[copybara]: https://github.com/google/copybara
[jsonpb-spec]: https://developers.google.com/protocol-buffers/docs/proto3#json
[lucicfg]: https://chromium.googlesource.com/infra/luci/luci-go/+/refs/heads/main/lucicfg/doc/README.md
[fuchsia-recipes]: https://fuchsia.googlesource.com/infra/recipes
<!-- Google-internal link - see http://b/173410810 -->
[textproto-spec]: https://goto.google.com/textformat-spec
