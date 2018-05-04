//-------------------------------------------------------------------------------------------------------
// Copyright (C) Microsoft. All rights reserved.
// Licensed under the MIT license. See LICENSE.txt file in the project root for full license information.
//-------------------------------------------------------------------------------------------------------
#include "RuntimeLibraryPch.h"

namespace Js
{
    JavascriptPromise::JavascriptPromise(DynamicType * type)
        : DynamicObject(type)
    {
        Assert(type->GetTypeId() == TypeIds_Promise);

        this->status = PromiseStatusCode_Undefined;
        this->isHandled = false;
        this->result = nullptr;
        this->resolveReactions = nullptr;
        this->rejectReactions = nullptr;
    }

    // Promise() as defined by ES 2016 Sections 25.4.3.1
    Var JavascriptPromise::NewInstance(RecyclableObject* function, CallInfo callInfo, ...)
    {
        PROBE_STACK(function->GetScriptContext(), Js::Constants::MinStackDefault);
        ARGUMENTS(args, callInfo);
        AssertMsg(args.Info.Count > 0, "Should always have implicit 'this'");

        ScriptContext* scriptContext = function->GetScriptContext();
        JavascriptLibrary* library = scriptContext->GetLibrary();

        CHAKRATEL_LANGSTATS_INC_LANGFEATURECOUNT(ES6, Promise, scriptContext);

        // SkipDefaultNewObject function flag should have prevented the default object from
        // being created, except when call true a host dispatch
        Var newTarget = args.GetNewTarget();
        bool isCtorSuperCall = JavascriptOperators::GetAndAssertIsConstructorSuperCall(args);

        AUTO_TAG_NATIVE_LIBRARY_ENTRY(function, callInfo, _u("Promise"));

        // 1. If NewTarget is undefined, throw a TypeError exception.
        if ((callInfo.Flags & CallFlags_New) != CallFlags_New || (newTarget != nullptr && JavascriptOperators::IsUndefined(newTarget)))
        {
            JavascriptError::ThrowTypeError(scriptContext, JSERR_ClassConstructorCannotBeCalledWithoutNew, _u("Promise"));
        }

        // 2. If IsCallable(executor) is false, throw a TypeError exception.
        if (args.Info.Count < 2 || !JavascriptConversion::IsCallable(args[1]))
        {
            JavascriptError::ThrowTypeError(scriptContext, JSERR_FunctionArgument_NeedFunction, _u("Promise"));
        }
        RecyclableObject* executor = RecyclableObject::FromVar(args[1]);

        // 3. Let promise be ? OrdinaryCreateFromConstructor(NewTarget, "%PromisePrototype%", <<[[PromiseState]], [[PromiseResult]], [[PromiseFulfillReactions]], [[PromiseRejectReactions]], [[PromiseIsHandled]] >>).
        JavascriptPromise* promise = library->CreatePromise();
        if (isCtorSuperCall)
        {
            JavascriptOperators::OrdinaryCreateFromConstructor(RecyclableObject::FromVar(newTarget), promise, library->GetPromisePrototype(), scriptContext);
        }

        JavascriptPromiseResolveOrRejectFunction* resolve;
        JavascriptPromiseResolveOrRejectFunction* reject;

        // 4. Set promise's [[PromiseState]] internal slot to "pending".
        // 5. Set promise's [[PromiseFulfillReactions]] internal slot to a new empty List.
        // 6. Set promise's [[PromiseRejectReactions]] internal slot to a new empty List.
        // 7. Set promise's [[PromiseIsHandled]] internal slot to false.
        // 8. Let resolvingFunctions be CreateResolvingFunctions(promise).
        InitializePromise(promise, &resolve, &reject, scriptContext);

        JavascriptExceptionObject* exception = nullptr;

        // 9. Let completion be Call(executor, undefined, << resolvingFunctions.[[Resolve]], resolvingFunctions.[[Reject]] >>).
        try
        {
            CALL_FUNCTION(scriptContext->GetThreadContext(),
                executor, CallInfo(CallFlags_Value, 3),
                library->GetUndefined(),
                resolve,
                reject);
        }
        catch (const JavascriptException& err)
        {
            exception = err.GetAndClear();
        }

        if (exception != nullptr)
        {
            // 10. If completion is an abrupt completion, then
            //    a. Perform ? Call(resolvingFunctions.[[Reject]], undefined, << completion.[[Value]] >>).
            TryRejectWithExceptionObject(exception, reject, scriptContext);
        }

        // 11. Return promise.
        return promise;
    }

    void JavascriptPromise::InitializePromise(JavascriptPromise* promise, JavascriptPromiseResolveOrRejectFunction** resolve, JavascriptPromiseResolveOrRejectFunction** reject, ScriptContext* scriptContext)
    {
        Assert(promise->status == PromiseStatusCode_Undefined);
        Assert(resolve);
        Assert(reject);

        Recycler* recycler = scriptContext->GetRecycler();
        JavascriptLibrary* library = scriptContext->GetLibrary();

        promise->status = PromiseStatusCode_Unresolved;

        promise->resolveReactions = RecyclerNew(recycler, JavascriptPromiseReactionList, recycler);
        promise->rejectReactions = RecyclerNew(recycler, JavascriptPromiseReactionList, recycler);

        JavascriptPromiseResolveOrRejectFunctionAlreadyResolvedWrapper* alreadyResolvedRecord = RecyclerNewStructZ(scriptContext->GetRecycler(), JavascriptPromiseResolveOrRejectFunctionAlreadyResolvedWrapper);
        alreadyResolvedRecord->alreadyResolved = false;

        *resolve = library->CreatePromiseResolveOrRejectFunction(EntryResolveOrRejectFunction, promise, false, alreadyResolvedRecord);
        *reject = library->CreatePromiseResolveOrRejectFunction(EntryResolveOrRejectFunction, promise, true, alreadyResolvedRecord);
    }

    bool JavascriptPromise::Is(Var aValue)
    {
        return Js::JavascriptOperators::GetTypeId(aValue) == TypeIds_Promise;
    }

    JavascriptPromise* JavascriptPromise::FromVar(Js::Var aValue)
    {
        AssertOrFailFastMsg(Is(aValue), "Ensure var is actually a 'JavascriptPromise'");

        return static_cast<JavascriptPromise *>(aValue);
    }

    JavascriptPromise* JavascriptPromise::UnsafeFromVar(Js::Var aValue)
    {
        AssertMsg(Is(aValue), "Ensure var is actually a 'JavascriptPromise'");

        return static_cast<JavascriptPromise *>(aValue);
    }

    BOOL JavascriptPromise::GetDiagValueString(StringBuilder<ArenaAllocator>* stringBuilder, ScriptContext* requestContext)
    {
        stringBuilder->AppendCppLiteral(_u("[...]"));

        return TRUE;
    }

    BOOL JavascriptPromise::GetDiagTypeString(StringBuilder<ArenaAllocator>* stringBuilder, ScriptContext* requestContext)
    {
        stringBuilder->AppendCppLiteral(_u("Promise"));
        return TRUE;
    }

    JavascriptPromiseReactionList* JavascriptPromise::GetResolveReactions()
    {
        return this->resolveReactions;
    }

    JavascriptPromiseReactionList* JavascriptPromise::GetRejectReactions()
    {
        return this->rejectReactions;
    }

    // Promise.all as described in ES 2015 Section 25.4.4.1
    Var JavascriptPromise::EntryAll(RecyclableObject* function, CallInfo callInfo, ...)
    {
        PROBE_STACK(function->GetScriptContext(), Js::Constants::MinStackDefault);
        ARGUMENTS(args, callInfo);
        Assert(!(callInfo.Flags & CallFlags_New));

        ScriptContext* scriptContext = function->GetScriptContext();

        AUTO_TAG_NATIVE_LIBRARY_ENTRY(function, callInfo, _u("Promise.all"));

        // 1. Let C be the this value.
        Var constructor = args[0];

        // 2. If Type(C) is not Object, throw a TypeError exception.
        if (!JavascriptOperators::IsObject(constructor))
        {
            JavascriptError::ThrowTypeError(scriptContext, JSERR_This_NeedObject, _u("Promise.all"));
        }

        JavascriptLibrary* library = scriptContext->GetLibrary();
        Var iterable;

        if (args.Info.Count > 1)
        {
            iterable = args[1];
        }
        else
        {
            iterable = library->GetUndefined();
        }

        // 3. Let promiseCapability be NewPromiseCapability(C).
        JavascriptPromiseCapability* promiseCapability = NewPromiseCapability(constructor, scriptContext);

        // We know that constructor is an object at this point - further, we even know that it is a constructor - because NewPromiseCapability
        // would throw otherwise. That means we can safely cast constructor into a RecyclableObject* now and avoid having to perform ToObject
        // as part of the Invoke operation performed inside the loop below.
        RecyclableObject* constructorObject = RecyclableObject::FromVar(constructor);

        uint32 index = 0;
        JavascriptArray* values = nullptr;

        // We can't use a simple counter for the remaining element count since each Promise.all Resolve Element Function needs to know how many
        // elements are remaining when it runs and needs to update that counter for all other functions created by this call to Promise.all.
        // We can't just use a static variable, either, since this element count is only used for the Promise.all Resolve Element Functions created
        // by this call to Promise.all.
        JavascriptPromiseAllResolveElementFunctionRemainingElementsWrapper* remainingElementsWrapper = RecyclerNewStructZ(scriptContext->GetRecycler(), JavascriptPromiseAllResolveElementFunctionRemainingElementsWrapper);
        remainingElementsWrapper->remainingElements = 1;

        JavascriptExceptionObject* exception = nullptr;
        try
        {
            // 4. Let iterator be GetIterator(iterable).
            RecyclableObject* iterator = JavascriptOperators::GetIterator(iterable, scriptContext);
            values = library->CreateArray(0);

            JavascriptOperators::DoIteratorStepAndValue(iterator, scriptContext, [&](Var next)
            {
                Var resolveVar = JavascriptOperators::GetProperty(constructorObject, Js::PropertyIds::resolve, scriptContext);

                if (!JavascriptConversion::IsCallable(resolveVar))
                {
                    JavascriptError::ThrowTypeError(scriptContext, JSERR_NeedFunction);
                }

                RecyclableObject* resolveFunc = RecyclableObject::FromVar(resolveVar);

                Var nextPromise = CALL_FUNCTION(scriptContext->GetThreadContext(),
                    resolveFunc, Js::CallInfo(CallFlags_Value, 2),
                    constructorObject,
                    next);

                JavascriptPromiseAllResolveElementFunction* resolveElement = library->CreatePromiseAllResolveElementFunction(EntryAllResolveElementFunction, index, values, promiseCapability, remainingElementsWrapper);

                remainingElementsWrapper->remainingElements++;

                RecyclableObject* nextPromiseObject;

                if (!JavascriptConversion::ToObject(nextPromise, scriptContext, &nextPromiseObject))
                {
                    JavascriptError::ThrowTypeError(scriptContext, JSERR_NeedObject);
                }

                Var thenVar = JavascriptOperators::GetProperty(nextPromiseObject, Js::PropertyIds::then, scriptContext);

                if (!JavascriptConversion::IsCallable(thenVar))
                {
                    JavascriptError::ThrowTypeError(scriptContext, JSERR_NeedFunction);
                }

                RecyclableObject* thenFunc = RecyclableObject::FromVar(thenVar);

                CALL_FUNCTION(scriptContext->GetThreadContext(),
                    thenFunc, Js::CallInfo(CallFlags_Value, 3),
                    nextPromiseObject,
                    resolveElement,
                    promiseCapability->GetReject());

                index++;
            });
        }
        catch (const JavascriptException& err)
        {
            exception = err.GetAndClear();
        }

        if (exception != nullptr)
        {
            TryRejectWithExceptionObject(exception, promiseCapability->GetReject(), scriptContext);

            // We need to explicitly return here to make sure we don't resolve in case index == 0 here.
            // That would happen if GetIterator or IteratorValue throws an exception in the first iteration.
            return promiseCapability->GetPromise();
        }

        remainingElementsWrapper->remainingElements--;

        // We want this call to happen outside the try statement because if it throws, we aren't supposed to reject the promise.
        if (remainingElementsWrapper->remainingElements == 0)
        {
            Assert(values != nullptr);

            TryCallResolveOrRejectHandler(promiseCapability->GetResolve(), values, scriptContext);
        }

        return promiseCapability->GetPromise();
    }

