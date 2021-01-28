# Banjo libraries

Banjo is an IDL used to describe device driver interfaces. As part of the
[Banjo deprecation effort][deprecation-ticket], the language will be replaced by
FIDL: the libraries in this folder will become FIDL libraries under
`//sdk/fidl`.


## Dependency analysis

The aggregation target in `BUILD.gn` as well as the `analyze_deps.py` script can
produce a representation of the dependencies between the various Banjo
libraries. That information will help inform the migration process by
identifying which libraries need to move together.

To use the script, run the following commands:
```
$ fx gn desc out/default/ //sdk/banjo deps --tree --format=json > local/deps.json
$ ./sdk/banjo/analyze_deps.py --gn-deps local/deps.json
```


[deprecation-ticket]: http://fxbug.dev/67196
