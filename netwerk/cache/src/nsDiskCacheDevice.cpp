/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*-
 *
 * The contents of this file are subject to the Mozilla Public
 * License Version 1.1 (the "License"); you may not use this file
 * except in compliance with the License. You may obtain a copy of
 * the License at http://www.mozilla.org/MPL/
 * 
 * Software distributed under the License is distributed on an "AS
 * IS" basis, WITHOUT WARRANTY OF ANY KIND, either express or
 * implied. See the License for the specific language governing
 * rights and limitations under the License.
 * 
 * The Original Code is nsMemoryCacheDevice.cpp, released February 22, 2001.
 * 
 * The Initial Developer of the Original Code is Netscape Communications
 * Corporation.  Portions created by Netscape are
 * Copyright (C) 2001 Netscape Communications Corporation.  All
 * Rights Reserved.
 * 
 * Contributor(s): 
 *    Gordon Sheridan <gordon@netscape.com>
 *    Patrick C. Beard <beard@netscape.com>
 */

#include "nsDiskCacheDevice.h"
#include "nsICacheService.h"
#include "nsIFileTransportService.h"
#include "nsDirectoryServiceDefs.h"
#include "nsISupportsArray.h"

static NS_DEFINE_CID(kFileTransportServiceCID, NS_FILETRANSPORTSERVICE_CID);

// XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX

// I don't want to have to use preferences to obtain this, rather, I would
// rather be initialized with this information. To get started, the same
// mechanism

#include "nsIPref.h"

static const char CACHE_DIR_PREF[] = { "browser.cache.directory" };

static int PR_CALLBACK cacheDirectoryChanged(const char *pref, void *closure)
{
	nsresult rv;
	NS_WITH_SERVICE(nsIPref, prefs, NS_PREF_CONTRACTID, &rv);
	if (NS_FAILED(rv))
		return rv;
    
	nsCOMPtr<nsILocalFile> cacheDirectory;
    rv = prefs->GetFileXPref(CACHE_DIR_PREF, getter_AddRefs( cacheDirectory ));
	if (NS_FAILED(rv))
		return rv;

    nsDiskCacheDevice* device = NS_STATIC_CAST(nsDiskCacheDevice*, closure);
    device->setCacheDirectory(cacheDirectory);
    
    return NS_OK;
}

static nsresult InstallPrefListeners(nsDiskCacheDevice* device)
{
	nsresult rv;
	NS_WITH_SERVICE(nsIPref, prefs, NS_PREF_CONTRACTID, &rv);
	if (NS_FAILED(rv))
		return rv;
	rv = prefs->RegisterCallback(CACHE_DIR_PREF, cacheDirectoryChanged, device); 
	if (NS_FAILED(rv))
		return rv;

	nsCOMPtr<nsILocalFile> cacheDirectory;
    rv = prefs->GetFileXPref(CACHE_DIR_PREF, getter_AddRefs( cacheDirectory ));
    if (NS_FAILED(rv)) {
        nsCOMPtr<nsIFile> currentProcessDir;
        rv = NS_GetSpecialDirectory(NS_XPCOM_CURRENT_PROCESS_DIR, 
                                    getter_AddRefs(currentProcessDir));
    	if (NS_FAILED(rv))
    		return rv;

        // XXX use current process directory during development only.
        cacheDirectory = do_QueryInterface(currentProcessDir, &rv);
    	if (NS_FAILED(rv))
    		return rv;
        rv = prefs->SetFileXPref(CACHE_DIR_PREF, cacheDirectory);
    	if (NS_FAILED(rv))
    		return rv;
    }
    
    // cause the preference to be set up initially.
    device->setCacheDirectory(cacheDirectory);
    
    return NS_OK;
}

static nsresult RemovePrefListeners(nsDiskCacheDevice* device)
{
	nsresult rv;
	NS_WITH_SERVICE(nsIPref, prefs, NS_PREF_CONTRACTID, &rv);
	if ( NS_FAILED (rv ) )
		return rv;

	rv = prefs->UnregisterCallback(CACHE_DIR_PREF, cacheDirectoryChanged, device);
	if ( NS_FAILED( rv ) )
		return rv;

    return NS_OK;
}

// XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX

nsDiskCacheDevice::nsDiskCacheDevice()
    :   mTotalCachedDataSize(LL_ZERO)
{
}

nsDiskCacheDevice::~nsDiskCacheDevice()
{
    RemovePrefListeners(this);
}

nsresult
nsDiskCacheDevice::Init()
{
    nsresult rv = InstallPrefListeners(this);
    if (NS_FAILED(rv)) return rv;
    
    rv = mInactiveEntries.Init();
    if (NS_FAILED(rv)) return rv;
    
    return  NS_OK;
}

nsresult
nsDiskCacheDevice::Create(nsCacheDevice **result)
{
    nsDiskCacheDevice * device = new nsDiskCacheDevice();
    if (!device)  return NS_ERROR_OUT_OF_MEMORY;

    nsresult rv = device->Init();
    if (NS_FAILED(rv)) {
        delete device;
        device = nsnull;
    }
    *result = device;
    return rv;
}


const char *
nsDiskCacheDevice::GetDeviceID()
{
    return "disk";
}


nsCacheEntry *
nsDiskCacheDevice::FindEntry(nsCString * key)
{
    nsCacheEntry * entry = mInactiveEntries.GetEntry(key);
    if (!entry)  return nsnull;

    //** need nsCacheService::CreateEntry();
    entry->MarkActive(); // so we don't evict it
    //** find eviction element and move it to the tail of the queue
    
    return entry;;
}


