/**
 * @fileoverview Simple dashboard view for the Scrutiny tool.
 */

class DashboardView {
  constructor() {
    this.pluginsTile = document.getElementById('plugins');
    this.modelTile = document.getElementById('model');
    this.collectorsTile = document.getElementById('collectors');
    this.controllersTile = document.getElementById('controllers');
    this.scheduleButton = document.getElementById('schedule-button');
    this.connected = true;
    this.title = document.getElementById('title');
    this.titleOffline = document.getElementById('title-offline');
  }

  async init() {
    console.log('[Dashboard] - Init');
    let self = this;
    this.scheduleButton.addEventListener('click', async function() {
      self.scheduleButton.textContent = 'Queuing...';
      await self.scheduleCollector();
      setTimeout(function() {
        self.scheduleButton.textContent = 'Schedule'
      }, 2000);
    });
  }

  async collectData() {
    try {
      await post_request(location.origin + '/api/engine/health/status', null);
      this.connected = true;
    } catch {
      this.connected = false;
      return;
    }
    this.plugins = await post_request(location.origin + '/api/engine/plugin/list', null);
    this.model = await post_request(location.origin + '/api/engine/model/stats', null);
    this.collectors = await post_request(location.origin + '/api/engine/collector/list', null);
    this.controllers = await post_request(location.origin + '/api/engine/controller/list', null);
  }

  async refresh() {
    console.log('[Dashboard] - Refresh');
    await this.collectData();
    if (this.connected) {
      this.title.style.display = 'inline';
      this.titleOffline.style.display = 'none';
      this.setPluginList();
      this.setModelList();
      this.setCollectorList();
      this.setControllerList();
    } else {
      this.title.style.display = 'none';
      this.titleOffline.style.display = 'inline';
    }
  }

  async scheduleCollector() {
    console.log('[Dashboard] - Collectors Scheduled');
    await post_request(location.origin + '/api/engine/collector/schedule', null);
  }

  async setPluginList() {
    while (this.pluginsTile.firstChild) {
      this.pluginsTile.removeChild(this.pluginsTile.firstChild);
    }

    let header = document.createElement('tr');
    let pluginName = document.createElement('th');
    pluginName.appendChild(document.createTextNode('Plugin'));
    let stateName = document.createElement('th');
    stateName.appendChild(document.createTextNode('Loaded'));
    header.appendChild(pluginName);
    header.appendChild(stateName);
    this.pluginsTile.appendChild(header);

    this.plugins.forEach((plugin) => {
      let entry = document.createElement('tr');
      let pluginName = document.createElement('td');
      pluginName.appendChild(document.createTextNode(this.splitCamelCase(plugin.name)));
      let stateName = document.createElement('td');
      stateName.style.textAlign = 'center';
      if (plugin.state == 'Loaded') {
        stateName.style.color = '#189910';
        stateName.appendChild(document.createTextNode('✓'));
      } else {
        stateName.style.color = '#d83010';
        stateName.appendChild(document.createTextNode('✗'));
      }
      entry.appendChild(pluginName);
      entry.appendChild(stateName);
      this.pluginsTile.appendChild(entry);
    });
  }

  async setCollectorList() {
    while (this.collectorsTile.firstChild) {
      this.collectorsTile.removeChild(this.collectorsTile.firstChild);
    }

    let header = document.createElement('tr');
    let collectorName = document.createElement('th');
    collectorName.appendChild(document.createTextNode('Collector'));
    let stateName = document.createElement('th');
    stateName.appendChild(document.createTextNode('State'));
    header.appendChild(collectorName);
    header.appendChild(stateName);
    this.collectorsTile.appendChild(header);

    this.collectors.forEach((collector) => {
      let entry = document.createElement('tr');
      let collectorName = document.createElement('td');
      collectorName.appendChild(document.createTextNode(this.splitCamelCase(collector.name)));
      let stateName = document.createElement('td');
      stateName.appendChild(document.createTextNode(collector.state));
      entry.appendChild(collectorName);
      entry.appendChild(stateName);
      this.collectorsTile.appendChild(entry);
    });
  }

  async setControllerList() {
    while (this.controllersTile.firstChild) {
      this.controllersTile.removeChild(this.controllersTile.firstChild);
    }

    let header = document.createElement('tr');
    let controllerName = document.createElement('th');
    controllerName.appendChild(document.createTextNode('Namespace'));
    header.appendChild(controllerName);
    this.controllersTile.appendChild(header);

    this.controllers.forEach((controller) => {
      let entry = document.createElement('tr');
      let controllerEntry = document.createElement('td');
      let controllerLink = document.createElement('a');
      controllerLink.appendChild(document.createTextNode(controller));
      controllerLink.href = controller;
      controllerEntry.appendChild(controllerLink);
      entry.appendChild(controllerEntry);
      this.controllersTile.appendChild(entry);
    });
  }

  async setModelList() {
    while (this.modelTile.firstChild) {
      this.modelTile.removeChild(this.modelTile.firstChild);
    }

    let header = document.createElement('tr');
    let pluginName = document.createElement('th');
    pluginName.appendChild(document.createTextNode('Field'));
    let stateName = document.createElement('th');
    stateName.appendChild(document.createTextNode('Count'));
    header.appendChild(pluginName);
    header.appendChild(stateName);
    this.modelTile.appendChild(header);

    for (var modelField in this.model) {
      let entry = document.createElement('tr');
      let keyName = document.createElement('td');
      let keyEntry = modelField;
      keyEntry = keyEntry[0].toUpperCase() + keyEntry.slice(1);
      keyName.appendChild(document.createTextNode(keyEntry));
      let valueName = document.createElement('td');
      valueName.appendChild(document.createTextNode(this.model[modelField]));
      entry.appendChild(keyName);
      entry.appendChild(valueName);
      this.modelTile.appendChild(entry);
    }
  }

  splitCamelCase(text) {
    let words = text.match(/[A-Z][a-z]+/g);
    return words.join(' ');
  }
}


window.onload = async function(e) {
  const view = new DashboardView();
  await view.init();
  await view.refresh();

  window.setInterval(async function() {
    await view.refresh();
  }, 5000);
}

