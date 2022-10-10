# README

This is a helper script to speed things up during the 2022 FIDL docs fixit.

## DO_NOT_REMOVE_COMMENT

There are a number of comments like `// DO_NOT_REMOVE_COMMENT` and its companion
`// /DO_NOT_REMOVE_COMMENT` sprinkled throughout fuchsia.git. It should go
without saying, but please don't remove these comments. The scaffold script uses
them to properly place certain text spans when generating new canonical
examples.

If there are multiple such ranges in a single file, both the start and end tags
of each range may be suffixed with a colon followed by the same identifier tag:
`// DO_NOT_REMOVE_COMMENT:tag` and `// /DO_NOT_REMOVE_COMMENT:tag`.

## Templating

The scaffolder basically takes one or more of the directory templates
`./templates` (`create_code`, `create_docs`, etc), performs substitutions based
on the user's supplied inputs, and writes the result to the correct location in
fuchsia.git.

For each file in a given directory template, four sequential actions are
performed:

1. Substitutions are performed on the file's name.
1. Substitutions are performed on the file's contents.
1. The `.template` suffix is stripped from the file's name, and it is written to
   the correct output location.

After this process has completed, there are usually a number of `TODO` comments
left for the invoking user to manually fill in. These are enumerated to the user
in the command line output on success.

### Substitutions performed

A number of substitutions are performed by this scaffolding algorithm, all
listed here.

<!--

// LINT.IfChange

-->

#### Series

The name of the canonical example "series" the newly scaffolded output belongs
to is rendered in a few forms:

- `series_flat_case`: For instance, "myseries".
- `series_sentence_case`: For instance, "My series".
- `series_text_case`: For instance, "my series".
- `series_snake_case`: For instance, "my_series".

#### Variant

The name of is particular variant of the canonical example "series" the newly
scaffolded output belongs to is rendered in several forms. Note that for new
series, this value is always inferred to be "baseline":

- `series_flatcase`: For instance, "myvariant".
- `variant_snake_case`: For instance, "my_variant".

#### Protocol

The name of the main `@discoverable` protocol that is the first contact point
between the client and server in the given canonical example variant is rendered
in the following forms. Note that the variant's FIDL may still have multiple
protocols - this name only refers to the *first* contact point:

- `protocol_pascal_case`: For instance, "MyProtocol".
- `protocol_snake_case`: For instance, "my_protocol".

#### Other

The `bug` representing the monorail bug number, and a `dns` for `DO` + `NOT` +
`SUBMIT` are also passed in.

<!--

// LINT.ThenChange(/tools/fidl/scripts/canonical_example/scaffold.py)

-->