    // Promise.prototype.catch as defined in ES 2015 Section 25.4.5.1
    Var JavascriptPromise::EntryCatch(RecyclableObject* function, CallInfo callInfo, ...)
    {
        PROBE_STACK(function->GetScriptContext(), Js::Constants::MinStackDefault);
        ARGUMENTS(args, callInfo);
        Assert(!(callInfo.Flags & CallFlags_New));

        ScriptContext* scriptContext = function->GetScriptContext();

        AUTO_TAG_NATIVE_LIBRARY_ENTRY(function, callInfo, _u("Promise.prototype.catch"));

        RecyclableObject* promise;

        if (!JavascriptConversion::ToObject(args[0], scriptContext, &promise))
        {
            JavascriptError::ThrowTypeError(scriptContext, JSERR_This_NeedObject, _u("Promise.prototype.catch"));
        }

        Var funcVar = JavascriptOperators::GetProperty(promise, Js::PropertyIds::then, scriptContext);

        if (!JavascriptConversion::IsCallable(funcVar))
        {
            JavascriptError::ThrowTypeError(scriptContext, JSERR_FunctionArgument_NeedFunction, _u("Promise.prototype.catch"));
        }

        Var onRejected;
        RecyclableObject* undefinedVar = scriptContext->GetLibrary()->GetUndefined();

        if (args.Info.Count > 1)
        {
            onRejected = args[1];
        }
        else
        {
            onRejected = undefinedVar;
        }

        RecyclableObject* func = RecyclableObject::FromVar(funcVar);

        return CALL_FUNCTION(scriptContext->GetThreadContext(),
            func, Js::CallInfo(CallFlags_Value, 3),
            promise,
            undefinedVar,
            onRejected);
    }

    // Promise.race as described in ES 2015 Section 25.4.4.3
    Var JavascriptPromise::EntryRace(RecyclableObject* function, CallInfo callInfo, ...)
    {
        PROBE_STACK(function->GetScriptContext(), Js::Constants::MinStackDefault);
        ARGUMENTS(args, callInfo);
        Assert(!(callInfo.Flags & CallFlags_New));

        ScriptContext* scriptContext = function->GetScriptContext();

        AUTO_TAG_NATIVE_LIBRARY_ENTRY(function, callInfo, _u("Promise.race"));

        // 1. Let C be the this value.
        Var constructor = args[0];

        // 2. If Type(C) is not Object, throw a TypeError exception.
        if (!JavascriptOperators::IsObject(constructor))
        {
            JavascriptError::ThrowTypeError(scriptContext, JSERR_This_NeedObject, _u("Promise.race"));
        }

        Var undefinedVar = scriptContext->GetLibrary()->GetUndefined();
        Var iterable;

        if (args.Info.Count > 1)
        {
            iterable = args[1];
        }
        else
        {
            iterable = undefinedVar;
        }

        // 3. Let promiseCapability be NewPromiseCapability(C).
        JavascriptPromiseCapability* promiseCapability = NewPromiseCapability(constructor, scriptContext);

        // We know that constructor is an object at this point - further, we even know that it is a constructor - because NewPromiseCapability
        // would throw otherwise. That means we can safely cast constructor into a RecyclableObject* now and avoid having to perform ToObject
        // as part of the Invoke operation performed inside the loop below.
        RecyclableObject* constructorObject = RecyclableObject::FromVar(constructor);
        JavascriptExceptionObject* exception = nullptr;

        try
        {
            // 4. Let iterator be GetIterator(iterable).
            RecyclableObject* iterator = JavascriptOperators::GetIterator(iterable, scriptContext);

            JavascriptOperators::DoIteratorStepAndValue(iterator, scriptContext, [&](Var next)
            {
                Var resolveVar = JavascriptOperators::GetProperty(constructorObject, Js::PropertyIds::resolve, scriptContext);

                if (!JavascriptConversion::IsCallable(resolveVar))
                {
                    JavascriptError::ThrowTypeError(scriptContext, JSERR_NeedFunction);
                }

                RecyclableObject* resolveFunc = RecyclableObject::FromVar(resolveVar);

                Var nextPromise = CALL_FUNCTION(scriptContext->GetThreadContext(),
                    resolveFunc, Js::CallInfo(CallFlags_Value, 2),
                    constructorObject,
                    next);

                RecyclableObject* nextPromiseObject;

                if (!JavascriptConversion::ToObject(nextPromise, scriptContext, &nextPromiseObject))
                {
                    JavascriptError::ThrowTypeError(scriptContext, JSERR_NeedObject);
                }

                Var thenVar = JavascriptOperators::GetProperty(nextPromiseObject, Js::PropertyIds::then, scriptContext);

                if (!JavascriptConversion::IsCallable(thenVar))
                {
                    JavascriptError::ThrowTypeError(scriptContext, JSERR_NeedFunction);
                }

                RecyclableObject* thenFunc = RecyclableObject::FromVar(thenVar);

                CALL_FUNCTION(scriptContext->GetThreadContext(),
                    thenFunc, Js::CallInfo(CallFlags_Value, 3),
                    nextPromiseObject,
                    promiseCapability->GetResolve(),
                    promiseCapability->GetReject());

            });
        }
        catch (const JavascriptException& err)
        {
            exception = err.GetAndClear();
        }

        if (exception != nullptr)
        {
            TryRejectWithExceptionObject(exception, promiseCapability->GetReject(), scriptContext);
        }

        return promiseCapability->GetPromise();
    }

    // Promise.reject as described in ES 2015 Section 25.4.4.4
    Var JavascriptPromise::EntryReject(RecyclableObject* function, CallInfo callInfo, ...)
    {
        PROBE_STACK(function->GetScriptContext(), Js::Constants::MinStackDefault);
        ARGUMENTS(args, callInfo);
        Assert(!(callInfo.Flags & CallFlags_New));

        ScriptContext* scriptContext = function->GetScriptContext();

        AUTO_TAG_NATIVE_LIBRARY_ENTRY(function, callInfo, _u("Promise.reject"));

        // 1. Let C be the this value.
        Var constructor = args[0];

        // 2. If Type(C) is not Object, throw a TypeError exception.
        if (!JavascriptOperators::IsObject(constructor))
        {
            JavascriptError::ThrowTypeError(scriptContext, JSERR_This_NeedObject, _u("Promise.reject"));
        }

        Var r;

        if (args.Info.Count > 1)
        {
            r = args[1];
        }
        else
        {
            r = scriptContext->GetLibrary()->GetUndefined();
        }

        // 3. Let promiseCapability be NewPromiseCapability(C).
        // 4. Perform ? Call(promiseCapability.[[Reject]], undefined, << r >>).
        // 5. Return promiseCapability.[[Promise]].
        return CreateRejectedPromise(r, scriptContext, constructor);
    }

    // Promise.resolve as described in ES 2015 Section 25.4.4.5
    Var JavascriptPromise::EntryResolve(RecyclableObject* function, CallInfo callInfo, ...)
    {
        PROBE_STACK(function->GetScriptContext(), Js::Constants::MinStackDefault);
        ARGUMENTS(args, callInfo);
        Assert(!(callInfo.Flags & CallFlags_New));

        ScriptContext* scriptContext = function->GetScriptContext();

        AUTO_TAG_NATIVE_LIBRARY_ENTRY(function, callInfo, _u("Promise.resolve"));

        Var x;

        // 1. Let C be the this value.
        Var constructor = args[0];

        // 2. If Type(C) is not Object, throw a TypeError exception.
        if (!JavascriptOperators::IsObject(constructor))
        {
            JavascriptError::ThrowTypeError(scriptContext, JSERR_This_NeedObject, _u("Promise.resolve"));
        }

        if (args.Info.Count > 1)
        {
            x = args[1];
        }
        else
        {
            x = scriptContext->GetLibrary()->GetUndefined();
        }

        // 3. If IsPromise(x) is true,
        if (JavascriptPromise::Is(x))
        {
            // a. Let xConstructor be Get(x, "constructor").
            Var xConstructor = JavascriptOperators::GetProperty((RecyclableObject*)x, PropertyIds::constructor, scriptContext);

            // b. If SameValue(xConstructor, C) is true, return x.
            if (JavascriptConversion::SameValue(xConstructor, constructor))
            {
                return x;
            }
        }

        // 4. Let promiseCapability be NewPromiseCapability(C).
        // 5. Perform ? Call(promiseCapability.[[Resolve]], undefined, << x >>).
        // 6. Return promiseCapability.[[Promise]].
        return CreateResolvedPromise(x, scriptContext, constructor);
    }

    // Promise.prototype.then as described in ES 2015 Section 25.4.5.3
    Var JavascriptPromise::EntryThen(RecyclableObject* function, CallInfo callInfo, ...)
    {
        PROBE_STACK(function->GetScriptContext(), Js::Constants::MinStackDefault);
        ARGUMENTS(args, callInfo);
        Assert(!(callInfo.Flags & CallFlags_New));

        ScriptContext* scriptContext = function->GetScriptContext();

        AUTO_TAG_NATIVE_LIBRARY_ENTRY(function, callInfo, _u("Promise.prototype.then"));

        if (args.Info.Count < 1 || !JavascriptPromise::Is(args[0]))
        {
            JavascriptError::ThrowTypeError(scriptContext, JSERR_This_NeedPromise, _u("Promise.prototype.then"));
        }

        JavascriptLibrary* library = scriptContext->GetLibrary();
        JavascriptPromise* promise = JavascriptPromise::FromVar(args[0]);
        RecyclableObject* rejectionHandler;
        RecyclableObject* fulfillmentHandler;

        if (args.Info.Count > 1 && JavascriptConversion::IsCallable(args[1]))
        {
            fulfillmentHandler = RecyclableObject::FromVar(args[1]);
        }
        else
        {
            fulfillmentHandler = library->GetIdentityFunction();
        }

        if (args.Info.Count > 2 && JavascriptConversion::IsCallable(args[2]))
        {
            rejectionHandler = RecyclableObject::FromVar(args[2]);
        }
        else
        {
            rejectionHandler = library->GetThrowerFunction();
        }

        return CreateThenPromise(promise, fulfillmentHandler, rejectionHandler, scriptContext);
    }

