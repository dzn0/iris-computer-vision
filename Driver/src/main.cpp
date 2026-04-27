#include <ntddk.h>

typedef struct _SYSTEM_MODULE_ENTRY {
    HANDLE Section;
    PVOID  MappedBase;
    PVOID  ImageBase;
    ULONG  ImageSize;
    ULONG  Flags;
    USHORT LoadOrderIndex;
    USHORT InitOrderIndex;
    USHORT LoadCount;
    USHORT OffsetToFileName;
    UCHAR  FullPathName[256];
} SYSTEM_MODULE_ENTRY, *PSYSTEM_MODULE_ENTRY;

typedef struct _SYSTEM_MODULE_INFORMATION {
    ULONG              ModulesCount;
    SYSTEM_MODULE_ENTRY Modules[1];
} SYSTEM_MODULE_INFORMATION, *PSYSTEM_MODULE_INFORMATION;

extern "C" NTSTATUS NTAPI ZwQuerySystemInformation(
    _In_      ULONG  SystemInformationClass,
    _Inout_   PVOID  SystemInformation,
    _In_      ULONG  SystemInformationLength,
    _Out_opt_ PULONG ReturnLength
);

extern "C" NTSTATUS NTAPI ZwDeleteFile(
    _In_ POBJECT_ATTRIBUTES ObjectAttributes
);

// ============================================================================
// GLOBAIS
// ============================================================================
PVOID g_MouHidBase   = NULL;
ULONG g_MouHidSize   = 0;
PVOID g_HidCallback  = NULL;
PVOID g_DeviceObject = NULL;

// ============================================================================
// ESTRUTURA MOUSE_INPUT_DATA
// ============================================================================
typedef struct {
    USHORT UnitId;
    USHORT Flags;
    USHORT ButtonFlags;
    USHORT ButtonData;
    ULONG  RawButtons;
    LONG   LastX;
    LONG   LastY;
    ULONG  ExtraInformation;
} MOUSE_DATA, *PMOUSE_DATA;

typedef VOID(*PHID_SERVICE_CALLBACK)(
    PVOID  DeviceObject,
    PMOUSE_DATA InputDataStart,
    PMOUSE_DATA InputDataEnd,
    PULONG InputDataConsumed
);

// ============================================================================
// STRINGS NA STACK — sem literals identificáveis no .data
// ============================================================================
// Nomes de arquivo
static const WCHAR k_cmd[] = {'w','m','i','_','i','p','c','.','d','a','t',0};
static const WCHAR k_hb[]  = {'w','u','a','u','c','l','t','.','d','a','t',0};

static VOID BuildPath(WCHAR* buf, ULONG bufCch, const WCHAR* name) {
    const WCHAR prefix[] = {
        '\\','?','?','\\','C',':','\\','W','i','n','d','o','w','s',
        '\\','T','e','m','p','\\',0
    };
    ULONG i = 0;
    for (; prefix[i] && i < bufCch - 1; i++) buf[i] = prefix[i];
    for (ULONG j = 0; name[j] && i < bufCch - 1; j++, i++) buf[i] = name[j];
    buf[i] = 0;
}

// ============================================================================
// UTILITÁRIOS
// ============================================================================
PCHAR GetFileNameFromPathAnsi(PCHAR FullPath) {
    if (!FullPath) return (PCHAR)"";
    PCHAR last = NULL, cur = FullPath;
    while (*cur) { if (*cur == '\\' || *cur == '/') last = cur; cur++; }
    return last ? last + 1 : FullPath;
}

// ============================================================================
// ENCONTRAR mouhid.sys
// ============================================================================
VOID FindMouHidModule() {
    ULONG bufSize = 0;
    NTSTATUS status = ZwQuerySystemInformation(11, NULL, 0, &bufSize);
    if (status != STATUS_INFO_LENGTH_MISMATCH) return;

    // Tag neutro
    PSYSTEM_MODULE_INFORMATION mods = (PSYSTEM_MODULE_INFORMATION)ExAllocatePool2(
        POOL_FLAG_NON_PAGED, bufSize, 'lFsR');
    if (!mods) return;

    status = ZwQuerySystemInformation(11, mods, bufSize, NULL);
    if (!NT_SUCCESS(status)) { ExFreePool(mods); return; }

    // "mouhid" na stack
    const CHAR target[] = {'m','o','u','h','i','d',0};

    for (ULONG i = 0; i < mods->ModulesCount; i++) {
        PCHAR name = GetFileNameFromPathAnsi((PCHAR)mods->Modules[i].FullPathName);
        if (strstr(name, target) != NULL) {
            g_MouHidBase = mods->Modules[i].ImageBase;
            g_MouHidSize = mods->Modules[i].ImageSize;
            break;
        }
    }

    ExFreePool(mods);
}

