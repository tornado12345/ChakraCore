//-------------------------------------------------------------------------------------------------------
// Copyright (C) Microsoft. All rights reserved.
// Licensed under the MIT license. See LICENSE.txt file in the project root for full license information.
//-------------------------------------------------------------------------------------------------------

"use strict";

(function (intrinsic) {
    var platform = intrinsic.JsBuiltIn;

    let FunctionsEnum = {
        ArrayValues: { className: "Array", methodName: "values", argumentsCount: 0, forceInline: true /*optional*/, alias: "Symbol.iterator" },
        ArrayKeys: { className: "Array", methodName: "keys", argumentsCount: 0, forceInline: true /*optional*/ },
        ArrayEntries: { className: "Array", methodName: "entries", argumentsCount: 0, forceInline: true /*optional*/ }
    };

    var setPrototype = platform.builtInSetPrototype;
    var _objectDefineProperty = platform.builtInJavascriptObjectEntryDefineProperty;
    var Symbol = platform.Symbol;
    var CreateObject = platform.builtInJavascriptObjectCreate;

    platform.registerChakraLibraryFunction("ArrayIterator", function (arrayObj, iterationKind) {
        "use strict";
        __chakraLibrary.InitInternalProperties(this, 4, "__$arrayObj$__", "__$nextIndex$__", "__$kind$__", "__$internalDone$__");
        this.__$arrayObj$__ = arrayObj;
        this.__$nextIndex$__ = 0;
        this.__$kind$__ = iterationKind;
        this.__$internalDone$__ = false; // We use this additional property to enable hoisting load of arrayObj outside the loop, we write to this property instead of the arrayObj
    });

    // ArrayIterator's prototype is the C++ Iterator, which is also the prototype for StringIterator, MapIterator etc
    var iteratorPrototype = platform.GetIteratorPrototype();
    // Establish prototype chain here
    __chakraLibrary.ArrayIterator.prototype = CreateObject(iteratorPrototype);
    __chakraLibrary.raiseNeedObjectOfType = platform.raiseNeedObjectOfType;
    __chakraLibrary.raiseThis_NullOrUndefined = platform.raiseThis_NullOrUndefined;

    _objectDefineProperty(__chakraLibrary.ArrayIterator.prototype, 'next',
        // Object's getter and setter can get overriden on the prototype, in that case while setting the value attributes, we will end up with TypeError
        // So, we need to set the prototype of attributes to null
        setPrototype({
            value: function () {
                "use strict";
                let o = this;

                if (!(o instanceof __chakraLibrary.ArrayIterator)) {
                    __chakraLibrary.raiseNeedObjectOfType("Array Iterator.prototype.next", "Array Iterator");
                }

                let a = o.__$arrayObj$__;
                let value, done;

                if (o.__$internalDone$__ === true) {
                    value = undefined;
                    done = true;
                } else {
                    let index = o.__$nextIndex$__;
                    let len = __chakraLibrary.isArray(a) ? a.length : __chakraLibrary.GetLength(a);

                    if (index < len) { // < comparison should happen instead of >= , because len can be NaN
                        let itemKind = o.__$kind$__;

                        o.__$nextIndex$__ = index + 1;

                        if (itemKind === 1 /*ArrayIterationKind.Value*/) {
                            value = a[index];
                        } else if (itemKind === 0 /*ArrayIterationKind.Key*/) { // TODO (megupta) : Use clean enums here ?
                            value = index;
                        } else {
                            let elementKey = index;
                            let elementValue = a[index];
                            value = [elementKey, elementValue];
                        }
                        done = false;
                    } else {
                        o.__$internalDone$__ = true;
                        value = undefined;
                        done = true;
                    }
                }
                return { value: value, done: done };
            },
            writable: true,
            enumerable: false,
            configurable: true
        }, null)
    );

    _objectDefineProperty(__chakraLibrary.ArrayIterator.prototype, Symbol.toStringTag, setPrototype({ value: "Array Iterator", writable: false, enumerable: false, configurable: true }, null));

    _objectDefineProperty(__chakraLibrary.ArrayIterator.prototype.next, 'length', setPrototype({ value: 0, writable: false, enumerable: false, configurable: true }, null));

    _objectDefineProperty(__chakraLibrary.ArrayIterator.prototype.next, 'name', setPrototype({ value: "next", writable: false, enumerable: false, configurable: true }, null));

    platform.registerChakraLibraryFunction("CreateArrayIterator", function (arrayObj, iterationKind) {
        "use strict";
        return new __chakraLibrary.ArrayIterator(arrayObj, iterationKind);
    });

    platform.registerFunction(FunctionsEnum.ArrayKeys, function () {
        "use strict";
        if (this === null || this === undefined) {
            __chakraLibrary.raiseThis_NullOrUndefined("Array.prototype.keys");
        }
        let o = __chakraLibrary.Object(this);
        return __chakraLibrary.CreateArrayIterator(o, 0 /* ArrayIterationKind.Key*/);
    });

    platform.registerFunction(FunctionsEnum.ArrayValues, function () {
        "use strict";
        if (this === null || this === undefined) {
            __chakraLibrary.raiseThis_NullOrUndefined("Array.prototype.values");
        }
        let o = __chakraLibrary.Object(this);
        return __chakraLibrary.CreateArrayIterator(o, 1 /* ArrayIterationKind.Value*/);
    });

    platform.registerFunction(FunctionsEnum.ArrayEntries, function () {
        "use strict";
        if (this === null || this === undefined) {
            __chakraLibrary.raiseThis_NullOrUndefined("Array.prototype.entries");
        }
        let o = __chakraLibrary.Object(this);
        return __chakraLibrary.CreateArrayIterator(o, 2 /* ArrayIterationKind.KeyAndValue*/);
    });
});