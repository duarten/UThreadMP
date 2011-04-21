#pragma once

#include <windows.h>

#ifndef CONTAINING_RECORD

#define CONTAINING_RECORD(address, type, field) \
        ((type *)((PCHAR)(address) - (size_t)(&((type *)0)->field)))

#endif

//
// Initialized the specified doubly linked list.
//

FORCEINLINE
VOID
InitializeListHead (
    __out PLIST_ENTRY ListHead
    )
{
    ListHead->Flink = ListHead->Blink = ListHead;
}

//
// Returns true if the specified list is empty.
//

FORCEINLINE
BOOL
IsListEmpty (
    __in LIST_ENTRY * ListHead
    )
{
    return (BOOL) (ListHead->Flink == ListHead);
}

//
// Removes the specified entry from the list that
// contains it.
//

FORCEINLINE
BOOL
RemoveEntryList (
    __in PLIST_ENTRY Entry
    )
{
    PLIST_ENTRY Blink;
    PLIST_ENTRY Flink;

    Flink = Entry->Flink;
    Blink = Entry->Blink;
    Blink->Flink = Flink;
    Flink->Blink = Blink;
    return Flink == Blink;
}

//
// Removes the entry that is at the front of the list.
//

FORCEINLINE
PLIST_ENTRY
RemoveHeadList (
    __inout PLIST_ENTRY ListHead
    )
{
    PLIST_ENTRY Flink;
    PLIST_ENTRY Entry;

    Entry = ListHead->Flink;
    Flink = Entry->Flink;
    ListHead->Flink = Flink;
    Flink->Blink = ListHead;
    return Entry;
}

//
// Removes the entry that is at the tail of the list.
//

FORCEINLINE
PLIST_ENTRY
RemoveTailList (
    __inout PLIST_ENTRY ListHead
    )
{
    PLIST_ENTRY Blink;
    PLIST_ENTRY Entry;

    Entry = ListHead->Blink;
    Blink = Entry->Blink;
    ListHead->Blink = Blink;
    Blink->Flink = ListHead;
    return Entry;
}

//
// Inserts the specified entry at the tail of the list.
//

FORCEINLINE
VOID
InsertTailList (
    __in PLIST_ENTRY ListHead,
    __out PLIST_ENTRY Entry
    )
{
    PLIST_ENTRY Blink;

    Blink = ListHead->Blink;
    Entry->Flink = ListHead;
    Entry->Blink = Blink;
    Blink->Flink = Entry;
    ListHead->Blink = Entry;
}

//
// Inserts the specified entry at the head of the list.
//

FORCEINLINE
VOID
InsertHeadList (
    __inout PLIST_ENTRY ListHead,
    __out PLIST_ENTRY Entry
    )
{
    PLIST_ENTRY Flink;

    Flink = ListHead->Flink;
    Entry->Flink = Flink;
    Entry->Blink = ListHead;
    Flink->Blink = Entry;
    ListHead->Flink = Entry;
}