    // Promise.prototype.finally as described in the draft ES 2018 #sec-promise.prototype.finally
    Var JavascriptPromise::EntryFinally(RecyclableObject* function, CallInfo callInfo, ...)
    {
        PROBE_STACK(function->GetScriptContext(), Js::Constants::MinStackDefault);
        ARGUMENTS(args, callInfo);
        Assert(!(callInfo.Flags & CallFlags_New));

        ScriptContext* scriptContext = function->GetScriptContext();

        AUTO_TAG_NATIVE_LIBRARY_ENTRY(function, callInfo, _u("Promise.prototype.finally"));
        // 1. Let promise be the this value
        // 2. If Type(promise) is not Object, throw a TypeError exception
        if (args.Info.Count < 1 || !JavascriptOperators::IsObject(args[0]))
        {
            JavascriptError::ThrowTypeError(scriptContext, JSERR_This_NeedObject, _u("Promise.prototype.finally"));
        }

        JavascriptLibrary* library = scriptContext->GetLibrary();
        RecyclableObject* promise = RecyclableObject::UnsafeFromVar(args[0]);
        // 3. Let C be ? SpeciesConstructor(promise, %Promise%).
        RecyclableObject* constructor = JavascriptOperators::SpeciesConstructor(promise, scriptContext->GetLibrary()->GetPromiseConstructor(), scriptContext);
        // 4. Assert IsConstructor(C)
        Assert(JavascriptOperators::IsConstructor(constructor));

        // 5. If IsCallable(onFinally) is false
        //   a. Let thenFinally be onFinally
        //   b. Let catchFinally be onFinally 
        // 6. Else,
        //   a. Let thenFinally be a new built-in function object as defined in ThenFinally Function.
        //   b. Let catchFinally be a new built-in function object as defined in CatchFinally Function.
        //   c. Set thenFinally and catchFinally's [[Constructor]] internal slots to C.
        //   d. Set thenFinally and catchFinally's [[OnFinally]] internal slots to onFinally.

        Var thenFinally = nullptr;
        Var catchFinally = nullptr;

        if (args.Info.Count > 1)
        {
            if (JavascriptConversion::IsCallable(args[1]))
            {
                //note to avoid duplicating code the ThenFinallyFunction works as both thenFinally and catchFinally using a flag
                thenFinally = library->CreatePromiseThenFinallyFunction(EntryThenFinallyFunction, RecyclableObject::FromVar(args[1]), constructor, false);
                catchFinally = library->CreatePromiseThenFinallyFunction(EntryThenFinallyFunction, RecyclableObject::FromVar(args[1]), constructor, true);
            }
            else
            {
                thenFinally = args[1];
                catchFinally = args[1];
            }
        }
        else
        {
            thenFinally = library->GetUndefined();
            catchFinally = library->GetUndefined();
        }

        Assert(thenFinally != nullptr && catchFinally != nullptr);

        // 7. Return ? Invoke(promise, "then", << thenFinally, catchFinally >>).
        Var funcVar = JavascriptOperators::GetProperty(promise, Js::PropertyIds::then, scriptContext);
        if (!JavascriptConversion::IsCallable(funcVar))
        {
            JavascriptError::ThrowTypeError(scriptContext, JSERR_FunctionArgument_NeedFunction, _u("Promise.prototype.finally"));
        }
        RecyclableObject* func = RecyclableObject::UnsafeFromVar(funcVar);

        return CALL_FUNCTION(scriptContext->GetThreadContext(),
            func, Js::CallInfo(CallFlags_Value, 3),
            promise,
            thenFinally,
            catchFinally);
    }

    // ThenFinallyFunction as described in draft ES2018 #sec-thenfinallyfunctions
    // AND CatchFinallyFunction as described in draft ES2018 #sec-catchfinallyfunctions
    Var JavascriptPromise::EntryThenFinallyFunction(RecyclableObject* function, CallInfo callInfo, ...)
    {
        PROBE_STACK(function->GetScriptContext(), Js::Constants::MinStackDefault);
        ARGUMENTS(args, callInfo);
        Assert(!(callInfo.Flags & CallFlags_New));
        ScriptContext* scriptContext = function->GetScriptContext();
        
        JavascriptLibrary* library = scriptContext->GetLibrary();

        JavascriptPromiseThenFinallyFunction* thenFinallyFunction = JavascriptPromiseThenFinallyFunction::FromVar(function);

        // 1. Let onFinally be F.[[OnFinally]]
        // 2. Assert: IsCallable(onFinally)=true
        Assert(JavascriptConversion::IsCallable(thenFinallyFunction->GetOnFinally()));

        // 3. Let result be ? Call(onFinally, undefined)
        Var result = CALL_FUNCTION(scriptContext->GetThreadContext(), thenFinallyFunction->GetOnFinally(), CallInfo(CallFlags_Value, 1), library->GetUndefined());

        // 4. Let C be F.[[Constructor]]
        // 5. Assert IsConstructor(C)
        Assert(JavascriptOperators::IsConstructor(thenFinallyFunction->GetConstructor()));

        // 6. Let promise be ? PromiseResolve(c, result)
        Var promiseVar = CreateResolvedPromise(result, scriptContext, thenFinallyFunction->GetConstructor());

        // 7. Let valueThunk be equivalent to a function that returns value
        // OR 7. Let thrower be equivalent to a function that throws reason

        Var valueOrReason = nullptr;

        if (args.Info.Count > 1)
        {
            valueOrReason = args[1];
        }
        else
        {
            valueOrReason = scriptContext->GetLibrary()->GetUndefined();
        }

        JavascriptPromiseThunkFinallyFunction* thunkFinallyFunction = library->CreatePromiseThunkFinallyFunction(EntryThunkFinallyFunction, valueOrReason, thenFinallyFunction->GetShouldThrow());

        // 8. Return ? Invoke(promise, "then", <<valueThunk>>)
        RecyclableObject* promise = JavascriptOperators::ToObject(promiseVar, scriptContext);
        Var funcVar = JavascriptOperators::GetProperty(promise, Js::PropertyIds::then, scriptContext);

        if (!JavascriptConversion::IsCallable(funcVar))
        {
            JavascriptError::ThrowTypeError(scriptContext, JSERR_FunctionArgument_NeedFunction, _u("Promise.prototype.finally"));
        }

        RecyclableObject* func = RecyclableObject::FromVar(funcVar);

        return CALL_FUNCTION(scriptContext->GetThreadContext(),
            func, Js::CallInfo(CallFlags_Value, 2),
            promiseVar,
            thunkFinallyFunction);
    }

    // valueThunk Function as referenced within draft ES2018 #sec-thenfinallyfunctions
    // and thrower as referenced within draft ES2018 #sec-catchfinallyfunctions
    Var JavascriptPromise::EntryThunkFinallyFunction(RecyclableObject* function, CallInfo callInfo, ...)
    {
        PROBE_STACK(function->GetScriptContext(), Js::Constants::MinStackDefault);
        ARGUMENTS(args, callInfo);
        Assert(!(callInfo.Flags & CallFlags_New));

        JavascriptPromiseThunkFinallyFunction* thunkFinallyFunction = JavascriptPromiseThunkFinallyFunction::FromVar(function);

        if (!thunkFinallyFunction->GetShouldThrow())
        {
            return thunkFinallyFunction->GetValue();  
        }
        else
        {
            JavascriptExceptionOperators::Throw(thunkFinallyFunction->GetValue(), function->GetScriptContext());
        }
    }

    // Promise Reject and Resolve Functions as described in ES 2015 Section 25.4.1.4.1 and 25.4.1.4.2
    Var JavascriptPromise::EntryResolveOrRejectFunction(RecyclableObject* function, CallInfo callInfo, ...)
    {
        PROBE_STACK(function->GetScriptContext(), Js::Constants::MinStackDefault);
        ARGUMENTS(args, callInfo);
        Assert(!(callInfo.Flags & CallFlags_New));

        ScriptContext* scriptContext = function->GetScriptContext();
        JavascriptLibrary* library = scriptContext->GetLibrary();
        Var undefinedVar = library->GetUndefined();
        Var resolution;

        if (args.Info.Count > 1)
        {
            resolution = args[1];
        }
        else
        {
            resolution = undefinedVar;
        }

        JavascriptPromiseResolveOrRejectFunction* resolveOrRejectFunction = JavascriptPromiseResolveOrRejectFunction::FromVar(function);

        if (resolveOrRejectFunction->IsAlreadyResolved())
        {
            return undefinedVar;
        }

        resolveOrRejectFunction->SetAlreadyResolved(true);

        bool rejecting = resolveOrRejectFunction->IsRejectFunction();

        JavascriptPromise* promise = resolveOrRejectFunction->GetPromise();

        return promise->ResolveHelper(resolution, rejecting, scriptContext);
    }

    Var JavascriptPromise::Resolve(Var resolution, ScriptContext* scriptContext)
    {
        return this->ResolveHelper(resolution, false, scriptContext);
    }

    Var JavascriptPromise::Reject(Var resolution, ScriptContext* scriptContext)
    {
        return this->ResolveHelper(resolution, true, scriptContext);
    }

    Var JavascriptPromise::ResolveHelper(Var resolution, bool isRejecting, ScriptContext* scriptContext)
    {
        JavascriptLibrary* library = scriptContext->GetLibrary();
        Var undefinedVar = library->GetUndefined();

        // We only need to check SameValue and check for thenable resolution in the Resolve function case (not Reject)
        if (!isRejecting)
        {
            if (JavascriptConversion::SameValue(resolution, this))
            {
                JavascriptError* selfResolutionError = scriptContext->GetLibrary()->CreateTypeError();
                JavascriptError::SetErrorMessage(selfResolutionError, JSERR_PromiseSelfResolution, _u(""), scriptContext);

                resolution = selfResolutionError;
                isRejecting = true;
            }
            else if (RecyclableObject::Is(resolution))
            {
                try
                {
                    RecyclableObject* thenable = RecyclableObject::FromVar(resolution);
                    Var then = JavascriptOperators::GetPropertyNoCache(thenable, Js::PropertyIds::then, scriptContext);

                    if (JavascriptConversion::IsCallable(then))
                    {
                        JavascriptPromiseResolveThenableTaskFunction* resolveThenableTaskFunction = library->CreatePromiseResolveThenableTaskFunction(EntryResolveThenableTaskFunction, this, thenable, RecyclableObject::FromVar(then));

                        library->EnqueueTask(resolveThenableTaskFunction);

                        return undefinedVar;
                    }
                }
                catch (const JavascriptException& err)
                {
                    resolution = err.GetAndClear()->GetThrownObject(scriptContext);

                    if (resolution == nullptr)
                    {
                        resolution = undefinedVar;
                    }

                    isRejecting = true;
                }
            }
        }

        JavascriptPromiseReactionList* reactions;
        PromiseStatus newStatus;

        // Need to check rejecting state again as it might have changed due to failures
        if (isRejecting)
        {
            reactions = this->GetRejectReactions();
            newStatus = PromiseStatusCode_HasRejection;
            if (!GetIsHandled())
            {
                scriptContext->GetLibrary()->CallNativeHostPromiseRejectionTracker(this, resolution, false);
            }
        }
        else
        {
            reactions = this->GetResolveReactions();
            newStatus = PromiseStatusCode_HasResolution;
        }

        Assert(resolution != nullptr);

        this->result = resolution;
        this->resolveReactions = nullptr;
        this->rejectReactions = nullptr;
        this->status = newStatus;

        return TriggerPromiseReactions(reactions, resolution, scriptContext);
    }

    // Promise Capabilities Executor Function as described in ES 2015 Section 25.4.1.6.2
    Var JavascriptPromise::EntryCapabilitiesExecutorFunction(RecyclableObject* function, CallInfo callInfo, ...)
    {
        PROBE_STACK(function->GetScriptContext(), Js::Constants::MinStackDefault);
        ARGUMENTS(args, callInfo);
        Assert(!(callInfo.Flags & CallFlags_New));

        ScriptContext* scriptContext = function->GetScriptContext();
        Var undefinedVar = scriptContext->GetLibrary()->GetUndefined();
        Var resolve = undefinedVar;
        Var reject = undefinedVar;

        if (args.Info.Count > 1)
        {
            resolve = args[1];

            if (args.Info.Count > 2)
            {
                reject = args[2];
            }
        }

        JavascriptPromiseCapabilitiesExecutorFunction* capabilitiesExecutorFunction = JavascriptPromiseCapabilitiesExecutorFunction::FromVar(function);
        JavascriptPromiseCapability* promiseCapability = capabilitiesExecutorFunction->GetCapability();

        if (!JavascriptOperators::IsUndefined(promiseCapability->GetResolve()) || !JavascriptOperators::IsUndefined(promiseCapability->GetReject()))
        {
            JavascriptError::ThrowTypeErrorVar(scriptContext, JSERR_UnexpectedMetadataFailure, _u("Promise"));
        }

        promiseCapability->SetResolve(resolve);
        promiseCapability->SetReject(reject);

        return undefinedVar;
    }

