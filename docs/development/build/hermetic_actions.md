# Hermetic build actions

Fuchsia's build system uses a tool to trace filesystem actions performed by
build actions in order to detect that build actions correctly and fully state
their inputs and outputs.

Continue reading this guide if you ran into an error that looks like this:

```
ERROR: [label] read/wrote [path] but it is not a specified input/output!
```

## Build graph correctness

The build is defined as a directed acyclic graph such that actions have their
inputs flowing into them and their outputs flowing from them. For instance, an
action that compiles a `.cc` file into a `.o` file will have the source file as
an input and an object file as an output.

This graph representation ensures that the build system can correctly perform
incremental builds. An incremental build is when a build was already performed,
but then some of the sources were changed, and now the build system is being
asked to rebuild. In an incremental build, the build system will attempt to do
the least amount of work needed, only rebuilding actions whose inputs have
changed, whether due to modifications done by the user to sources or due to
changes in the outputs of other actions that needed to be re-run.

For any action in the build graph, it's required that all inputs and outputs be
stated in order for the build graph to be correct and for actions to be
hermetic. However this is not enforced.
Build actions run in the user's shell, with full access to all files in the
source tree and in the `out/` directory, so they're not sandboxed and they can
reach anywhere.

If you're reading this, you're probably dealing with a build action that did not
fully state one or more of its inputs or outputs.

## Extending the build with custom actions

Developers can use the GN metabuild system to define custom actions in their
`BUILD.gn` files. This can be done with [`action`][action] and
[`action_foreach`][action_foreach]. Custom actions allow developers to invoke
custom tools at build time, and to hook them up to the dependency graph, such
that the tools can be invoked at build time and correctly re-invoked for
incremental builds when their inputs have changed.

Actions state their inputs using the following parameters:

*  `script`: the tool to run. Often this is a Python script, but it can be any
   program that can be executed on the host.
*  `inputs`: files that are used as inputs to the tool. For instance if the tool
   compresses a file, then the file to be compressed will be listed as an input.
*  `sources`: this is treated the same as `inputs`. The difference is only
   semantic, as `sources` are typically used for additional files used by the
   tool's `script`.

Actions state their outputs using the following parameter:

*  `outputs`: each action must produce at least one output file. Actions that
   don't generate an output file, for instance actions that validate certain
   inputs for correctness, will typically generate a "stamp file", which acts as
   an indicator that the action ran and can be empty.

If some of the inputs to an action are not known prior to running the action,
then additionally an action can specify a [`depfile`][depfile]. Depfiles list
inputs to the action's one or more outputs that were discovered at runtime. The
format of a depfile is one or more lines as follows:

```
[output_file1]: [input_file1] [input_file2...]
```

All paths in a depfile must be relative to `root_build_dir` (which is set as the
current working directory for actions).

## Filesystem action tracing for detecting non-hermetic actions

The Fuchsia build system uses a filesystem action tracing tool to detect if
actions read or wrote files that were not listed as inputs or outputs, either
explicitly in the `BUILD.gn` file or in a depfile, as shown above. This is done
in lieu of a sandbox for running actions, and as a runtime sanitizer of sorts.

If you are reading this page then you're likely contending with an error from
this system. The error will have listed precisely which files were read or
written but were not specified as inputs/outputs in `BUILD.gn` or in a depfile.
You should correct these omissions and attempt to rebuild until the error goes
away.

In order to reproduce this error in a local build, you will need to ensure that
action tracing is enabled:

<pre class="prettyprint">
<code class="devsite-terminal">fx set <var>what</var> --args=build_should_trace_actions=true</code>
</pre>

Note that if your action is not defined hermetically, and you haven't corrected
it, then upon attempting to rebuild the action you may not be encountering an
error. Because the action is not defined hermetically, it may not be correctly
picked up in an incremental build (which is part of the problem that you're
trying to solve). To force all build actions to run, you'll need to clean up
your build's output cache first:

```posix-terminal
rm -rf $(fx get-build-dir)
```

## Suppressing hermetic action checks

Actions that are currently not hermetic have the following parameter set:

```gn
action("foo") {
  ...
  # TODO(fxbug.dev/xxxxx): delete the line below and fix this
  hermetic_deps = false
}
```

This suppresses the check that's described above.
If you spot an action that has this suppression, you should remove the
suppression, attempt to reproduce the issue as outlined above, and fix it.

## Common issues and how to fix them

### Inputs not known until action runtime

As explained above, sometimes not all inputs are known at build time and so
cannot be specified in `BUILD.gn` definitions. This is what [depfiles][depfiles]
are for.

You can find an example for fixing a build action to generate a depfile here:

472565: [build] Generate depfile in generate_fidl_json.py |
https://fuchsia-review.googlesource.com/c/fuchsia/+/472565

### Expanding arguments from a file

There is a common pattern used especially in Python scripts to expand the
contents of a file as arguments. In `BUILD.gn` you will find:

```gn
action("foo") {
   script = "myaction.py"
   args = [ "@" + rebase_path(args_file, root_build_dir) ]
   ...
}
```

Then in the associated Python file `myaction.py` you will find:

```python
def main():
    parser = argparse.ArgumentParser(fromfile_prefix_chars='@')
    args = parser.parse_args()
    ...
```

The problem with the above is that `args_file` is read at runtime by the Python
script, and should be specified as an input. To fix:

```gn
action("foo") {
   script = "myaction.py"
   inputs = [ args_file ]
   args = [ "@" + rebase_path(args_file, root_build_dir) ]
   ...
}
```

[action]: https://gn.googlesource.com/gn/+/master/docs/reference.md#func_action
[action_foreach]: https://gn.googlesource.com/gn/+/master/docs/reference.md#func_action_foreach
[depfile]: https://gn.googlesource.com/gn/+/master/docs/reference.md#var_depfile
