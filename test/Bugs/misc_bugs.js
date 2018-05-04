//-------------------------------------------------------------------------------------------------------
// Copyright (C) Microsoft. All rights reserved.
// Licensed under the MIT license. See LICENSE.txt file in the project root for full license information.
//-------------------------------------------------------------------------------------------------------

WScript.LoadScriptFile("..\\UnitTestFramework\\UnitTestFramework.js");

var tests = [
  {
    name: "calling Symbol.toPrimitive on Date prototype should not AV",
    body: function () {
         Date.prototype[Symbol.toPrimitive].call({},'strin' + 'g');
    }
  },
  {
    name: "updated stackTraceLimit should not fire re-entrancy assert",
    body: function () {
        Error.__defineGetter__('stackTraceLimit', function () { return 1;});
        assert.throws(()=> Array.prototype.map.call([]));
    }
  },
  {
    name: "Array.prototype.slice should not fire re-entrancy error when the species returns proxy",
    body: function () {
        let arr = [1, 2];
        arr.__proto__ = {
          constructor: {
            [Symbol.species]: function () {
              return new Proxy({}, {
                defineProperty(...args) {
                  return Reflect.defineProperty(...args);
                }
              });
            }
          }
        }
        Array.prototype.slice.call(arr);
    }
  },
  {
    name: "rest param under eval with arguments usage in the body should not fail assert",
    body: function () {
        f();
        function f() {
            eval("function bar(...x){arguments;}")
        }
    }
  },
  {
    name: "Token left after parsing lambda result to the syntax error",
    body: function () {
        assert.throws(()=> { eval('function foo ([ [] = () => { } = {a2:z2}]) { };'); });
    }
  },
  {
    name: "Token left after parsing lambda in ternary operator should not throw",
    body: function () {
        assert.doesNotThrow(()=> { eval('function foo () {  true ? e => {} : 1};'); });
    }
  },
  {
    name: "ArrayBuffer.slice with proxy constructor should not fail fast",
    body: function () {
      let arr = new ArrayBuffer(10);
      arr.constructor = new Proxy(ArrayBuffer, {});
      
      arr.slice(1,2);
    }
  },
  {
    name: "Large proxy chain should not cause IsConstructor to crash on stack overflow",
    body: function () {
      let p = new Proxy(Object, {});
      for (let  i=0; i<20000; ++i)
      {
          p = new Proxy(p, {});
      }
      try
      {
          let a = new p();
      }
      catch(e)
      {
      }
    }
  },
  {
    name: "splice an array which has getter/setter at 4294967295 should not fail due to re-entrancy error",
    body: function () {
        var base = 4294967290;
        var arr = [];
        for (var i = 0; i < 10; i++) {
            arr[base + i] = 100 + i;
        }
        Object.defineProperty(arr, 4294967295, {
          get: function () { }, set : function(b) {  }
            }
        );

        assert.throws(()=> {arr.splice(4294967290, 0, 200, 201, 202, 203, 204, 205, 206);});
    }
  }
  
];

testRunner.runTests(tests, { verbose: WScript.Arguments[0] != "summary" });
