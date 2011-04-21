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

#pragma once

#include <Windows.h>
#include "UThread.h"

typedef struct _WAIT_REQUEST *PWAIT_REQUEST;

typedef BOOL TrySatisfyWaitFunc(PWAIT_REQUEST);

//
// Wait block used to queue requests on synchronizers. Alongside the wait request it holds
// a handle to the waiting thread.
//

typedef struct _WAIT_BLOCK {
    LIST_ENTRY WaitListEntry;
    HANDLE ThreadHandle;
    PWAIT_REQUEST WaitRequest;
} WAIT_BLOCK, *PWAIT_BLOCK;

//
// The common members of a synchronizer, containing the lock that protects the wait list  
// and the signal state. The TrySatisfyWait function pointer is used to dynamically call 
// the function that consumes the synchronizer's signal state.
//

typedef struct _SYNC_OBJECT {
    CRITICAL_SECTION Lock;
    LIST_ENTRY WaitListHead;
    ULONG SignalState;
    TrySatisfyWaitFunc *TrySatisfyWait;
} SYNC_OBJECT, *PSYNC_OBJECT;

//
// A mutex, where SignalState is the number of recursive acquires by the Owner thread.
//

typedef struct _UTHREAD_MUTEX {
    SYNC_OBJECT Header;
    HANDLE Owner;
} UTHREAD_MUTEX, *PUTHREAD_MUTEX;

//
// A semaphore, where SignalState is the number of permits upper bounded by Limit.
//

typedef struct _UTHREAD_SEMAPHORE {
    SYNC_OBJECT Header;
    ULONG Limit;
} UTHREAD_SEMAPHORE, *PUTHREAD_SEMAPHORE;

//
// A structure that represents a wait for the associate object to become signalled.
//

typedef struct _WAIT_REQUEST {
    PSYNC_OBJECT SyncObject;
} WAIT_REQUEST, *PWAIT_REQUEST;

//
// A structure that represents a wait for the associate semaphore to have the specified 
// number of Permits available.
//

typedef struct _SEMAPHORE_WAIT_REQUEST {
    WAIT_REQUEST Header;
    ULONG Permits;
} SEMAPHORE_WAIT_REQUEST, *PSEMAPHORE_WAIT_REQUEST;

//
// Blocks the current thread until the specified request is satisfied.
//

VOID
UtWaitForSyncObject (
    PWAIT_REQUEST Request
    );

//
// Releases the resources associated with the specified sync object.
//

VOID
UtCloseSyncObject (
    PSYNC_OBJECT SyncObject
    );

//
// Initializes a mutex instance. If Owned is TRUE, then the current thread becomes the owner.
//

VOID
UtInitializeMutex (
    PUTHREAD_MUTEX Mutex,
    BOOL Owned
    );

//
// Releases the specified mutex, eventually unblocking a waiting thread to which the
// ownership of the mutex is transfered.
//

VOID
UtReleaseMutex (
    PUTHREAD_MUTEX Mutex
    );

//
// Initializes a semaphore instance. Permits is the starting number of available permits and 
// Limit is the maximum number of permits allowed for the specified semaphore instance.
//

VOID
UtInitializeSemaphore (
    PUTHREAD_SEMAPHORE Semaphore,
    ULONG Permits,
    ULONG Limit
    );

//
// Adds the specified number of permits to the semaphore, eventually unblocking waiting threads.
//

VOID
UtReleaseSemaphore (
    PUTHREAD_SEMAPHORE Semaphore,
    ULONG Permits
    );
