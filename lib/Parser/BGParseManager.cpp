//-------------------------------------------------------------------------------------------------------
// Copyright (C) Microsoft. All rights reserved.
// Licensed under the MIT license. See LICENSE.txt file in the project root for full license information.
//-------------------------------------------------------------------------------------------------------

#include "ParserPch.h"

#include "Memory/AutoPtr.h"
#include "Common/Event.h"
#include "Base/ThreadContextTlsEntry.h"
#include "Base/ThreadBoundThreadContextManager.h"
#include "Base/Utf8SourceInfo.h"
#include "BGParseManager.h"
#include "Base/ScriptContext.h"
#include "ByteCodeSerializer.h"

// Global, process singleton
BGParseManager* BGParseManager::s_BGParseManager = nullptr;
DWORD           BGParseManager::s_lastCookie = 0;
CriticalSection BGParseManager::s_staticMemberLock;

// Static member management
BGParseManager* BGParseManager::GetBGParseManager()
{
    AutoCriticalSection lock(&s_staticMemberLock);
    if (s_BGParseManager == nullptr)
    {        
        AUTO_NESTED_HANDLED_EXCEPTION_TYPE(ExceptionType_DisableCheck);
        s_BGParseManager = HeapNew(BGParseManager);
        s_BGParseManager->Processor()->AddManager(s_BGParseManager);
    }
    return s_BGParseManager;
}

void BGParseManager::DeleteBGParseManager()
{
    AutoCriticalSection lock(&s_staticMemberLock);
    if (s_BGParseManager != nullptr)
    {
        BGParseManager* manager = s_BGParseManager;
        s_BGParseManager = nullptr;
        HeapDelete(manager);
    }
}

DWORD BGParseManager::GetNextCookie()
{
    AutoCriticalSection lock(&s_staticMemberLock);
    return ++s_lastCookie;
}


// Note: runs on any thread
BGParseManager::BGParseManager()
    : JsUtil::WaitableJobManager(ThreadBoundThreadContextManager::GetSharedJobProcessor())
{
}

// Note: runs on any thread
BGParseManager::~BGParseManager()
{
    // First, remove the manager from the JobProcessor so that any remaining jobs
    // are moved back into this manager's processed workitem list
    Processor()->RemoveManager(this);

    Assert(this->workitemsProcessing.IsEmpty());

    // Now, free all remaining processed jobs
    BGParseWorkItem *item = (BGParseWorkItem*)this->workitemsProcessed.Head();
    while (item != nullptr)
    {
        // Get the next item first so that the current item
        // can be safely removed and freed
        BGParseWorkItem* temp = item;
        item = (BGParseWorkItem*)item->Next();

        this->workitemsProcessed.Unlink(temp);
        HeapDelete(temp);
    }
}

// Returns the BGParseWorkItem that matches the provided cookie
// Note: runs on any thread
BGParseWorkItem* BGParseManager::FindJob(DWORD dwCookie, bool waitForResults)
{
    AutoOptionalCriticalSection autoLock(Processor()->GetCriticalSection());

    Assert(dwCookie != 0);
    BGParseWorkItem* matchedWorkitem = nullptr;

    // First, look among processed jobs
    for (BGParseWorkItem *item = this->workitemsProcessed.Head(); item != nullptr && matchedWorkitem == nullptr; item = (BGParseWorkItem*)item->Next())
    {
        if (item->GetCookie() == dwCookie)
        {
            matchedWorkitem = item;
        }
    }

    if (matchedWorkitem == nullptr)
    {
        // Then, look among processing jobs
        for (BGParseWorkItem *item = this->workitemsProcessing.Head(); item != nullptr && matchedWorkitem == nullptr; item = (BGParseWorkItem*)item->Next())
        {
            if (item->GetCookie() == dwCookie)
            {
                matchedWorkitem = item;
            }
        }

        // Lastly, look among queued jobs
        if (matchedWorkitem == nullptr)
        {
            Processor()->ForEachJob([&](JsUtil::Job * job) {
                if (job->Manager() == this)
                {
                    BGParseWorkItem* workitem = (BGParseWorkItem*)job;
                    if (workitem->GetCookie() == dwCookie)
                    {
                        matchedWorkitem = workitem;
                        return false;
                    }
                }
                return true;
            });
        }

        // Since this job isn't already processed and the caller needs the results, create an event
        // that the caller can wait on for results to complete
        if (waitForResults && matchedWorkitem != nullptr)
        {
            // TODO: Is it possible for one event to be shared to reduce the number of heap allocations?
            matchedWorkitem->CreateCompletionEvent();
        }
    }

    return matchedWorkitem;
}

