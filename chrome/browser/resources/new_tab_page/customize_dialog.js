// Copyright 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'chrome://resources/cr_elements/cr_button/cr_button.m.js';
import 'chrome://resources/cr_elements/cr_dialog/cr_dialog.m.js';
import 'chrome://resources/polymer/v3_0/iron-pages/iron-pages.js';
import 'chrome://resources/polymer/v3_0/iron-selector/iron-selector.js';
import './customize_backgrounds.js';
import './customize_shortcuts.js';
import './customize_themes.js';

import {html, PolymerElement} from 'chrome://resources/polymer/v3_0/polymer/polymer_bundled.min.js';

import {BrowserProxy} from './browser_proxy.js';

/**
 * Dialog that lets the user customize the NTP such as the background color or
 * image.
 */
class CustomizeDialogElement extends PolymerElement {
  static get is() {
    return 'ntp-customize-dialog';
  }

  static get template() {
    return html`{__html_template__}`;
  }

  static get properties() {
    return {
      /** @type {!newTabPage.mojom.Theme} */
      theme: Object,

      /** @private */
      selectedPage_: {
        type: String,
        value: 'backgrounds',
      },
    };
  }

  constructor() {
    super();
    /** @private {newTabPage.mojom.PageHandlerRemote} */
    this.pageHandler_ = BrowserProxy.getInstance().handler;
    /** @private {!Array<!IntersectionObserver>} */
    this.intersectionObservers_ = [];
  }

  /** @override */
  disconnectedCallback() {
    super.disconnectedCallback();
    this.intersectionObservers_.forEach(observer => {
      observer.disconnect();
    });
    this.intersectionObservers_ = [];
  }

  /** @override */
  ready() {
    super.ready();

    ['menu', 'pages'].forEach(id => {
      const container = this.$[id];
      const topProbe = document.createElement('div');
      container.prepend(topProbe);
      const bottomProbe = document.createElement('div');
      container.append(bottomProbe);
      const topBorder = document.createElement('div');
      topBorder.toggleAttribute('scroll-border', true);
      container.parentNode.insertBefore(topBorder, container);
      const bottomBorder = document.createElement('div');
      bottomBorder.toggleAttribute('scroll-border', true);
      container.parentNode.insertBefore(bottomBorder, container.nextSibling);
      const observer = new IntersectionObserver(entries => {
        entries.forEach(({target, intersectionRatio}) => {
          const show = intersectionRatio === 0;
          if (target === topProbe) {
            topBorder.toggleAttribute('show', show);
          } else if (target === bottomProbe) {
            bottomBorder.toggleAttribute('show', show);
          }
        });
      }, {root: container});
      observer.observe(topProbe);
      observer.observe(bottomProbe);
      this.intersectionObservers_.push(observer);
    });
  }

  /** @private */
  onCancelClick_() {
    this.pageHandler_.revertThemeChanges();
    this.$.dialog.cancel();
  }

  /** @private */
  onDoneClick_() {
    this.pageHandler_.confirmThemeChanges();
    this.shadowRoot.querySelector('ntp-customize-shortcuts').apply();
    this.$.dialog.close();
  }

  /**
   * @param {!Event} e
   * @private
   */
  onMenuItemKeyDown_(e) {
    if (!['Enter', ' '].includes(e.key)) {
      return;
    }
    e.preventDefault();
    e.stopPropagation();
    this.selectedPage_ = e.target.getAttribute('page-name');
  }
}

customElements.define(CustomizeDialogElement.is, CustomizeDialogElement);
