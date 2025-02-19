// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

/** @implements {settings.PrivacyPageBrowserProxy} */
class TestPrivacyPageBrowserProxy extends TestBrowserProxy {
  constructor() {
    super([
      'getMetricsReporting',
      'recordSettingsPageHistogram',
      'setMetricsReportingEnabled',
      'showManageSSLCertificates',
      'setBlockAutoplayEnabled',
      'getSecureDnsSetting',
    ]);

    /** @type {!MetricsReporting} */
    this.metricsReporting = {
      enabled: true,
      managed: true,
    };

    /**
     * @type {!SecureDnsSetting}
     * @private
     */
    this.secureDnsSetting = {
      mode: 'secure',
    };
  }

  /** @override */
  getMetricsReporting() {
    this.methodCalled('getMetricsReporting');
    return Promise.resolve(this.metricsReporting);
  }

  /** @override*/
  recordSettingsPageHistogram(value) {
    this.methodCalled('recordSettingsPageHistogram', value);
  }

  /** @override */
  setMetricsReportingEnabled(enabled) {
    this.methodCalled('setMetricsReportingEnabled', enabled);
  }

  /** @override */
  showManageSSLCertificates() {
    this.methodCalled('showManageSSLCertificates');
  }

  /** @override */
  setBlockAutoplayEnabled(enabled) {
    this.methodCalled('setBlockAutoplayEnabled', enabled);
  }

  /** @override */
  getSecureDnsSetting() {
    this.methodCalled('getSecureDnsSetting');
    return Promise.resolve(this.secureDnsSetting);
  }
}
