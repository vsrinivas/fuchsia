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
  }

  async searchPackages() {
    let query = this.search.value;
    this.packages = await post_request(
        location.origin + '/api/search/packages', JSON.stringify({'files': query}));
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
      for (const [key, value] of Object.entries(pkg.contents)) {
        let a = document.createElement('a');
        let link = document.createTextNode(key + '\n');
        a.appendChild(link);
        a.title = key;
        a.href = window.location.origin + '/api/blob?merkle=' + value;
        text.appendChild(a);
      }
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
