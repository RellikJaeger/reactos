/* $Id$
 *
 * COPYRIGHT:       See COPYING in the top level directory
 * PROJECT:         ReactOS system libraries
 * FILE:            lib/kernel32/misc/dllmain.c
 * PURPOSE:         Initialization
 * PROGRAMMER:      Ariadne ( ariadne@xs4all.nl)
 * UPDATE HISTORY:
 *                  Created 01/11/98
 */

/* INCLUDES ******************************************************************/

#include <k32.h>

//#define NDEBUG
#include "../include/debug.h"

/* GLOBALS *******************************************************************/

extern UNICODE_STRING SystemDirectory;
extern UNICODE_STRING WindowsDirectory;

HANDLE hProcessHeap = NULL;
HMODULE hCurrentModule = NULL;
HANDLE hBaseDir = NULL;
PPEB Peb;
ULONG SessionId;

static BOOL DllInitialized = FALSE;
static BOOL ConsoleInitialized = FALSE;

BOOL STDCALL
DllMain(HANDLE hInst,
	DWORD dwReason,
	LPVOID lpReserved);

/* Critical section for various kernel32 data structures */
RTL_CRITICAL_SECTION DllLock;
RTL_CRITICAL_SECTION ConsoleLock;

extern BOOL WINAPI DefaultConsoleCtrlHandler(DWORD Event);
extern __declspec(noreturn) VOID CALLBACK ConsoleControlDispatcher(DWORD CodeAndFlag);

extern BOOL FASTCALL NlsInit();
extern VOID FASTCALL NlsUninit();
BOOLEAN InWindows = FALSE;

HANDLE
STDCALL
DuplicateConsoleHandle(HANDLE hConsole,
                       DWORD dwDesiredAccess,
                       BOOL	bInheritHandle,
                       DWORD dwOptions);

#define WIN_OBJ_DIR L"\\Windows"
#define SESSION_DIR L"\\Sessions"

/* FUNCTIONS *****************************************************************/

NTSTATUS
WINAPI
OpenBaseDirectory(PHANDLE DirHandle)
{
    OBJECT_ATTRIBUTES ObjectAttributes;
    UNICODE_STRING Name = RTL_CONSTANT_STRING(L"\\BaseNamedObjects");
    NTSTATUS Status;

    InitializeObjectAttributes(&ObjectAttributes,
                               &Name,
                               OBJ_CASE_INSENSITIVE,
                               NULL,
                               NULL);

    Status = NtOpenDirectoryObject(DirHandle,
                                   DIRECTORY_ALL_ACCESS &
                                   ~(DELETE | WRITE_DAC | WRITE_OWNER),
                                   &ObjectAttributes);
    if (!NT_SUCCESS(Status))
    {
        /* FIXME: It's not our job to create the BNO directory, csr does it */
        Status = NtCreateDirectoryObject(DirHandle,
                                         DIRECTORY_ALL_ACCESS,
                                         &ObjectAttributes);
        if (!NT_SUCCESS(Status))
        {
            DPRINT1("NtCreateDirectoryObject() failed\n");
        }
    }

    DPRINT("Opened BNO: %lx\n", *DirHandle);
    return Status;
}

/*
 * @unimplemented
 */
BOOL
WINAPI
BaseQueryModuleData(IN LPSTR ModuleName,
                    IN LPSTR Unknown,
                    IN PVOID Unknown2,
                    IN PVOID Unknown3,
                    IN PVOID Unknown4)
{
    DPRINT1("BaseQueryModuleData called: %s %s %x %x %x\n",
            ModuleName,
            Unknown,
            Unknown2,
            Unknown3,
            Unknown4);
    return FALSE;
}

/*
 * @unimplemented
 */
NTSTATUS
WINAPI
BaseProcessInitPostImport(VOID)
{
    /* FIXME: Initialize TS pointers */
    return STATUS_SUCCESS;
}

