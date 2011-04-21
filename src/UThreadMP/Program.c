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

#include <crtdbg.h>
#include <stdio.h>

#include "UThread.h"
#include "SyncObjects.h"
#include "List.h"

///////////////////////////////////////////////////////////////
//															 //
// Test 1: 10 threads, each one printing its number 16 times //
//															 //
///////////////////////////////////////////////////////////////

ULONG Test1_Count;

VOID
Test1_Thread (
    UT_ARGUMENT Argument
    ) 
{
    UCHAR Char;
    ULONG Index;
    
    Char = (UCHAR) Argument;	

    for (Index = 0; Index < 1000; ++Index) {
        putchar(Char);

        if ((rand() % 4) == 0) {
            UtYield();
        }
    }

    ++Test1_Count;
    
    UtExit();
}

VOID
Test1 (
    ) 
{
    ULONG Index;

    Test1_Count = 0; 

    printf("\n :: Test 1 - BEGIN :: \n\n");

    for (Index = 0; Index < 10; ++Index) {
        UtCreate(Test1_Thread, (UT_ARGUMENT) ('0' + Index));
    }   

    UtRun(AVAILABLE_PROCESSORS);

    _ASSERTE(Test1_Count == 10);
    printf("\n\n :: Test 1 - END :: \n");
}

///////////////////////////////////////////////////////////////
//															 //
// Test 2: Testing mutexes									 //
//															 //
///////////////////////////////////////////////////////////////
                                
ULONG Test2_Count;

VOID
Test2_Thread1 (
    UT_ARGUMENT Argument
    ) 
{
    PUTHREAD_MUTEX Mutex;
    WAIT_REQUEST WaitRequest;

    Mutex = (PUTHREAD_MUTEX) Argument;
    WaitRequest.SyncObject = &Mutex->Header;

    printf("Thread1 running\n");

    printf("Thread1 acquiring the mutex...\n");
    UtWaitForSyncObject(&WaitRequest);
    printf("Thread1 acquired the mutex...\n");
    
    UtYield();

    printf("Thread1 acquiring the mutex again...\n");
    UtWaitForSyncObject(&WaitRequest);
    printf("Thread1 acquired the mutex again...\n");

    UtYield();

    printf("Thread1 releasing the mutex...\n");
    UtReleaseMutex(Mutex);
    printf("Thread1 released the mutex...\n");

    UtYield();

    printf("Thread1 releasing the mutex again...\n");
    UtReleaseMutex(Mutex);
    printf("Thread1 released the mutex again...\n");

    printf("Thread1 exiting\n");
    ++Test2_Count;
}

VOID
Test2_Thread2 (
    UT_ARGUMENT Argument
    ) 
{
    PUTHREAD_MUTEX Mutex;
    WAIT_REQUEST WaitRequest;

    Mutex = (PUTHREAD_MUTEX) Argument;
    WaitRequest.SyncObject = &Mutex->Header;

    printf("Thread2 running\n");

    printf("Thread2 acquiring the mutex...\n");
    UtWaitForSyncObject(&WaitRequest);
    printf("Thread2 acquired the mutex...\n");
    
    UtYield();

    printf("Thread2 releasing the mutex...\n");
    UtReleaseMutex(Mutex);
    printf("Thread2 released the mutex...\n");
    
    printf("Thread2 exiting\n");
    ++Test2_Count;
}

VOID
Test2_Thread3 (
    UT_ARGUMENT Argument
    ) 
{
    PUTHREAD_MUTEX Mutex;
    WAIT_REQUEST WaitRequest;

    Mutex = (PUTHREAD_MUTEX) Argument;
    WaitRequest.SyncObject = &Mutex->Header;

    printf("Thread3 running\n");

    printf("Thread3 acquiring the mutex...\n");
    UtWaitForSyncObject(&WaitRequest);
    printf("Thread3 acquired the mutex...\n");
    
    UtYield();

    printf("Thread3 releasing the mutex...\n");
    UtReleaseMutex(Mutex);
    printf("Thread3 released the mutex...\n");

    printf("Thread3 exiting\n");
    ++Test2_Count;
}

