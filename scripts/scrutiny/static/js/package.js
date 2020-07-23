/**
 * @fileoverview Simple search view for files in packages.
 */

class PackageView {
  constructor() {
    this.search = document.getElementById('package-search');
    this.searchButton = document.getElementById('package-search-button');
    this.searchButton.addEventListener('click', () => this.searchPackages());
    this.searchResults = document.getElementById('search-results');
  }

  async init() {
    console.log('[Packages] - Loading all packages');
    this.packages = await this.post_request(location.origin + '/api/packages', null);
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

  async searchPackages() {
    let query = this.search.value;
    // Clear out all the current search tiles.
    while (this.searchResults.firstChild) {
      this.searchResults.removeChild(this.searchResults.firstChild);
    }

    let matches = [];
    this.packages.forEach((pkg) => {
      for (const [key, value] of Object.entries(pkg.contents)) {
        if (key.includes(query)) {
          matches.push(pkg);
          break;
        }
      }
    });

    matches.sort((lhs, rhs) => {
      if (lhs.url > rhs.url) {
        return 1;
      }
      if (rhs.url > lhs.url) {
        return -1;
      }
      return 0;
    });

    let stats = document.createElement('pre');
    stats.className = 'search-stats';
    stats.appendChild(document.createTextNode(matches.length + ' results'));
    this.searchResults.appendChild(stats);

    matches.forEach((pkg) => {
      let tile = document.createElement('div');
      tile.className = 'tile tile-content';

      let header = document.createElement('div');
      header.className = 'tile-header tile-header-search';

      let title = document.createElement('span');
      title.className = 'search-header-text';
      title.appendChild(document.createTextNode(pkg.url));
      header.appendChild(title);

      let body = document.createElement('div');
      body.className = 'tile-body';

      let text = document.createElement('pre');
      let files = [];
      for (const [key, value] of Object.entries(pkg.contents)) {
        files.push(key);
      }
      text.appendChild(document.createTextNode(files.join('\n')));
      body.appendChild(text);

      tile.appendChild(header);
      tile.appendChild(body);
      this.searchResults.appendChild(tile);
    });
  }
}

window.onload = async function(e) {
  const view = new PackageView();
  view.init();
}