// Creates a new job to parse the provided script on a background thread
// Note: runs on any thread
HRESULT BGParseManager::QueueBackgroundParse(LPCUTF8 pszSrc, size_t cbLength, char16 *fullPath, DWORD* dwBgParseCookie)
{
    HRESULT hr = S_OK;
    if (cbLength > 0)
    {
        BGParseWorkItem* workitem;
        {
            AUTO_NESTED_HANDLED_EXCEPTION_TYPE(ExceptionType_DisableCheck);
            workitem = HeapNew(BGParseWorkItem, this, (const byte *)pszSrc, cbLength, fullPath);
        }

        // Add the job to the processor
        {
            AutoOptionalCriticalSection autoLock(Processor()->GetCriticalSection());
            Processor()->AddJob(workitem, false /*prioritize*/);
        }

        (*dwBgParseCookie) = workitem->GetCookie();

        if (PHASE_TRACE1(Js::BgParsePhase))
        {
            Js::Tick now = Js::Tick::Now();
            Output::Print(
                _u("[BgParse: Start -- cookie: %04d on thread 0x%X at %.2f ms -- %s]\n"),
                workitem->GetCookie(),
                ::GetCurrentThreadId(),
                now.ToMilliseconds(),
                fullPath
            );
        }
    }
    else
    {
        hr = E_INVALIDARG;
    }

    return hr;
}

// Returns the data provided when the parse was queued
// Note: runs on any thread, but the buffer lifetimes are not guaranteed after parse results are returned
HRESULT BGParseManager::GetInputFromCookie(DWORD cookie, LPCUTF8* ppszSrc, size_t* pcbLength)
{
    HRESULT hr = E_FAIL;

    // Find the job associated with this cookie
    BGParseWorkItem* workitem = FindJob(cookie, false);
    if (workitem != nullptr)
    {
        (*ppszSrc) = workitem->GetScriptSrc();
        (*pcbLength) = workitem->GetScriptLength();
        hr = S_OK;
    }

    return hr;
}

// Deserializes the background parse results into this thread
// Note: *must* run on a UI/Execution thread with an available ScriptContext
HRESULT BGParseManager::GetParseResults(Js::ScriptContext* scriptContextUI, DWORD cookie, LPCUTF8 pszSrc, SRCINFO const * pSrcInfo, Js::ParseableFunctionInfo** ppFunc, CompileScriptException* pse, size_t& srcLength)
{
    // TODO: Is there a way to cache the environment from which serialization begins to
    // determine whether or not deserialization will succeed? Specifically, being able
    // to assert/compare the flags used during background parse with the flags expected
    // from the UI thread?

    HRESULT hr = E_FAIL;

    // Find the job associated with this cookie
    BGParseWorkItem* workitem = FindJob(cookie, true);
    if (workitem != nullptr)
    {
        // Synchronously wait for the job to complete
        workitem->WaitForCompletion();

        Field(Js::FunctionBody*) functionBody = nullptr;
        hr = workitem->GetParseHR();
        if (hr == S_OK)
        {
            srcLength = workitem->GetParseSourceLength();
            hr = Js::ByteCodeSerializer::DeserializeFromBuffer(
                scriptContextUI,
                0, // flags
                (const byte *)pszSrc,
                pSrcInfo,
                workitem->GetReturnBuffer(),
                nullptr, // nativeModule
                &functionBody
            );
        }

        *ppFunc = functionBody;
        workitem->TransferCSE(pse);
    }

    if (PHASE_TRACE1(Js::BgParsePhase))
    {
        Js::Tick now = Js::Tick::Now();
        Output::Print(
            _u("[BgParse: End   -- cookie: %04d on thread 0x%X at %.2f ms -- hr: 0x%X]\n"),
            workitem->GetCookie(),
            ::GetCurrentThreadId(),
            now.ToMilliseconds(),
            hr
        );
    }

    return hr;
}