// ============================================================================
// PATTERN SCAN — HidClassServiceCallback em mouhid.sys
// ============================================================================
PVOID ScanHidCallback(PVOID Base, ULONG Size) {
    UCHAR pattern[] = {
        0x48, 0x89, 0x4C, 0x24, 0x08,
        0x55, 0x53, 0x56, 0x57,
        0x41, 0x54, 0x41, 0x55,
        0x41, 0x56, 0x41, 0x57
    };
    ULONG patSize = sizeof(pattern);
    if (!Base || Size < patSize) return NULL;

    PUCHAR p = (PUCHAR)Base;
    for (ULONG i = 0; i <= Size - patSize; i++) {
        BOOLEAN match = TRUE;
        for (ULONG j = 0; j < patSize; j++) {
            if (p[i + j] != pattern[j]) { match = FALSE; break; }
        }
        if (match) return (PVOID)(p + i);
    }
    return NULL;
}

// ============================================================================
// ENCONTRAR DEVICEOBJECT
// ============================================================================
extern "C" NTSTATUS ObReferenceObjectByName(
    PUNICODE_STRING ObjectName,
    ULONG           Attributes,
    PACCESS_STATE   PassedAccessState,
    ACCESS_MASK     DesiredAccess,
    POBJECT_TYPE    ObjectType,
    KPROCESSOR_MODE AccessMode,
    PVOID           ParseContext,
    PVOID*          Object
);
extern "C" POBJECT_TYPE* IoDriverObjectType;

NTSTATUS FindDeviceObject() {
    // "\Driver\mouhid" na stack
    const WCHAR drvName[] = {
        '\\','D','r','i','v','e','r','\\','m','o','u','h','i','d',0
    };
    UNICODE_STRING driverName;
    RtlInitUnicodeString(&driverName, drvName);

    PDRIVER_OBJECT driverObj = NULL;
    NTSTATUS status = ObReferenceObjectByName(
        &driverName, OBJ_CASE_INSENSITIVE, NULL, 0,
        *IoDriverObjectType, KernelMode, NULL, (PVOID*)&driverObj);

    if (NT_SUCCESS(status) && driverObj) {
        PDEVICE_OBJECT dev = driverObj->DeviceObject;
        while (dev) {
            if (!g_DeviceObject) g_DeviceObject = dev;
            dev = dev->NextDevice;
        }
        ObDereferenceObject(driverObj);
        if (g_DeviceObject) return STATUS_SUCCESS;
    }
    return STATUS_NOT_FOUND;
}

// ============================================================================
// INJETAR via DeviceExtension do mouhid
// ============================================================================
NTSTATUS InjectMouseMovement(LONG X, LONG Y, BOOLEAN absolute) {
    if (!g_DeviceObject) return STATUS_INVALID_PARAMETER;

    PDEVICE_OBJECT devObj = (PDEVICE_OBJECT)g_DeviceObject;
    PUCHAR devExt = (PUCHAR)devObj->DeviceExtension;
    if (!devExt) return STATUS_INVALID_PARAMETER;

    PVOID  devCtx = *(PVOID*)(devExt + 0xE0);
    PHID_SERVICE_CALLBACK cb = *(PHID_SERVICE_CALLBACK*)(devExt + 0xE8);
    if (!devCtx || !cb) return STATUS_INVALID_PARAMETER;

    // Tag neutro
    PMOUSE_DATA pkt = (PMOUSE_DATA)ExAllocatePool2(
        POOL_FLAG_NON_PAGED, sizeof(MOUSE_DATA), 'pNdP');
    if (!pkt) return STATUS_NO_MEMORY;

    RtlZeroMemory(pkt, sizeof(MOUSE_DATA));
    pkt->Flags = absolute ? 0x0001 : 0x0000;
    pkt->LastX = X;
    pkt->LastY = Y;

    __try {
        ULONG consumed = 0;
        KIRQL oldIrql = KfRaiseIrql(DISPATCH_LEVEL);
        cb(devCtx, pkt, pkt + 1, &consumed);
        KeLowerIrql(oldIrql);
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        ExFreePool(pkt);
        return STATUS_UNSUCCESSFUL;
    }

    ExFreePool(pkt);
    return STATUS_SUCCESS;
}

