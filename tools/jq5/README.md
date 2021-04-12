# jq5
`jq5` is a tool that extends much of `jq`'s functionality to json5 files. In particular, it preserves
most comments, especially if the structure of the output resembles that of the input, such as in the case
of large-scale changes of `.cml` files.

## Invocation
`jq5` can currently be invoked with `fx jq5 <my_filter> <file1> <file2> ... <filen>`, where `<my_filter>` is a valid `jq` filter and the files contain valid json5 objects and no other text.

The tool also has support for stdin. For example, one might use it by calling `cat <my_file> | fx jq5 <my_filter>`. They may also simply call `fx jq5 <my_filter>` and type the json5 object in manually. Unfortunately, unlike `jq`, `jq5` does not parse while receiving input, so errors will only be realized after stdin is closed.

## Examples

Listed below are changes that were authored using this tool.
Please feel free to contribute to this list.

* [513020: [cml] Change arrays of 1 in .offer.[n].to to singletons.](https://fuchsia-review.googlesource.com/c/fuchsia/+/513020)

## The Problem:
`jq` offers rich features for manipulating the structure of JSON files, but unfortunately does not support the json5 format. In some cases it may be acceptable to translate the json5 objects in question to JSON before feeding them to `jq`. Such an approach would preserve all the actual data, but any comments would be lost.

## The Solution:

`jq5`'s current approach is to make use of `json5format`'s representation of a json5 object. This representation is essentially that of a "decorated tree": objects and arrays potentially have children, while "primitives" are its leaves. Associated with each node is an instance of a specialized data structure for comments.

`jq5` makes use of this representation as follows:

1. Parse the original json5 into a `json5format::ParsedDocument`.
2. Use `serde_json5` to create a string of a JSON object containing all of the original object's data but none of its comments.
3. Run `jq` on the JSON string produced using `serde_json5`.
4. Parse the output of `jq` into a `json5format::ParsedDocument`.
5. For each comment in the original json5 object, determine whether the path from the root of the corresponding node specifies a node in the output of `jq`. If it does, clone the comment and attach it to the specified node in the output.
6. Use `json5format` to create a string of the output and print it to `stdout`.

Some elaboration is useful on step 4. Consider the following json5 object:
```
{
    //Foo
    foo: 1,
    //Bar
    bar: 2,
}
```
Assuming it is stored in `foobar.json5`, running `fx jq5 '{foo: .foo, baz: .bar}' foobar.json5` will yield the following output:
```
{
    //Foo
    foo: 1,
    baz: 2,
}
```
Because the path `.foo`  exists in the output, all comments from the node specified by that path in the original object will be transferred to the output. However, `.bar` does not exist in the output. Perhaps more illuminating, suppose `my_object.json5` contains the following:
```
{
    foo: 1,
    sub_object: {
        bar: 2,
        my_array: [
            //Comment 1
            1,

            //Comment 2
            2,

            //Comment 3
            3,
        ],
    },
}
```
Running `fx jq5 'del(.sub_object.my_array[0])' my_object.json5 ` will yield this output:
```
{
    foo: 1,
    sub_object: {
        bar: 2,
        my_array: [
            //Comment 1
            2,

            //Comment 2
            3,
        ],
    },
}
```

Notably, the comments are now associated the wrong elements. Because the comment `//Comment 1` is associated to `.sub_object.my_array[0]` and that path specifies a node in the output, `//Comment 1` gets transferred to that element, even though its path specified a different value in the original. This is a deficiency, and remedies are currently being considered.

## Caveats

As mentioned above, the method of replacing comments is not perfect. It fails to replace some comments and puts others where they are not necessarily intended.

A quirk of the "JSON-ification" process also yields some imperfections. When deserializing a serialized JSON object using `serde_json5`, the elements of an object are alphabetized. This means that if `foobar.json5` contains
```
{
    foo: 1,
    bar: 2,
}
```
then running `fx jq5 . foobar.json5` yields
```
{
    bar: 1,
    foo: 1,
}
```
This ideally wouldn't happen, but does not affect the actual data after serialization or the placement of comments.

## Options

`path-to-jq` can be used to specify a custom `jq` for `jq5`'s invocation of `jq`. The default behavior is to invoke `jq` by using `fx jq`, which finds the appropriate prebuilt `jq` binary in a Fuchsia checkout.

## Feedback
Suggestions to improve `jq5` are more than welcome! Email or ping the owners listed in this directory.