VOID
Test2 (
    ) 
{
    UTHREAD_MUTEX Mutex;
    
    UtInitializeMutex(&Mutex, FALSE);
    
    printf("\n-:: Test 2 - BEGIN ::-\n\n");

    Test2_Count = 0;
    
    UtCreate(Test2_Thread1, &Mutex);
    UtCreate(Test2_Thread2, &Mutex);
    UtCreate(Test2_Thread3, &Mutex);
    UtRun(AVAILABLE_PROCESSORS);

    _ASSERTE(Test2_Count == 3);
    
    printf("\n-:: Test 2 -  END  ::-\n");
}

///////////////////////////////////////////////////////////////
//															 //
// Test 3: building a mailbox with a mutex and a semaphore   //
//															 //
///////////////////////////////////////////////////////////////

//
// Mailbox containing message queue, a lock to ensure exclusive access 
// and a semaphore to control the message queue.
//

typedef struct _MAILBOX {
    UTHREAD_MUTEX Lock;      
    UTHREAD_SEMAPHORE Semaphore;   
    LIST_ENTRY MessageQueue;
} MAILBOX, *PMAILBOX;

//
// Mailbox message.
//

typedef struct _MAILBOX_MESSAGE {
    LIST_ENTRY QueueEntry;
    PVOID Data;
} MAILBOX_MESSAGE, *PMAILBOX_MESSAGE;

static 
VOID 
Mailbox_Initialize (
    __out PMAILBOX Mailbox
    )
{
    UtInitializeMutex(&Mailbox->Lock, FALSE);
    UtInitializeSemaphore(&Mailbox->Semaphore, 0, ~0U);
    InitializeListHead(&Mailbox->MessageQueue);
}

static 
VOID 
Mailbox_Close (
    __inout PMAILBOX Mailbox
    )
{
    UtCloseSyncObject(&Mailbox->Lock.Header);
    UtCloseSyncObject(&Mailbox->Semaphore.Header);
}

static
VOID
Mailbox_Post (
    __inout PMAILBOX Mailbox,
    __in PVOID Data
    )
{
    PMAILBOX_MESSAGE Message;
    WAIT_REQUEST WaitRequest;

    //
    // Create an envelope.
    //
    
    Message = (PMAILBOX_MESSAGE) malloc(sizeof *Message);

    _ASSERTE(Message != NULL);

    Message->Data = Data;;

    //
    // Insert the message in the mailbox queue.
    //

    WaitRequest.SyncObject = &Mailbox->Lock.Header;
    UtWaitForSyncObject(&WaitRequest);

    UtYield();
    InsertTailList(&Mailbox->MessageQueue, &Message->QueueEntry);

    //printf("** enqueued: 0x%08x **\n", Message);
    UtReleaseMutex(&Mailbox->Lock);
    
    //
    // Add one permit to indicate the availability of one more message.
    // 

    UtReleaseSemaphore(&Mailbox->Semaphore, 1);
}

static 
PVOID
Mailbox_Wait (
    __inout PMAILBOX Mailbox
    )
{
    PVOID Data;
    PMAILBOX_MESSAGE Message;
    SEMAPHORE_WAIT_REQUEST SemaphoreRequest;
    
    //
    // Wait for a message to be available in the mailbox.
    //

    SemaphoreRequest.Header.SyncObject = &Mailbox->Semaphore.Header;
    SemaphoreRequest.Permits = 1;
    UtWaitForSyncObject(&SemaphoreRequest.Header);
    
    //
    // Get the envelope from the mailbox queue.
    //

    SemaphoreRequest.Header.SyncObject = &Mailbox->Lock.Header;
    UtWaitForSyncObject(&SemaphoreRequest.Header);
    
    UtYield();
    Message = CONTAINING_RECORD(RemoveHeadList(&Mailbox->MessageQueue), MAILBOX_MESSAGE, QueueEntry);
    
    _ASSERTE(Message != NULL);
    //printf("** dequeued: 0x%08x **\n", Message);
    
    UtReleaseMutex(&Mailbox->Lock);
    
    //
    // Extract the message from the envelope.
    //

    Data = Message->Data;

    //
    // Destroy the envelope and return the message.
    //

    free(Message);
    return Data;
}

