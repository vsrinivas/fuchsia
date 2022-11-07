# config_data Package

This Rust crate constructs the `config_data` Package, which is a Fuchsia package
that provides assembly-time selected files as configuration for CFv1 components,
and for CFv2 components via routed capabilities, .

More accurately, the `config_data` package is a set of namespaces that are
injected into the namespaces of the components' packages.

The layout within the package itself is:

```
// (root)
  some_package_name/
    file
    dir1/
      file2
  another_package_name/
    file3
  ...
```
