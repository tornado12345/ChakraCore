//-------------------------------------------------------------------------------------------------------
// Copyright (C) Microsoft. All rights reserved.
// Licensed under the MIT license. See LICENSE.txt file in the project root for full license information.
//-------------------------------------------------------------------------------------------------------
#pragma once
class SourceContextInfo;

#if ENABLE_PROFILE_INFO
namespace Js
{
    enum ExecutionFlags : BYTE
    {
        ExecutionFlags_NotExecuted = 0x00,
        ExecutionFlags_Executed = 0x01,
        ExecutionFlags_HasNoInfo = 0x02
    };
    //
    // For every source file, an instance of SourceDynamicProfileManager is used to save/load data.
    // It uses the WININET cache to save/load profile data.
    // For testing scenarios enabled using DYNAMIC_PROFILE_STORAGE macro, this can persist the profile info into a file as well.
    class SourceDynamicProfileManager
    {
    public:
        SourceDynamicProfileManager(Recycler* allocator) : isNonCachableScript(false), cachedStartupFunctions(nullptr), recycler(allocator),
#ifdef DYNAMIC_PROFILE_STORAGE
            dynamicProfileInfoMapSaving(&HeapAllocator::Instance),
#endif
            dynamicProfileInfoMap(allocator), startupFunctions(nullptr), profileDataCache(nullptr) 
        {
        }

        ExecutionFlags IsFunctionExecuted(Js::LocalFunctionId functionId);
        DynamicProfileInfo * GetDynamicProfileInfo(FunctionBody * functionBody);
        Recycler* GetRecycler() { return recycler; }
        void UpdateDynamicProfileInfo(LocalFunctionId functionId, DynamicProfileInfo * dynamicProfileInfo);
        void RemoveDynamicProfileInfo(LocalFunctionId functionId);
        void MarkAsExecuted(LocalFunctionId functionId);
        static SourceDynamicProfileManager * LoadFromDynamicProfileStorage(SourceContextInfo* info, ScriptContext* scriptContext, IActiveScriptDataCache* profileDataCache);
        void EnsureStartupFunctions(uint numberOfFunctions);
        void Reuse();
        uint SaveToProfileCacheAndRelease(SourceContextInfo* info);
        bool IsProfileLoaded() { return cachedStartupFunctions != nullptr; }
        bool IsProfileLoadedFromWinInet() { return profileDataCache != nullptr; }
        bool LoadFromProfileCache(IActiveScriptDataCache* profileDataCache, LPCWSTR url);
        IActiveScriptDataCache* GetProfileCache() { return profileDataCache; }
        uint GetStartupFunctionsLength() { return (this->startupFunctions ? this->startupFunctions->Length() : 0); }
#ifdef DYNAMIC_PROFILE_STORAGE
        void ClearSavingData();
#endif

    private:
        friend class DynamicProfileInfo;
        FieldNoBarrier(Recycler*) recycler;

#ifdef DYNAMIC_PROFILE_STORAGE
        // while Finalizing Javascript library we can't allocate memory from recycler, 
        // dynamicProfileInfoMapSaving is heap allocated and used for serializing dynamic profile cache
        typedef JsUtil::BaseDictionary<LocalFunctionId, DynamicProfileInfo *, HeapAllocator> DynamicProfileInfoMapSavingType;
        FieldNoBarrier(DynamicProfileInfoMapSavingType) dynamicProfileInfoMapSaving;
        
        void SaveDynamicProfileInfo(LocalFunctionId functionId, DynamicProfileInfo * dynamicProfileInfo);
        void SaveToDynamicProfileStorage(char16 const * url);
        void AddSavingItem(LocalFunctionId functionId, DynamicProfileInfo *info);
        template <typename T>
        static SourceDynamicProfileManager * Deserialize(T * reader, Recycler* allocator);
        template <typename T>
        bool Serialize(T * writer);
#endif
        uint SaveToProfileCache();
        bool ShouldSaveToProfileCache(SourceContextInfo* info) const;

    //------ Private data members -------- /
    private:
        Field(bool) isNonCachableScript;                    // Indicates if this script can be cached in WININET
        Field(IActiveScriptDataCache*) profileDataCache;    // WININET based cache to store profile info
        Field(BVFixed*) startupFunctions;                   // Bit vector representing functions that are executed at startup
        Field(BVFixed const *) cachedStartupFunctions;      // Bit vector representing functions executed at startup that are loaded from a persistent or in-memory cache
                                                            // It's not modified but used as an input for deferred parsing/bytecodegen
        typedef JsUtil::BaseDictionary<LocalFunctionId, DynamicProfileInfo *, Recycler, PowerOf2SizePolicy>  DynamicProfileInfoMapType;
        Field(DynamicProfileInfoMapType) dynamicProfileInfoMap;

        static const uint MAX_FUNCTION_COUNT = 10000;  // Consider data corrupt if there are more functions than this

#ifdef ENABLE_WININET_PROFILE_DATA_CACHE
        //
        // Simple read-only wrapper around IStream - templatized and returns boolean result to indicate errors
        //
        class SimpleStreamReader
        {
        public:
            SimpleStreamReader(IStream* stream) : stream(stream) {}
            ~SimpleStreamReader()
            {
                this->Close();
            }

            template <typename T>
            bool Read(T * data)
            {
                ULONG bytesRead;
                HRESULT hr = stream->Read(data, sizeof(T), &bytesRead);
                // hr is S_FALSE if bytesRead < sizeOf(T)
                return hr == S_OK;
            }

            template <typename T>
            bool ReadArray(_Out_writes_(len) T * data, ULONG len)
            {
                ULONG bytesSize = sizeof(T) * len;
                ULONG bytesRead;
                HRESULT hr = stream->Read(data, bytesSize, &bytesRead);
                // hr is S_FALSE if bytesRead < bytesSize
                return hr == S_OK;
            }
        private:
            void Close()
            {
                Assert(stream);

                stream->Release();
                stream = NULL;
            }

            IStream* stream;
        };

        //
        // Simple write-only wrapper around IStream - templatized and returns boolean result to indicate errors
        //
        class SimpleStreamWriter
        {
        public:
            SimpleStreamWriter(IStream* stream) : stream(stream) {}
            ~SimpleStreamWriter()
            {
                this->Close();
            }

            template <typename T>
            bool Write(_In_ T const& data)
            {
                ULONG bytesWritten;
                HRESULT hr = stream->Write(&data, sizeof(T), &bytesWritten);
                // hr is S_FALSE if bytesRead < sizeOf(T)
                return hr == S_OK;
            }

            template <typename T>
            bool WriteArray(_In_reads_(len) T * data, _In_ ULONG len)
            {
                ULONG bytesSize = sizeof(T) * len;
                ULONG bytesWritten;
                HRESULT hr = stream->Write(data, bytesSize, &bytesWritten);
                // hr is S_FALSE if bytesRead < bytesSize
                return hr == S_OK;
            }
        private:
            void Close()
            {
                Assert(stream);

                stream->Release();
                stream = NULL;
            }

            IStream* stream;
        };
#endif  // ENABLE_WININET_PROFILE_DATA_CACHE
    };
};
#endif  // ENABLE_PROFILE_INFO
