//-------------------------------------------------------------------------------------------------------
// Copyright (C) Microsoft. All rights reserved.
// Licensed under the MIT license. See LICENSE.txt file in the project root for full license information.
//-------------------------------------------------------------------------------------------------------
#include "RuntimeLibraryPch.h"

namespace Js
{
    FunctionInfo BoundFunction::functionInfo(FORCE_NO_WRITE_BARRIER_TAG(BoundFunction::NewInstance), FunctionInfo::DoNotProfile);

    BoundFunction::BoundFunction(DynamicType * type)
        : JavascriptFunction(type, &functionInfo),
        targetFunction(nullptr),
        boundThis(nullptr),
        count(0),
        boundArgs(nullptr)
    {
        // Constructor used during copy on write.
        DebugOnly(VerifyEntryPoint());
    }

    BoundFunction::BoundFunction(Arguments args, DynamicType * type)
        : JavascriptFunction(type, &functionInfo),
        count(0),
        boundArgs(nullptr)
    {

        DebugOnly(VerifyEntryPoint());
        AssertMsg(args.Info.Count > 0, "wrong number of args in BoundFunction");

        ScriptContext *scriptContext = this->GetScriptContext();
        targetFunction = RecyclableObject::FromVar(args[0]);

        Assert(!CrossSite::NeedMarshalVar(targetFunction, scriptContext));

        // Let proto be targetFunction.[[GetPrototypeOf]]().
        RecyclableObject* proto = JavascriptOperators::GetPrototype(targetFunction);
        if (proto != type->GetPrototype())
        {
            if (type->GetIsShared())
            {
                this->ChangeType();
                type = this->GetDynamicType();
            }
            type->SetPrototype(proto);
        }
        // If targetFunction is proxy, need to make sure that traps are called in right order as per 19.2.3.2 in RC#4 dated April 3rd 2015.
        // Here although we won't use value of length, this is just to make sure that we call traps involved with HasOwnProperty(Target, "length") and Get(Target, "length")
        if (JavascriptProxy::Is(targetFunction))
        {
            if (JavascriptOperators::HasOwnProperty(targetFunction, PropertyIds::length, scriptContext, nullptr) == TRUE)
            {
                int len = 0;
                Var varLength;
                if (targetFunction->GetProperty(targetFunction, PropertyIds::length, &varLength, nullptr, scriptContext))
                {
                    len = JavascriptConversion::ToInt32(varLength, scriptContext);
                }
            }
            GetTypeHandler()->EnsureObjectReady(this);
        }

        if (args.Info.Count > 1)
        {
            boundThis = args[1];

            // function object and "this" arg
            const uint countAccountedFor = 2;
            count = args.Info.Count - countAccountedFor;

            // Store the args excluding function obj and "this" arg
            if (args.Info.Count > 2)
            {
                boundArgs = RecyclerNewArray(scriptContext->GetRecycler(), Field(Var), count);

                for (uint i=0; i<count; i++)
                {
                    boundArgs[i] = args[i+countAccountedFor];
                }
            }
        }
        else
        {
            // If no "this" is passed, "undefined" is used
            boundThis = scriptContext->GetLibrary()->GetUndefined();
        }
    }

    BoundFunction::BoundFunction(RecyclableObject* targetFunction, Var boundThis, Var* args, uint argsCount, DynamicType * type)
        : JavascriptFunction(type, &functionInfo),
        count(argsCount),
        boundArgs(nullptr)
    {
        DebugOnly(VerifyEntryPoint());

        this->targetFunction = targetFunction;
        this->boundThis = boundThis;

        if (argsCount != 0)
        {
            this->boundArgs = RecyclerNewArray(this->GetScriptContext()->GetRecycler(), Field(Var), argsCount);

            for (uint i = 0; i < argsCount; i++)
            {
                this->boundArgs[i] = args[i];
            }
        }
    }