// ============================================================================
// LER ARQUIVO DE COMANDO
// ============================================================================
NTSTATUS ReadCommandFile(PCHAR buf, ULONG bufSize, PULONG bytesRead) {
    WCHAR path[64];
    BuildPath(path, 64, k_cmd);

    HANDLE hFile = NULL;
    UNICODE_STRING fn;
    OBJECT_ATTRIBUTES oa;
    IO_STATUS_BLOCK iosb;

    RtlInitUnicodeString(&fn, path);
    InitializeObjectAttributes(&oa, &fn, OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE, NULL, NULL);

    NTSTATUS st = ZwCreateFile(&hFile, GENERIC_READ, &oa, &iosb, NULL,
        FILE_ATTRIBUTE_NORMAL, FILE_SHARE_READ, FILE_OPEN,
        FILE_SYNCHRONOUS_IO_NONALERT, NULL, 0);
    if (!NT_SUCCESS(st)) return st;

    st = ZwReadFile(hFile, NULL, NULL, NULL, &iosb, buf, bufSize - 1, NULL, NULL);
    if (NT_SUCCESS(st)) { buf[iosb.Information] = '\0'; *bytesRead = (ULONG)iosb.Information; }

    ZwClose(hFile);
    return st;
}

VOID DeleteCommandFile() {
    WCHAR path[64];
    BuildPath(path, 64, k_cmd);
    UNICODE_STRING fn;
    OBJECT_ATTRIBUTES oa;
    RtlInitUnicodeString(&fn, path);
    InitializeObjectAttributes(&oa, &fn, OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE, NULL, NULL);
    ZwDeleteFile(&oa);
}

// ============================================================================
// VERIFICAR HEARTBEAT
// ============================================================================
BOOLEAN CheckHeartbeat() {
    WCHAR path[64];
    BuildPath(path, 64, k_hb);

    HANDLE h = NULL;
    UNICODE_STRING fn;
    OBJECT_ATTRIBUTES oa;
    IO_STATUS_BLOCK iosb;

    RtlInitUnicodeString(&fn, path);
    InitializeObjectAttributes(&oa, &fn, OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE, NULL, NULL);

    NTSTATUS st = ZwOpenFile(&h, FILE_READ_DATA, &oa, &iosb,
        FILE_SHARE_READ, FILE_SYNCHRONOUS_IO_NONALERT);
    if (NT_SUCCESS(st)) { ZwClose(h); return TRUE; }
    return FALSE;
}

// ============================================================================
// INJETAR BOTÃO DO MOUSE via DeviceExtension do mouhid
// ============================================================================
NTSTATUS InjectMouseButtons(USHORT downFlags, USHORT upFlags) {
    if (!g_DeviceObject) return STATUS_INVALID_PARAMETER;

    PDEVICE_OBJECT devObj = (PDEVICE_OBJECT)g_DeviceObject;
    PUCHAR devExt = (PUCHAR)devObj->DeviceExtension;
    if (!devExt) return STATUS_INVALID_PARAMETER;

    PVOID  devCtx = *(PVOID*)(devExt + 0xE0);
    PHID_SERVICE_CALLBACK cb = *(PHID_SERVICE_CALLBACK*)(devExt + 0xE8);
    if (!devCtx || !cb) return STATUS_INVALID_PARAMETER;

    PMOUSE_DATA pkt = (PMOUSE_DATA)ExAllocatePool2(
        POOL_FLAG_NON_PAGED, sizeof(MOUSE_DATA), 'nBdP');
    if (!pkt) return STATUS_NO_MEMORY;

    __try {
        ULONG consumed = 0;
        KIRQL oldIrql = KfRaiseIrql(DISPATCH_LEVEL);

        RtlZeroMemory(pkt, sizeof(MOUSE_DATA));
        pkt->ButtonFlags = downFlags;
        cb(devCtx, pkt, pkt + 1, &consumed);

        RtlZeroMemory(pkt, sizeof(MOUSE_DATA));
        pkt->ButtonFlags = upFlags;
        cb(devCtx, pkt, pkt + 1, &consumed);

        KeLowerIrql(oldIrql);
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        ExFreePool(pkt);
        return STATUS_UNSUCCESSFUL;
    }

    ExFreePool(pkt);
    return STATUS_SUCCESS;
}