BOOL
STDCALL
BasepInitConsole(VOID)
{
    CSR_API_MESSAGE Request;
    ULONG CsrRequest;
    NTSTATUS Status;
    PRTL_USER_PROCESS_PARAMETERS Parameters = NtCurrentPeb()->ProcessParameters;

    WCHAR lpTest[MAX_PATH];
    GetModuleFileNameW(NULL, lpTest, MAX_PATH);
    DPRINT("BasepInitConsole for : %S\n", lpTest);
    DPRINT("Our current console handles are: %lx, %lx, %lx %lx\n", 
           Parameters->ConsoleHandle, Parameters->StandardInput, 
           Parameters->StandardOutput, Parameters->StandardError);

    /* We have nothing to do if this isn't a console app... */
    if (RtlImageNtHeader(GetModuleHandle(NULL))->OptionalHeader.Subsystem !=
        IMAGE_SUBSYSTEM_WINDOWS_CUI)
    {
        DPRINT("Image is not a console application\n");
        Parameters->ConsoleHandle = NULL;
        return TRUE;
    }

    /* Assume one is needed */
    Request.Data.AllocConsoleRequest.ConsoleNeeded = TRUE;

    /* Handle the special flags given to us by BasepInitializeEnvironment */
    if (Parameters->ConsoleHandle == HANDLE_DETACHED_PROCESS)
    {
        /* No console to create */
        DPRINT("No console to create\n");
        Parameters->ConsoleHandle = NULL;
        Request.Data.AllocConsoleRequest.ConsoleNeeded = FALSE;
    }
    else if (Parameters->ConsoleHandle == HANDLE_CREATE_NEW_CONSOLE)
    {
        /* We'll get the real one soon */
        DPRINT("Creating new console\n");
        Parameters->ConsoleHandle = NULL;
    }
    else if (Parameters->ConsoleHandle == HANDLE_CREATE_NO_WINDOW)
    {
        /* We'll get the real one soon */
        DPRINT1("NOT SUPPORTED: HANDLE_CREATE_NO_WINDOW\n");
        Parameters->ConsoleHandle = NULL;
    }
    else
    {
        if (Parameters->ConsoleHandle == INVALID_HANDLE_VALUE)
        {
            Parameters->ConsoleHandle = 0;
        }
        DPRINT("Using existing console: %x\n", Parameters->ConsoleHandle);
    }

    /* Initialize Console Ctrl Handler */
    ConsoleInitialized = TRUE;
    RtlInitializeCriticalSection(&ConsoleLock);
    SetConsoleCtrlHandler(DefaultConsoleCtrlHandler, TRUE);

    /* Now use the proper console handle */
    Request.Data.AllocConsoleRequest.Console = Parameters->ConsoleHandle;

    /*
     * Normally, we should be connecting to the Console CSR Server...
     * but we don't have one yet, so we will instead simply send a create
     * console message to the Base Server. When we finally have a Console
     * Server, this code should be changed to send connection data instead.
     *
     * Also note that this connection should be made for any console app, even
     * in the case above where -we- return.
     */
    CsrRequest = MAKE_CSR_API(ALLOC_CONSOLE, CSR_CONSOLE);
    Request.Data.AllocConsoleRequest.CtrlDispatcher = ConsoleControlDispatcher;
    Status = CsrClientCallServer(&Request, 
                                 NULL,
                                 CsrRequest,
                                 sizeof(CSR_API_MESSAGE));
    if(!NT_SUCCESS(Status) || !NT_SUCCESS(Status = Request.Status))
    {
        DPRINT1("CSR Failed to give us a console\n");
        /* We're lying here, so at least the process can load... */
        return TRUE;
    }

    /* We got the handles, let's set them */
    if ((Parameters->ConsoleHandle = Request.Data.AllocConsoleRequest.Console))
    {
        /* If we already had some, don't use the new ones */
        if (!Parameters->StandardInput)
        {
            Parameters->StandardInput = Request.Data.AllocConsoleRequest.InputHandle;
        }
        if (!Parameters->StandardOutput)
        {
            Parameters->StandardOutput = Request.Data.AllocConsoleRequest.OutputHandle;
        }
        if (!Parameters->StandardError)
        {
            Parameters->StandardError = Request.Data.AllocConsoleRequest.OutputHandle;
        }
    }

    DPRINT("Console setup: %lx, %lx, %lx, %lx\n", 
            Parameters->ConsoleHandle,
            Parameters->StandardInput,
            Parameters->StandardOutput,
            Parameters->StandardError);
    return TRUE;
}


