'use strict';

// These tests can be imported as a module or added as a script to tests lite
// bindings.

const kTestMessage = 'hello there';
const kTestNumbers = [0, 1, 1, 2, 3, 5, 8, 13, 21];

class TargetImpl {
  constructor() {
    this.numPokes = 0;
    this.target = new liteJsTest.mojom.TestMessageTargetReceiver(this);
  }

  poke() { this.numPokes++; }
  ping() { return Promise.resolve(); }
  repeat(message, numbers) { return {message: message, numbers: numbers}; }
  echo(nested) { return Promise.resolve({nested: nested}); }
  deconstruct(test_struct) {}
  flatten(values) {}
  flattenUnions(unions) {}
  requestSubinterface(request, client) {}
}

promise_test(() => {
  let impl = new TargetImpl;
  let remote = impl.target.$.bindNewPipeAndPassRemote();
  remote.poke();
  return remote.ping().then(() => {
    assert_equals(impl.numPokes, 1);
  });
}, 'messages with replies return Promises that resolve on reply received');

promise_test(() => {
  let impl = new TargetImpl;
  let remote = impl.target.$.bindNewPipeAndPassRemote();
  return remote.repeat(kTestMessage, kTestNumbers)
               .then(reply => {
                 assert_equals(reply.message, kTestMessage);
                 assert_array_equals(reply.numbers, kTestNumbers);
               });
}, 'implementations can reply with multiple reply arguments');

promise_test(() => {
  let impl = new TargetImpl;
  let remote = impl.target.$.bindNewPipeAndPassRemote();
  const enumValue = liteJsTest.mojom.TestMessageTarget_NestedEnum.kFoo;
  return remote.echo(enumValue)
               .then(({nested}) => assert_equals(nested, enumValue));
}, 'nested enums are usable as arguments and responses.');

promise_test(async (t) => {
  const impl = new TargetImpl;
  const remote = impl.target.$.bindNewPipeAndPassRemote();

  await remote.ping();
  remote.$.close();

  await promise_rejects(t, new Error(), remote.ping());
}, 'after the pipe is closed all future calls should fail');

promise_test(async (t) => {
  const impl = new TargetImpl;
  const remote = impl.target.$.bindNewPipeAndPassRemote();

  // None of these promises should successfully resolve because we are
  // immediately closing the pipe.
  const promises = []
  for (let i = 0; i < 10; i++) {
    promises.push(remote.ping());
  }

  remote.$.close();

  for (const promise of promises) {
    await promise_rejects(t, new Error(), promise);
  }
}, 'closing the pipe drops any pending messages');

promise_test(() => {
  let impl = new TargetImpl;

  // Intercept any browser-bound request for TestMessageTarget and bind it
  // instead to the local |impl| object.
  let interceptor = new MojoInterfaceInterceptor(
      liteJsTest.mojom.TestMessageTarget.$interfaceName);
  interceptor.oninterfacerequest = e => {
    impl.target.$.bindHandle(e.handle);
  }
  interceptor.start();

  let remote = liteJsTest.mojom.TestMessageTarget.getRemote();
  remote.poke();
  return remote.ping().then(() => {
    assert_equals(impl.numPokes, 1);
  });
}, 'getRemote() attempts to send requests to the frame host');

promise_test(() => {
  let router = new liteJsTest.mojom.TestMessageTargetCallbackRouter;
  let remote = router.$.bindNewPipeAndPassRemote();
  return new Promise(resolve => {
    router.poke.addListener(resolve);
    remote.poke();
  });
}, 'basic generated CallbackRouter behavior works as intended');

promise_test(() => {
  let router = new liteJsTest.mojom.TestMessageTargetCallbackRouter;
  let remote = router.$.bindNewPipeAndPassRemote();
  let numPokes = 0;
  router.poke.addListener(() => ++numPokes);
  router.ping.addListener(() => Promise.resolve());
  remote.poke();
  return remote.ping().then(() => assert_equals(numPokes, 1));
}, 'CallbackRouter listeners can reply to messages');

promise_test(() => {
  let router = new liteJsTest.mojom.TestMessageTargetCallbackRouter;
  let remote = router.$.bindNewPipeAndPassRemote();
  router.repeat.addListener(
    (message, numbers) => ({message: message, numbers: numbers}));
  return remote.repeat(kTestMessage, kTestNumbers)
               .then(reply => {
                 assert_equals(reply.message, kTestMessage);
                 assert_array_equals(reply.numbers, kTestNumbers);
               });
}, 'CallbackRouter listeners can reply with multiple reply arguments');

