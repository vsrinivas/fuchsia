;;; fuchsia-common.el --- Description -*- lexical-binding: t; -*-
;;;
;;; Copyright (c) 2022 The Fuchsia Authors. All rights reserved.
;;; Use of this source code is governed by a BSD-style license that can be
;;; found in the LICENSE file.

(require 'seq)

(defgroup fuchsia nil
  "Conveniences working with Fuchsia source code."
  :group 'extensions
  :group 'convenience
  :version "28.1")

(defcustom fuchsia-root-file ".jiri_root"
  "Marker file name for fuchsia project roots."
  :type 'string)

(defun fuchsia--root ()
  "Return the path to the root of the current fuchsia project."
  (or (locate-dominating-file default-directory fuchsia-root-file)
    (error "Current buffer is not in a fuchsia checkout")))

(defun fuchsia--in-root (fn)
  "Run a function FN from the current fuchsia project root."
  (let ((default-directory (fuchsia--root)))
    (funcall fn)))

(defvar fuchsia-execution-buffer-name nil
  "Name to give to the compilation buffer for this command")

(defun fuchsia--root-shell-command (command-strings &optional exec)
  "Run a shell command from the current parent fuchsia root.
COMMAND-STRINGS is a list of strings which will be shell-quoted and
concatenated. "
  (let ((buf-name (or fuchsia-execution-buffer-name
                    "*fuchsia*"))
         (cmd-str (combine-and-quote-strings command-strings)))
    (fuchsia--in-root
      (cond
        ((eq exec 'compile)
          (lambda ()
            (let
              ((compilation-buffer-name-function
                 (lambda (&rest _) buf-name)))
              (compile cmd-str))))
        ((eq exec 'async)
          (lambda ()
            (start-file-process-shell-command (car command-strings) buf-name cmd-str)
            (message "Started process")))
        (t
          (lambda ()
            (message (shell-command-to-string cmd-str))))))))


(defun fuchsia--commands-to-sym (components)
  "Return a symbol given a list of command COMPONENTS.

This is used internally to codegen function names.
e.g.
 (fuchsia-commmands-to-sym '(\"foo\" \"bar\")) => fuchsia-foo-bar."
  (intern (mapconcat 'identity
            (cons "fuchsia" components)
            "-")))

(defun fuchsia--default-subcmd (components &optional properties)
  "Define a default interactive command for a fuchsia tool.

Given a list of shell command strings strings COMPONENTS, return
a form which can be evaluted to defun a default command for those
components. PROPERTIES is a property list that defines some
configuration for the generated function.

This resulting command is interactive and takes no arguments, and
prints the result returned by the function.

e.g.
 (eval (fuchsia--default-subcmd '(\"foo\" \"bar\")))

The above would register a command fuchsia-foo-bar which relies on a
function fuchsia-foo and passes it '(bar).

Configurations that can be used in PROPERTIES include:

 :exec Default is run synchronously and also `message' the
   result. May be 'compile to use a compilation-mode buffer, or
   'async for an an async process.
"
  `(defun ,(fuchsia--commands-to-sym components) ()
     (interactive)
     (let ((fuchsia-execution-buffer-name ,(plist-get properties :name)))
       (,(fuchsia--commands-to-sym (list (car components))) ',(cdr components) ,(plist-get properties :exec)))))

(defun fuchsia--register-default-subcmds (decls)
  "Register a list of default fuchsia tool commands.
Generate interative commands with `fuchsia-default-subcmd' for
each of the command string lists in SUBCMDS.

e.g.
 (fuchsia--register-default-submds ((\"foo\" \"bar\") (\"foo\" \"baz\")))

The above would define default commands for the \"foo bar\" and
\"foo baz\" commands which internally rely upon \"fuchsia-foo\.
See `fuchsia-fx' as a reference implementation for such a
method."
  (cl-map 'list
    (lambda (decl)
      (let ((commands (car decl))
             (properties (car (cdr decl))))
        (eval (fuchsia--default-subcmd commands properties))))
    decls))

(provide 'fuchsia-common)
;;; fuchsia-common.el ends here
