/**
 * @fileoverview Simple view for files in bootfs.
 */

class BootfsView {
  constructor() {
    this.bootfsResults = document.getElementById('bootfs-results');
  }

  async init() {
    console.log('[Bootfs] - Loading all files');
    this.bootfs = await this.post_request(location.origin + '/api/bootfs', null);
    this.bootfs.sort();
  }

  post_request(url, body) {
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

  async showFiles() {
    // Clear out all the current search tiles.
    while (this.bootfsResults.firstChild) {
      this.bootfsResults.removeChild(this.bootfsResults.firstChild);
    }

    let stats = document.createElement('pre');
    stats.className = 'search-stats';
    stats.appendChild(document.createTextNode(this.bootfs.length + ' results'));
    this.bootfsResults.appendChild(stats);

    let tile = document.createElement('div');
    tile.className = 'tile tile-content';

    let header = document.createElement('div');
    header.className = 'tile-header tile-header-search';

    let title = document.createElement('span');
    title.className = 'search-header-text';
    title.appendChild(document.createTextNode('BootFS Files'));
    header.appendChild(title);

    let body = document.createElement('div');
    body.className = 'tile-body';

    let text = document.createElement('pre');
    text.appendChild(document.createTextNode(this.bootfs.join('\n')));
    body.appendChild(text);

    tile.appendChild(header);
    tile.appendChild(body);
    this.bootfsResults.appendChild(tile);
  }
}

window.onload = async function(e) {
  const view = new BootfsView();
  await view.init();
  await view.showFiles();
}
