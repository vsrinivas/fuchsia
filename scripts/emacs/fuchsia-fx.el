;;; fuchsia-fx.el --- Description -*- lexical-binding: t; -*-
;;;
;;; Copyright (c) 2022 The Fuchsia Authors. All rights reserved.
;;; Use of this source code is governed by a BSD-style license that can be
;;; found in the LICENSE file.

(require 'fuchsia-common)

(defcustom fuchsia-fx-command ".jiri_root/bin/fx"
  "Path to fx executable from fuchsia root."
  :type 'string)

(defun fuchsia-fx (cmd-strs &optional exec)
  (fuchsia--root-shell-command
   (cl-concatenate 'list
                   (list fuchsia-fx-command)
                   (when current-prefix-arg '("-i"))
                   cmd-strs)
   exec))

(setq fuchsia--fx-default-commands '((("fx" "help") (:exec 'compile ))
                                     (("fx" "build") (:exec 'compile))
                                     (("fx" "ota") (:exec 'compile))
                                     (("fx" "serve") (:exec 'async :name "*fuchsia-fx-serve*"))
                                     (("fx" "format-code"))))

(fuchsia--register-default-subcmds fuchsia--fx-default-commands)

(defun fuchsia-fx-list-boards ()
  (interactive)
  (split-string (fuchsia-fx '("list-boards")) "\n"))

(defun fuchsia-fx-list-products ()
  (interactive)
  (split-string (fuchsia-fx '("list-products")) "\n"))

(defun fuchsia-fx-list-packages ()
  (interactive)
  (split-string (fuchsia-fx '("list-packages")) "\n"))

(defun fuchsia-fx-set (product board with-bases)
  "Set the PRODUCT and BOARD configuration for Fuchsia."
  (interactive
   (list
    (completing-read "Products: " (fuchsia-fx-list-products))
    (completing-read "Boards: " (fuchsia-fx-list-boards))
    (completing-read-multiple "With: " nil))
  (fuchsia-fx (list "set" product board))))

(provide 'fuchsia-fx)
;;; fuchsia-fx.el ends here