    // Promise Reaction Task Function as described in ES 2015 Section 25.4.2.1
    Var JavascriptPromise::EntryReactionTaskFunction(RecyclableObject* function, CallInfo callInfo, ...)
    {
        PROBE_STACK(function->GetScriptContext(), Js::Constants::MinStackDefault);
        ARGUMENTS(args, callInfo);
        Assert(!(callInfo.Flags & CallFlags_New));

        ScriptContext* scriptContext = function->GetScriptContext();
        Var undefinedVar = scriptContext->GetLibrary()->GetUndefined();

        JavascriptPromiseReactionTaskFunction* reactionTaskFunction = JavascriptPromiseReactionTaskFunction::FromVar(function);
        JavascriptPromiseReaction* reaction = reactionTaskFunction->GetReaction();
        Var argument = reactionTaskFunction->GetArgument();
        JavascriptPromiseCapability* promiseCapability = reaction->GetCapabilities();
        RecyclableObject* handler = reaction->GetHandler();
        Var handlerResult = nullptr;
        JavascriptExceptionObject* exception = nullptr;

        {
            Js::JavascriptExceptionOperators::AutoCatchHandlerExists autoCatchHandlerExists(scriptContext);
            try
            {
                handlerResult = CALL_FUNCTION(scriptContext->GetThreadContext(),
                    handler, Js::CallInfo(Js::CallFlags::CallFlags_Value, 2),
                    undefinedVar,
                    argument);
            }
            catch (const JavascriptException& err)
            {
                exception = err.GetAndClear();
            }
        }

        if (exception != nullptr)
        {
            return TryRejectWithExceptionObject(exception, promiseCapability->GetReject(), scriptContext);
        }

        Assert(handlerResult != nullptr);

        return TryCallResolveOrRejectHandler(promiseCapability->GetResolve(), handlerResult, scriptContext);
    }

    Var JavascriptPromise::TryCallResolveOrRejectHandler(Var handler, Var value, ScriptContext* scriptContext)
    {
        Var undefinedVar = scriptContext->GetLibrary()->GetUndefined();

        if (!JavascriptConversion::IsCallable(handler))
        {
            JavascriptError::ThrowTypeError(scriptContext, JSERR_NeedFunction);
        }

        RecyclableObject* handlerFunc = RecyclableObject::FromVar(handler);

        return CALL_FUNCTION(scriptContext->GetThreadContext(),
            handlerFunc, CallInfo(CallFlags_Value, 2),
            undefinedVar,
            value);
    }

    Var JavascriptPromise::TryRejectWithExceptionObject(JavascriptExceptionObject* exceptionObject, Var handler, ScriptContext* scriptContext)
    {
        Var thrownObject = exceptionObject->GetThrownObject(scriptContext);

        if (thrownObject == nullptr)
        {
            thrownObject = scriptContext->GetLibrary()->GetUndefined();
        }

        return TryCallResolveOrRejectHandler(handler, thrownObject, scriptContext);
    }

    Var JavascriptPromise::CreateRejectedPromise(Var resolution, ScriptContext* scriptContext, Var promiseConstructor)
    {
        if (promiseConstructor == nullptr)
        {
            promiseConstructor = scriptContext->GetLibrary()->GetPromiseConstructor();
        }

        JavascriptPromiseCapability* promiseCapability = NewPromiseCapability(promiseConstructor, scriptContext);

        TryCallResolveOrRejectHandler(promiseCapability->GetReject(), resolution, scriptContext);

        return promiseCapability->GetPromise();
    }

    Var JavascriptPromise::CreateResolvedPromise(Var resolution, ScriptContext* scriptContext, Var promiseConstructor)
    {
        if (promiseConstructor == nullptr)
        {
            promiseConstructor = scriptContext->GetLibrary()->GetPromiseConstructor();
        }

        JavascriptPromiseCapability* promiseCapability = NewPromiseCapability(promiseConstructor, scriptContext);

        TryCallResolveOrRejectHandler(promiseCapability->GetResolve(), resolution, scriptContext);

        return promiseCapability->GetPromise();
    }

    Var JavascriptPromise::CreatePassThroughPromise(JavascriptPromise* sourcePromise, ScriptContext* scriptContext)
    {
        JavascriptLibrary* library = scriptContext->GetLibrary();

        return CreateThenPromise(sourcePromise, library->GetIdentityFunction(), library->GetThrowerFunction(), scriptContext);
    }

    Var JavascriptPromise::CreateThenPromise(JavascriptPromise* sourcePromise, RecyclableObject* fulfillmentHandler, RecyclableObject* rejectionHandler, ScriptContext* scriptContext)
    {
        RecyclableObject* constructor = JavascriptOperators::SpeciesConstructor(sourcePromise, scriptContext->GetLibrary()->GetPromiseConstructor(), scriptContext);
        JavascriptPromiseCapability* promiseCapability = NewPromiseCapability(constructor, scriptContext);

        JavascriptPromiseReaction* resolveReaction = JavascriptPromiseReaction::New(promiseCapability, fulfillmentHandler, scriptContext);
        JavascriptPromiseReaction* rejectReaction = JavascriptPromiseReaction::New(promiseCapability, rejectionHandler, scriptContext);

        switch (sourcePromise->status)
        {
        case PromiseStatusCode_Unresolved:
            sourcePromise->resolveReactions->Add(resolveReaction);
            sourcePromise->rejectReactions->Add(rejectReaction);
            break;
        case PromiseStatusCode_HasResolution:
            EnqueuePromiseReactionTask(resolveReaction, sourcePromise->result, scriptContext);
            break;
        case PromiseStatusCode_HasRejection:
            if (!sourcePromise->GetIsHandled())
            {
                scriptContext->GetLibrary()->CallNativeHostPromiseRejectionTracker(sourcePromise, sourcePromise->result, true);
            }
            EnqueuePromiseReactionTask(rejectReaction, sourcePromise->result, scriptContext);
            break;
        default:
            AssertMsg(false, "Promise status is in an invalid state");
            break;
        }

        sourcePromise->SetIsHandled();

        return promiseCapability->GetPromise();
    }

    // Promise Resolve Thenable Job as described in ES 2015 Section 25.4.2.2
    Var JavascriptPromise::EntryResolveThenableTaskFunction(RecyclableObject* function, CallInfo callInfo, ...)
    {
        PROBE_STACK(function->GetScriptContext(), Js::Constants::MinStackDefault);
        ARGUMENTS(args, callInfo);
        Assert(!(callInfo.Flags & CallFlags_New));

        ScriptContext* scriptContext = function->GetScriptContext();
        JavascriptLibrary* library = scriptContext->GetLibrary();

        JavascriptPromiseResolveThenableTaskFunction* resolveThenableTaskFunction = JavascriptPromiseResolveThenableTaskFunction::FromVar(function);
        JavascriptPromise* promise = resolveThenableTaskFunction->GetPromise();
        RecyclableObject* thenable = resolveThenableTaskFunction->GetThenable();
        RecyclableObject* thenFunction = resolveThenableTaskFunction->GetThenFunction();

        JavascriptPromiseResolveOrRejectFunctionAlreadyResolvedWrapper* alreadyResolvedRecord = RecyclerNewStructZ(scriptContext->GetRecycler(), JavascriptPromiseResolveOrRejectFunctionAlreadyResolvedWrapper);
        alreadyResolvedRecord->alreadyResolved = false;

        JavascriptPromiseResolveOrRejectFunction* resolve = library->CreatePromiseResolveOrRejectFunction(EntryResolveOrRejectFunction, promise, false, alreadyResolvedRecord);
        JavascriptPromiseResolveOrRejectFunction* reject = library->CreatePromiseResolveOrRejectFunction(EntryResolveOrRejectFunction, promise, true, alreadyResolvedRecord);
        JavascriptExceptionObject* exception = nullptr;

        {
            Js::JavascriptExceptionOperators::AutoCatchHandlerExists autoCatchHandlerExists(scriptContext);
            try
            {
                return CALL_FUNCTION(scriptContext->GetThreadContext(),
                    thenFunction, Js::CallInfo(Js::CallFlags::CallFlags_Value, 3),
                    thenable,
                    resolve,
                    reject);
            }
            catch (const JavascriptException& err)
            {
                exception = err.GetAndClear();
            }
        }

        Assert(exception != nullptr);

        return TryRejectWithExceptionObject(exception, reject, scriptContext);
    }

    // Promise Identity Function as described in ES 2015Section 25.4.5.3.1
    Var JavascriptPromise::EntryIdentityFunction(RecyclableObject* function, CallInfo callInfo, ...)
    {
        PROBE_STACK(function->GetScriptContext(), Js::Constants::MinStackDefault);
        ARGUMENTS(args, callInfo);
        Assert(!(callInfo.Flags & CallFlags_New));

        if (args.Info.Count > 1)
        {
            Assert(args[1] != nullptr);

            return args[1];
        }
        else
        {
            return function->GetScriptContext()->GetLibrary()->GetUndefined();
        }
    }

    // Promise Thrower Function as described in ES 2015Section 25.4.5.3.3
    Var JavascriptPromise::EntryThrowerFunction(RecyclableObject* function, CallInfo callInfo, ...)
    {
        PROBE_STACK(function->GetScriptContext(), Js::Constants::MinStackDefault);
        ARGUMENTS(args, callInfo);
        Assert(!(callInfo.Flags & CallFlags_New));

        ScriptContext* scriptContext = function->GetScriptContext();
        Var arg;

        if (args.Info.Count > 1)
        {
            Assert(args[1] != nullptr);

            arg = args[1];
        }
        else
        {
            arg = scriptContext->GetLibrary()->GetUndefined();
        }

        JavascriptExceptionOperators::Throw(arg, scriptContext);
    }

    // Promise.all Resolve Element Function as described in ES6.0 (Release Candidate 3) Section 25.4.4.1.2
    Var JavascriptPromise::EntryAllResolveElementFunction(RecyclableObject* function, CallInfo callInfo, ...)
    {
        PROBE_STACK(function->GetScriptContext(), Js::Constants::MinStackDefault);
        ARGUMENTS(args, callInfo);
        Assert(!(callInfo.Flags & CallFlags_New));

        ScriptContext* scriptContext = function->GetScriptContext();
        Var undefinedVar = scriptContext->GetLibrary()->GetUndefined();
        Var x;

        if (args.Info.Count > 1)
        {
            x = args[1];
        }
        else
        {
            x = undefinedVar;
        }

        JavascriptPromiseAllResolveElementFunction* allResolveElementFunction = JavascriptPromiseAllResolveElementFunction::FromVar(function);

        if (allResolveElementFunction->IsAlreadyCalled())
        {
            return undefinedVar;
        }

        allResolveElementFunction->SetAlreadyCalled(true);

        uint32 index = allResolveElementFunction->GetIndex();
        JavascriptArray* values = allResolveElementFunction->GetValues();
        JavascriptPromiseCapability* promiseCapability = allResolveElementFunction->GetCapabilities();
        JavascriptExceptionObject* exception = nullptr;

        try
        {
            values->SetItem(index, x, PropertyOperation_None);
        }
        catch (const JavascriptException& err)
        {
            exception = err.GetAndClear();
        }

        if (exception != nullptr)
        {
            return TryRejectWithExceptionObject(exception, promiseCapability->GetReject(), scriptContext);
        }

        if (allResolveElementFunction->DecrementRemainingElements() == 0)
        {
            return TryCallResolveOrRejectHandler(promiseCapability->GetResolve(), values, scriptContext);
        }

        return undefinedVar;
    }

    Var JavascriptPromise::EntryJavascriptPromiseAsyncSpawnExecutorFunction(RecyclableObject* function, CallInfo callInfo, ...)
    {
        PROBE_STACK(function->GetScriptContext(), Js::Constants::MinStackDefault);
        ARGUMENTS(args, callInfo);

        ScriptContext* scriptContext = function->GetScriptContext();
        JavascriptLibrary* library = scriptContext->GetLibrary();
        Var undefinedVar = library->GetUndefined();
        Var resolve = undefinedVar;
        Var reject = undefinedVar;

        Assert(args.Info.Count == 3);

        resolve = args[1];
        reject = args[2];

        Assert(JavascriptPromiseAsyncSpawnExecutorFunction::Is(function));
        JavascriptPromiseAsyncSpawnExecutorFunction* asyncSpawnExecutorFunction = JavascriptPromiseAsyncSpawnExecutorFunction::FromVar(function);
        Var self = asyncSpawnExecutorFunction->GetTarget();

        Var varCallArgs[] = { undefinedVar, self };
        JavascriptGenerator* gen = asyncSpawnExecutorFunction->GetGenerator();
        JavascriptPromiseAsyncSpawnStepArgumentExecutorFunction* nextFunction = library->CreatePromiseAsyncSpawnStepArgumentExecutorFunction(EntryJavascriptPromiseAsyncSpawnStepNextExecutorFunction, gen, varCallArgs);

        Assert(JavascriptConversion::IsCallable(resolve) && JavascriptConversion::IsCallable(reject));
        AsyncSpawnStep(nextFunction, gen, resolve, reject);

        return undefinedVar;
    }

