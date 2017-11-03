" Copyright (c) 2017 The Fuchsia Authors. All rights reserved.
" Use of this source code is governed by a BSD-style license that can be
" found in the LICENSE file.

" Look for the fuchsia root containing the current directory by looking for a
" .jiri_manifest file
let jiri_manifest = findfile(".jiri_manifest", ".;")
if jiri_manifest != ""
  let g:fuchsia_dir = fnamemodify(jiri_manifest, ":h")
  " Get the current build dir from fx
  let g:fuchsia_build_dir = systemlist(g:fuchsia_dir . "/scripts/fx get-build-dir")[0]
  " Tell YCM where to find its configuration script
  let g:ycm_global_ycm_extra_conf = g:fuchsia_dir . '/scripts/vim/ycm_extra_conf.py'

  let &runtimepath = g:fuchsia_dir . "/scripts/vim/," .
        \ g:fuchsia_dir . "/garnet/public/lib/fidl/tools/vim/," .
        \ &runtimepath

  " The "filetype plugin" line must come AFTER the changes to runtimepath
  " above (so the proper directories are searched), but must come BEFORE the
  " FuchsiaBuffer function below (to work around a bug on MacOS where
  " Ctrl-] does not work because filetype is undefined instead of being
  " equal to "cpp".)
  filetype plugin indent on

  function FuchsiaBuffer()
    let full_path = expand("%:p")
    let extension = expand("%:e")

    " Only run if the buffer is inside the Fuchsia dir
    if full_path !~ "^" . g:fuchsia_dir
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
          \ g:fuchsia_dir . "/out/build-zircon/*/sysroot/include"

    " Make sure Dart files are recognized as such.
    if extension == "dart"
      set filetype=dart
    endif

    " Treat files in a packages directory (or subdirectory) without a filetype
    " that don't have an extension as JSON files.
    if &filetype == "" && full_path =~ "/packages/" && extension == ""
      set filetype=json sw=4
    endif

    " The Buf* autocmds sometimes run before and sometimes after FileType.
    if &filetype == "cpp"
      call FuchsiaCppBuffer()
    endif
  endfunction

  " This may be called twice because autocmds arrive in different orders on
  " different platforms.
  function FuchsiaCppBuffer()
    if g:loaded_youcompleteme
      " Replace the normal go to tag key with YCM when editing C/CPP.
      nnoremap <C-]> :YcmCompleter GoTo<cr>
    endif
  endfunction

  augroup fuchsia
    au!
    autocmd BufRead,BufNewFile * call FuchsiaBuffer()
    autocmd FileType cpp call FuchsiaCppBuffer()
  augroup END

endif