    BoundFunction* BoundFunction::New(ScriptContext* scriptContext, ArgumentReader args)
    {
        Recycler* recycler = scriptContext->GetRecycler();

        BoundFunction* boundFunc = RecyclerNew(recycler, BoundFunction, args,
            scriptContext->GetLibrary()->GetBoundFunctionType());
        return boundFunc;
    }

    Var BoundFunction::NewInstance(RecyclableObject* function, CallInfo callInfo, ...)
    {
        RUNTIME_ARGUMENTS(args, callInfo);
        ScriptContext* scriptContext = function->GetScriptContext();

        if (args.Info.Count == 0)
        {
            JavascriptError::ThrowTypeError(scriptContext, JSERR_NeedFunction /* TODO-ERROR: get arg name - args[0] */);
        }

        BoundFunction *boundFunction = (BoundFunction *) function;
        Var targetFunction = boundFunction->targetFunction;

        //
        // var o = new boundFunction()
        // a new object should be created using the actual function object
        //
        Var newVarInstance = nullptr;
        if (callInfo.Flags & CallFlags_New)
        {
          if (JavascriptProxy::Is(targetFunction))
          {
            JavascriptProxy* proxy = JavascriptProxy::FromVar(targetFunction);
            Arguments proxyArgs(CallInfo(CallFlags_New, 1), &targetFunction);
            args.Values[0] = newVarInstance = proxy->ConstructorTrap(proxyArgs, scriptContext, 0);
          }
          else
          {
            args.Values[0] = newVarInstance = JavascriptOperators::NewScObjectNoCtor(targetFunction, scriptContext);
          }
        }

        Js::Arguments actualArgs = args;

        if (boundFunction->count > 0)
        {
            // OACR thinks that this can change between here and the check in the for loop below
            const unsigned int argCount = args.Info.Count;

            if ((boundFunction->count + argCount) > CallInfo::kMaxCountArgs)
            {
                JavascriptError::ThrowRangeError(scriptContext, JSERR_ArgListTooLarge);
            }

            Field(Var) *newValues = RecyclerNewArray(scriptContext->GetRecycler(), Field(Var), boundFunction->count + argCount);

            uint index = 0;

            //
            // For [[Construct]] use the newly created var instance
            // For [[Call]] use the "this" to which bind bound it.
            //
            if (callInfo.Flags & CallFlags_New)
            {
                newValues[index++] = args[0];
            }
            else
            {
                newValues[index++] = boundFunction->boundThis;
            }

            for (uint i = 0; i < boundFunction->count; i++)
            {
                newValues[index++] = boundFunction->boundArgs[i];
            }

            // Copy the extra args
            for (uint i=1; i<argCount; i++)
            {
                newValues[index++] = args[i];
            }

            actualArgs = Arguments(args.Info, unsafe_write_barrier_cast<Var*>(newValues));
            actualArgs.Info.Count = boundFunction->count + argCount;
        }
        else
        {
            if (!(callInfo.Flags & CallFlags_New))
            {
                actualArgs.Values[0] = boundFunction->boundThis;
            }
        }

        RecyclableObject* actualFunction = RecyclableObject::FromVar(targetFunction);
        // Number of arguments are allowed to be more than Constants::MaxAllowedArgs in runtime. Need to use the larger argcount logic for this call.
        Var aReturnValue = JavascriptFunction::CallFunction<true>(actualFunction, actualFunction->GetEntryPoint(), actualArgs, /* useLargeArgCount */ true);

        //
        // [[Construct]] and call returned a non-object
        // return the newly created var instance
        //
        if ((callInfo.Flags & CallFlags_New) && !JavascriptOperators::IsObject(aReturnValue))
        {
            aReturnValue = newVarInstance;
        }

        return aReturnValue;
    }

