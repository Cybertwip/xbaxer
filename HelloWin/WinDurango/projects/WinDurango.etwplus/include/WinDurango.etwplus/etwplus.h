// etwplus.h : Header file for your target.

#pragma once
#include <Windows.h>
#include <evntprov.h>

typedef struct _ETX_FIELD_DESCRIPTOR
{
    UINT8 Type : 5;
    UINT8 IsLengthField : 1;
} ETX_FIELD_DESCRIPTOR, *PETX_FIELD_DESCRIPTOR, *LPETX_FIELD_DESCRIPTOR;

typedef struct _ETX_EVENT_DESCRIPTOR
{
    EVENT_DESCRIPTOR EtwDescriptor;
    LPCSTR Name;
    LPCSTR SchemaVersion;
    ETX_FIELD_DESCRIPTOR const *FieldDescriptors;
    UINT8 NumFields;
    UINT8 UploadEnabled;
    UINT8 CurrentUploadEnabledState;
    UINT8 DefaultUploadEnabledState;
    INT8 CurrentPopulationSample;
    INT8 DefaultPopulationSample;
    UINT8 CurrentLatency;
    UINT8 DefaultLatency;
    UINT8 CurrentPriority;
    UINT8 DefaultPriority;
} ETX_EVENT_DESCRIPTOR, *PETX_EVENT_DESCRIPTOR, *LPETX_EVENT_DESCRIPTOR;

typedef struct _ETX_PROVIDER_DESCRIPTOR
{
    LPCSTR Name;
    GUID Guid;
    ULONG NumEvents;
    PETX_EVENT_DESCRIPTOR EventDescriptors;
    UINT8 UploadEnabled;
    UINT8 CurrentUploadEnabledState;
    UINT8 DefaultUploadEnabledState;
    INT8 CurrentPopulationSample;
    INT8 DefaultPopulationSample;
    UINT8 CurrentLatency;
    UINT8 DefaultLatency;
    UINT8 CurrentPriority;
    UINT8 DefaultPriority;
} ETX_PROVIDER_DESCRIPTOR, *PETX_PROVIDER_DESCRIPTOR, *LPETX_PROVIDER_DESCRIPTOR;