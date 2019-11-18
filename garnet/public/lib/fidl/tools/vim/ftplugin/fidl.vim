" Vim ftplugin for Fidl

if exists("b:did_ftplugin")
  finish
endif
let b:did_ftplugin = 1

let s:save_cpo = &cpo
set cpo&vim

augroup fidl.vim
  autocmd!

  " Set some recommended formatting options defined in
  " https://fuchsia.dev/fuchsia-src/development/api/fidl.md#organization

  setlocal comments=sr1:/*,mb:*,ex:*/,:///,://
  setlocal commentstring=//%s
  setlocal formatoptions-=t formatoptions+=croqnlj

  if get(g:, 'fidl_recommended_style', 1)
    let b:fidl_set_style = 1
    setlocal tabstop=4 shiftwidth=4 softtabstop=4 expandtab
    setlocal textwidth=80
  endif

  let b:undo_ftplugin = "
    \ setlocal formatoptions< comments< commentstring< formatlistpat<
    \|if exists('b:fidl_set_style')
      \|setlocal tabstop< shiftwidth< softtabstop< expandtab< textwidth<
    \|endif
    \"
augroup END

let &cpo = s:save_cpo
unlet s:save_cpo
