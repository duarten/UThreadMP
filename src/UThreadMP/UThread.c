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

#include "UThread.h"
#include "List.h"

//
// The data structure representing the layout of a thread's execution context when saved in the stack.
//

typedef struct _UTHREAD_CONTEXT {
    ULONG EDI;
    ULONG ESI;
    ULONG EBX;
    ULONG EBP;
    VOID (*RetAddr)();
} UTHREAD_CONTEXT, *PUTHREAD_CONTEXT;

//
// The descriptor of a user thread, containing an intrusive link through which the thread is linked in the ready queue, 
// the thread's starting function and argument, the memory block used as the thread's stack and a pointer to the stored 
// execution context. The Running flag is used to synchronize the switching out of a thread running on a virtual processor 
// with the switching in of the same thread on another virtual processor. The WaitPending flag is used to short circuit the
// blocking path of the associated thread, which can be released before effectively blocking.
//

#pragma pack(push, 1)
typedef struct _UTHREAD {   
    LIST_ENTRY Link;
    UT_FUNCTION Function;   
    UT_ARGUMENT Argument;   
    PUCHAR Stack;
    PUTHREAD_CONTEXT ThreadContext;  
    volatile ULONG WaitPending;
    UCHAR Running;
} UTHREAD, *PUTHREAD;
#pragma pack(pop)

//
// The data local to a virtual processor.
//

typedef struct _PRCB {
    PUTHREAD RunningThread;
    PUTHREAD IdleThread;
} PRCB, *PPRCB;

//
// The fixed stack size for a user thread.
//

#define STACK_SIZE (16 * 4096)

//
// The number of existing user threads.
//

static ULONG NumberOfThreads;

//
// The sentinel of the circular list linking the user threads that are schedulable. The next thread
// to run is retrieved from the head of the list.
//

static LIST_ENTRY ReadyQueue;

//
// The number of virtual processors, each affinitized to a physical processor.
//

static ULONG NumberOfVirtualProcessors;

//
// The number of idle threads running. When all virtual processors are running their respective idle thread, 
// the scheduler exits and the main thread resumes its execution.
//

static volatile ULONG IdleCount;

//
// The semaphore used to signal the idle threads of user threads becoming runnable or 
// of the scheduler's finalization.
//

static HANDLE IdleSemaphore;

//
// The scheduler lock, which serializes access to the ready queue and the idle thread count.
//

static CRITICAL_SECTION UThreadLock;

//
// The PRCB local to each virtual processor.
//

static __declspec(thread) PRCB CurrentPrcb;

//
// Forward declaration of helper functions.
//

//
// The trampoline function that a user thread begins by executing, through which the associated function is called.
//

VOID
InternalStart (
    );

//
// Performs a context switch from CurrentThread (switch out) to NextThread (switch in).
// __fastcall sets the calling convention such that CurrentThread is in ECX and NextThread in EDX.
//

VOID
__fastcall
ContextSwitch (
    __inout PUTHREAD CurrentThread,
    __inout PUTHREAD NextThread
    );

//
// Frees the resources associated with CurrentThread and switches to NextThread.
// __fastcall sets the calling convention such that CurrentThread is in ECX and NextThread in EDX.
//

static
VOID
__fastcall
InternalExit (
    __inout PUTHREAD Thread,
    __in PUTHREAD NextThread
    );

//
// The entry point of an idle thread.
//

static
ULONG
WINAPI
IdleThreadLoop (
    __in PVOID Argument
    );

//
// Returns and removes the first user thread in the ready queue. If the ready queue is empty, 
// the current virtual processor's idle thread is returned.
//

FORCEINLINE
PUTHREAD
FindNextThread (
    __in BOOL Locked
    )
{
    PUTHREAD Thread;

    if (Locked == FALSE) {
        EnterCriticalSection(&UThreadLock);
    }

    Thread = IsListEmpty(&ReadyQueue) 
            ? CurrentPrcb.IdleThread 
            : CONTAINING_RECORD(RemoveHeadList(&ReadyQueue), UTHREAD, Link);

    if (Locked == FALSE) {
        LeaveCriticalSection(&UThreadLock);
    }

    return Thread;
}

//
// Removes a thread from the ready queue and switches to it. If the function is called with
// the UThreadLock taken, the lock is released before the context switch.
//

FORCEINLINE
VOID
Schedule (
    __in BOOL Locked
    )
{
    PUTHREAD CurrentThread;
    PUTHREAD NextThread;

    CurrentThread = CurrentPrcb.RunningThread;
    NextThread = FindNextThread(Locked);
    
    if (Locked) {
        LeaveCriticalSection(&UThreadLock);
    }

    //
    // Ensure that we don't switch to the running thread.
    //

    if (NextThread != CurrentThread) {
        ContextSwitch(CurrentThread, NextThread);
    }
}