#define TERMINATOR ((PCHAR)~0)

ULONG Test3_CountProducers;
ULONG Test3_CountConsumers;

VOID
Test3_ProducerThread (
    __in UT_ARGUMENT Argument
    ) 
{
    static ULONG CurrentId = 0;
    ULONG ProducerId;
    PMAILBOX Mailbox;
    PCHAR Message;
    ULONG MessageNumber;

    Mailbox = (PMAILBOX) Argument;
    ProducerId = ++CurrentId;
    
    for (MessageNumber = 0; MessageNumber < 5000; ++MessageNumber) {
        Message = (PCHAR) malloc(64);
        sprintf_s(Message, 64, "Message %04d from producer %d", MessageNumber, ProducerId);
        printf(" ** producer %d: sending message %04d [0x%08x]\n", ProducerId, MessageNumber, Message);
        
        Mailbox_Post(Mailbox, Message);
        
        if ((rand() % 2) == 0) { 
            UtYield();
        }
        //Sleep(1000); 
    }
    
    ++Test3_CountProducers;
}

VOID
Test3_ConsumerThread (
    __in UT_ARGUMENT Argument
    ) 
{
    ULONG ConsumerId;
    static ULONG CurrentId = 0;
    PMAILBOX Mailbox;
    PCHAR Message;
    ULONG MessageCount;
    
    ConsumerId = ++CurrentId;
    Mailbox = (PMAILBOX) Argument;
    MessageCount = 0;

    do {

        //
        // Get a message from the mailbox.
        //

        Message = (PCHAR) Mailbox_Wait(Mailbox);
        if (Message != TERMINATOR) {
            ++MessageCount;
            printf(" ++ consumer %d: got \"%s\" [0x%08x]\n", ConsumerId, Message, Message);
            
            //
            // Free the memory used by the message.
            //

            free(Message);
        } else {
            printf(" ++ consumer %d: exiting after %d messages\n", ConsumerId, MessageCount);
            break;
        }
    } while (TRUE);
    
    ++Test3_CountConsumers;
}

VOID
Test3_FirstThread (
    UT_ARGUMENT Argument
    )
{
    MAILBOX Mailbox;

    UNREFERENCED_PARAMETER(Argument);

    Mailbox_Initialize(&Mailbox);

    Test3_CountProducers = 0;
    Test3_CountConsumers = 0;

    UtCreate(Test3_ConsumerThread, &Mailbox);
    UtCreate(Test3_ConsumerThread, &Mailbox);
    UtCreate(Test3_ProducerThread, &Mailbox);
    UtCreate(Test3_ProducerThread, &Mailbox);
    UtCreate(Test3_ProducerThread, &Mailbox);
    UtCreate(Test3_ProducerThread, &Mailbox);

    do {
        UtYield();
    } while (Test3_CountProducers != 4);

    Mailbox_Post(&Mailbox, TERMINATOR);
    Mailbox_Post(&Mailbox, TERMINATOR);
    
    do {
        UtYield();
    } while (Test3_CountConsumers != 2);

    Mailbox_Close(&Mailbox);
}

VOID
Test3 ( 
    ) 
{
    printf("\n-:: Test 3 - BEGIN ::-\n\n");
    UtCreate(Test3_FirstThread, NULL);
    UtRun(AVAILABLE_PROCESSORS);
    printf("\n-:: Test 3 -  END  ::-\n");
}

int main (
    )
{
    UtInit();

    Test1();
    Test2();
    Test3();

    getchar();
    return 0;
}