    Var JavascriptPromise::EntryJavascriptPromiseAsyncSpawnStepNextExecutorFunction(RecyclableObject* function, CallInfo callInfo, ...)
    {
        PROBE_STACK(function->GetScriptContext(), Js::Constants::MinStackDefault);

        JavascriptPromiseAsyncSpawnStepArgumentExecutorFunction* asyncSpawnStepArgumentExecutorFunction = JavascriptPromiseAsyncSpawnStepArgumentExecutorFunction::FromVar(function);
        Var argument = asyncSpawnStepArgumentExecutorFunction->GetArgument();

        JavascriptFunction* next = function->GetScriptContext()->GetLibrary()->EnsureGeneratorNextFunction();
        return CALL_FUNCTION(function->GetScriptContext()->GetThreadContext(), next, CallInfo(CallFlags_Value, 2), asyncSpawnStepArgumentExecutorFunction->GetGenerator(), argument);
    }

    Var JavascriptPromise::EntryJavascriptPromiseAsyncSpawnStepThrowExecutorFunction(RecyclableObject* function, CallInfo callInfo, ...)
    {
        PROBE_STACK(function->GetScriptContext(), Js::Constants::MinStackDefault);

        JavascriptPromiseAsyncSpawnStepArgumentExecutorFunction* asyncSpawnStepArgumentExecutorFunction = JavascriptPromiseAsyncSpawnStepArgumentExecutorFunction::FromVar(function);
        JavascriptFunction* throw_ = function->GetScriptContext()->GetLibrary()->EnsureGeneratorThrowFunction();
        return CALL_FUNCTION(function->GetScriptContext()->GetThreadContext(), throw_, CallInfo(CallFlags_Value, 2), asyncSpawnStepArgumentExecutorFunction->GetGenerator(), asyncSpawnStepArgumentExecutorFunction->GetArgument());
    }

    Var JavascriptPromise::EntryJavascriptPromiseAsyncSpawnCallStepExecutorFunction(RecyclableObject* function, CallInfo callInfo, ...)
    {
        PROBE_STACK(function->GetScriptContext(), Js::Constants::MinStackDefault);
        ARGUMENTS(args, callInfo);

        ScriptContext* scriptContext = function->GetScriptContext();
        JavascriptLibrary* library = scriptContext->GetLibrary();
        Var undefinedVar = library->GetUndefined();

        Var argument = undefinedVar;

        if (args.Info.Count > 1)
        {
            argument = args[1];
        }

        JavascriptPromiseAsyncSpawnStepArgumentExecutorFunction* asyncSpawnStepExecutorFunction = JavascriptPromiseAsyncSpawnStepArgumentExecutorFunction::FromVar(function);
        JavascriptPromiseAsyncSpawnStepArgumentExecutorFunction* functionArg;
        JavascriptGenerator* gen = asyncSpawnStepExecutorFunction->GetGenerator();
        Var reject = asyncSpawnStepExecutorFunction->GetReject();
        Var resolve = asyncSpawnStepExecutorFunction->GetResolve();

        if (asyncSpawnStepExecutorFunction->GetIsReject())
        {
            functionArg = library->CreatePromiseAsyncSpawnStepArgumentExecutorFunction(EntryJavascriptPromiseAsyncSpawnStepThrowExecutorFunction, gen, argument, NULL, NULL, false);
        }
        else
        {
            functionArg = library->CreatePromiseAsyncSpawnStepArgumentExecutorFunction(EntryJavascriptPromiseAsyncSpawnStepNextExecutorFunction, gen, argument, NULL, NULL, false);
        }

        AsyncSpawnStep(functionArg, gen, resolve, reject);

        return undefinedVar;
    }

    void JavascriptPromise::AsyncSpawnStep(JavascriptPromiseAsyncSpawnStepArgumentExecutorFunction* nextFunction, JavascriptGenerator* gen, Var resolve, Var reject)
    {
        ScriptContext* scriptContext = gen->GetScriptContext();
        JavascriptLibrary* library = scriptContext->GetLibrary();
        Var undefinedVar = library->GetUndefined();

        JavascriptExceptionObject* exception = nullptr;
        Var value = nullptr;
        RecyclableObject* next = nullptr;
        bool done;

        try
        {
            Var nextVar = CALL_FUNCTION(scriptContext->GetThreadContext(), nextFunction, CallInfo(CallFlags_Value, 1), undefinedVar);
            next = RecyclableObject::FromVar(nextVar);
        }
        catch (const JavascriptException& err)
        {
            exception = err.GetAndClear();
        }

        if (exception != nullptr)
        {
            // finished with failure, reject the promise
            TryRejectWithExceptionObject(exception, reject, scriptContext);
            return;
        }

        Assert(next != nullptr);
        done = JavascriptConversion::ToBool(JavascriptOperators::GetProperty(next, PropertyIds::done, scriptContext), scriptContext);
        if (done)
        {
            // finished with success, resolve the promise
            value = JavascriptOperators::GetProperty(next, PropertyIds::value, scriptContext);
            if (!JavascriptConversion::IsCallable(resolve))
            {
                JavascriptError::ThrowTypeError(scriptContext, JSERR_NeedFunction);
            }
            CALL_FUNCTION(scriptContext->GetThreadContext(), RecyclableObject::FromVar(resolve), CallInfo(CallFlags_Value, 2), undefinedVar, value);

            return;
        }

        // not finished, chain off the yielded promise and `step` again
        JavascriptPromiseAsyncSpawnStepArgumentExecutorFunction* successFunction = library->CreatePromiseAsyncSpawnStepArgumentExecutorFunction(EntryJavascriptPromiseAsyncSpawnCallStepExecutorFunction, gen, undefinedVar, resolve, reject);
        JavascriptPromiseAsyncSpawnStepArgumentExecutorFunction* failFunction = library->CreatePromiseAsyncSpawnStepArgumentExecutorFunction(EntryJavascriptPromiseAsyncSpawnCallStepExecutorFunction, gen, undefinedVar, resolve, reject, true);

        JavascriptFunction* promiseResolve = library->EnsurePromiseResolveFunction();
        value = JavascriptOperators::GetProperty(next, PropertyIds::value, scriptContext);
        Var promiseVar = CALL_FUNCTION(scriptContext->GetThreadContext(), promiseResolve, CallInfo(CallFlags_Value, 2), library->GetPromiseConstructor(), value);
        JavascriptPromise* promise = FromVar(promiseVar);

        Var promiseThen = JavascriptOperators::GetProperty(promise, PropertyIds::then, scriptContext);
        if (!JavascriptConversion::IsCallable(promiseThen))
        {
            JavascriptError::ThrowTypeError(scriptContext, JSERR_NeedFunction);
        }
        CALL_FUNCTION(scriptContext->GetThreadContext(), RecyclableObject::FromVar(promiseThen), CallInfo(CallFlags_Value, 3), promise, successFunction, failFunction);
    }

#if ENABLE_TTD
    void JavascriptPromise::MarkVisitKindSpecificPtrs(TTD::SnapshotExtractor* extractor)
    {
        if(this->result != nullptr)
        {
            extractor->MarkVisitVar(this->result);
        }

        if(this->resolveReactions != nullptr)
        {
            for(int32 i = 0; i < this->resolveReactions->Count(); ++i)
            {
                this->resolveReactions->Item(i)->MarkVisitPtrs(extractor);
            }
        }

        if(this->rejectReactions != nullptr)
        {
            for(int32 i = 0; i < this->rejectReactions->Count(); ++i)
            {
                this->rejectReactions->Item(i)->MarkVisitPtrs(extractor);
            }
        }
    }

    TTD::NSSnapObjects::SnapObjectType JavascriptPromise::GetSnapTag_TTD() const
    {
        return TTD::NSSnapObjects::SnapObjectType::SnapPromiseObject;
    }

    void JavascriptPromise::ExtractSnapObjectDataInto(TTD::NSSnapObjects::SnapObject* objData, TTD::SlabAllocator& alloc)
    {
        JsUtil::List<TTD_PTR_ID, HeapAllocator> depOnList(&HeapAllocator::Instance);

        TTD::NSSnapObjects::SnapPromiseInfo* spi = alloc.SlabAllocateStruct<TTD::NSSnapObjects::SnapPromiseInfo>();

        spi->Result = this->result;

        //Primitive kinds always inflated first so we only need to deal with complex kinds as depends on
        if(this->result != nullptr && TTD::JsSupport::IsVarComplexKind(this->result))
        {
            depOnList.Add(TTD_CONVERT_VAR_TO_PTR_ID(this->result));
        }

        spi->Status = this->status;

        spi->ResolveReactionCount = (this->resolveReactions != nullptr) ? this->resolveReactions->Count() : 0;
        spi->ResolveReactions = nullptr;
        if(spi->ResolveReactionCount != 0)
        {
            spi->ResolveReactions = alloc.SlabAllocateArray<TTD::NSSnapValues::SnapPromiseReactionInfo>(spi->ResolveReactionCount);

            for(uint32 i = 0; i < spi->ResolveReactionCount; ++i)
            {
                this->resolveReactions->Item(i)->ExtractSnapPromiseReactionInto(spi->ResolveReactions + i, depOnList, alloc);
            }
        }

        spi->RejectReactionCount = (this->rejectReactions != nullptr) ? this->rejectReactions->Count() : 0;
        spi->RejectReactions = nullptr;
        if(spi->RejectReactionCount != 0)
        {
            spi->RejectReactions = alloc.SlabAllocateArray<TTD::NSSnapValues::SnapPromiseReactionInfo>(spi->RejectReactionCount);

            for(uint32 i = 0; i < spi->RejectReactionCount; ++i)
            {
                this->rejectReactions->Item(i)->ExtractSnapPromiseReactionInto(spi->RejectReactions+ i, depOnList, alloc);
            }
        }

        //see what we need to do wrt dependencies
        if(depOnList.Count() == 0)
        {
            TTD::NSSnapObjects::StdExtractSetKindSpecificInfo<TTD::NSSnapObjects::SnapPromiseInfo*, TTD::NSSnapObjects::SnapObjectType::SnapPromiseObject>(objData, spi);
        }
        else
        {
            uint32 depOnCount = depOnList.Count();
            TTD_PTR_ID* depOnArray = alloc.SlabAllocateArray<TTD_PTR_ID>(depOnCount);

            for(uint32 i = 0; i < depOnCount; ++i)
            {
                depOnArray[i] = depOnList.Item(i);
            }

            TTD::NSSnapObjects::StdExtractSetKindSpecificInfo<TTD::NSSnapObjects::SnapPromiseInfo*, TTD::NSSnapObjects::SnapObjectType::SnapPromiseObject>(objData, spi, alloc, depOnCount, depOnArray);
        }
    }

    JavascriptPromise* JavascriptPromise::InitializePromise_TTD(ScriptContext* scriptContext, uint32 status, Var result, JsUtil::List<Js::JavascriptPromiseReaction*, HeapAllocator>& resolveReactions, JsUtil::List<Js::JavascriptPromiseReaction*, HeapAllocator>& rejectReactions)
    {
        Recycler* recycler = scriptContext->GetRecycler();
        JavascriptLibrary* library = scriptContext->GetLibrary();

        JavascriptPromise* promise = library->CreatePromise();

        promise->status = (PromiseStatus)status;
        promise->result = result;

        promise->resolveReactions = RecyclerNew(recycler, JavascriptPromiseReactionList, recycler);
        promise->resolveReactions->Copy(&resolveReactions);

        promise->rejectReactions = RecyclerNew(recycler, JavascriptPromiseReactionList, recycler);
        promise->rejectReactions->Copy(&rejectReactions);

        return promise;
    }
#endif

