;; fidl-mode.el --- Emacs support for editing FIDL -*- lexical-binding: t; -*-

;; Copyright 2019 The Fuchsia Authors. All rights reserved.
;; Use of this source code is governed by a BSD-style license that can be
;; found in the LICENSE file.

(defvar fidl-mode-syntax-table nil "Syntax table for 'fidl-mode'.")
(setq fidl-mode-syntax-table
      (let ((syn-table (make-syntax-table)))
        (modify-syntax-entry ?\/ ". 124b" syn-table)
        (modify-syntax-entry ?*  ". 23"  syn-table)
        (modify-syntax-entry ?\n "> b" syn-table)
        syn-table))

(setq fidl-font-lock-keywords
      (let* (
             (x-keywords '("as" "bits" "compose" "const" "enum" "library"
                           "protocol" "struct" "table" "union" "using" "xunion" ))
             (x-types '("struct" "protocol" "library" "bool" "int8" "int16" "int32"
                        "int64" "uint8" "uint16" "uint32" "uint64" "float32" "float64" ))
             (x-constants '("true" "false"))
             (x-events '())
             (x-functions '())

             (x-keywords-regexp (regexp-opt x-keywords 'words))
             (x-types-regexp (regexp-opt x-types 'words))
             (x-constants-regexp (regexp-opt x-constants 'words))
             (x-events-regexp (regexp-opt x-events 'words))
             (x-functions-regexp (regexp-opt x-functions 'words))
             )

        `(
          (,x-keywords-regexp . font-lock-keyword-face)
          (,x-constants-regexp . font-lock-constant-face)
          (,x-types-regexp . font-lock-type-face)
          (,x-events-regexp . font-lock-builtin-face)
          (,x-functions-regexp . font-lock-function-name-face)
          ;; note: order above matters, because once colored, that part won't change.
          ;; in general, put longer words first
          )))

(defvar fidl-mode-hook nil)

;;;###autoload
(add-to-list 'auto-mode-alist '("\\.fidl\\'" . fidl-mode))

;;;###autoload

(define-derived-mode fidl-mode prog-mode "fidl"
  "Major mode for editing FIDL (Fuchsia Interface Definition Language)."
  (interactive)

  ;; Formatting
  (setq-local indent-tabs-mode nil)
  (setq-local tab-width 4)
  (setq-local comment-start "//")
  (setq-local comment-end "")

  ;; Syntax Highlighting
  (setq-local font-lock-defaults '(fidl-font-lock-keywords))
  (set-syntax-table fidl-mode-syntax-table)

  ;; UI
  (setq major-mode 'fidl-mode)
  (setq mode-name "fidl")

  ;; Hooks
  (run-hooks 'fidl-mode-hook)
  )

(provide 'fidl-mode)