nsresult
nsDiskCacheDevice::DeactivateEntry(nsCacheEntry * entry)
{
    nsCString * key = entry->Key();

    nsCacheEntry * ourEntry = mInactiveEntries.GetEntry(key);
    NS_ASSERTION(ourEntry, "DeactivateEntry called for an entry we don't have!");
    if (!ourEntry)
        return NS_ERROR_INVALID_POINTER;

    //** update disk entry from nsCacheEntry
    //** MarkInactive(); // to make it evictable again
    return NS_ERROR_NOT_IMPLEMENTED;
}


nsresult
nsDiskCacheDevice::BindEntry(nsCacheEntry * entry)
{
    nsresult  rv = mInactiveEntries.AddEntry(entry);
    if (NS_FAILED(rv))
        return rv;

    //** add size of entry to memory totals
    return NS_OK;
}

nsresult
nsDiskCacheDevice::DoomEntry(nsCacheEntry * entry)
{
    return NS_ERROR_NOT_IMPLEMENTED;
}

class PlaceHolder : public nsISupports {
public:
    NS_IMETHOD QueryInterface(REFNSIID aIID, void** aInstancePtr);
    NS_IMETHOD_(nsrefcnt) AddRef(void) { return 1; }
    NS_IMETHOD_(nsrefcnt) Release(void) { return 1; }
};
NS_IMPL_QUERY_INTERFACE0(PlaceHolder)
static PlaceHolder gPlaceHolder;

static nsresult
getTransportArray(nsCacheEntry * entry, nsCOMPtr<nsISupportsArray>& array)
{
    nsCOMPtr<nsISupports> data;
    nsresult rv = entry->GetData(getter_AddRefs(data));
    if (NS_SUCCEEDED(rv) && data) {
        array = do_QueryInterface(data, &rv);
    } else {
        rv = NS_NewISupportsArray(getter_AddRefs(array));
        if (NS_SUCCEEDED(rv) && array) {
            entry->SetData(array.get());
            array->AppendElement(&gPlaceHolder);
            array->AppendElement(&gPlaceHolder);
            array->AppendElement(&gPlaceHolder);
        }
    }
    return rv;
}

nsresult
nsDiskCacheDevice::GetTransportForEntry(nsCacheEntry * entry,
                                        nsCacheAccessMode mode, 
                                        nsITransport ** result)
{
    NS_ENSURE_ARG_POINTER(entry);
    NS_ENSURE_ARG_POINTER(result);

    // Could keep an array of the 3 distinct access modes cached.
    nsCOMPtr<nsISupportsArray> array;
    nsresult rv = getTransportArray(entry, array);
    if (NS_FAILED(rv))
        return rv;

    PRUint32 transportIndex = (mode - 1);
    rv = array->QueryElementAt(transportIndex, NS_GET_IID(nsITransport), (void**)result);
    if (NS_FAILED(rv)) {
        NS_WITH_SERVICE(nsIFileTransportService, service, kFileTransportServiceCID, &rv);
        if (NS_SUCCEEDED(rv)) {
            // XXX generate the name of the cache entry from the hash code of its key,
            // modulo the number of files we're willing to keep cached.
            nsCOMPtr<nsIFile> entryFile;
            rv = getFileForEntry(entry, getter_AddRefs(entryFile));
            if (NS_SUCCEEDED(rv)) {
                PRInt32 ioFlags;
                switch (mode) {
                case nsICache::ACCESS_READ:
                    ioFlags = PR_RDONLY;
                    break;
                case nsICache::ACCESS_WRITE:
                    ioFlags = PR_WRONLY | PR_CREATE_FILE | PR_TRUNCATE;
                    break;
                case nsICache::ACCESS_READ_WRITE:
                    ioFlags = PR_RDWR | PR_CREATE_FILE;
                    break;
                }
                nsCOMPtr<nsITransport> transport;
                rv = service->CreateTransport(entryFile, ioFlags, PR_IRUSR | PR_IWUSR,
                                              getter_AddRefs(transport));
                if (NS_SUCCEEDED(rv)) {
                    array->SetElementAt(transportIndex, transport.get());
                    NS_ADDREF(*result = transport);
                }
            }
        }
    }
    
    return rv;
}

/**
 * This routine will get called every time an open descriptor.
 */
nsresult
nsDiskCacheDevice::OnDataSizeChange(nsCacheEntry * entry, PRInt32 deltaSize)
{
    PRInt64 delta = LL_INIT(deltaSize < 0 ? -1 : 0, deltaSize);
    LL_ADD(mTotalCachedDataSize, mTotalCachedDataSize, delta);
    return NS_OK;
}

void nsDiskCacheDevice::setCacheDirectory(nsILocalFile* cacheDirectory)
{
    mCacheDirectory = cacheDirectory;
}

nsresult nsDiskCacheDevice::getFileForEntry(nsCacheEntry * entry, nsIFile ** result)
{
    if (mCacheDirectory) {
        nsCOMPtr<nsIFile> entryFile;
        nsresult rv = mCacheDirectory->Clone(getter_AddRefs(entryFile));
    	if (NS_FAILED(rv))
    		return rv;
        // generate the hash code for this entry, and use that as a file name.
        PLDHashNumber hash = ::PL_DHashStringKey(NULL, entry->Key()->get());
        char name[32];
        ::sprintf(name, "%08X", hash);
        entryFile->Append(name);
        NS_ADDREF(*result = entryFile);
        return NS_OK;
    }
    return NS_ERROR_NOT_AVAILABLE;
}

//** need methods for enumerating entries
