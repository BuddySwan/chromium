// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

(async function() {
  TestRunner.addResult(`Tests Application Panel WebSQL support.\n`);
  await TestRunner.loadModule('application_test_runner');
    // Note: every test that uses a storage API must manually clean-up state from previous tests.
  await ApplicationTestRunner.resetState();

  await TestRunner.loadModule('console_test_runner');
  await TestRunner.showPanel('resources');
  await TestRunner.evaluateInPagePromise(`
      function parse(val) {
          // This is here for the JSON file imported via the script tag below
      }
    `);
  await TestRunner.addScriptTag('../resources/json-value.js');

  function dump(node, prefix) {
    for (var child of node.children()) {
      TestRunner.addResult(prefix + child.listItemElement.textContent + (child.selected ? ' (selected)' : ''));
      dump(child, prefix + '  ');
    }
  }

  function dumpCurrentState(label) {
    TestRunner.addResult(label);
    dump(UI.panels.resources._sidebar._sidebarTree.rootElement(), '');
  }

  async function createTable(queryView) {
    queryView._prompt.setText('CREATE TABLE table1 (id INTEGER PRIMARY KEY ASC, text_field TEXT)');
    queryView._enterKeyPressed(new KeyboardEvent('keydown'));
    await queryView.once(Resources.DatabaseQueryView.Events.SchemaUpdated);
    return new Promise(resolve => setTimeout(resolve));
  }
  UI.viewManager.showView('resources');
  dumpCurrentState('Initial state:');

  await TestRunner.evaluateInPagePromise(
      'openDatabase("inspector-test-db", "1.0", "Database for inspector test", 1024*1024)');

  var parent = UI.panels.resources._sidebar._sidebarTree.rootElement();
  var databaseElement = ApplicationTestRunner.findTreeElement(parent, ['Storage', 'Web SQL', 'inspector-test-db']);

  TestRunner.addResult('Found: ' + !!databaseElement);

  if (!databaseElement)
    return;

  databaseElement.select();
  dumpCurrentState('Database created state:');

  var queryView = UI.panels.resources.visibleView;
  if (!queryView instanceof Resources.DatabaseQueryView) {
    TestRunner.addResult('Not a Resources.DatabaseQueryView');
    return;
  }

  await createTable(queryView);
  while (!ApplicationTestRunner.findTreeElement(databaseElement, ['table1'])) {
    databaseElement.expand();
    await new Promise(resolve => setTimeout(resolve));
  }

  dumpCurrentState('Table added:');
  TestRunner.completeTest();
})();
