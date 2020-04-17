# fx create

The `fx create` command generates scaffolding for new projects. See `fx create --help` for
usage details.

## Running tests

The tests should be included in the build along with the `//tools/create` target.

* Run all tests: `fx test //tools/create`
* Run only golden tests: `fx test //tools/create/goldens`

The golden projects have their unit tests run as well. To only run host tests,
pass the `--host` flag to `fx test`.

## Adding a new project type

1. Add a new directory with the project type name under `//tools/create/templates/`.
2. Populate the directory with the intended project structure
    * Name all template files with the `.tmpl` extension or `.tmpl-<lang>` for
      language-specific template files.
    * Use `{{PROJECT_NAME}}` in a file/directory name to substitute the user's `PROJECT_NAME`.
3. Edit the `templates` target in `//tools/create/templates/BUILD.gn` to include all your new
   template files.
4. Add the project type to the help doc-string in `CreateArgs` in `//tools/create/src/main.rs`.
5. Create a golden project in `//tools/create/goldens`.
    * Use the `fx create` command to create a golden project, using the `--override-copyright-year 2020`
      flag. This makes sure that tests don't start failing in 2021+.
6. Add a test target to `//tools/create/goldens/BUILD.gn`.
    * See the existing tests for examples. Use the template in `//tools/create/goldens/golden_test.gni`.
    * The test will generate a project and compare it with your golden project.
    * Execute the test with `fx test //tools/create/goldens`.

## Templates

### Layout

Each top-level directory in `//tools/create/templates` corresponds to a project type
of the same name.

Files with the `.tmpl` and `.tmpl-*` extensions in these directories are [handlebars] templates.
Template expansion is performed on the templates and the directory structure is replicated in
the new project directory.

Templates with the `.tmpl` extension are language agnostic.

Templates with the `.tmpl-<lang>` extension are expanded only when the user supplies the
`--lang=<lang>` flag.

Multiple languages can be supported in the same template directory. For instance, the
`component-v2` command supports `cpp` and `rust` languages by having both a `BUILD.gn.tmpl-cpp`
and `BUILD.gn.tmpl-rust` file.

Partial templates are fragments of template files that can be included by other templates.
Partial templates begin with `_` and can be included in a template with the `{{>name_of_partial}}`
without the leading `_`.

Only partial templates at the root or within the project type directory being executed are
available to a template.

E.g. file layout:

* `templates/`
  - `_copyright.tmpl`
  - `component-v1/`
    * `_header.tmpl`
  - `component-v2/`
    * `_common.tmpl`

When creating a project of type `component-v1`, the partial templates `_copyright.tmpl` and
`component-v1/_header.tmpl` are visible and can be executed with `{{>copyright}}` and
`{{>component-v1/header}}`.

### Variables

The template expansion uses [handlebars] syntax. Expand a variable with the syntax `{{VAR_NAME}}`.

The available variable names are:

* `COPYRIGHT_YEAR`: Today's year, eg. 2020, for use in copyright headers,
* `PROJECT_NAME`: The user-specified project name,
* `PROJECT_PATH`: The path from the fuchsia root directory to the new project,
* `PROJECT_TYPE`: The project-type as specified on the command line, e.g: 'component-v2'.
* `TEMPLATE_PATH`: The path to the source template being executed.

Path names are also treated as template strings and can contain [handlebars] syntax.

Eg. `component/meta/{{PROJECT_NAME}}.cml.tmpl` with `PROJECT_NAME="foo"` expands to `meta/foo.cml`.

#### Adding a new variable

To make a new variable accessible to templates, add it to `TemplateArgs` in
`//tools/create/src/main.rs`.

### Helpers

Helper functions can be invoked with the syntax `{{helper VAR_NAME}}`.

The available helper functions are:

* `pascal_case`: Converts a string argument into *P*ascal*C*ase,
* `snake_case`: Converts a string argument into snake*_*case,
* `screaming_snake_case`: Converts a string argument into SCREAMING_SNAKE_CASE,

#### Adding a new helper

To make a new helper function accessible to templates, follow instructions in
`//tools/create/src/template_helpers.rs`.

[handlebars]: https://docs.rs/handlebars