//
// Places the specified thread at the end of the ready queue, possibly notifying an idle thread.
// If this function is called, then there's at least one active virtual processor.
//

FORCEINLINE
VOID
ReadyThread (
    __in PUTHREAD Thread
    )
{
    BOOL ReleaseIdleThread;

    EnterCriticalSection(&UThreadLock);
    {
        InsertTailList(&ReadyQueue, &Thread->Link);
        if ((ReleaseIdleThread = (IdleCount > 0))) {
            IdleCount -= 1;
        }
    }
    LeaveCriticalSection(&UThreadLock);

    if (ReleaseIdleThread) {
        ReleaseSemaphore(IdleSemaphore, 1, NULL);
    }
}

//
// Definition of the public interface.
//

//
// Initializes the UThread library. Should be called only once per process.
//

VOID
UtInit (
    )
{
    InitializeCriticalSection(&UThreadLock);
    InitializeListHead(&ReadyQueue);    
}

//
// Initializes the scheduler. The operating system thread that calls the function, alongside the other system threads 
// that it creates (as many as NumberOfSystemThreads - 1), represents a virtual processor and embodies that processor's  
// idle thread.
//

VOID
UtRun (
    __in ULONG NumberOfSystemThreads
    )
{
    ULONG Index;
    ULONG ProcessorMask;
    SYSTEM_INFO SystemInfo;
    PHANDLE ThreadHandles;    

    _ASSERTE(NumberOfSystemThreads == AVAILABLE_PROCESSORS || 
             (NumberOfSystemThreads > 0 && NumberOfSystemThreads <= MAXIMUM_WAIT_OBJECTS));

    //
    // There can be only one scheduler instance running.
    //

    _ASSERTE(CurrentPrcb.RunningThread == NULL);

    //
    // Create the remaining virtual processors. Each virtual processor is represented by a system thread, affinitized 
    // to a particular physical processor. The thread also embodies the idle thread of that virtual processor.
    //

    if (NumberOfSystemThreads == AVAILABLE_PROCESSORS) {
        GetSystemInfo(&SystemInfo);
        NumberOfSystemThreads = SystemInfo.dwNumberOfProcessors;
    }

    NumberOfVirtualProcessors = NumberOfSystemThreads;
    ProcessorMask = NumberOfSystemThreads - 1;
    IdleSemaphore = CreateSemaphore(NULL, 0, NumberOfSystemThreads - 1, NULL);

    ThreadHandles = (HANDLE *) malloc(sizeof(HANDLE) * (NumberOfSystemThreads - 1));
    for (Index = 0; Index < NumberOfSystemThreads - 1; ++Index) {
        ThreadHandles[Index] = CreateThread(NULL, 0, IdleThreadLoop, NULL, 0, NULL);
        SetThreadIdealProcessor(ThreadHandles[Index], Index & ProcessorMask);
    }

    SetThreadIdealProcessor(GetCurrentThread(), Index & ProcessorMask);

    //
    // Start the current virtual processor.
    //

    IdleThreadLoop(NULL);

    //
    // All virtual processors became idle. We must exit the scheduler and reset its state to allow another call to
    // UtRun(). We wait for all the other virtual processors to exit so we can safely close the semaphore handle.
    //

    _ASSERTE(IdleCount == NumberOfVirtualProcessors);

    WaitForMultipleObjects(NumberOfSystemThreads - 1, ThreadHandles, TRUE, INFINITE);
    for (Index = 0; Index < NumberOfSystemThreads - 1; ++Index) {
        CloseHandle(ThreadHandles[Index]);
    }
    
    free(ThreadHandles);
    CloseHandle(IdleSemaphore);
    IdleCount = 0;
    CurrentPrcb.RunningThread = NULL;
}

//
// Creates a user thread to run the specified function. The thread is placed at the end of the ready queue.
//

