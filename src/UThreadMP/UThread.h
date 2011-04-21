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

#include <crtdbg.h>
#include <stdio.h>
#include <Windows.h>

#define AVAILABLE_PROCESSORS (~0)

typedef VOID * UT_ARGUMENT;
typedef VOID (*UT_FUNCTION)(UT_ARGUMENT);

//
// Initializes the UThread library. Should be called only once per process.
//

VOID
UtInit (
    );

//
// Initializes the scheduler. The operating system thread that calls the function becomes 
// a primary thread and creates the remaining ones (as many as NumberOfSystemThreads - 1).
//

VOID
UtRun (
    __in ULONG NumberOfSystemThreads
    );

//
// Creates a user thread to run the specified function. The thread is placed at the end of the ready queue.
//

HANDLE
UtCreate (
    __in UT_FUNCTION Function,
    __in UT_ARGUMENT Argument
    );

//
// Terminates the execution of the currently running thread. All associated resources will be released 
// after the context switch to the next ready thread.
//

__declspec(noreturn)
VOID
UtExit (
    );

//
// Relinquishes the processor to the first user thread in the ready queue. If there are no ready threads, 
// the function returns immediately.
//

VOID
UtYield (
    );

//
// Returns a HANDLE to the executing user thread.
//

HANDLE
UtSelf (
    );

//
// Block the current user thread.
//

VOID
UtPark (
    );

//
// Unblocks the specified user thread.
//

VOID
UtUnpark (
    __in HANDLE ThreadHandle
    );