    // NewPromiseCapability as described in ES6.0 (draft 29) Section 25.4.1.6
    JavascriptPromiseCapability* JavascriptPromise::NewPromiseCapability(Var constructor, ScriptContext* scriptContext)
    {
        if (!JavascriptOperators::IsConstructor(constructor))
        {
            JavascriptError::ThrowTypeError(scriptContext, JSERR_NeedFunction);
        }

        RecyclableObject* constructorFunc = RecyclableObject::FromVar(constructor);

        return CreatePromiseCapabilityRecord(constructorFunc, scriptContext);
    }

    // CreatePromiseCapabilityRecord as described in ES6.0 (draft 29) Section 25.4.1.6.1
    JavascriptPromiseCapability* JavascriptPromise::CreatePromiseCapabilityRecord(RecyclableObject* constructor, ScriptContext* scriptContext)
    {
        JavascriptLibrary* library = scriptContext->GetLibrary();
        Var undefinedVar = library->GetUndefined();
        JavascriptPromiseCapability* promiseCapability = JavascriptPromiseCapability::New(undefinedVar, undefinedVar, undefinedVar, scriptContext);

        JavascriptPromiseCapabilitiesExecutorFunction* executor = library->CreatePromiseCapabilitiesExecutorFunction(EntryCapabilitiesExecutorFunction, promiseCapability);

        CallInfo callinfo = Js::CallInfo((Js::CallFlags)(Js::CallFlags::CallFlags_Value | Js::CallFlags::CallFlags_New), 2);
        Var argVars[] = { constructor, executor };
        Arguments args(callinfo, argVars);
        Var promise = JavascriptFunction::CallAsConstructor(constructor, nullptr, args, scriptContext);

        if (!JavascriptConversion::IsCallable(promiseCapability->GetResolve()) || !JavascriptConversion::IsCallable(promiseCapability->GetReject()))
        {
            JavascriptError::ThrowTypeError(scriptContext, JSERR_NeedFunction, _u("Promise"));
        }

        promiseCapability->SetPromise(promise);

        return promiseCapability;
    }

    // TriggerPromiseReactions as defined in ES 2015 Section 25.4.1.7
    Var JavascriptPromise::TriggerPromiseReactions(JavascriptPromiseReactionList* reactions, Var resolution, ScriptContext* scriptContext)
    {
        JavascriptLibrary* library = scriptContext->GetLibrary();

        if (reactions != nullptr)
        {
            for (int i = 0; i < reactions->Count(); i++)
            {
                JavascriptPromiseReaction* reaction = reactions->Item(i);

                EnqueuePromiseReactionTask(reaction, resolution, scriptContext);
            }
        }

        return library->GetUndefined();
    }

    void JavascriptPromise::EnqueuePromiseReactionTask(JavascriptPromiseReaction* reaction, Var resolution, ScriptContext* scriptContext)
    {
        Assert(resolution != nullptr);

        JavascriptLibrary* library = scriptContext->GetLibrary();
        JavascriptPromiseReactionTaskFunction* reactionTaskFunction = library->CreatePromiseReactionTaskFunction(EntryReactionTaskFunction, reaction, resolution);

        library->EnqueueTask(reactionTaskFunction);
    }

    JavascriptPromiseResolveOrRejectFunction::JavascriptPromiseResolveOrRejectFunction(DynamicType* type)
        : RuntimeFunction(type, &Js::JavascriptPromise::EntryInfo::ResolveOrRejectFunction), promise(nullptr), isReject(false), alreadyResolvedWrapper(nullptr)
    { }

    JavascriptPromiseResolveOrRejectFunction::JavascriptPromiseResolveOrRejectFunction(DynamicType* type, FunctionInfo* functionInfo, JavascriptPromise* promise, bool isReject, JavascriptPromiseResolveOrRejectFunctionAlreadyResolvedWrapper* alreadyResolvedRecord)
        : RuntimeFunction(type, functionInfo), promise(promise), isReject(isReject), alreadyResolvedWrapper(alreadyResolvedRecord)
    { }

    bool JavascriptPromiseResolveOrRejectFunction::Is(Var var)
    {
        if (JavascriptFunction::Is(var))
        {
            JavascriptFunction* obj = JavascriptFunction::UnsafeFromVar(var);

            return VirtualTableInfo<JavascriptPromiseResolveOrRejectFunction>::HasVirtualTable(obj)
                || VirtualTableInfo<CrossSiteObject<JavascriptPromiseResolveOrRejectFunction>>::HasVirtualTable(obj);
        }

        return false;
    }

    JavascriptPromiseResolveOrRejectFunction* JavascriptPromiseResolveOrRejectFunction::FromVar(Var var)
    {
        AssertOrFailFast(JavascriptPromiseResolveOrRejectFunction::Is(var));

        return static_cast<JavascriptPromiseResolveOrRejectFunction*>(var);
    }

    JavascriptPromiseResolveOrRejectFunction* JavascriptPromiseResolveOrRejectFunction::UnsafeFromVar(Var var)
    {
        Assert(JavascriptPromiseResolveOrRejectFunction::Is(var));

        return static_cast<JavascriptPromiseResolveOrRejectFunction*>(var);
    }

    JavascriptPromise* JavascriptPromiseResolveOrRejectFunction::GetPromise()
    {
        return this->promise;
    }

    bool JavascriptPromiseResolveOrRejectFunction::IsRejectFunction()
    {
        return this->isReject;
    }

    bool JavascriptPromiseResolveOrRejectFunction::IsAlreadyResolved()
    {
        Assert(this->alreadyResolvedWrapper);

        return this->alreadyResolvedWrapper->alreadyResolved;
    }

    void JavascriptPromiseResolveOrRejectFunction::SetAlreadyResolved(bool is)
    {
        Assert(this->alreadyResolvedWrapper);

        this->alreadyResolvedWrapper->alreadyResolved = is;
    }

#if ENABLE_TTD
    void JavascriptPromiseResolveOrRejectFunction::MarkVisitKindSpecificPtrs(TTD::SnapshotExtractor* extractor)
    {
        TTDAssert(this->promise != nullptr, "Was not expecting that!!!");

        extractor->MarkVisitVar(this->promise);
    }

    TTD::NSSnapObjects::SnapObjectType JavascriptPromiseResolveOrRejectFunction::GetSnapTag_TTD() const
    {
        return TTD::NSSnapObjects::SnapObjectType::SnapPromiseResolveOrRejectFunctionObject;
    }

    void JavascriptPromiseResolveOrRejectFunction::ExtractSnapObjectDataInto(TTD::NSSnapObjects::SnapObject* objData, TTD::SlabAllocator& alloc)
    {
        TTD::NSSnapObjects::SnapPromiseResolveOrRejectFunctionInfo* sprri = alloc.SlabAllocateStruct<TTD::NSSnapObjects::SnapPromiseResolveOrRejectFunctionInfo>();

        uint32 depOnCount = 1;
        TTD_PTR_ID* depOnArray = alloc.SlabAllocateArray<TTD_PTR_ID>(depOnCount);

        sprri->PromiseId = TTD_CONVERT_VAR_TO_PTR_ID(this->promise);
        depOnArray[0] = sprri->PromiseId;

        sprri->IsReject = this->isReject;

        sprri->AlreadyResolvedWrapperId = TTD_CONVERT_PROMISE_INFO_TO_PTR_ID(this->alreadyResolvedWrapper);
        sprri->AlreadyResolvedValue = this->alreadyResolvedWrapper->alreadyResolved;

        TTD::NSSnapObjects::StdExtractSetKindSpecificInfo<TTD::NSSnapObjects::SnapPromiseResolveOrRejectFunctionInfo*, TTD::NSSnapObjects::SnapObjectType::SnapPromiseResolveOrRejectFunctionObject>(objData, sprri, alloc, depOnCount, depOnArray);
    }
#endif

    JavascriptPromiseAsyncSpawnExecutorFunction::JavascriptPromiseAsyncSpawnExecutorFunction(DynamicType* type, FunctionInfo* functionInfo, JavascriptGenerator* generator, Var target)
        : RuntimeFunction(type, functionInfo), generator(generator), target(target)
    { }

    bool JavascriptPromiseAsyncSpawnExecutorFunction::Is(Var var)
    {
        if (JavascriptFunction::Is(var))
        {
            JavascriptFunction* obj = JavascriptFunction::UnsafeFromVar(var);

            return VirtualTableInfo<JavascriptPromiseAsyncSpawnExecutorFunction>::HasVirtualTable(obj)
                || VirtualTableInfo<CrossSiteObject<JavascriptPromiseAsyncSpawnExecutorFunction>>::HasVirtualTable(obj);
        }

        return false;
    }

    JavascriptPromiseAsyncSpawnExecutorFunction* JavascriptPromiseAsyncSpawnExecutorFunction::FromVar(Var var)
    {
        AssertOrFailFast(JavascriptPromiseAsyncSpawnExecutorFunction::Is(var));

        return static_cast<JavascriptPromiseAsyncSpawnExecutorFunction*>(var);
    }

    JavascriptPromiseAsyncSpawnExecutorFunction* JavascriptPromiseAsyncSpawnExecutorFunction::UnsafeFromVar(Var var)
    {
        Assert(JavascriptPromiseAsyncSpawnExecutorFunction::Is(var));

        return static_cast<JavascriptPromiseAsyncSpawnExecutorFunction*>(var);
    }


    JavascriptGenerator* JavascriptPromiseAsyncSpawnExecutorFunction::GetGenerator()
    {
        return this->generator;
    }

    Var JavascriptPromiseAsyncSpawnExecutorFunction::GetTarget()
    {
        return this->target;
    }

#if ENABLE_TTD
    void JavascriptPromiseAsyncSpawnExecutorFunction::MarkVisitKindSpecificPtrs(TTD::SnapshotExtractor* extractor)
    {
        if (this->generator != nullptr)
        {
            extractor->MarkVisitVar(this->generator);
        }

        if (this->target != nullptr)
        {
            extractor->MarkVisitVar(this->target);
        }
    }

    TTD::NSSnapObjects::SnapObjectType JavascriptPromiseAsyncSpawnExecutorFunction::GetSnapTag_TTD() const
    {
        return TTD::NSSnapObjects::SnapObjectType::JavascriptPromiseAsyncSpawnExecutorFunction;
    }

    void JavascriptPromiseAsyncSpawnExecutorFunction::ExtractSnapObjectDataInto(TTD::NSSnapObjects::SnapObject* objData, TTD::SlabAllocator& alloc)
    {
        TTD::NSSnapObjects::SnapJavascriptPromiseAsyncSpawnExecutorFunctionInfo* info = alloc.SlabAllocateStruct<TTD::NSSnapObjects::SnapJavascriptPromiseAsyncSpawnExecutorFunctionInfo>();
        info->generator= TTD_CONVERT_VAR_TO_PTR_ID(this->generator);
        info->target = TTD_CONVERT_JSVAR_TO_TTDVAR(this->target);
        TTD::NSSnapObjects::StdExtractSetKindSpecificInfo<TTD::NSSnapObjects::SnapJavascriptPromiseAsyncSpawnExecutorFunctionInfo*, TTD::NSSnapObjects::SnapObjectType::JavascriptPromiseAsyncSpawnExecutorFunction>(objData, info);
    }
#endif

    JavascriptPromiseAsyncSpawnStepArgumentExecutorFunction::JavascriptPromiseAsyncSpawnStepArgumentExecutorFunction(DynamicType* type, FunctionInfo* functionInfo, JavascriptGenerator* generator, Var argument, Var resolve, Var reject, bool isReject)
        : RuntimeFunction(type, functionInfo), generator(generator), argument(argument), resolve(resolve), reject(reject), isReject(isReject)
    { }

