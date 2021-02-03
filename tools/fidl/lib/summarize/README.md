# summarize

The `summarize` library is used to produce an API summary of a FIDL library.
The information is extracted from the [FIDL intermediate
representation][fidlir] (IR) abstract syntax tree.

## Use

Here is an example that includes the library into another go library, which is
then rolled into a binary.
```
go_library("gopkg") {
  name = "main"
  sources = [ "main.go" ]
  deps = [
    "//tools/fidl/lib/fidlgen",
    "//tools/fidl/lib/summarize:gopkg",
  ]
}

go_binary("fidl_api_summarize") {
  gopackage = "main"
  deps = [ ":gopkg" ]
}
```

Then, you can refer to the functionality as:

```
import (
  // ...
  "go.fuchsia.dev/fuchsia/tools/fidl/lib/fidlgen"
  "go.fuchsia.dev/fuchsia/tools/fidl/lib/summarize"
)

func main() {
    // This is an incomplete code fragment: you will need to initialize in and w
    // properly.
    var (
      // in needs to be properly initialized.  The reader must yield the FIDL IR
      // you want to analyze encoded as JSON text.
      in io.Reader
      // w is where the API summary output is to be written.  Needs to be properly
      // initialized.
      w io.Writer
      // root is the root of the FIDL IR AST.
      root fidlgen.Root
    )
	root, err := fidlgen.DecodeJSONIr(in)
	if err != nil {
		fmt.Fprintf(os.Stderr, "Could not parse FIDL IR from: %v: %v", *in, err)
		os.Exit(1)
	}
	if err := summarize.Write(root, w); err != nil {
		fmt.Fprintf(os.Stderr, "While summarizing %v into %v: %v", *in, *out, err)
		os.Exit(1)
	}
}
```

## API usage hints

The two main functions offered by the library are `summarize.Write`, which
will write out an API summary in text format to a writer, and
`summarize.Elements` which yields a sequence of API elements for further
analysis.

## Compile
```
fx build tools/fidl/fidl_api_summarize
```
## Test

Prerequisites:

- Make sure you have `--with=//tools/fidl:tests` in your `fx set`

Run the tests like so:
```
fx test tools/fidl/lib/summarize
```
<!-- xref -->

[fidlir]: /docs/reference/fidl/language/json-ir