HANDLE
UtCreate (
    __in UT_FUNCTION Function,
    __in UT_ARGUMENT Argument
    )
{
    PUTHREAD Thread;

    //
    // Dynamically allocate an instance of UTHREAD and the associated stack.
    //

    Thread = (PUTHREAD) malloc(sizeof *Thread);
    Thread->Stack = (PUCHAR) malloc(STACK_SIZE);
    _ASSERTE(Thread != NULL && Thread->Stack != NULL);

    //
    // Zero the stack for emotional confort.
    //
    
    RtlZeroMemory(Thread->Stack, STACK_SIZE);

    Thread->Function = Function;
    Thread->Argument = Argument;

    //
    // Map an UTHREAD_CONTEXT instance on the thread's stack.
    // We'll use it to save the initial context of the thread.
    //
    // +------------+
    // | 0x00000000 |    <- Highest word of a thread's stack space
    // +============+       (needs to be set to 0 for Visual Studio to
    // |  RetAddr   | \     correctly present a thread's call stack).
    // +------------+  |
    // |    EBP     |  |
    // +------------+  |
    // |    EBX     |   >   Thread->ThreadContext mapped on the stack.
    // +------------+  |
    // |    ESI     |  |
    // +------------+  |
    // |    EDI     | /  <- The stack pointer will be set to this address
    // +============+       at the next context switch to this thread.
    // |            | \
    // +------------+  |
    // |     :      |  |
    //       :          >   Remaining stack space.
    // |     :      |  |
    // +------------+  |
    // |            | /  <- Lowest word of a thread's stack space
    // +------------+       (Thread->Stack always points to this location).
    //

    Thread->ThreadContext = (PUTHREAD_CONTEXT) (Thread->Stack + STACK_SIZE - sizeof(ULONG) - sizeof *Thread->ThreadContext);

    //
    // Set the thread's initial context by initializing the values of EDI, EBX, ESI and EBP (must be
    // zero for Visual Studio to correctly present a thread's call stack) and by hooking the return
    // address. Upon the first context switch to this thread, after popping the dummy values of the 
    // "saved" registers, a ret instruction will place InternalStart's address on the processor's IP.
    //

    Thread->ThreadContext->EDI = 0x33333333;
    Thread->ThreadContext->ESI = 0x22222222;
    Thread->ThreadContext->EBX = 0x11111111;
    Thread->ThreadContext->EBP = 0x00000000;											  
    Thread->ThreadContext->RetAddr = InternalStart;

    //
    // Ready the thread.
    //
    
    Thread->Running = 0;
    Thread->WaitPending = 1;
    InterlockedIncrement(&NumberOfThreads);
    ReadyThread(Thread);
    
    return (HANDLE) Thread;
}

//
// Terminates the execution of the currently running thread. All associated resources will be released 
// after the context switch to the next ready thread.
//

__declspec(noreturn)
VOID
UtExit (
    )
{
    InterlockedDecrement(&NumberOfThreads);
    InternalExit(CurrentPrcb.RunningThread, FindNextThread(FALSE));
    _ASSERTE(!"supposed to be here!");
}

//
// Relinquishes the processor to the first user thread in the ready queue. If there are no ready threads, 
// the function returns immediately.
//

VOID
UtYield (
    ) 
{
    EnterCriticalSection(&UThreadLock);
    InsertTailList(&ReadyQueue, &CurrentPrcb.RunningThread->Link);
    Schedule(TRUE);
}

//
// Returns a HANDLE to the executing user thread.
//

HANDLE
UtSelf (
    )
{
    return (HANDLE) CurrentPrcb.RunningThread;
}

//
// Blocks the current user thread.
//

VOID
UtPark (
    )
{
    PUTHREAD CurrentThread = CurrentPrcb.RunningThread;
    
    //
    // Try to clear the WaitPending flag. 
    //    
    
    if (CurrentThread->WaitPending == 1 && InterlockedExchange(&CurrentThread->WaitPending, 0) == 1) {
        
        //
        // The unblocking thread will place the current thread in the ready queue. Note that after we
        // cleared the WaitPending flag, the current thread may have been unparked and placed in the 
        // ready queue.
        //

        Schedule(FALSE);
    }	

    CurrentThread->WaitPending = 1;
}

//
// Unblocks the user thread associated with the specified thread parker instance.
//

VOID
UtUnpark (
    __in HANDLE ThreadHandle
    )
{    
    PUTHREAD Thread = (PUTHREAD) ThreadHandle;

    //
    // Try to clear the WaitPending flag. If we fail to clear it, then the target thread is already executing the wait logic
    // and must be placed in the ready queue.
    //

    if (Thread->WaitPending == 0 || InterlockedExchange(&Thread->WaitPending, 0) == 0) {
        ReadyThread(Thread);
    }
}

//
// Definition of the helper functions.
//

//
// The trampoline function that a user thread begins by executing, through which the associated function is called.
//

VOID
InternalStart (
    )
{
    CurrentPrcb.RunningThread->Function(CurrentPrcb.RunningThread->Argument);
    UtExit();
}

//
// The entry point of an idle thread.
//