    bool JavascriptPromiseAsyncSpawnStepArgumentExecutorFunction::Is(Var var)
    {
        if (JavascriptFunction::Is(var))
        {
            JavascriptFunction* obj = JavascriptFunction::UnsafeFromVar(var);

            return VirtualTableInfo<JavascriptPromiseAsyncSpawnStepArgumentExecutorFunction>::HasVirtualTable(obj)
                || VirtualTableInfo<CrossSiteObject<JavascriptPromiseAsyncSpawnStepArgumentExecutorFunction>>::HasVirtualTable(obj);
        }

        return false;
    }

    JavascriptPromiseAsyncSpawnStepArgumentExecutorFunction* JavascriptPromiseAsyncSpawnStepArgumentExecutorFunction::FromVar(Var var)
    {
        AssertOrFailFast(JavascriptPromiseAsyncSpawnStepArgumentExecutorFunction::Is(var));

        return static_cast<JavascriptPromiseAsyncSpawnStepArgumentExecutorFunction*>(var);
    }

    JavascriptPromiseAsyncSpawnStepArgumentExecutorFunction* JavascriptPromiseAsyncSpawnStepArgumentExecutorFunction::UnsafeFromVar(Var var)
    {
        Assert(JavascriptPromiseAsyncSpawnStepArgumentExecutorFunction::Is(var));

        return static_cast<JavascriptPromiseAsyncSpawnStepArgumentExecutorFunction*>(var);
    }

    JavascriptGenerator* JavascriptPromiseAsyncSpawnStepArgumentExecutorFunction::GetGenerator()
    {
        return this->generator;
    }

    Var JavascriptPromiseAsyncSpawnStepArgumentExecutorFunction::GetResolve()
    {
        return this->resolve;
    }

    Var JavascriptPromiseAsyncSpawnStepArgumentExecutorFunction::GetReject()
    {
        return this->reject;
    }

    bool JavascriptPromiseAsyncSpawnStepArgumentExecutorFunction::GetIsReject()
    {
        return this->isReject;
    }

    Var JavascriptPromiseAsyncSpawnStepArgumentExecutorFunction::GetArgument()
    {
        return this->argument;
    }

#if ENABLE_TTD
    void JavascriptPromiseAsyncSpawnStepArgumentExecutorFunction::MarkVisitKindSpecificPtrs(TTD::SnapshotExtractor* extractor)
    {
        if (this->generator != nullptr)
        {
            extractor->MarkVisitVar(this->generator);
        }

        if (this->reject != nullptr)
        {
            extractor->MarkVisitVar(this->reject);
        }

        if (this->resolve != nullptr)
        {
            extractor->MarkVisitVar(this->resolve);
        }

        if (this->argument != nullptr)
        {
            extractor->MarkVisitVar(this->argument);
        }
    }

    TTD::NSSnapObjects::SnapObjectType JavascriptPromiseAsyncSpawnStepArgumentExecutorFunction::GetSnapTag_TTD() const
    {
        return TTD::NSSnapObjects::SnapObjectType::JavascriptPromiseAsyncSpawnStepArgumentExecutorFunction;
    }

    void JavascriptPromiseAsyncSpawnStepArgumentExecutorFunction::ExtractSnapObjectDataInto(TTD::NSSnapObjects::SnapObject* objData, TTD::SlabAllocator& alloc)
    {
        TTD::NSSnapObjects::SnapJavascriptPromiseAsyncSpawnStepArgumentExecutorFunctionInfo* info = alloc.SlabAllocateStruct<TTD::NSSnapObjects::SnapJavascriptPromiseAsyncSpawnStepArgumentExecutorFunctionInfo>();
        info->generator = TTD_CONVERT_VAR_TO_PTR_ID(this->generator);
        info->reject = this->reject;
        info->resolve = this->resolve;
        info->argument = this->argument;
        info->isReject = this->isReject;

        info->entryPoint = 0;
        JavascriptMethod entryPoint = this->GetFunctionInfo()->GetOriginalEntryPoint();
        if (entryPoint == JavascriptPromise::EntryJavascriptPromiseAsyncSpawnStepNextExecutorFunction)
        {
            info->entryPoint = 1;
        }
        else if (entryPoint == JavascriptPromise::EntryJavascriptPromiseAsyncSpawnStepThrowExecutorFunction)
        {
            info->entryPoint = 2;
        }
        else if (entryPoint == JavascriptPromise::EntryJavascriptPromiseAsyncSpawnCallStepExecutorFunction)
        {
            info->entryPoint = 3;
        }
        else
        {
            TTDAssert(false, "Unexpected entrypoint found JavascriptPromiseAsyncSpawnStepArgumentExecutorFunction");
        }

        const uint32 maxDeps = 4;
        uint32 depCount = 0;
        TTD_PTR_ID* depArray = alloc.SlabReserveArraySpace<TTD_PTR_ID>(maxDeps);
        if (this->reject != nullptr &&  TTD::JsSupport::IsVarComplexKind(this->reject))
        {
            depArray[depCount] = TTD_CONVERT_VAR_TO_PTR_ID(this->reject);
            depCount++;
        }

        if (this->resolve != nullptr &&  TTD::JsSupport::IsVarComplexKind(this->resolve))
        {
            depArray[depCount] = TTD_CONVERT_VAR_TO_PTR_ID(this->resolve);
            depCount++;
        }

        if (this->argument != nullptr &&  TTD::JsSupport::IsVarComplexKind(this->argument))
        {
            depArray[depCount] = TTD_CONVERT_VAR_TO_PTR_ID(this->argument);
            depCount++;
        }

        if (this->generator != nullptr)
        {
            depArray[depCount] = TTD_CONVERT_VAR_TO_PTR_ID(this->generator);
            depCount++;
        }

        if (depCount > 0)
        {
            alloc.SlabCommitArraySpace<TTD_PTR_ID>(depCount, maxDeps);
        }
        else 
        {
            alloc.SlabAbortArraySpace<TTD_PTR_ID>(maxDeps);
        }

        if (depCount == 0)
        {
            TTD::NSSnapObjects::StdExtractSetKindSpecificInfo<TTD::NSSnapObjects::SnapJavascriptPromiseAsyncSpawnStepArgumentExecutorFunctionInfo*, TTD::NSSnapObjects::SnapObjectType::JavascriptPromiseAsyncSpawnStepArgumentExecutorFunction>(objData, info);
        }
        else
        {
            TTDAssert(depArray != nullptr, "depArray should be non-null if depCount is > 0");
            TTD::NSSnapObjects::StdExtractSetKindSpecificInfo<TTD::NSSnapObjects::SnapJavascriptPromiseAsyncSpawnStepArgumentExecutorFunctionInfo*, TTD::NSSnapObjects::SnapObjectType::JavascriptPromiseAsyncSpawnStepArgumentExecutorFunction>(objData, info, alloc, depCount, depArray);
        }
    }
#endif

    JavascriptPromiseCapabilitiesExecutorFunction::JavascriptPromiseCapabilitiesExecutorFunction(DynamicType* type, FunctionInfo* functionInfo, JavascriptPromiseCapability* capability)
        : RuntimeFunction(type, functionInfo), capability(capability)
    { }

    bool JavascriptPromiseCapabilitiesExecutorFunction::Is(Var var)
    {
        if (JavascriptFunction::Is(var))
        {
            JavascriptFunction* obj = JavascriptFunction::UnsafeFromVar(var);

            return VirtualTableInfo<JavascriptPromiseCapabilitiesExecutorFunction>::HasVirtualTable(obj)
                || VirtualTableInfo<CrossSiteObject<JavascriptPromiseCapabilitiesExecutorFunction>>::HasVirtualTable(obj);
        }

        return false;
    }

    JavascriptPromiseCapabilitiesExecutorFunction* JavascriptPromiseCapabilitiesExecutorFunction::FromVar(Var var)
    {
        AssertOrFailFast(JavascriptPromiseCapabilitiesExecutorFunction::Is(var));

        return static_cast<JavascriptPromiseCapabilitiesExecutorFunction*>(var);
    }

    JavascriptPromiseCapabilitiesExecutorFunction* JavascriptPromiseCapabilitiesExecutorFunction::UnsafeFromVar(Var var)
    {
        Assert(JavascriptPromiseCapabilitiesExecutorFunction::Is(var));

        return static_cast<JavascriptPromiseCapabilitiesExecutorFunction*>(var);
    }

    JavascriptPromiseCapability* JavascriptPromiseCapabilitiesExecutorFunction::GetCapability()
    {
        return this->capability;
    }

#if ENABLE_TTD
    void JavascriptPromiseCapabilitiesExecutorFunction::MarkVisitKindSpecificPtrs(TTD::SnapshotExtractor* extractor)
    {
        TTDAssert(false, "Not Implemented Yet");
    }

    TTD::NSSnapObjects::SnapObjectType JavascriptPromiseCapabilitiesExecutorFunction::GetSnapTag_TTD() const
    {
        TTDAssert(false, "Not Implemented Yet");
        return TTD::NSSnapObjects::SnapObjectType::Invalid;
    }

    void JavascriptPromiseCapabilitiesExecutorFunction::ExtractSnapObjectDataInto(TTD::NSSnapObjects::SnapObject* objData, TTD::SlabAllocator& alloc)
    {
        TTDAssert(false, "Not Implemented Yet");
    }
#endif

    JavascriptPromiseCapability* JavascriptPromiseCapability::New(Var promise, Var resolve, Var reject, ScriptContext* scriptContext)
    {
        return RecyclerNew(scriptContext->GetRecycler(), JavascriptPromiseCapability, promise, resolve, reject);
    }

    Var JavascriptPromiseCapability::GetResolve()
    {
        return this->resolve;
    }

    Var JavascriptPromiseCapability::GetReject()
    {
        return this->reject;
    }

    Var JavascriptPromiseCapability::GetPromise()
    {
        return this->promise;
    }

    void JavascriptPromiseCapability::SetPromise(Var promise)
    {
        this->promise = promise;
    }

    void JavascriptPromiseCapability::SetResolve(Var resolve)
    {
        this->resolve = resolve;
    }

    void JavascriptPromiseCapability::SetReject(Var reject)
    {
        this->reject = reject;
    }

#if ENABLE_TTD
    void JavascriptPromiseCapability::MarkVisitPtrs(TTD::SnapshotExtractor* extractor)
    {
        TTDAssert(this->promise != nullptr && this->resolve != nullptr && this->reject != nullptr, "Seems odd, I was not expecting this!!!");

        extractor->MarkVisitVar(this->promise);

        extractor->MarkVisitVar(this->resolve);
        extractor->MarkVisitVar(this->reject);
    }

    void JavascriptPromiseCapability::ExtractSnapPromiseCapabilityInto(TTD::NSSnapValues::SnapPromiseCapabilityInfo* snapPromiseCapability, JsUtil::List<TTD_PTR_ID, HeapAllocator>& depOnList, TTD::SlabAllocator& alloc)
    {
        snapPromiseCapability->CapabilityId = TTD_CONVERT_PROMISE_INFO_TO_PTR_ID(this);

        snapPromiseCapability->PromiseVar = this->promise;
        if(TTD::JsSupport::IsVarComplexKind(this->promise))
        {
            depOnList.Add(TTD_CONVERT_VAR_TO_PTR_ID(this->resolve));
        }

        snapPromiseCapability->ResolveVar = this->resolve;
        if(TTD::JsSupport::IsVarComplexKind(this->resolve))
        {
            depOnList.Add(TTD_CONVERT_VAR_TO_PTR_ID(this->resolve));
        }

        snapPromiseCapability->RejectVar = this->reject;
        if(TTD::JsSupport::IsVarComplexKind(this->reject))
        {
            depOnList.Add(TTD_CONVERT_VAR_TO_PTR_ID(this->reject));
        }
    }
#endif

