/**
 * @fileoverview Simple search view for component manifests.
 */

class ManifestView {
  constructor() {
    this.search = document.getElementById('manifest-search');
    this.searchButton = document.getElementById('manifest-search-button');
    this.searchButton.addEventListener('click', () => this.searchManifests());
    this.searchResults = document.getElementById('search-results');
  }

  async init() {
    console.log('[Manifests] - Loading all component manifests');
    this.components = await post_request(location.origin + '/api/components', null);
  }

  async searchManifests() {
    let query = this.search.value;
    // Clear out all the current search tiles.
    while (this.searchResults.firstChild) {
      this.searchResults.removeChild(this.searchResults.firstChild);
    }
    let requests = [];
    let urls = [];
    this.components.forEach((component) => {
      if (component.url.includes(query) && !component.inferred) {
        // Populate with new manifest search tiles.
        let api = location.origin + '/api/component/manifest';
        let query = JSON.stringify({'component_id': component.id});
        requests.push(post_request(api, query));
        urls.push(component.url);
      }
    });

    let manifests = await Promise.all(requests);
    let matches = [];
    for (let i = 0; i < manifests.length; i++) {
      matches.push({'url': urls[i], 'manifest': manifests[i]});
    }

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

    matches.forEach((component) => {
      let manifest = component.manifest;
      try {
        manifest = JSON.parse(manifest);
        manifest = JSON.stringify(manifest, undefined, 4);
      } catch (err) {
        console.log('[Malformed Manifest] ' + query);
      }

      let tile = document.createElement('div');
      tile.className = 'tile tile-content';

      let header = document.createElement('div');
      header.className = 'tile-header tile-header-search';

      let title = document.createElement('span');
      title.className = 'search-header-text';
      title.appendChild(document.createTextNode(component.url));
      header.appendChild(title);

      let body = document.createElement('div');
      body.className = 'tile-body';

      let text = document.createElement('pre');
      text.appendChild(document.createTextNode(manifest));
      body.appendChild(text);

      tile.appendChild(header);
      tile.appendChild(body);
      this.searchResults.appendChild(tile);
    });
  }
}

window.onload = async function(e) {
  const view = new ManifestView();
  view.init();
}
