# ICU Data

## CLDR data (a.k.a. ICU data)

Fuchsia uses the CLDR data bundle provided by `web_engine`.
That is, it does not use any of the files supplied by `//third_party/icu`,
because of some size-constrained builds.

If your build action needs to refer to the ICU data file, e.g. to use it as a
`resource`, you can refer to it as:

```
${root_build_dir}/icu_data/icudtl.dat
```

In order to refer to it, you must ensure that it gets built.  This is achieved
by adding an appropriate dependency declaration.

You do this by adding a target
label `//src/lib/icu_data:icudtl($host_toolchain)` into your `deps` on an action.
Specially, if your action is a `go_library`, you must list the target label in
`non_go_deps` instead of `deps`.  Similarly, for all actions that require special
deps handling, you may need to use it in `non_dart_deps`, `non_rust_deps` etc., 
as appropriate.