    JavascriptPromiseReaction* JavascriptPromiseReaction::New(JavascriptPromiseCapability* capabilities, RecyclableObject* handler, ScriptContext* scriptContext)
    {
        return RecyclerNew(scriptContext->GetRecycler(), JavascriptPromiseReaction, capabilities, handler);
    }

    JavascriptPromiseCapability* JavascriptPromiseReaction::GetCapabilities()
    {
        return this->capabilities;
    }

    RecyclableObject* JavascriptPromiseReaction::GetHandler()
    {
        return this->handler;
    }

#if ENABLE_TTD
    void JavascriptPromiseReaction::MarkVisitPtrs(TTD::SnapshotExtractor* extractor)
    {
        TTDAssert(this->handler != nullptr && this->capabilities != nullptr, "Seems odd, I was not expecting this!!!");

        extractor->MarkVisitVar(this->handler);

        this->capabilities->MarkVisitPtrs(extractor);
    }

    void JavascriptPromiseReaction::ExtractSnapPromiseReactionInto(TTD::NSSnapValues::SnapPromiseReactionInfo* snapPromiseReaction, JsUtil::List<TTD_PTR_ID, HeapAllocator>& depOnList, TTD::SlabAllocator& alloc)
    {
        TTDAssert(this->handler != nullptr && this->capabilities != nullptr, "Seems odd, I was not expecting this!!!");

        snapPromiseReaction->PromiseReactionId = TTD_CONVERT_PROMISE_INFO_TO_PTR_ID(this);

        snapPromiseReaction->HandlerObjId = TTD_CONVERT_VAR_TO_PTR_ID(this->handler);
        depOnList.Add(snapPromiseReaction->HandlerObjId);

        this->capabilities->ExtractSnapPromiseCapabilityInto(&snapPromiseReaction->Capabilities, depOnList, alloc);
    }
#endif

    JavascriptPromiseReaction* JavascriptPromiseReactionTaskFunction::GetReaction()
    {
        return this->reaction;
    }

    Var JavascriptPromiseReactionTaskFunction::GetArgument()
    {
        return this->argument;
    }

#if ENABLE_TTD
    void JavascriptPromiseReactionTaskFunction::MarkVisitKindSpecificPtrs(TTD::SnapshotExtractor* extractor)
    {
        TTDAssert(this->argument != nullptr && this->reaction != nullptr, "Was not expecting this!!!");

        extractor->MarkVisitVar(this->argument);

        this->reaction->MarkVisitPtrs(extractor);
    }

    TTD::NSSnapObjects::SnapObjectType JavascriptPromiseReactionTaskFunction::GetSnapTag_TTD() const
    {
        return TTD::NSSnapObjects::SnapObjectType::SnapPromiseReactionTaskFunctionObject;
    }

    void JavascriptPromiseReactionTaskFunction::ExtractSnapObjectDataInto(TTD::NSSnapObjects::SnapObject* objData, TTD::SlabAllocator& alloc)
    {
        TTD::NSSnapObjects::SnapPromiseReactionTaskFunctionInfo* sprtfi = alloc.SlabAllocateStruct<TTD::NSSnapObjects::SnapPromiseReactionTaskFunctionInfo>();

        JsUtil::List<TTD_PTR_ID, HeapAllocator> depOnList(&HeapAllocator::Instance);

        sprtfi->Argument = this->argument;

        if(this->argument != nullptr && TTD::JsSupport::IsVarComplexKind(this->argument))
        {
            depOnList.Add(TTD_CONVERT_VAR_TO_PTR_ID(this->argument));
        }

        this->reaction->ExtractSnapPromiseReactionInto(&sprtfi->Reaction, depOnList, alloc);

        //see what we need to do wrt dependencies
        if(depOnList.Count() == 0)
        {
            TTD::NSSnapObjects::StdExtractSetKindSpecificInfo<TTD::NSSnapObjects::SnapPromiseReactionTaskFunctionInfo*, TTD::NSSnapObjects::SnapObjectType::SnapPromiseReactionTaskFunctionObject>(objData, sprtfi);
        }
        else
        {
            uint32 depOnCount = depOnList.Count();
            TTD_PTR_ID* depOnArray = alloc.SlabAllocateArray<TTD_PTR_ID>(depOnCount);

            for(uint32 i = 0; i < depOnCount; ++i)
            {
                depOnArray[i] = depOnList.Item(i);
            }

            TTD::NSSnapObjects::StdExtractSetKindSpecificInfo<TTD::NSSnapObjects::SnapPromiseReactionTaskFunctionInfo*, TTD::NSSnapObjects::SnapObjectType::SnapPromiseReactionTaskFunctionObject>(objData, sprtfi, alloc, depOnCount, depOnArray);
        }
    }
#endif

    JavascriptPromise* JavascriptPromiseResolveThenableTaskFunction::GetPromise()
    {
        return this->promise;
    }

    RecyclableObject* JavascriptPromiseResolveThenableTaskFunction::GetThenable()
    {
        return this->thenable;
    }

    RecyclableObject* JavascriptPromiseResolveThenableTaskFunction::GetThenFunction()
    {
        return this->thenFunction;
    }

#if ENABLE_TTD
    void JavascriptPromiseResolveThenableTaskFunction::MarkVisitKindSpecificPtrs(TTD::SnapshotExtractor* extractor)
    {
        TTDAssert(false, "Not Implemented Yet");
    }

    TTD::NSSnapObjects::SnapObjectType JavascriptPromiseResolveThenableTaskFunction::GetSnapTag_TTD() const
    {
        TTDAssert(false, "Not Implemented Yet");
        return TTD::NSSnapObjects::SnapObjectType::Invalid;
    }

    void JavascriptPromiseResolveThenableTaskFunction::ExtractSnapObjectDataInto(TTD::NSSnapObjects::SnapObject* objData, TTD::SlabAllocator& alloc)
    {
        TTDAssert(false, "Not Implemented Yet");
    }
#endif

    JavascriptPromiseAllResolveElementFunction::JavascriptPromiseAllResolveElementFunction(DynamicType* type)
        : RuntimeFunction(type, &Js::JavascriptPromise::EntryInfo::AllResolveElementFunction), index(0), values(nullptr), capabilities(nullptr), remainingElementsWrapper(nullptr), alreadyCalled(false)
    { }

    JavascriptPromiseAllResolveElementFunction::JavascriptPromiseAllResolveElementFunction(DynamicType* type, FunctionInfo* functionInfo, uint32 index, JavascriptArray* values, JavascriptPromiseCapability* capabilities, JavascriptPromiseAllResolveElementFunctionRemainingElementsWrapper* remainingElementsWrapper)
        : RuntimeFunction(type, functionInfo), index(index), values(values), capabilities(capabilities), remainingElementsWrapper(remainingElementsWrapper), alreadyCalled(false)
    { }

    bool JavascriptPromiseAllResolveElementFunction::Is(Var var)
    {
        if (JavascriptFunction::Is(var))
        {
            JavascriptFunction* obj = JavascriptFunction::UnsafeFromVar(var);

            return VirtualTableInfo<JavascriptPromiseAllResolveElementFunction>::HasVirtualTable(obj)
                || VirtualTableInfo<CrossSiteObject<JavascriptPromiseAllResolveElementFunction>>::HasVirtualTable(obj);
        }

        return false;
    }

    JavascriptPromiseAllResolveElementFunction* JavascriptPromiseAllResolveElementFunction::FromVar(Var var)
    {
        AssertOrFailFast(JavascriptPromiseAllResolveElementFunction::Is(var));

        return static_cast<JavascriptPromiseAllResolveElementFunction*>(var);
    }

    JavascriptPromiseAllResolveElementFunction* JavascriptPromiseAllResolveElementFunction::UnsafeFromVar(Var var)
    {
        Assert(JavascriptPromiseAllResolveElementFunction::Is(var));

        return static_cast<JavascriptPromiseAllResolveElementFunction*>(var);
    }

    JavascriptPromiseCapability* JavascriptPromiseAllResolveElementFunction::GetCapabilities()
    {
        return this->capabilities;
    }

    uint32 JavascriptPromiseAllResolveElementFunction::GetIndex()
    {
        return this->index;
    }

    uint32 JavascriptPromiseAllResolveElementFunction::GetRemainingElements()
    {
        return this->remainingElementsWrapper->remainingElements;
    }

    JavascriptArray* JavascriptPromiseAllResolveElementFunction::GetValues()
    {
        return this->values;
    }

    uint32 JavascriptPromiseAllResolveElementFunction::DecrementRemainingElements()
    {
        return --(this->remainingElementsWrapper->remainingElements);
    }

    bool JavascriptPromiseAllResolveElementFunction::IsAlreadyCalled() const
    {
        return this->alreadyCalled;
    }

    void JavascriptPromiseAllResolveElementFunction::SetAlreadyCalled(const bool is)
    {
        this->alreadyCalled = is;
    }

#if ENABLE_TTD
    void JavascriptPromiseAllResolveElementFunction::MarkVisitKindSpecificPtrs(TTD::SnapshotExtractor* extractor)
    {
        TTDAssert(this->capabilities != nullptr && this->remainingElementsWrapper != nullptr && this->values != nullptr, "Don't think these can be null");

        this->capabilities->MarkVisitPtrs(extractor);
        extractor->MarkVisitVar(this->values);
    }

    TTD::NSSnapObjects::SnapObjectType JavascriptPromiseAllResolveElementFunction::GetSnapTag_TTD() const
    {
        return TTD::NSSnapObjects::SnapObjectType::SnapPromiseAllResolveElementFunctionObject;
    }

    void JavascriptPromiseAllResolveElementFunction::ExtractSnapObjectDataInto(TTD::NSSnapObjects::SnapObject* objData, TTD::SlabAllocator& alloc)
    {
        TTD::NSSnapObjects::SnapPromiseAllResolveElementFunctionInfo* sprai = alloc.SlabAllocateStruct<TTD::NSSnapObjects::SnapPromiseAllResolveElementFunctionInfo>();

        JsUtil::List<TTD_PTR_ID, HeapAllocator> depOnList(&HeapAllocator::Instance);
        this->capabilities->ExtractSnapPromiseCapabilityInto(&sprai->Capabilities, depOnList, alloc);

        sprai->Index = this->index;
        sprai->RemainingElementsWrapperId = TTD_CONVERT_PROMISE_INFO_TO_PTR_ID(this->remainingElementsWrapper);
        sprai->RemainingElementsValue = this->remainingElementsWrapper->remainingElements;

        sprai->Values = TTD_CONVERT_VAR_TO_PTR_ID(this->values);
        depOnList.Add(sprai->Values);

        sprai->AlreadyCalled = this->alreadyCalled;

        uint32 depOnCount = depOnList.Count();
        TTD_PTR_ID* depOnArray = alloc.SlabAllocateArray<TTD_PTR_ID>(depOnCount);

        for(uint32 i = 0; i < depOnCount; ++i)
        {
            depOnArray[i] = depOnList.Item(i);
        }

        TTD::NSSnapObjects::StdExtractSetKindSpecificInfo<TTD::NSSnapObjects::SnapPromiseAllResolveElementFunctionInfo*, TTD::NSSnapObjects::SnapObjectType::SnapPromiseAllResolveElementFunctionObject>(objData, sprai, alloc, depOnCount, depOnArray);
    }
#endif

    Var JavascriptPromise::EntryGetterSymbolSpecies(RecyclableObject* function, CallInfo callInfo, ...)
    {
        ARGUMENTS(args, callInfo);

        Assert(args.Info.Count > 0);

        return args[0];
    }

    //static
    JavascriptPromise* JavascriptPromise::CreateEnginePromise(ScriptContext *scriptContext)
    {
        JavascriptPromiseResolveOrRejectFunction *resolve = nullptr;
        JavascriptPromiseResolveOrRejectFunction *reject = nullptr;

        JavascriptPromise *promise = scriptContext->GetLibrary()->CreatePromise();
        JavascriptPromise::InitializePromise(promise, &resolve, &reject, scriptContext);

        return promise;
    }
} // namespace Js
