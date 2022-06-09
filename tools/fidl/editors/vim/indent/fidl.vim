" Vim indent for Fidl

if exists("b:did_indent")
  finish
endif
let b:did_indent = 1

" Use the cindent formatter as the base formatter.
setlocal cindent

" This customizes cident to indent unclosed parentheses in order to match the
" fidl formatter.
"
" `(0`: When inside an unclosed parentheses, align the next line to match with
" the first non-whitespace character after the parentheses on the previous line.
"
" `Ws`: If the prior line ends with an unclosed parentheses, indent the next
" line 1 shiftwidth from the start of the prior line or next unclosed
" parentheses.
"
" See `:help inoptions-values` for more details about these settings.
setlocal cinoptions=(0,Ws