promise_test(() => {
  let targetRouter = new liteJsTest.mojom.TestMessageTargetCallbackRouter;
  let targetRemote = targetRouter.$.bindNewPipeAndPassRemote();
  let subinterfaceRouter = new liteJsTest.mojom.SubinterfaceCallbackRouter;
  targetRouter.requestSubinterface.addListener((request, client) => {
    let values = [];
    subinterfaceRouter.$.bindHandle(request.handle);
    subinterfaceRouter.push.addListener(value => values.push(value));
    subinterfaceRouter.flush.addListener(() => {
      client.didFlush(values);
      values = [];
    });
  });

  let clientRouter = new liteJsTest.mojom.SubinterfaceClientCallbackRouter;
  let subinterfaceRemote = new liteJsTest.mojom.SubinterfaceRemote;
  targetRemote.requestSubinterface(
    subinterfaceRemote.$.bindNewPipeAndPassReceiver(),
    clientRouter.$.bindNewPipeAndPassRemote());
  return new Promise(resolve => {
    clientRouter.didFlush.addListener(values => {
      assert_array_equals(values, kTestNumbers);
      resolve();
    });

    kTestNumbers.forEach(n => subinterfaceRemote.push(n));
    subinterfaceRemote.flush();
  });
}, 'can send and receive interface requests and proxies');

promise_test(() => {
  const targetRouter = new liteJsTest.mojom.TestMessageTargetCallbackRouter;
  const targetRemote = targetRouter.$.bindNewPipeAndPassRemote();
  targetRouter.deconstruct.addListener(({x, y, z}) => ({
    x: x,
    y: y,
    z: z
  }));

  return targetRemote.deconstruct({x: 1}).then(reply => {
    assert_equals(reply.x, 1);
    assert_equals(reply.y, 2);
    assert_equals(reply.z, 1);
  });
}, 'structs with default values from nested enums and constants are ' +
   'correctly serialized');

promise_test(() => {
  const targetRouter = new liteJsTest.mojom.TestMessageTargetCallbackRouter;
  const targetRemote = targetRouter.$.bindNewPipeAndPassRemote();
  targetRouter.flatten.addListener(values => ({values: values.map(v => v.x)}));
  return targetRemote.flatten([{x: 1}, {x: 2}, {x: 3}]).then(reply => {
    assert_array_equals(reply.values, [1, 2, 3]);
  });
}, 'regression test for complex array serialization');

promise_test(() => {
  const targetRouter = new liteJsTest.mojom.TestMessageTargetCallbackRouter;
  const targetRemote = targetRouter.$.bindNewPipeAndPassRemote();
  targetRouter.flattenUnions.addListener(unions => {
    return {x: unions.filter(u => u.x !== undefined).map(u => u.x),
            s: unions.filter(u => u.s !== undefined).map(u => u.s.x)};
  });

  return targetRemote.flattenUnions(
    [{x: 1}, {x: 2}, {s: {x: 3}}, {s: {x: 4}}, {x: 5}, {s: {x: 6}}])
                     .then(reply => {
                       assert_array_equals(reply.x, [1, 2, 5]);
                       assert_array_equals(reply.s, [3, 4, 6]);
                     });
}, 'can serialize and deserialize unions');

promise_test(() => {
  let impl = new TargetImpl;
  let remote = impl.target.$.bindNewPipeAndPassRemote();

  // Poke a bunch of times. These should never race with the assertion below,
  // because the |flushForTesting| request/response is ordered against other
  // messages on |remote|.
  const kNumPokes = 100;
  for (let i = 0; i < kNumPokes; ++i)
    remote.poke();
  return remote.$.flushForTesting().then(() => {
    assert_equals(impl.numPokes, kNumPokes);
  });
}, 'can use generated flushForTesting API for synchronization in tests');

promise_test(async(t) => {
  const impl = new TargetImpl;
  const remote = impl.target.$.bindNewPipeAndPassRemote();
  const disconnectPromise = new Promise(resolve => impl.target.onConnectionError.addListener(resolve));
  remote.$.close();
  return disconnectPromise;
}, 'InterfaceTarget connection error handler runs when set on an Interface object');

promise_test(() => {
  const router = new liteJsTest.mojom.TestMessageTargetCallbackRouter;
  const remote = router.$.bindNewPipeAndPassRemote();
  const disconnectPromise = new Promise(resolve => router.onConnectionError.addListener(resolve));
  remote.$.close();
  return disconnectPromise;
}, 'InterfaceTarget connection error handler runs when set on an InterfaceCallbackRouter object');