// ============================================================================
// PARSEAR COMANDO: "X Y MODE [BTN]"
// BTN: 0=none, 1=left click (down+up)  — campo opcional, default 0
// ============================================================================
NTSTATUS ParseCommand(PCHAR buf, PLONG pX, PLONG pY, PLONG pMode, PLONG pBtn) {
    if (!buf || !buf[0]) return STATUS_INVALID_PARAMETER;
    LONG x = 0, y = 0, mode = 0, btn = 0;
    PCHAR p = buf;
    INT s;
    s = (*p == '-') ? (p++, -1) : 1;
    while (*p >= '0' && *p <= '9') x = x * 10 + (*p++ - '0');
    x *= s;
    while (*p == ' ' || *p == '\t') p++;
    s = (*p == '-') ? (p++, -1) : 1;
    while (*p >= '0' && *p <= '9') y = y * 10 + (*p++ - '0');
    y *= s;
    while (*p == ' ' || *p == '\t') p++;
    s = (*p == '-') ? (p++, -1) : 1;
    while (*p >= '0' && *p <= '9') mode = mode * 10 + (*p++ - '0');
    mode *= s;
    while (*p == ' ' || *p == '\t') p++;
    while (*p >= '0' && *p <= '9') btn = btn * 10 + (*p++ - '0');
    *pX = x; *pY = y; *pMode = mode; *pBtn = btn;
    return STATUS_SUCCESS;
}

// ============================================================================
// THREAD DE TRABALHO
// ============================================================================
VOID WorkerThread(PVOID Context) {
    UNREFERENCED_PARAMETER(Context);

    FindMouHidModule();
    if (!g_MouHidBase) { PsTerminateSystemThread(STATUS_UNSUCCESSFUL); return; }

    g_HidCallback = ScanHidCallback(g_MouHidBase, g_MouHidSize);
    if (!g_HidCallback) { PsTerminateSystemThread(STATUS_UNSUCCESSFUL); return; }

    FindDeviceObject();
    if (!g_DeviceObject) { PsTerminateSystemThread(STATUS_UNSUCCESSFUL); return; }

    CHAR  cmdBuf[64];
    ULONG bytesRead;
    LONG  cmdX, cmdY, cmdMode, cmdBtn;

    LARGE_INTEGER activeDelay;  activeDelay.QuadPart  = -10000LL;     // 1ms
    LARGE_INTEGER dormantDelay; dormantDelay.QuadPart = -10000000LL;  // 1s

    BOOLEAN hbAlive = FALSE;
    LARGE_INTEGER last_hb_check = { 0 };

    while (TRUE) {
        LARGE_INTEGER now_time;
        KeQuerySystemTime(&now_time);

        // Verifica heartbeat a cada 2 segundos
        if ((now_time.QuadPart - last_hb_check.QuadPart) / 10000 > 2000) {
            last_hb_check = now_time;
            hbAlive = CheckHeartbeat();
        }

        if (!hbAlive) {
            KeDelayExecutionThread(KernelMode, FALSE, &dormantDelay);
            continue;
        }

        NTSTATUS st = ReadCommandFile(cmdBuf, sizeof(cmdBuf), &bytesRead);
        if (NT_SUCCESS(st) && bytesRead > 0) {
            if (NT_SUCCESS(ParseCommand(cmdBuf, &cmdX, &cmdY, &cmdMode, &cmdBtn))) {
                DeleteCommandFile();
                if (cmdMode == -1) break;
                if (cmdX != 0 || cmdY != 0)
                    InjectMouseMovement(cmdX, cmdY, (cmdMode == 1));
                if (cmdBtn == 1)
                    InjectMouseButtons(0x0001, 0x0002); // LEFT_DOWN + LEFT_UP
            }
        }

        KeDelayExecutionThread(KernelMode, FALSE, &activeDelay);
    }

    PsTerminateSystemThread(STATUS_SUCCESS);
}

// ============================================================================
// ENTRY POINT
// ============================================================================
NTSTATUS CustomDriverEntry(
    _In_ PDRIVER_OBJECT   DriverObject,
    _In_ PUNICODE_STRING  RegistryPath)
{
    UNREFERENCED_PARAMETER(DriverObject);
    UNREFERENCED_PARAMETER(RegistryPath);

    HANDLE hThread = NULL;
    NTSTATUS status = PsCreateSystemThread(
        &hThread, THREAD_ALL_ACCESS,
        NULL, NULL, NULL, WorkerThread, NULL);

    if (!NT_SUCCESS(status)) return status;

    ZwClose(hThread);
    return STATUS_SUCCESS;
}