    JavascriptFunction * BoundFunction::GetTargetFunction() const
    {
        if (targetFunction != nullptr)
        {
            RecyclableObject* _targetFunction = targetFunction;
            while (JavascriptProxy::Is(_targetFunction))
            {
                _targetFunction = JavascriptProxy::FromVar(_targetFunction)->GetTarget();
            }

            if (JavascriptFunction::Is(_targetFunction))
            {
                return JavascriptFunction::FromVar(_targetFunction);
            }

            // targetFunction should always be a JavascriptFunction.
            Assert(FALSE);
        }
        return nullptr;
    }

    JavascriptString* BoundFunction::GetDisplayNameImpl() const
    {
        JavascriptString* displayName = GetLibrary()->GetEmptyString();
        if (targetFunction != nullptr)
        {
            Var value = JavascriptOperators::GetPropertyNoCache(targetFunction, PropertyIds::name, targetFunction->GetScriptContext());
            if (JavascriptString::Is(value))
            {
                displayName = JavascriptString::FromVar(value);
            }
        }
        return LiteralString::Concat(LiteralString::NewCopySz(_u("bound "), this->GetScriptContext()), displayName);
    }

    RecyclableObject* BoundFunction::GetBoundThis()
    {
        if (boundThis != nullptr && RecyclableObject::Is(boundThis))
        {
            return RecyclableObject::FromVar(boundThis);
        }
        return NULL;
    }

    inline BOOL BoundFunction::IsConstructor() const
    {
        if (this->targetFunction != nullptr)
        {
            return JavascriptOperators::IsConstructor(this->GetTargetFunction());
        }

        return false;
    }

    PropertyQueryFlags BoundFunction::HasPropertyQuery(PropertyId propertyId, _Inout_opt_ PropertyValueInfo* info)
    {
        if (propertyId == PropertyIds::length)
        {
            return PropertyQueryFlags::Property_Found;
        }

        return JavascriptFunction::HasPropertyQuery(propertyId, info);
    }

    PropertyQueryFlags BoundFunction::GetPropertyQuery(Var originalInstance, PropertyId propertyId, Var* value, PropertyValueInfo* info, ScriptContext* requestContext)
    {
        BOOL result;
        if (GetPropertyBuiltIns(originalInstance, propertyId, value, info, requestContext, &result))
        {
            return JavascriptConversion::BooleanToPropertyQueryFlags(result);
        }

        return JavascriptFunction::GetPropertyQuery(originalInstance, propertyId, value, info, requestContext);
    }

    PropertyQueryFlags BoundFunction::GetPropertyQuery(Var originalInstance, JavascriptString* propertyNameString, Var* value, PropertyValueInfo* info, ScriptContext* requestContext)
    {
        BOOL result;
        PropertyRecord const* propertyRecord;
        this->GetScriptContext()->FindPropertyRecord(propertyNameString, &propertyRecord);

        if (propertyRecord != nullptr && GetPropertyBuiltIns(originalInstance, propertyRecord->GetPropertyId(), value, info, requestContext, &result))
        {
            return JavascriptConversion::BooleanToPropertyQueryFlags(result);
        }

        return JavascriptFunction::GetPropertyQuery(originalInstance, propertyNameString, value, info, requestContext);
    }

    bool BoundFunction::GetPropertyBuiltIns(Var originalInstance, PropertyId propertyId, Var* value, PropertyValueInfo* info, ScriptContext* requestContext, BOOL* result)
    {
        if (propertyId == PropertyIds::length)
        {
            // Get the "length" property of the underlying target function
            int len = 0;
            Var varLength;
            if (targetFunction->GetProperty(targetFunction, PropertyIds::length, &varLength, nullptr, requestContext))
            {
                len = JavascriptConversion::ToInt32(varLength, requestContext);
            }

            // Reduce by number of bound args
            len = len - this->count;
            len = max(len, 0);

            *value = JavascriptNumber::ToVar(len, requestContext);
            *result = true;
            return true;
        }

        return false;
    }

    PropertyQueryFlags BoundFunction::GetPropertyReferenceQuery(Var originalInstance, PropertyId propertyId, Var* value, PropertyValueInfo* info, ScriptContext* requestContext)
    {
        return BoundFunction::GetPropertyQuery(originalInstance, propertyId, value, info, requestContext);
    }