BOOL
STDCALL
DllMain(HANDLE hDll,
        DWORD dwReason,
        LPVOID lpReserved)
{
    NTSTATUS Status;
    BOOLEAN IsServer;
    ULONG Dummy;
    ULONG DummySize = sizeof(Dummy);
    WCHAR SessionDir[256];

    DPRINT("DllMain(hInst %lx, dwReason %lu)\n",
           hDll, dwReason);

    /* Cache the PEB and Session ID */
    Peb = NtCurrentPeb();
    SessionId = Peb->SessionId;

    switch (dwReason)
    {
        case DLL_PROCESS_ATTACH:

        /* OK, yes, this is really retarded but it works for now */
        InWindows = NtCurrentPeb()->BeingDebugged;

        /*
         * CreateProcess will run in the real kernel32 and it will write
         * its own BaseProcessStartThunk EIP in the CONTEXT that ZwContinue
         * will get. We'll be first called by Ldr while initializing, and we'll
         * be wrapped in 3 layers of SEH, followed by two frames, finally 
         * followed by our CONTEXT on the stack. We'll modify the EIP in it
         * to match the correct one (our own) and then everything works.
         * Tested on XP and 2K3, probably doesn't work in 2K.
         */
        if (InWindows)
        {
            /* 
             * Due to yet another bug in how Windows handles .local, LDR will
             * actually end up loading us twice. The second time will be the
             * "official" load, at a totally different address. It will be,
             * it will be at -that- address that all the APIs will be called.
             * However, that address is dynamic while this one will be static,
             * so we'll do initilization with this one. Plus, at this one,
             * we know exactly that we're within 3 SEH layers.
             */
            if (hDll == (HANDLE)0x7c800000)
            {
                PULONG Eip;
                Eip = (PULONG)*(PULONG)*(PULONG)NtCurrentTeb()->Tib.ExceptionList +
                       0x9 +
                       FIELD_OFFSET(CONTEXT, Eip) / sizeof(ULONG);
                *Eip = (ULONG)BaseProcessStartThunk;
            }
        }

        /* Don't bother us for each thread */
        LdrDisableThreadCalloutsForDll((PVOID)hDll);

        /* Setup the right Object Directory path */
        if (!SessionId)
        {
            /* Use the raw path */
            wcscpy(SessionDir, WIN_OBJ_DIR);
        }
        else
        {
            /* Use the session path */
            swprintf(SessionDir,
                     L"%ws\\%ld%ws",
                     SESSION_DIR,
                     SessionId,
                     WIN_OBJ_DIR);
        }

        /* Connect to the base server */
        DPRINT("Connecting to CSR...\n");
        Status = CsrClientConnectToServer(SessionDir,
                                          InWindows ? 1 : 0,
                                          &Dummy,
                                          &DummySize,
                                          &IsServer);
        if (!NT_SUCCESS(Status))
        {
            DPRINT1("Failed to connect to CSR (Status %lx)\n", Status);
            ZwTerminateProcess(NtCurrentProcess(), Status);
            return FALSE;
        }

        /* Check if we are running a CSR Server */
        if (!IsServer)
        {
            /* Set the termination port for the thread */
            DPRINT("Creating new thread for CSR\n");
            CsrNewThread();
        }

        hProcessHeap = RtlGetProcessHeap();
        hCurrentModule = hDll;
        DPRINT("Heap: %p\n", hProcessHeap);

        /*
         * Initialize WindowsDirectory and SystemDirectory
         */
        DPRINT("NtSystemRoot: %S\n", SharedUserData->NtSystemRoot);
        RtlCreateUnicodeString (&WindowsDirectory, SharedUserData->NtSystemRoot);
        SystemDirectory.MaximumLength = WindowsDirectory.MaximumLength + 18;
        SystemDirectory.Length = WindowsDirectory.Length + 18;
        SystemDirectory.Buffer = RtlAllocateHeap(hProcessHeap,
                                                 0,
                                                 SystemDirectory.MaximumLength);
        wcscpy(SystemDirectory.Buffer, WindowsDirectory.Buffer);
        wcscat(SystemDirectory.Buffer, L"\\System32");

        /* Open object base directory */
        Status = OpenBaseDirectory(&hBaseDir);
        if (!NT_SUCCESS(Status))
        {
            DPRINT1("Failed to open object base directory (Status %lx)\n", Status);
            return FALSE;
        }

        /* Initialize the DLL critical section */
        RtlInitializeCriticalSection(&DllLock);

        /* Initialize the National Language Support routines */
        if (!NlsInit())
        {
            DPRINT1("NLS Init failed\n");
            return FALSE;
        }

        /* Initialize Console Support */
        if (!BasepInitConsole())
        {
            DPRINT1("Failure to set up console\n");
            return FALSE;
        }

        /* Insert more dll attach stuff here! */
        DllInitialized = TRUE;
        DPRINT1("Initialization complete\n");
        break;

        case DLL_PROCESS_DETACH:

            DPRINT("DLL_PROCESS_DETACH\n");
            if (DllInitialized == TRUE)
            {
                /* Insert more dll detach stuff here! */
                NlsUninit();

                /* Delete DLL critical section */
                if (ConsoleInitialized == TRUE)
                {
                    RtlDeleteCriticalSection (&ConsoleLock);
                }
                RtlDeleteCriticalSection (&DllLock);

                /* Close object base directory */
                NtClose(hBaseDir);

                RtlFreeUnicodeString (&SystemDirectory);
                RtlFreeUnicodeString (&WindowsDirectory);
            }
            break;

        default:
            break;
    }

    return TRUE;
}

/* EOF */