// Overloaded function called by JobProcessor to do work
// Note: runs on background thread
bool BGParseManager::Process(JsUtil::Job *const job, JsUtil::ParallelThreadData *threadData) 
{
#if ENABLE_BACKGROUND_JOB_PROCESSOR
    Assert(job->Manager() == this);

    // Create script context on this thread
    ThreadContext* threadContext = ThreadBoundThreadContextManager::EnsureContextForCurrentThread();

    // If there is no script context created for this thread yet, create it now
    if (threadData->scriptContextBG == nullptr)
    {
        threadData->scriptContextBG = Js::ScriptContext::New(threadContext);
        threadData->scriptContextBG->Initialize();
        threadData->canDecommit = true;
    }
    
    // Parse the workitem's data
    BGParseWorkItem* workItem = (BGParseWorkItem*)job;
    workItem->ParseUTF8Core(threadData->scriptContextBG);

    return true;
#else
    Assert(!"BGParseManager does not work without ThreadContext");
    return false;
#endif
}

// Callback before the provided job will be processed
// Note: runs on any thread
void BGParseManager::JobProcessing(JsUtil::Job* job)
{
    Assert(job->Manager() == this);
    Assert(Processor()->GetCriticalSection()->IsLocked());

    this->workitemsProcessing.LinkToEnd((BGParseWorkItem*)job);
}

// Callback after the provided job was processed. succeeded is true if the job
// was executed as well.
// Note: runs on any thread
void BGParseManager::JobProcessed(JsUtil::Job *const job, const bool succeeded)
{
    Assert(job->Manager() == this);
    Assert(Processor()->GetCriticalSection()->IsLocked());

    BGParseWorkItem* workItem = (BGParseWorkItem*)job;
    if (succeeded)
    {
        Assert(this->workitemsProcessing.Contains(workItem));
        this->workitemsProcessing.Unlink(workItem);
    }

    this->workitemsProcessed.LinkToEnd(workItem);
    workItem->JobProcessed();
}

// Define needed for jobs.inl
// Note: Runs on any thread
BGParseWorkItem* BGParseManager::GetJob(BGParseWorkItem* workitem)
{
    Assert(!"BGParseManager::WasAddedToJobProcessor");
    return nullptr;
}

// Define needed for jobs.inl
// Note: Runs on any thread
bool BGParseManager::WasAddedToJobProcessor(JsUtil::Job *const job) const
{
    Assert(!"BGParseManager::WasAddedToJobProcessor");
    return true;
}


// Note: runs on any thread
BGParseWorkItem::BGParseWorkItem(
    BGParseManager* manager,
    const byte* pszScript,
    size_t cbScript,
    char16 *fullPath
    )
    : JsUtil::Job(manager),
    script(pszScript),
    cb(cbScript),
    path(nullptr),
    parseHR(S_OK),
    parseSourceLength(0),
    bufferReturn(nullptr),
    bufferReturnBytes(0),
    complete(nullptr)
{
    this->cookie = BGParseManager::GetNextCookie();

    Assert(fullPath != nullptr);
    this->path = SysAllocString(fullPath);
}

BGParseWorkItem::~BGParseWorkItem()
{
    SysFreeString(this->path);
    if (this->complete != nullptr)
    {
        HeapDelete(this->complete);
    }
}

void BGParseWorkItem::TransferCSE(CompileScriptException* pse)
{
    this->cse.CopyInto(pse);
}

