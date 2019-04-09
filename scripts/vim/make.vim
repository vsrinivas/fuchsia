" Ignore error lines that are in googletest headers because these are usually
" noise.
function! QfIgnoreTestHeaders()
  let errors=getqflist()
  let num_errors=len(errors)
  let i=0
  while i < num_errors
    let error=errors[i]
    if error.valid
      let name=bufname(error.bufnr)
      if name =~ "third_party/googletest/googletest/include"
        let errors[i].valid = 0
      endif
    endif
    let i=i+1
  endwhile
  call setqflist(errors)
endfunction

augroup fuchsia_make
  au!
  au QuickfixCmdPost make call QfIgnoreTestHeaders()
augroup END
