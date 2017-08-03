" Copyright (c) 2017 The Fuchsia Authors. All rights reserved.
" Use of this source code is governed by a BSD-style license that can be
" found in the LICENSE file.

" Only run if $FUCHSIA_DIR has been set by env.sh
if $FUCHSIA_DIR != ""
  let g:ycm_global_ycm_extra_conf = $FUCHSIA_DIR . '/scripts/vim/ycm_extra_conf.py'

  set runtimepath+=$FUCHSIA_DIR/scripts/vim/
  set runtimepath+=$FUCHSIA_DIR/lib/fidl/tools/vim/

  function FuchsiaBuffer()
    " Set up path so that 'gf' and :find do what we want.
    let &l:path = $PWD. "/**" . "," . $FUCHSIA_DIR . "," .
          \ $FUCHSIA_BUILD_DIR . "," .
          \ $FUCHSIA_BUILD_DIR . "/gen"
    if g:loaded_youcompleteme && &filetype == "cpp"
      " Replace the normal go to tag key with YCM when editing C/CPP.
      nnoremap <C-]> :YcmCompleter GoTo<cr>
    endif
  endfunction

  augroup fuchsia
    autocmd BufRead,BufNewFile $FUCHSIA_DIR/** call FuchsiaBuffer()
  augroup END

endif
