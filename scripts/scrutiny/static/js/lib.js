// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

/**
 * @fileoverview Library of commonly used functions for views.
 */

// Simple wrapper around posting a json request.
function post_request(url, body) {
  return new Promise(function(resolve, reject) {
           const jsonRequest = new XMLHttpRequest();
           jsonRequest.open('POST', url);
           // Only accept 200 success.
           jsonRequest.onload = function() {
             if (this.status == 200) {
               resolve(JSON.parse(jsonRequest.response));
             } else {
               reject({status: this.status, statusText: jsonRequest.statusText});
             }
           };

           // Reject all errors.
           jsonRequest.onerror = function() {
             reject({status: this.status, statusText: jsonRequest.statusText});
           };
           jsonRequest.send(body);
         })
      .catch();
}
