# Third-party Dart packages


There are two types of third-party dependencies in the Fuchsia tree:

- Extracted from [pub][pub].
- Synced from Git.

## Pub dependencies

Pub dependencies are host at [`//third-party/dart-pkg`][dart-3p]. That project
is regularly kept up-to-date with [a script][dart-3p-script] that relies on the
`pub` tool to resolve versions and fetch sources for the packages that are used
in the tree.
This script uses a set of canonical local packages which are assumed to be
providing the necessary package coverage for the entire tree.

Additionally, projects may request third-party dependencies to be imported
through the following procedure:

1. Create a `dart_dependencies.yaml` file in the project
2. Add the desired dependencies in that file:

   ```
   name: my_project
   dependencies:
     foo: ^4.0.0
     bar: >=0.1.0
   ```

3. Add a reference to the file in `//scripts/dart/update_3p_packages.py`
4. Run that script
5. Merge your changes to `dart_dependencies.yaml` to master
6. Merge the files downloaded by running the 'update_3p_packages.py' script, and the script itself, to master.
7. In the '//topaz/manifest/dart' manifest, update the project node 'third_part/dart-pkg' revision attribute with the SHA from your commit in Step 6.
8. Merge your change to the '//topaz/manifest/dart' manifest file to master.

[pub]: https://pub.dartlang.org/ "Pub"
[dart-3p]: https://fuchsia.googlesource.com/third_party/dart-pkg/+/master "Third-party dependencies"
[dart-3p-script]: /scripts/dart/update_3p_packages.py "Dependencies script"
