# VS Code Configuration

## General configurations

### Speed up automatic file reloading
VS Code watches external file changes. It automatically reloads the lastest stored file if it does not have a working copy that conflicts. Watching and detecting changes, however, can take long time. The larger the code base, the longer it takes to detect the file change. Excluding some directories from the search space improves the speed.

Follow the menu Code -> Preferences -> Text Editor -> File -> Add Pattern
Add a directory pattern you want to exclude the search from. Alternatively one can directory modify `settings.json` and add exclude pattern similar to below

```
    "files.watcherExclude": {
        "**/.git": true,
        "**/.svn": true,
        "**/.hg": true,
        "**/CVS": true,
        "**/.DS_Store": true,
        "**/topaz": true,
        "**/.cipd": true,
        "**/.idea": true,
        "**/.ssh": true,
        "**/buildtools": true,
        "**/zircon/prebuilt": true,
        "**/src/chromium": true,
        "**/garnet/third_party": true,
        "**/garnet/test_data": true,
        "**/zircon/experimental": true,
        "**/zircon/third_party": true,
        "**/out": true,
        "**/rustfmt.toml": true,
        "**/PATENTS": true,
        "**/.dir-locals.el": true,
        "**/.gitignore": true,
        "**/.jiri_manifest": true,
        "**/AUTHORS": true,
        "**/CMakeLists.txt": true,
        "**/.clang-tidy": true,
        "**/.clang-format": true,
        "**/.gitattributes": true,
        "**/.style.yapf": true,
        "**/CODE_OF_CONDUCT.md": true,
        "**/CONTRIBUTING.md": true,
        "**/LICENSE": true,
        "**/examples": true,
        "**/.jiri_root": true,
        "**/prebuilt": true,
    },
```


## Language specifics
Each language may require extra configuration. See more for

* [Rust](/docs/development/languages/rust/editors.md#visual-studio-code)
* [Dart](/docs/development/languages/dart/ides.md#visual-studio-code)
* [C/C++](/docs/development/languages/c-cpp/editors.md#visual-studio-code)
* [FIDL](/docs/development/languages/fidl/guides/editors.md#visual-studio-code)