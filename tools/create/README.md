# fx create

The `fx create` command generates scaffolding for new projects. See `fx create --help` for
usage details.

## Adding a new project type

1. Add a new directory with the project type name under `//tools/create/templates/`.
2. Populate the directory with the intended project structure
    * Name all template files with the `.tmpl` extension or `.tmpl-<lang>` for
      language-specific template files.
    * Use `$` in a file/directory name to substitute the user's `PROJECT_NAME`.
3. Edit the `copy_templates` target in `//tools/create/BUILD.gn` to include all your new
   template files.
4. Add the project type to the help doc-string in `CreateArgs` in `//tools/create/src/main.rs`.

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

### Variables

The template expansion uses [handlebars] syntax. Expand a variable with the syntax `{{VAR_NAME}}`.

The available variable names are:

* `COPYRIGHT_YEAR`: Today's year, eg. 2020, for use in copyright headers,
* `PROJECT_NAME`: The user-specified project name,
* `PROJECT_PATH`: The path from the fuchsia root directory to the new project,
* `PROJECT_TYPE`: The project-type as specified on the command line, e.g: 'component-v2'.

Path names are treated differently. If a `$` character is encountered in a template path, it is
substituted with the `PROJECT_NAME` variable expansion.

Eg. `component/meta/$.cml.tmpl` with `PROJECT_NAME="foo"` expands to `meta/foo.cml`.

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
`//tools/create/src/tmpl_helpers.rs`.

[handlebars]: https://docs.rs/handlebars
