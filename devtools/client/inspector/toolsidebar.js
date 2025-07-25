/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

"use strict";

const EventEmitter = require("devtools/shared/event-emitter");

function ToolSidebar(tabbox, panel, uid, options = {}) {
  EventEmitter.decorate(this);

  this._tabbox = tabbox;
  this._uid = uid;
  this._panelDoc = this._tabbox.ownerDocument;
  this._toolPanel = panel;
  this._options = options;

  this._tabs = [];

  if (this._options.hideTabstripe) {
    this._tabbox.setAttribute("hidetabs", "true");
  }

  this.render();

  this._toolPanel.emit("sidebar-created", this);
}

exports.ToolSidebar = ToolSidebar;

ToolSidebar.prototype = {
  TABPANEL_ID_PREFIX: "sidebar-panel-",

  // React

  get React() {
    return this._toolPanel.React;
  },

  get ReactDOM() {
    return this._toolPanel.ReactDOM;
  },

  get browserRequire() {
    return this._toolPanel.browserRequire;
  },

  get InspectorTabPanel() {
    return this._toolPanel.InspectorTabPanel;
  },

  get TabBar() {
    return this._toolPanel.TabBar;
  },

  // Rendering

  render: function() {
    const sidebar = this.TabBar({
      menuDocument: this._toolPanel._toolbox.doc,
      showAllTabsMenu: true,
      sidebarToggleButton: this._options.sidebarToggleButton,
      onSelect: this.handleSelectionChange.bind(this),
    });

    this._tabbar = this.ReactDOM.render(sidebar, this._tabbox);
  },

  /**
   * Adds all the queued tabs.
   */
  addAllQueuedTabs: function() {
    this._tabbar.addAllQueuedTabs();
  },

  /**
   * Register a side-panel tab.
   *
   * @param {String} tab uniq id
   * @param {String} title tab title
   * @param {React.Component} panel component. See `InspectorPanelTab` as an example.
   * @param {Boolean} selected true if the panel should be selected
   * @param {Number} index the position where the tab should be inserted
   */
  addTab: function(id, title, panel, selected, index) {
    this._tabbar.addTab(id, title, selected, panel, null, index);
    this.emit("new-tab-registered", id);
  },

  /**
   * Helper API for adding side-panels that use existing DOM nodes
   * (defined within inspector.xhtml) as the content.
   *
   * @param {String} tab uniq id
   * @param {String} title tab title
   * @param {Boolean} selected true if the panel should be selected
   * @param {Number} index the position where the tab should be inserted
   */
  addExistingTab: function(id, title, selected, index) {
    const panel = this.InspectorTabPanel({
      id: id,
      idPrefix: this.TABPANEL_ID_PREFIX,
      key: id,
      title: title,
    });

    this.addTab(id, title, panel, selected, index);
  },

  /**
   * Queues a side-panel tab to be added..
   *
   * @param {String} tab uniq id
   * @param {String} title tab title
   * @param {React.Component} panel component. See `InspectorPanelTab` as an example.
   * @param {Boolean} selected true if the panel should be selected
   * @param {Number} index the position where the tab should be inserted
   */
  queueTab: function(id, title, panel, selected, index) {
    this._tabbar.queueTab(id, title, selected, panel, null, index);
    this.emit("new-tab-registered", id);
  },

  /**
   * Helper API for queuing side-panels that use existing DOM nodes
   * (defined within inspector.xhtml) as the content.
   *
   * @param {String} tab uniq id
   * @param {String} title tab title
   * @param {Boolean} selected true if the panel should be selected
   * @param {Number} index the position where the tab should be inserted
   */
  queueExistingTab: function(id, title, selected, index) {
    const panel = this.InspectorTabPanel({
      id: id,
      idPrefix: this.TABPANEL_ID_PREFIX,
      key: id,
      title: title,
    });

    this.queueTab(id, title, panel, selected, index);
  },

  /**
   * Remove an existing tab.
   * @param {String} tabId The ID of the tab that was used to register it, or
   * the tab id attribute value if the tab existed before the sidebar
   * got created.
   * @param {String} tabPanelId Optional. If provided, this ID will be used
   * instead of the tabId to retrieve and remove the corresponding <tabpanel>
   */
  async removeTab(tabId, tabPanelId) {
    this._tabbar.removeTab(tabId);

    this.emit("tab-unregistered", tabId);
  },

  /**
   * Show or hide a specific tab.
   * @param {Boolean} isVisible True to show the tab/tabpanel, False to hide it.
   * @param {String} id The ID of the tab to be hidden.
   */
  toggleTab: function(isVisible, id) {
    this._tabbar.toggleTab(id, isVisible);
  },

  /**
   * Select a specific tab.
   */
  select: function(id) {
    this._tabbar.select(id);
  },

  /**
   * Return the id of the selected tab.
   */
  getCurrentTabID: function() {
    return this._currentTool;
  },

  /**
   * Returns the requested tab panel based on the id.
   * @param {String} id
   * @return {DOMNode}
   */
  getTabPanel: function(id) {
    // Search with and without the ID prefix as there might have been existing
    // tabpanels by the time the sidebar got created
    return this._panelDoc.querySelector(
      "#" + this.TABPANEL_ID_PREFIX + id + ", #" + id
    );
  },

  /**
   * Event handler.
   */
  handleSelectionChange: function(id) {
    if (this._destroyed) {
      return;
    }

    const previousTool = this._currentTool;
    if (previousTool) {
      this.emit(previousTool + "-unselected");
    }

    this._currentTool = id;

    this.emit(this._currentTool + "-selected");
    this.emit("select", this._currentTool);
  },

  /**
   * Show the sidebar.
   *
   * @param  {String} id
   *         The sidebar tab id to select.
   */
  show: function(id) {
    this._tabbox.removeAttribute("hidden");

    // If an id is given, select the corresponding sidebar tab.
    if (id) {
      this.select(id);
    }

    this.emit("show");
  },

  /**
   * Show the sidebar.
   */
  hide: function() {
    this._tabbox.setAttribute("hidden", "true");

    this.emit("hide");
  },

  /**
   * Clean-up.
   */
  destroy() {
    if (this._destroyed) {
      return;
    }
    this._destroyed = true;

    this.emit("destroy");

    this._toolPanel.emit("sidebar-destroyed", this);

    this._tabs = null;
    this._tabbox = null;
    this._panelDoc = null;
    this._toolPanel = null;
  },
};
