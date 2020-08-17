/**
 * @fileoverview Simple view for files in bootfs.
 */

class BootfsView {
  constructor() {
    this.bootfsResults = document.getElementById('bootfs-results');
  }

  async init() {
    console.log('[Bootfs] - Loading all files');
    this.bootfs = await post_request(location.origin + '/api/zbi/bootfs', null);
    this.bootfs.sort();
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