    BOOL BoundFunction::SetProperty(PropertyId propertyId, Var value, PropertyOperationFlags flags, PropertyValueInfo* info)
    {
        BOOL result;
        if (SetPropertyBuiltIns(propertyId, value, flags, info, &result))
        {
            return result;
        }

        return JavascriptFunction::SetProperty(propertyId, value, flags, info);
    }

    BOOL BoundFunction::SetProperty(JavascriptString* propertyNameString, Var value, PropertyOperationFlags flags, PropertyValueInfo* info)
    {
        BOOL result;
        PropertyRecord const* propertyRecord;
        this->GetScriptContext()->FindPropertyRecord(propertyNameString, &propertyRecord);

        if (propertyRecord != nullptr && SetPropertyBuiltIns(propertyRecord->GetPropertyId(), value, flags, info, &result))
        {
            return result;
        }

        return JavascriptFunction::SetProperty(propertyNameString, value, flags, info);
    }

    bool BoundFunction::SetPropertyBuiltIns(PropertyId propertyId, Var value, PropertyOperationFlags flags, PropertyValueInfo* info, BOOL* result)
    {
        if (propertyId == PropertyIds::length)
        {
            JavascriptError::ThrowCantAssignIfStrictMode(flags, this->GetScriptContext());

            *result = false;
            return true;
        }

        return false;
    }

    BOOL BoundFunction::GetAccessors(PropertyId propertyId, Var *getter, Var *setter, ScriptContext * requestContext)
    {
        return DynamicObject::GetAccessors(propertyId, getter, setter, requestContext);
    }

    DescriptorFlags BoundFunction::GetSetter(PropertyId propertyId, Var *setterValue, PropertyValueInfo* info, ScriptContext* requestContext)
    {
        return DynamicObject::GetSetter(propertyId, setterValue, info, requestContext);
    }

    DescriptorFlags BoundFunction::GetSetter(JavascriptString* propertyNameString, Var *setterValue, PropertyValueInfo* info, ScriptContext* requestContext)
    {
        return DynamicObject::GetSetter(propertyNameString, setterValue, info, requestContext);
    }

    BOOL BoundFunction::InitProperty(PropertyId propertyId, Var value, PropertyOperationFlags flags, PropertyValueInfo* info)
    {
        return SetProperty(propertyId, value, PropertyOperation_None, info);
    }

    BOOL BoundFunction::DeleteProperty(PropertyId propertyId, PropertyOperationFlags flags)
    {
        if (propertyId == PropertyIds::length)
        {
            return false;
        }

        return JavascriptFunction::DeleteProperty(propertyId, flags);
    }

    BOOL BoundFunction::DeleteProperty(JavascriptString *propertyNameString, PropertyOperationFlags flags)
    {
        if (BuiltInPropertyRecords::length.Equals(propertyNameString))
        {
            return false;
        }

        return JavascriptFunction::DeleteProperty(propertyNameString, flags);
    }

    BOOL BoundFunction::IsWritable(PropertyId propertyId)
    {
        if (propertyId == PropertyIds::length)
        {
            return false;
        }

        return JavascriptFunction::IsWritable(propertyId);
    }

    BOOL BoundFunction::IsConfigurable(PropertyId propertyId)
    {
        if (propertyId == PropertyIds::length)
        {
            return false;
        }

        return JavascriptFunction::IsConfigurable(propertyId);
    }

    BOOL BoundFunction::IsEnumerable(PropertyId propertyId)
    {
        if (propertyId == PropertyIds::length)
        {
            return false;
        }

        return JavascriptFunction::IsEnumerable(propertyId);
    }

