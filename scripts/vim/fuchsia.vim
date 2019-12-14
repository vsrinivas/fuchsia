" Copyright (c) 2017 The Fuchsia Authors. All rights reserved.
" Use of this source code is governed by a BSD-style license that can be
" found in the LICENSE file.

" Use `fx build` to build when you type :make
" Helper to clean up quickfix list in make.vim
let &makeprg="fx build"

" Look for the fuchsia root containing the current directory by looking for a
" .jiri_manifest file
let jiri_manifest = findfile(".jiri_manifest", ".;")
if jiri_manifest != ""
  let g:fuchsia_dir = fnamemodify(jiri_manifest, ":p:h")
  " Get the current build dir from fx
  let g:fuchsia_build_dir = systemlist(g:fuchsia_dir . "/scripts/fx get-build-dir")[0]
  " Tell YCM where to find its configuration script
  let g:ycm_global_ycm_extra_conf = g:fuchsia_dir . '/scripts/youcompleteme/ycm_extra_conf.py'
  " Do not load fuchsia/.ycm_extra_conf in case the user created a symlink for
  " other editors.
  let g:ycm_extra_conf_globlist = [ '!' . g:fuchsia_dir . '/*']
  " Google-internal options - use clangd completer if the user has a compilation
  " database (built with `fx compdb`).
  if filereadable(g:fuchsia_dir . '/compile_commands.json')
    let g:ycm_use_clangd = 1
    let g:ycm_clangd_binary_path = systemlist(g:fuchsia_dir . "/scripts/youcompleteme/paths.py CLANG_PATH")[0] . '/bin/clangd'
    " Disable clang tidy warnings for YCM, they're too noisy and cause you to quickly hit the YCM
    " diagnostics limit
    let g:ycm_clangd_args = ["-clang-tidy=false"]
  else
    let g:ycm_use_clangd = 0
  endif


  let &runtimepath .= "," .
        \ g:fuchsia_dir . "/scripts/vim/," .
        \ g:fuchsia_dir . "/garnet/public/lib/fidl/tools/vim/," .
        \ g:fuchsia_dir . "/third_party/json5.vim/"

  " The "filetype plugin" line must come AFTER the changes to runtimepath
  " above (so the proper directories are searched), but must come BEFORE the
  " FuchsiaBuffer function below (to work around a bug on MacOS where
  " Ctrl-] does not work because filetype is undefined instead of being
  " equal to "cpp".)
  filetype plugin indent on

  function! FuchsiaBuffer()
    let full_path = expand("%:p")
    let extension = expand("%:e")

    " Only run if the buffer is inside the Fuchsia dir
    if full_path !~ '^\V' . escape(g:fuchsia_dir, '\')
      return
    endif

    let b:is_fuchsia = 1

    " Set up path so that 'gf' and :find do what we want.
    " This includes the directory of the file, cwd, all layers, layer public
    " directories, the build directory, the gen directory and the zircon
    " sysroot include directory.
    let &l:path = ".,," .
          \ $PWD . "/**/," .
          \ g:fuchsia_dir . "," .
          \ g:fuchsia_dir . "/*/," .
          \ g:fuchsia_dir . "/*/public/," .
          \ g:fuchsia_build_dir . "," .
          \ g:fuchsia_build_dir . "/gen," .
          \ g:fuchsia_build_dir . "/sdk/exported/zircon_sysroot/arch/*/sysroot/include"

    " Make sure Dart files are recognized as such.
    if extension == "dart"
      set filetype=dart
    endif

    " The Buf* autocmds sometimes run before and sometimes after FileType.
    if &filetype == "cpp"
      call FuchsiaCppBuffer()
    endif
  endfunction

  " This may be called twice because autocmds arrive in different orders on
  " different platforms.
  function! FuchsiaCppBuffer()
    if !exists("b:is_fuchsia") || !b:is_fuchsia
      return
    endif
    if exists('g:loaded_youcompleteme')
      " Replace the normal go to tag key with YCM when editing C/CPP.
      nnoremap <C-]> :YcmCompleter GoTo<cr>
    endif
    set textwidth=100
  endfunction

  augroup fuchsia
    au!
    autocmd BufRead,BufNewFile * call FuchsiaBuffer()
    autocmd Filetype cpp call FuchsiaCppBuffer()

    " .cmx files are JSON
    autocmd BufRead,BufNewFile *.cmx set syntax=json
    " .cml files are JSON5
    autocmd BufRead,BufNewFile *.cml set syntax=json5

    " .tmpl.go files are Go template files
    autocmd BufRead,BufNewFile *.tmpl.go set syntax=gotmpl

    " If this is a golden file, strip the .golden and run autocommands
    " This will allow syntax highlighting of FIDL goldens.
    autocmd BufNewFile *.golden execute "doautocmd BufNewFile " . expand("%:r")
    autocmd BufRead *.golden execute "doautocmd BufRead " . expand("%:r")
  augroup END

endif
