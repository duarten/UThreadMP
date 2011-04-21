// Copyright 2010 Duarte Nunes
// 
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
// 
// http://www.apache.org/licenses/LICENSE-2.0
// 
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//

#include "SyncObjects.h"
#include "List.h"

//
// Forward declaration of helper functions.
//

//
// Tries to acquire the specified mutex.
//

BOOL
TryAcquireMutex (
    PWAIT_REQUEST WaitRequest
    );

//
// Tries to acquire the specified semaphore.
//

BOOL
TryAcquireSemaphore (
    PWAIT_REQUEST WaitRequest
    );

//
// Definition of the sync object's public interface.
//

//
// Blocks the current thread until the specified request is satisfied.
//

VOID
UtWaitForSyncObject (
    PWAIT_REQUEST WaitRequest
    )
{
    BOOL Done;
    PSYNC_OBJECT SyncObject;
    WAIT_BLOCK WaitBlock;    

    SyncObject = WaitRequest->SyncObject;
    
    EnterCriticalSection(&SyncObject->Lock);
    {
        //
        // If the wait request cannot be immediatly satisfied, insert a wait block in the object's wait list.
        //

        if ((Done = SyncObject->TrySatisfyWait(WaitRequest)) == FALSE) {
            WaitBlock.ThreadHandle = UtSelf();
            WaitBlock.WaitRequest = WaitRequest;
            InsertHeadList(&SyncObject->WaitListHead, &WaitBlock.WaitListEntry);
        }        
    }
    LeaveCriticalSection(&SyncObject->Lock);

    if (!Done) {
        UtPark();
    }
}

//
// Releases the resources associated with the specified sync object.
//

VOID
UtCloseSyncObject (
    PSYNC_OBJECT Object
    )
{
    DeleteCriticalSection(&Object->Lock);
}

//
// Initializes a mutex instance. If Owned is TRUE, then the current thread becomes the owner.
//

VOID
UtInitializeMutex (
    PUTHREAD_MUTEX Mutex,
    BOOL Owned
    )
{
    InitializeCriticalSection(&Mutex->Header.Lock);
    InitializeListHead(&Mutex->Header.WaitListHead);
    Mutex->Header.TrySatisfyWait = TryAcquireMutex;
    Mutex->Header.SignalState = Owned ? 1 : 0;
    Mutex->Owner = Owned ? UtSelf() : NULL;
}

//
// Tries to acquire the specified mutex.
//

BOOL
TryAcquireMutex (
    PWAIT_REQUEST WaitRequest
    )
{
    HANDLE CurrentThread;
    PUTHREAD_MUTEX Mutex;
    
    CurrentThread = UtSelf();
    Mutex = (PUTHREAD_MUTEX) WaitRequest->SyncObject;

    if (Mutex->Header.SignalState == 0) {
        
        //
        // Mutex is free. Acquire the mutex by setting its owner to the requesting thread.
        //

        Mutex->Owner = CurrentThread;
        Mutex->Header.SignalState = 1;
        return TRUE;
    }

    if (CurrentThread == Mutex->Owner) {
        
        //
        // Recursive aquisition. Increment the recursion counter.
        //

        Mutex->Header.SignalState += 1;
        return TRUE;
    }

    return FALSE;
}

//
// Releases the specified mutex, eventually unblocking a waiting thread to which the
// ownership of the mutex is transfered.
//

VOID
UtReleaseMutex (
    PUTHREAD_MUTEX Mutex
    )
{
    BOOL MustUnpark;
    PWAIT_BLOCK WaitBlock;

    _ASSERTE(UtSelf() == Mutex->Owner);

    MustUnpark = FALSE;

    EnterCriticalSection(&Mutex->Header.Lock);
    {
        if ((Mutex->Header.SignalState -= 1) == 0) {	    
            if (!IsListEmpty(&Mutex->Header.WaitListHead)) {
                WaitBlock = CONTAINING_RECORD(RemoveHeadList(&Mutex->Header.WaitListHead), WAIT_BLOCK, WaitListEntry);
                Mutex->Owner = WaitBlock->ThreadHandle;
                Mutex->Header.SignalState = 1;
                MustUnpark = TRUE;
            } else {
                Mutex->Owner = NULL;
            }        
        }
    }
    LeaveCriticalSection(&Mutex->Header.Lock);

    if (MustUnpark) {
        UtUnpark(WaitBlock->ThreadHandle);  
    }
}

//
// Initializes a semaphore instance. Permits is the starting number of available permits and 
// Limit is the maximum number of permits allowed for the specified semaphore instance.
//

VOID
UtInitializeSemaphore (
    PUTHREAD_SEMAPHORE Semaphore,
    ULONG Permits,
    ULONG Limit
    )
{
    InitializeCriticalSection(&Semaphore->Header.Lock);
    InitializeListHead(&Semaphore->Header.WaitListHead);
    Semaphore->Header.TrySatisfyWait = TryAcquireSemaphore;
    Semaphore->Header.SignalState = Permits;
    Semaphore->Limit = Limit;
}

//
// Adds the specified number of permits to the semaphore, eventually unblocking waiting threads.
//

VOID
UtReleaseSemaphore (
    PUTHREAD_SEMAPHORE Semaphore,
    ULONG Permits
    )
{
    PLIST_ENTRY ListHead;
    PWAIT_BLOCK WaitBlock;
    PLIST_ENTRY WaitEntry;
    LIST_ENTRY WakeListHead;	

    EnterCriticalSection(&Semaphore->Header.Lock);
    {
        if ((Semaphore->Header.SignalState += Permits) > Semaphore->Limit) {
            Semaphore->Header.SignalState = Semaphore->Limit;
        }

        ListHead = &Semaphore->Header.WaitListHead;
        InitializeListHead(&WakeListHead);

        //
        // Create a stack of all wait blocks whose associated wait request has been satisfied.
        //
    
        while (Semaphore->Header.SignalState > 0 && (WaitEntry = ListHead->Flink) != ListHead) {
            WaitBlock = CONTAINING_RECORD(WaitEntry, WAIT_BLOCK, WaitListEntry);

            if (!TryAcquireSemaphore(WaitBlock->WaitRequest)) {
            
                //
                // We stop at the first request that cannot be satisfied to ensure FIFO ordering.
                //

                break;            
            }

            InsertHeadList(&WakeListHead, RemoveHeadList(ListHead));        
        }
    }
    LeaveCriticalSection(&Semaphore->Header.Lock);

    ListHead = &WakeListHead;

    //
    // Unpark all threads whose wait request has been satisfied.
    //

    while (!IsListEmpty(ListHead)) {
        WaitBlock = CONTAINING_RECORD(RemoveHeadList(ListHead), WAIT_BLOCK, WaitListEntry);
        UtUnpark(WaitBlock->ThreadHandle);  
    }
}

//
// Tries to acquire the specified semaphore.
//

BOOL
TryAcquireSemaphore (
    PWAIT_REQUEST WaitRequest
    )
{
    PUTHREAD_SEMAPHORE Semaphore = (PUTHREAD_SEMAPHORE) WaitRequest->SyncObject;	

    if (Semaphore->Header.SignalState >= ((PSEMAPHORE_WAIT_REQUEST)WaitRequest)->Permits) {
        Semaphore->Header.SignalState -= ((PSEMAPHORE_WAIT_REQUEST)WaitRequest)->Permits;
        return TRUE;
    }

    return FALSE;
}