    BOOL BoundFunction::HasInstance(Var instance, ScriptContext* scriptContext, IsInstInlineCache* inlineCache)
    {
        return this->targetFunction->HasInstance(instance, scriptContext, inlineCache);
    }

#if ENABLE_TTD
    void BoundFunction::MarkVisitKindSpecificPtrs(TTD::SnapshotExtractor* extractor)
    {
        extractor->MarkVisitVar(this->targetFunction);

        if(this->boundThis != nullptr)
        {
            extractor->MarkVisitVar(this->boundThis);
        }

        for(uint32 i = 0; i < this->count; ++i)
        {
            extractor->MarkVisitVar(this->boundArgs[i]);
        }
    }

    void BoundFunction::ProcessCorePaths()
    {
        this->GetScriptContext()->TTDWellKnownInfo->EnqueueNewPathVarAsNeeded(this, this->targetFunction, _u("!targetFunction"));
        this->GetScriptContext()->TTDWellKnownInfo->EnqueueNewPathVarAsNeeded(this, this->boundThis, _u("!boundThis"));

        TTDAssert(this->count == 0, "Should only have empty args in core image");
    }

    TTD::NSSnapObjects::SnapObjectType BoundFunction::GetSnapTag_TTD() const
    {
        return TTD::NSSnapObjects::SnapObjectType::SnapBoundFunctionObject;
    }

    void BoundFunction::ExtractSnapObjectDataInto(TTD::NSSnapObjects::SnapObject* objData, TTD::SlabAllocator& alloc)
    {
        TTD::NSSnapObjects::SnapBoundFunctionInfo* bfi = alloc.SlabAllocateStruct<TTD::NSSnapObjects::SnapBoundFunctionInfo>();

        bfi->TargetFunction = TTD_CONVERT_VAR_TO_PTR_ID(static_cast<RecyclableObject*>(this->targetFunction));
        bfi->BoundThis = (this->boundThis != nullptr) ?
            TTD_CONVERT_VAR_TO_PTR_ID(static_cast<Var>(this->boundThis)) : TTD_INVALID_PTR_ID;

        bfi->ArgCount = this->count;
        bfi->ArgArray = nullptr;

        if(bfi->ArgCount > 0)
        {
            bfi->ArgArray = alloc.SlabAllocateArray<TTD::TTDVar>(bfi->ArgCount);
        }

        TTD_PTR_ID* depArray = alloc.SlabReserveArraySpace<TTD_PTR_ID>(bfi->ArgCount + 2 /*this and bound function*/);

        depArray[0] = bfi->TargetFunction;
        uint32 depCount = 1;

        if(this->boundThis != nullptr && TTD::JsSupport::IsVarComplexKind(this->boundThis))
        {
            depArray[depCount] = bfi->BoundThis;
            depCount++;
        }

        if(bfi->ArgCount > 0)
        {
            for(uint32 i = 0; i < bfi->ArgCount; ++i)
            {
                bfi->ArgArray[i] = this->boundArgs[i];

                //Primitive kinds always inflated first so we only need to deal with complex kinds as depends on
                if(TTD::JsSupport::IsVarComplexKind(this->boundArgs[i]))
                {
                    depArray[depCount] = TTD_CONVERT_VAR_TO_PTR_ID(this->boundArgs[i]);
                    depCount++;
                }
            }
        }
        alloc.SlabCommitArraySpace<TTD_PTR_ID>(depCount, depCount + bfi->ArgCount);

        TTD::NSSnapObjects::StdExtractSetKindSpecificInfo<TTD::NSSnapObjects::SnapBoundFunctionInfo*, TTD::NSSnapObjects::SnapObjectType::SnapBoundFunctionObject>(objData, bfi, alloc, depCount, depArray);
    }

    BoundFunction* BoundFunction::InflateBoundFunction(
        ScriptContext* ctx, RecyclableObject* function, Var bThis, uint32 ct, Field(Var)* args)
    {
        BoundFunction* res = RecyclerNew(ctx->GetRecycler(), BoundFunction, ctx->GetLibrary()->GetBoundFunctionType());

        res->boundThis = bThis;
        res->count = ct;
        res->boundArgs = args;

        res->targetFunction = function;

        return res;
    }
#endif
} // namespace Js
