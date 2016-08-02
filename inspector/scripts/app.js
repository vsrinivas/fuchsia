// Copyright 2016 The Fuchsia Authors. All rights reserved.

(function(document) {
  'use strict';

  let app = document.querySelector('#app');

  window.addEventListener('WebComponentsReady', function() {
    // Set up routing based on the hash.
    if (window.location.hash) {
      app.route = window.location.hash.substr(1);
    } else {
      app.route = '/';
    }
    window.addEventListener('hashchange', (event) => {
      app.route = window.location.hash.substr(1);
    });

    // Connect the refresh button.
    document.querySelector('#refresh').addEventListener('click', function() {

      document.querySelector('#endpointsInterface').reload();
      var mainInterface = document.querySelector('#mainInterface');
      if (mainInterface.json.url) {
        fetch(
          window.location.origin + '/refresh?url='+ mainInterface.json.url,
          {method: 'POST', cache: 'no-cache'});
      }
      mainInterface.reload();
    });

  });

  window.addEventListener('load-error', (event) => {
    app.$.toast.text = `Error loading ${event.detail.url}`;
    app.$.toast.show();
  });

})(document);