ULONG
WINAPI
IdleThreadLoop (
    __in PVOID Argument
    )
{
    UTHREAD IdleThread;
    PUTHREAD NextThread;

    UNREFERENCED_PARAMETER(Argument);
    
    //
    // IdleThread is a user thread proxy for the underlying operating system thread and is switched in 
    // when the scheduler becomes idle.
    //

    IdleThread.Running = 0;
    IdleThread.Function = (UT_FUNCTION) IdleThreadLoop;
    CurrentPrcb.IdleThread = &IdleThread;
    
    //
    // An idle thread is switched in when the ready queue is empty. It waits on the IdleSemaphore until
    // a new user thread is created or an existing one is unparked. If all idle threads are switched in,
    // then there is no way for user threads to become schedulable. Hence, the scheduler exits.
    //

    do {
        EnterCriticalSection(&UThreadLock);
        {
            if (!IsListEmpty(&ReadyQueue)) {
                NextThread = FindNextThread(TRUE);
            } else if ((IdleCount += 1) < NumberOfVirtualProcessors) {
                NextThread = NULL;    
            } else {                
                LeaveCriticalSection(&UThreadLock);
                ReleaseSemaphore(IdleSemaphore, NumberOfVirtualProcessors - 1, NULL);
                break;       
            }
        }
        LeaveCriticalSection(&UThreadLock);

        if (NextThread != NULL) {
            ContextSwitch(&IdleThread, NextThread);
        } else {
            WaitForSingleObject(IdleSemaphore, INFINITE);
        }
    } while (IdleCount < NumberOfVirtualProcessors);

    //
    // The main thread will resume execution on UtRun(). The other threads will exit to the underlaying OS.
    //

    return 0;
}

//
// Sets the RunningThread to Thread.
//

static
VOID
SetRunningThread (
    __in PUTHREAD Thread
    )
{
    CurrentPrcb.RunningThread = Thread;
}

//
// Perform a context switch from CurrentThread (switch out) to NextThread (switch in).
// __fastcall sets the calling convention such that CurrentThread is in ECX and NextThread in EDX.
// __declspec(naked) directs the compiler to omit any prologue or epilogue code.
//

__declspec(naked)
VOID
__fastcall
ContextSwitch (
    __inout PUTHREAD CurrentThread,
    __inout PUTHREAD NextThread
    )
{
    //
    // We must ensure the visibily of CurrentThread's stores with regard to itself in case it is switched to another processor.
    // On an x86 machine there is no need for a memory barrier, since memory ordering obeys causality (memory ordering respects 
    // transitive visibility): if a processor sees CurrentThread->SwapBusy set to 0, then it will also see its previous stores. 
    // Any compiler optimization is avoided by the use of inline assembly, making it unnecessary for Running to be volatile.
    //

    __asm {

        //
        // Switch out the running CurrentThread, saving the execution context on the thread's own stack.   
        // The return address is atop the stack, having been placed there by the call to this function.
        //

        push	ebp
        push	ebx
        push	esi
        push	edi

        //
        // Save ESP in CurrentThread->ThreadContext.
        //

        mov		dword ptr [ecx].ThreadContext, esp

        //
        // Clear the Running flag, making it possible for the thread to be switched back in.
        //

        mov		byte ptr [ecx].Running, 0

        //
        // Spin until NextThread has been switched out. The following instructions are not being executed
        // in the context of any user thread.
        //

retry:
        cmp     byte ptr [edx].Running, 0
        jne     short retry

        //
        // NextThread has been switched out. It is now save to load its context, thus switching it in. 
        //

        mov		esp, dword ptr [edx].ThreadContext	
        pop		edi
        pop		esi
        pop		ebx
        pop		ebp

        //
        // Mark the user thread as running, preventing it from being switched in to another processor.
        //

        mov		byte ptr [edx].Running, 1	

        //
        // Set NextThread as the running thread.
        //

        push    edx
        call    SetRunningThread
        add     esp, 4

        //
        // Jump to the return address saved on NextThread's stack when the function was called.
        //

        ret
    }
}

//
// Frees the resources associated with Thread.
// __fastcall sets the calling convention such that Thread is in ECX.
//

static
VOID
__fastcall
CleanupThread (
    __inout PUTHREAD Thread
    )
{
    free(Thread->Stack);
    free(Thread);
}

//
// Frees the resources associated with CurrentThread and switches to NextThread.
// __fastcall sets the calling convention such that CurrentThread is in ECX and NextThread in EDX.
// __declspec(naked) directs the compiler to omit any prologue or epilogue code.
//

__declspec(naked)
VOID
__fastcall
InternalExit (
    __inout PUTHREAD CurrentThread,
    __in PUTHREAD NextThread
    )
{
    __asm {
retry:
        cmp     byte ptr [edx].Running, 0
        jne     retry
        
        //
        // NextThread has been switched out. We load its stack pointer before freeing the
        // current thread's resources: since the memory block used as the CurrentThread's 
        // stack will also be freed, we cannot call CleanupThread() while using that stack.
        //

        mov		esp, dword ptr [edx].ThreadContext
        pop		edi
        pop		esi
        pop		ebx
        pop		ebp

        mov		byte ptr [edx].Running, 1	

        push    edx
        call	CleanupThread
        call    SetRunningThread
        add     esp, 4

        ret
    }
}