// This function parses the input data cached in BGParseWorkItem and caches the
// seralized bytecode
// Note: runs on BackgroundJobProcessor thread
// Note: All exceptions are caught by BackgroundJobProcessor
void BGParseWorkItem::ParseUTF8Core(Js::ScriptContext* scriptContext)
{
    if (PHASE_TRACE1(Js::BgParsePhase))
    {
        Js::Tick now = Js::Tick::Now();
        Output::Print(
            _u("[BgParse: Parse -- cookie: %04d on thread 0x%X at %.2f ms]\n"),
            GetCookie(),
            ::GetCurrentThreadId(),
            now.ToMilliseconds()
        );
    }

    Js::AutoDynamicCodeReference dynamicFunctionReference(scriptContext);
    SourceContextInfo* sourceContextInfo = scriptContext->GetSourceContextInfo(this->cookie, nullptr);
    if (sourceContextInfo == nullptr)
    {
        sourceContextInfo = scriptContext->CreateSourceContextInfo(this->cookie, this->path, wcslen(this->path), nullptr);
    }

    SRCINFO si = {
        /* sourceContextInfo   */ sourceContextInfo,
        /* dlnHost             */ 0,
        /* ulColumnHost        */ 0,
        /* lnMinHost           */ 0,
        /* ichMinHost          */ 0,
        /* ichLimHost          */ (ULONG)0,
        /* ulCharOffset        */ 0,
        /* mod                 */ 0,
        /* grfsi               */ 0
    };

    // Currently always called from a try-catch
    ENTER_PINNED_SCOPE(Js::Utf8SourceInfo, sourceInfo);
    sourceInfo = Js::Utf8SourceInfo::NewWithNoCopy(scriptContext, (LPUTF8)this->script, (int32)this->cb, static_cast<int32>(this->cb), &si, false);
    LEAVE_PINNED_SCOPE();

    // what's a better name for "fIsOriginalUtf8Source?" what if i want to say "isutf8source" but not "isoriginal"?
    charcount_t cchLength = 0;
    uint sourceIndex = 0;
    Js::ParseableFunctionInfo * func = nullptr;
    this->parseHR = scriptContext->CompileUTF8Core(sourceInfo, &si, true, this->script, this->cb, fscrGlobalCode, &this->cse, cchLength, this->parseSourceLength, sourceIndex, &func);
    if (this->parseHR == S_OK)
    {
        BEGIN_TEMP_ALLOCATOR(tempAllocator, scriptContext, _u("BGParseWorkItem"));
        Js::FunctionBody *functionBody = func->GetFunctionBody();
        this->parseHR = Js::ByteCodeSerializer::SerializeToBuffer(
            scriptContext,
            tempAllocator,
            (DWORD)this->cb,
            this->script,
            functionBody,
            functionBody->GetHostSrcInfo(),
            true,
            &this->bufferReturn,
            &this->bufferReturnBytes,
            0
        );
        END_TEMP_ALLOCATOR(tempAllocator, scriptContext);
        Assert(this->parseHR == S_OK);
    }
    else
    {
        Assert(this->cse.ei.bstrSource != nullptr);
        Assert(func == nullptr);
    }
}

void BGParseWorkItem::CreateCompletionEvent()
{
    Assert(this->complete == nullptr);
    this->complete = HeapNew(Event, false);
}

// Upon notification of job processed, set the event for those waiting for this job to complete
void BGParseWorkItem::JobProcessed()
{
    if (this->complete != nullptr)
    {
        this->complete->Set();
    }
}

// Wait for this job to finish processing
void BGParseWorkItem::WaitForCompletion()
{
    if (this->complete != nullptr)
    {
        if (PHASE_TRACE1(Js::BgParsePhase))
        {
            Js::Tick now = Js::Tick::Now();
            Output::Print(
                _u("[BgParse: Wait -- cookie: %04d on thread 0x%X at %.2f ms]\n"),
                GetCookie(),
                ::GetCurrentThreadId(),
                now.ToMilliseconds()
            );
        }

        this->complete->Wait();
    }
}