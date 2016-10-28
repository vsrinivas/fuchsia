# Writing a Mojom Code Generator (The Basics)

This guide is to help you write mojom code generators. Mojom code generators
generate bindings that can be used by client programs to communicate using
the mojo protocol.

## What is a Code Generator

A code generator is an executable that accepts a serialized mojom file graph
and writes one or more files to a specified path.

The generator accepts the following command line arguments:
* file-graph: A path to the file that contains the file graph. “-” indicates
  that the file graph will be passed through standard input.
* output-dir: A path to the directory where the code generator will output
  files.
* src-root-path: Relative path to the root of the source tree which contains
  the mojom files.
* no-gen-imports: If “no-gen-imports” is specified, only files with a non-null
  and non-empty specified_file_name must be generated. Otherwise, all files in
  the file graph must be generated.

## Running Your Generator

The easiest way to run your generator is to first use the mojom tool to create
a serialized mojom file graph and then point your generator at that file graph.

To do so, run the mojom tool (located at
`mojo/public/tools/bindings/mojom_tool/bin/linux64/mojom` if you have a
checkout of mojo)

```mojom parse -out test.mojom.bin test.mojom```

Then, simply invoke your code generator with the command line argument:
```--file-graph test.mojom.bin```

## The Go Library

There is a library which was written to make writing go generators easier

The easiest way to use that library is to see the deps example which can be
found alongside it: `mojom/generators/deps/deps_generator.go`

In the main function of your generator, call `common.GetCliConfig(os.Args)`
in order to parse the arguments to the generator, read and deserialize the
mojom file graph.

Then, call `common.GeneratorOutput`. The first argument passed to
`GeneratorOutput` is a function of type `writeOutput`. `writeOutput`
is where the logic for your code generator goes. It will be called once per
mojom file for which the generator must generate a corresponding file.

The first thing you probably want to do in your `writeOutput` function is
call `common.OutputWriterByFilePath`. This will return an object which will
allow you to write to the proper output file for the mojom file under
consideration. `OutputWriterByFilePath` is also where you specify the
extension you want your output file to use.

The mojom file graph can be accessed by calling the `FileGraph` method on
the `config` object.
