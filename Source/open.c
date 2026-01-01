/*
 * Open
 *
 * Copyright (c) 2025 amigazen project
 * Licensed under BSD 2-Clause License
 */

#include <exec/types.h>
#include <exec/execbase.h>
#include <dos/dos.h>
#include <intuition/intuition.h>
#include <intuition/intuitionbase.h>
#include <workbench/icon.h>
#include <workbench/workbench.h>
#include <workbench/startup.h>
#include <datatypes/datatypes.h>
#include <datatypes/datatypesclass.h>
#include <utility/tagitem.h>
#include <string.h>
#include <stdlib.h>

/* These pragmas are currently missing from NDK3.2R4 */
#pragma libcall DataTypesBase FindToolNodeA f6 9802
#pragma tagcall DataTypesBase FindToolNode f6 9802
#pragma libcall DataTypesBase LaunchToolA fc A9803
#pragma tagcall DataTypesBase LaunchTool fc A9803

/* Function prototypes for pragma functions */
struct ToolNode *FindToolNodeA(struct List *, struct TagItem *);
ULONG LaunchToolA(struct Tool *, STRPTR, struct TagItem *);

/* Tool type constants */
#ifndef TW_INFO
#define TW_INFO      1
#define TW_BROWSE    2
#define TW_EDIT      3
#define TW_PRINT     4
#define TW_MAIL      5
#endif

/* Tool attribute tags */
#ifndef TOOLA_Dummy
#define TOOLA_Dummy      (TAG_USER)
#define TOOLA_Program    (TOOLA_Dummy + 1)
#define TOOLA_Which      (TOOLA_Dummy + 2)
#define TOOLA_LaunchType (TOOLA_Dummy + 3)
#endif

#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/intuition.h>
#include <proto/icon.h>
#include <proto/wb.h>
#include <proto/datatypes.h>
#include <proto/utility.h>

/* Library base pointers */
extern struct ExecBase *SysBase;
extern struct DosLibrary *DOSBase;
extern struct IntuitionBase *IntuitionBase;
extern struct Library *IconBase;
extern struct Library *WorkbenchBase;
extern struct Library *DataTypesBase;
extern struct Library *UtilityBase;

/* Forward declarations */
BOOL InitializeLibraries(VOID);
VOID Cleanup(VOID);
VOID ShowUsage(VOID);
LONG OpenItem(STRPTR fileName, STRPTR forceTool, BOOL forceBrowse, BOOL forceEdit, BOOL forceInfo, BOOL forcePrint, BOOL forceMail, BOOL showAll);
BOOL IsDrawer(STRPTR fileName, BPTR fileLock);
BOOL IsExecutable(STRPTR fileName, BPTR fileLock);
BOOL IsBinaryAsset(STRPTR fileName);
BOOL IsInfoFile(STRPTR fileName);
BOOL OpenDrawer(STRPTR drawerPath, BOOL showAll);
BOOL OpenExecutable(STRPTR execPath);
BOOL OpenInfoFile(STRPTR fileName, BPTR fileLock);
BOOL OpenDataFile(STRPTR fileName, BPTR fileLock, STRPTR forceTool, BOOL forceBrowse, BOOL forceEdit, BOOL forceInfo, BOOL forcePrint, BOOL forceMail);
BOOL IsDefIconsRunning(VOID);
STRPTR GetDefIconsTypeIdentifier(STRPTR fileName, BPTR fileLock);
STRPTR GetDefIconsDefaultTool(STRPTR typeIdentifier);
STRPTR GetDatatypesTool(STRPTR fileName, BPTR fileLock, UWORD preferredTool);
struct ToolNode *GetDatatypesToolNode(STRPTR fileName, BPTR fileLock, UWORD preferredTool, struct DataType **dtnOut);
STRPTR GetIconDefaultTool(STRPTR fileName, BPTR fileLock);

static const char *verstag = "$VER: Open 47.1 (31.12.2025)\n";
static const char *stack_cookie = "$STACK: 4096\n";

/* Binary asset extensions to skip */
static const char *binaryAssets[] = {
    ".library",
    ".device",
    ".resource",
    ".font",
    ".hunk",
    ".o",
    NULL
};

/* Main entry point */
int main(int argc, char *argv[])
{
    struct WBStartup *wbs = NULL;
    struct WBArg *wbarg;
    SHORT i;
    BOOL fromWorkbench = FALSE;
    LONG result = RETURN_OK;
    BOOL success = TRUE;
    
    /* Check if running from Workbench */
    fromWorkbench = (argc == 0);
    
    if (fromWorkbench) {
        /* Workbench mode - get WBStartup message */
        wbs = (struct WBStartup *)argv;
        
        /* Initialize libraries */
        if (!InitializeLibraries()) {
            LONG errorCode = IoErr();
            PrintFault(errorCode ? errorCode : ERROR_OBJECT_NOT_FOUND, "Open");
            return RETURN_FAIL;
        }
        
        /* Check if we have any file arguments */
        if (wbs->sm_NumArgs <= 1) {
            /* No files to process - show error */
            Printf("Open: No file specified.\n");
            Printf("Open must be set as the default tool on a project icon.\n");
            Cleanup();
            return RETURN_FAIL;
        }
        
        /* Process each file argument (skip index 0 which is our tool) */
        for (i = 1, wbarg = &wbs->sm_ArgList[i]; i < wbs->sm_NumArgs; i++, wbarg++) {
            BPTR oldDir = NULL;
            
            if (wbarg->wa_Lock && wbarg->wa_Name && *wbarg->wa_Name) {
                /* Change to the file's directory */
                oldDir = CurrentDir(wbarg->wa_Lock);
                
                /* Open the file */
                if (OpenItem(wbarg->wa_Name, NULL, FALSE, FALSE, FALSE, FALSE, FALSE, FALSE) != RETURN_OK) {
                    success = FALSE;
                }
                
                /* Restore original directory */
                if (oldDir != NULL) {
                    CurrentDir(oldDir);
                }
            }
        }
        
        /* Cleanup */
        Cleanup();
        
        return success ? RETURN_OK : RETURN_FAIL;
    }
    
    /* CLI mode - parse arguments */
    {
        struct RDArgs *rda = NULL;
        STRPTR fileName = NULL;
        STRPTR forceTool = NULL;
        BOOL forceBrowse = FALSE;
        BOOL forceEdit = FALSE;
        BOOL forceInfo = FALSE;
        BOOL forcePrint = FALSE;
        BOOL forceMail = FALSE;
        BOOL showAll = FALSE;
        
        /* Command template - matches DataType command */
        static const char *template = "FILE/M,TOOL/K,VIEW=BROWSE/S,EDIT/S,INFO/S,PRINT/S,MAIL/S,SHOWALL/S";
        LONG args[8];
        
        /* Initialize args array */
        {
            LONG i;
            for (i = 0; i < 8; i++) {
                args[i] = 0;
            }
        }
        
        /* Parse command-line arguments */
        rda = ReadArgs(template, args, NULL);
        if (!rda) {
            LONG errorCode = IoErr();
            if (errorCode != 0) {
                PrintFault(errorCode, "Open");
            } else {
                ShowUsage();
            }
            return RETURN_FAIL;
        }
        
        /* Extract arguments */
        forceTool = (STRPTR)args[1];
        forceBrowse = (BOOL)(args[2] != 0);
        forceEdit = (BOOL)(args[3] != 0);
        forceInfo = (BOOL)(args[4] != 0);
        forcePrint = (BOOL)(args[5] != 0);
        forceMail = (BOOL)(args[6] != 0);
        showAll = (BOOL)(args[7] != 0);
        
        /* Initialize libraries */
        if (!InitializeLibraries()) {
            LONG errorCode = IoErr();
            PrintFault(errorCode ? errorCode : ERROR_OBJECT_NOT_FOUND, "Open");
            FreeArgs(rda);
            return RETURN_FAIL;
        }
        
        /* FILE/M returns an array of string pointers (STRPTR *), last entry is NULL */
        {
            STRPTR *fileArray = (STRPTR *)args[0];
            LONG fileCount = 0;
            
            /* Initialize result to OK - will be set to FAIL if any file fails */
            result = RETURN_OK;
            
            if (fileArray && fileArray[0]) {
                /* Process each file in the array */
                LONG i = 0;
                while (fileArray[i] != NULL) {
                    fileName = fileArray[i];
                    fileCount++;
                    
                    /* Open the item */
                    if (OpenItem(fileName, forceTool, forceBrowse, forceEdit, forceInfo, forcePrint, forceMail, showAll) != RETURN_OK) {
                        result = RETURN_FAIL;
                    }
                    
                    i++;
                }
            }
            
            /* If no files were provided, open current directory */
            if (fileCount == 0) {
                /* No arguments - open current directory */
                BPTR currentDirLock = NULL;
                STRPTR currentDirName = NULL;
                UBYTE dirNameBuffer[256];
                struct TagItem tags[3];
                LONG tagIndex = 0;
                
                /* Get current directory lock using GetCurrentDir() */
                currentDirLock = GetCurrentDir();
                if (currentDirLock) {
                    /* Get the directory name */
                    if (NameFromLock(currentDirLock, dirNameBuffer, sizeof(dirNameBuffer))) {
                        currentDirName = (STRPTR)dirNameBuffer;
                        
                        /* Build tags for OpenWorkbenchObjectA */
                        if (showAll) {
                            tags[tagIndex].ti_Tag = WBOPENA_Show;
                            tags[tagIndex].ti_Data = DDFLAGS_SHOWALL;
                            tagIndex++;
                        }
                        tags[tagIndex].ti_Tag = TAG_DONE;
                        
                        /* Open the current directory as a drawer */
                        SetIoErr(0);
                        result = OpenWorkbenchObjectA(currentDirName, tags) ? RETURN_OK : RETURN_FAIL;
                        if (result != RETURN_OK) {
                            LONG errorCode = IoErr();
                            if (errorCode != 0) {
                                PrintFault(errorCode, "Open");
                            } else {
                                Printf("Open: Failed to open current directory\n");
                            }
                        }
                    } else {
                        /* Failed to get directory name */
                        Printf("Open: Could not get current directory name\n");
                        result = RETURN_FAIL;
                    }
                } else {
                    /* GetCurrentDir() returns NULL for root - that's valid, try to open root */
                    struct TagItem tags[3];
                    LONG tagIndex = 0;
                    
                    if (showAll) {
                        tags[tagIndex].ti_Tag = WBOPENA_Show;
                        tags[tagIndex].ti_Data = DDFLAGS_SHOWALL;
                        tagIndex++;
                    }
                    tags[tagIndex].ti_Tag = TAG_DONE;
                    
                    SetIoErr(0);
                    result = OpenWorkbenchObjectA("", tags) ? RETURN_OK : RETURN_FAIL;
                    if (result != RETURN_OK) {
                        LONG errorCode = IoErr();
                        if (errorCode != 0) {
                            PrintFault(errorCode, "Open");
                        } else {
                            Printf("Open: Failed to open root directory\n");
                        }
                    }
                }
            }
        }
        
        /* Cleanup */
        if (rda) {
            FreeArgs(rda);
        }
        
        Cleanup();
        
        return result;
    }
}

/* Initialize required libraries */
BOOL InitializeLibraries(VOID)
{
    IntuitionBase = (struct IntuitionBase *)OpenLibrary("intuition.library", 39L);
    if (!IntuitionBase) {
        SetIoErr(ERROR_OBJECT_NOT_FOUND);
        return FALSE;
    }
    
    UtilityBase = OpenLibrary("utility.library", 39L);
    if (!UtilityBase) {
        SetIoErr(ERROR_OBJECT_NOT_FOUND);
        CloseLibrary((struct Library *)IntuitionBase);
        IntuitionBase = NULL;
        return FALSE;
    }
    
    WorkbenchBase = OpenLibrary("workbench.library", 44L);
    if (!WorkbenchBase) {
        SetIoErr(ERROR_OBJECT_NOT_FOUND);
        CloseLibrary(UtilityBase);
        UtilityBase = NULL;
        CloseLibrary((struct Library *)IntuitionBase);
        IntuitionBase = NULL;
        return FALSE;
    }
    
    DataTypesBase = OpenLibrary("datatypes.library", 45L);
    if (!DataTypesBase) {
        SetIoErr(ERROR_OBJECT_NOT_FOUND);
        CloseLibrary(WorkbenchBase);
        WorkbenchBase = NULL;
        CloseLibrary(UtilityBase);
        UtilityBase = NULL;
        CloseLibrary((struct Library *)IntuitionBase);
        IntuitionBase = NULL;
        return FALSE;
    }
    
    /* Open icon.library for DefIcons integration (optional) */
    IconBase = OpenLibrary("icon.library", 47L);
    /* Note: icon.library is optional - we continue even if it fails */
    
    return TRUE;
}

/* Cleanup libraries */
VOID Cleanup(VOID)
{
    if (DataTypesBase) {
        CloseLibrary(DataTypesBase);
        DataTypesBase = NULL;
    }
    
    if (IconBase) {
        CloseLibrary(IconBase);
        IconBase = NULL;
    }
    
    if (WorkbenchBase) {
        CloseLibrary(WorkbenchBase);
        WorkbenchBase = NULL;
    }
    
    if (UtilityBase) {
        CloseLibrary(UtilityBase);
        UtilityBase = NULL;
    }
    
    if (IntuitionBase) {
        CloseLibrary((struct Library *)IntuitionBase);
        IntuitionBase = NULL;
    }
}

/* Show usage information */
VOID ShowUsage(VOID)
{
    Printf("Usage: Open FILE=<filename> [TOOL=<toolname>] [BROWSE] [EDIT] [INFO] [PRINT] [MAIL]\n");
    Printf("\n");
    Printf("Options:\n");
    Printf("  FILE=<filename>  - File, drawer, or executable to open (required)\n");
    Printf("  TOOL=<toolname>  - Force specific tool to use\n");
    Printf("  BROWSE           - Force BROWSE tool for data files\n");
    Printf("  EDIT             - Force EDIT tool for data files\n");
    Printf("  INFO             - Force INFO tool for data files\n");
    Printf("  PRINT            - Force PRINT tool for data files\n");
    Printf("  MAIL             - Force MAIL tool for data files\n");
    Printf("  SHOWALL          - Show all files when opening drawers\n");
    Printf("\n");
    Printf("Open intelligently opens files, drawers, and executables:\n");
    Printf("  - Drawers are opened in Workbench\n");
    Printf("  - Executables are launched (binary assets like .library are skipped)\n");
    Printf("  - Data files are opened with the most appropriate tool\n");
    Printf("\n");
    Printf("Examples:\n");
    Printf("  Open RAM:                    - Open RAM: drawer\n");
    Printf("  Open SYS:C/Edit              - Run Edit command\n");
    Printf("  Open test.txt                - Open with default tool\n");
    Printf("  Open test.txt BROWSE         - Force BROWSE tool\n");
    Printf("  Open test.txt TOOL=MultiView - Force specific tool\n");
}

/* Main open function - determines type and opens appropriately */
LONG OpenItem(STRPTR fileName, STRPTR forceTool, BOOL forceBrowse, BOOL forceEdit, BOOL forceInfo, BOOL forcePrint, BOOL forceMail, BOOL showAll)
{
    BPTR fileLock = NULL;
    LONG result = RETURN_FAIL;
    LONG errorCode = 0;
    
    /* Lock the file/drawer */
    fileLock = Lock(fileName, ACCESS_READ);
    if (!fileLock) {
        errorCode = IoErr();
        PrintFault(errorCode ? errorCode : ERROR_OBJECT_NOT_FOUND, "Open");
        return RETURN_FAIL;
    }
    
    /* Determine what type of item this is */
    /* Check for .info files first (before drawer check) */
    if (IsInfoFile(fileName)) {
        /* It's a .info file - show icon information requester */
        /* Only do this for default open (not if forcing a tool) */
        if (!forceTool && !forceBrowse && !forceEdit && !forceInfo && !forcePrint && !forceMail) {
            result = OpenInfoFile(fileName, fileLock) ? RETURN_OK : RETURN_FAIL;
        } else {
            /* User wants to open with a tool - treat as data file */
            result = OpenDataFile(fileName, fileLock, forceTool, forceBrowse, forceEdit, forceInfo, forcePrint, forceMail) ? RETURN_OK : RETURN_FAIL;
        }
    } else if (IsDrawer(fileName, fileLock)) {
        /* It's a drawer - open it */
        result = OpenDrawer(fileName, showAll) ? RETURN_OK : RETURN_FAIL;
    } else if (IsExecutable(fileName, fileLock)) {
        /* It's an executable - check if it's a binary asset */
        if (IsBinaryAsset(fileName)) {
            Printf("Open: Skipping binary asset: %s\n", fileName);
            result = RETURN_OK; /* Not an error, just skipped */
        } else {
            /* Launch the executable */
            result = OpenExecutable(fileName) ? RETURN_OK : RETURN_FAIL;
        }
    } else {
        /* It's a data file - open with appropriate tool */
        result = OpenDataFile(fileName, fileLock, forceTool, forceBrowse, forceEdit, forceInfo, forcePrint, forceMail) ? RETURN_OK : RETURN_FAIL;
    }
    
    /* Cleanup */
    UnLock(fileLock);
    
    return result;
}

/* Check if item is a drawer */
BOOL IsDrawer(STRPTR fileName, BPTR fileLock)
{
    struct FileInfoBlock *fib = NULL;
    BOOL result = FALSE;
    
    if (!fileLock) {
        return FALSE;
    }
    
    fib = (struct FileInfoBlock *)AllocMem(sizeof(struct FileInfoBlock), MEMF_CLEAR);
    if (!fib) {
        return FALSE;
    }
    
    if (Examine(fileLock, fib)) {
        if (fib->fib_DirEntryType == ST_USERDIR) {
            result = TRUE;
        }
    }
    
    FreeMem(fib, sizeof(struct FileInfoBlock));
    
    return result;
}

/* Check if item is an executable */
BOOL IsExecutable(STRPTR fileName, BPTR fileLock)
{
    struct FileInfoBlock *fib = NULL;
    BOOL result = FALSE;
    STRPTR filePart = NULL;
    
    if (!fileLock) {
        return FALSE;
    }
    
    fib = (struct FileInfoBlock *)AllocMem(sizeof(struct FileInfoBlock), MEMF_CLEAR);
    if (!fib) {
        return FALSE;
    }
    
    if (Examine(fileLock, fib)) {
        /* Check if it's a file (not a drawer) */
        if (fib->fib_DirEntryType == ST_FILE) {
            /* Check if executable bit is set */
            if (fib->fib_Protection & FIBF_EXECUTE) {
                result = TRUE;
            } else {
                /* Check if it's in C: directory (CLI commands) */
                filePart = FilePart(fileName);
                if (filePart && strncmp(fileName, "C:", 2) == 0) {
                    result = TRUE;
                }
            }
        }
    }
    
    FreeMem(fib, sizeof(struct FileInfoBlock));
    
    return result;
}

/* Check if file is a binary asset that shouldn't be executed */
BOOL IsBinaryAsset(STRPTR fileName)
{
    STRPTR filePart = NULL;
    STRPTR ext = NULL;
    LONG i = 0;
    
    if (!fileName) {
        return FALSE;
    }
    
    filePart = FilePart(fileName);
    if (!filePart) {
        return FALSE;
    }
    
    /* Find the extension */
    ext = strrchr(filePart, '.');
    if (!ext) {
        return FALSE;
    }
    
    /* Check against binary asset list */
    i = 0;
    while (binaryAssets[i] != NULL) {
        if (Stricmp(ext, binaryAssets[i]) == 0) {
            return TRUE;
        }
        i++;
    }
    
    return FALSE;
}

/* Check if file is a .info file */
BOOL IsInfoFile(STRPTR fileName)
{
    STRPTR filePart = NULL;
    STRPTR ext = NULL;
    LONG nameLen = 0;
    
    if (!fileName) {
        return FALSE;
    }
    
    filePart = FilePart(fileName);
    if (!filePart) {
        return FALSE;
    }
    
    nameLen = strlen(filePart);
    
    /* Check if filename ends with .info */
    if (nameLen > 5) {
        ext = filePart + nameLen - 5;
        if (Stricmp(ext, ".info") == 0) {
            return TRUE;
        }
    }
    
    return FALSE;
}

/* Open a drawer in Workbench */
BOOL OpenDrawer(STRPTR drawerPath, BOOL showAll)
{
    struct TagItem tags[3];
    LONG tagIndex = 0;
    BOOL success = FALSE;
    LONG errorCode = 0;
    
    if (!drawerPath || !WorkbenchBase) {
        return FALSE;
    }
    
    /* Add SHOWALL tag if requested */
    if (showAll) {
        tags[tagIndex].ti_Tag = WBOPENA_Show;
        tags[tagIndex].ti_Data = DDFLAGS_SHOWALL;
        tagIndex++;
    }
    
    tags[tagIndex].ti_Tag = TAG_DONE;
    
    /* Clear any previous error */
    SetIoErr(0);
    
    /* Open the drawer */
    success = OpenWorkbenchObjectA(drawerPath, tags);
    errorCode = IoErr();
    
    if (!success || errorCode != 0) {
        Printf("Open: Failed to open drawer: %s\n", drawerPath);
        if (errorCode != 0) {
            PrintFault(errorCode, "Open");
        }
        return FALSE;
    }
    
    return TRUE;
}

/* Open an executable */
BOOL OpenExecutable(STRPTR execPath)
{
    struct TagItem tags[1];
    BOOL success = FALSE;
    LONG errorCode = 0;
    
    if (!execPath || !WorkbenchBase) {
        return FALSE;
    }
    
    tags[0].ti_Tag = TAG_DONE;
    
    /* Clear any previous error */
    SetIoErr(0);
    
    /* Launch the executable */
    success = OpenWorkbenchObjectA(execPath, tags);
    errorCode = IoErr();
    
    if (!success || errorCode != 0) {
        Printf("Open: Failed to launch executable: %s\n", execPath);
        if (errorCode != 0) {
            PrintFault(errorCode, "Open");
        }
        return FALSE;
    }
    
    return TRUE;
}

/* Open a .info file using WBInfo() */
BOOL OpenInfoFile(STRPTR fileName, BPTR fileLock)
{
    BPTR parentLock = NULL;
    STRPTR filePart = NULL;
    STRPTR iconName = NULL;
    struct Screen *workbenchScreen = NULL;
    ULONG result = FALSE;
    LONG nameLen = 0;
    LONG allocSize = 0;
    
    if (!fileName || !fileLock || !WorkbenchBase || !IntuitionBase) {
        return FALSE;
    }
    
    /* Get the parent directory lock and filename part */
    filePart = FilePart(fileName);
    if (!filePart) {
        return FALSE;
    }
    
    /* Remove .info extension from filename */
    nameLen = strlen(filePart);
    if (nameLen > 5 && Stricmp(filePart + nameLen - 5, ".info") == 0) {
        /* Allocate buffer for icon name without .info extension */
        /* nameLen - 4 gives us space for the name without ".info" + null terminator */
        allocSize = nameLen - 4;
        iconName = (STRPTR)AllocMem(allocSize, MEMF_CLEAR);
        if (iconName) {
            /* Copy filename without .info extension */
            /* Copy nameLen - 5 characters (everything except last 5 chars which is ".info") */
            Strncpy((UBYTE *)iconName, (UBYTE *)filePart, nameLen - 4);
            /* Remove the '.' character by null-terminating at the correct position */
            iconName[nameLen - 5] = '\0';
        } else {
            return FALSE;
        }
    } else {
        /* Filename doesn't end with .info extension, use as-is */
        iconName = filePart;
        allocSize = 0; /* Not allocated */
    }
    
    if (!iconName) {
        return FALSE;
    }
    
    parentLock = ParentDir(fileLock);
    if (!parentLock) {
        /* For root volumes/devices, ParentDir may return NULL */
        /* In this case, we can't use WBInfo properly */
        if (iconName != filePart && allocSize > 0) {
            FreeMem(iconName, allocSize);
        }
        Printf("Open: Cannot get parent directory for: %s\n", fileName);
        return FALSE;
    }
    
    /* Get Workbench screen pointer */
    workbenchScreen = LockPubScreen("Workbench");
    if (!workbenchScreen) {
        /* Fallback to default public screen if Workbench not available */
        workbenchScreen = LockPubScreen(NULL);
    }
    
    if (!workbenchScreen) {
        UnLock(parentLock);
        if (iconName != filePart && allocSize > 0) {
            FreeMem(iconName, allocSize);
        }
        Printf("Open: Failed to get Workbench screen\n");
        return FALSE;
    }
    
    /* Call WBInfo() with Workbench screen and icon name without .info */
    /* WBInfo expects: parent directory lock, icon name (without .info), screen */
    /* Clear any previous error */
    SetIoErr(0);
    
    result = WBInfo(parentLock, iconName, workbenchScreen);
    
    /* Check for errors */
    if (!result) {
        LONG wbError = IoErr();
        Printf("Open: WBInfo failed for: %s\n", fileName);
        Printf("Open: Icon name used: %s\n", iconName);
        if (wbError != 0) {
            PrintFault(wbError, "Open");
        }
    }
    
    /* Unlock the screen */
    UnlockPubScreen(NULL, workbenchScreen);
    
    /* Unlock parent directory */
    UnLock(parentLock);
    
    /* Free allocated icon name if we created it */
    if (iconName != filePart && allocSize > 0) {
        FreeMem(iconName, allocSize);
    }
    
    if (!result) {
        return FALSE;
    }
    
    return TRUE;
}

/* Open a data file with appropriate tool */
BOOL OpenDataFile(STRPTR fileName, BPTR fileLock, STRPTR forceTool, BOOL forceBrowse, BOOL forceEdit, BOOL forceInfo, BOOL forcePrint, BOOL forceMail)
{
    STRPTR tool = NULL;
    STRPTR defIconsType = NULL;
    STRPTR defIconsTool = NULL;
    STRPTR datatypesTool = NULL;
    STRPTR iconTool = NULL;
    BPTR parentLock = NULL;
    STRPTR filePart = NULL;
    BOOL success = FALSE;
    UWORD preferredTool = TW_BROWSE; /* Default to BROWSE for viewing */
    
    if (!fileName || !fileLock) {
        return FALSE;
    }
    
    /* If force tool specified, use it directly */
    if (forceTool && *forceTool) {
        tool = forceTool;
    } else {
        /* Determine preferred tool type from flags */
        if (forceBrowse) {
            preferredTool = TW_BROWSE;
        } else if (forceEdit) {
            preferredTool = TW_EDIT;
        } else if (forceInfo) {
            preferredTool = TW_INFO;
        } else if (forcePrint) {
            preferredTool = TW_PRINT;
        } else if (forceMail) {
            preferredTool = TW_MAIL;
        }
        
        /* Try DefIcons method first (if DefIcons is running) */
        if (IconBase && IsDefIconsRunning()) {
            filePart = FilePart(fileName);
            parentLock = ParentDir(fileLock);
            if (parentLock) {
                defIconsType = GetDefIconsTypeIdentifier(filePart, parentLock);
                if (defIconsType && *defIconsType) {
                    defIconsTool = GetDefIconsDefaultTool(defIconsType);
                    if (defIconsTool && *defIconsTool) {
                        tool = defIconsTool;
                    }
                }
                UnLock(parentLock);
            }
        }
        
        /* If DefIcons didn't provide a tool, try datatypes.library */
        /* Note: We'll get the tool name for display, but use ToolNode for LaunchToolA */
        if (!tool && DataTypesBase) {
            datatypesTool = GetDatatypesTool(fileName, fileLock, preferredTool);
            if (datatypesTool && *datatypesTool) {
                tool = datatypesTool;
            }
        }
        
        /* If still no tool, try icon default tool */
        if (!tool) {
            iconTool = GetIconDefaultTool(fileName, fileLock);
            if (iconTool && *iconTool) {
                tool = iconTool;
            }
        }
    }
    
    /* Launch the tool */
    if (tool && *tool) {
        /* Check if tool came from DefIcons or icon (use OpenWorkbenchObjectA) */
        if (tool == defIconsTool || tool == iconTool) {
            struct TagItem tags[3];
            BPTR toolFileLock = NULL;
            STRPTR toolFilePart = NULL;
            
            /* Build tags for OpenWorkbenchObjectA */
            toolFileLock = Lock(fileName, ACCESS_READ);
            if (toolFileLock) {
                toolFilePart = FilePart(fileName);
                parentLock = ParentDir(toolFileLock);
                if (parentLock) {
                    tags[0].ti_Tag = WBOPENA_ArgLock;
                    tags[0].ti_Data = (ULONG)parentLock;
                    tags[1].ti_Tag = WBOPENA_ArgName;
                    tags[1].ti_Data = (ULONG)toolFilePart;
                    tags[2].ti_Tag = TAG_DONE;
                    
                    SetIoErr(0);
                    success = OpenWorkbenchObjectA(tool, tags);
                    if (!success || IoErr() != 0) {
                        Printf("Open: Failed to launch tool: %s\n", tool);
                        PrintFault(IoErr(), "Open");
                    }
                    
                    UnLock(parentLock);
                }
                UnLock(toolFileLock);
            }
        } else {
            /* Tool came from datatypes.library (use LaunchToolA) */
            struct DataType *dtn = NULL;
            struct ToolNode *tn = NULL;
            struct TagItem launchTags[2];
            
            /* Get the ToolNode (this keeps the DataType locked) */
            tn = GetDatatypesToolNode(fileName, fileLock, preferredTool, &dtn);
            if (tn && dtn) {
                /* LaunchToolA expects a Tool structure pointer */
                /* The ToolNode contains the Tool structure at tn_Tool */
                launchTags[0].ti_Tag = TAG_DONE;
                
                SetIoErr(0);
                success = LaunchToolA(&tn->tn_Tool, fileName, launchTags);
                if (!success || IoErr() != 0) {
                    Printf("Open: Failed to launch datatypes tool: %s\n", tool);
                    PrintFault(IoErr(), "Open");
                }
                
                /* Release the DataType (ToolNode is only valid while DataType is locked) */
                ReleaseDataType(dtn);
            } else {
                /* Fallback to OpenWorkbenchObjectA if we can't get ToolNode */
                struct TagItem tags[3];
                BPTR toolFileLock = NULL;
                STRPTR toolFilePart = NULL;
                
                toolFileLock = Lock(fileName, ACCESS_READ);
                if (toolFileLock) {
                    toolFilePart = FilePart(fileName);
                    parentLock = ParentDir(toolFileLock);
                    if (parentLock) {
                        tags[0].ti_Tag = WBOPENA_ArgLock;
                        tags[0].ti_Data = (ULONG)parentLock;
                        tags[1].ti_Tag = WBOPENA_ArgName;
                        tags[1].ti_Data = (ULONG)toolFilePart;
                        tags[2].ti_Tag = TAG_DONE;
                        
                        SetIoErr(0);
                        success = OpenWorkbenchObjectA(tool, tags);
                        if (!success || IoErr() != 0) {
                            Printf("Open: Failed to launch tool: %s\n", tool);
                            PrintFault(IoErr(), "Open");
                        }
                        
                        UnLock(parentLock);
                    }
                    UnLock(toolFileLock);
                }
            }
        }
        
        /* Free allocated tool strings */
        if (defIconsTool) {
            FreeVec(defIconsTool);
        }
        if (datatypesTool) {
            FreeVec(datatypesTool);
        }
        if (iconTool) {
            FreeVec(iconTool);
        }
    } else {
        Printf("Open: No tool found to open: %s\n", fileName);
        Printf("Open: Try installing DefIcons or configuring datatypes for this file type.\n");
        success = FALSE;
    }
    
    return success;
}

/* Check if DefIcons is running */
BOOL IsDefIconsRunning(VOID)
{
    struct MsgPort *port;
    
    if (!SysBase) {
        return FALSE;
    }
    
    port = FindPort("DEFICONS");
    
    if (port != NULL) {
        return TRUE;
    }
    return FALSE;
}

/* Get DefIcons type identifier */
STRPTR GetDefIconsTypeIdentifier(STRPTR fileName, BPTR fileLock)
{
    static UBYTE typeBuffer[256];
    struct TagItem tags[4];
    LONG errorCode = 0;
    struct DiskObject *icon = NULL;
    BPTR oldDir = NULL;
    
    if (!IconBase || !fileName) {
        return NULL;
    }
    
    typeBuffer[0] = '\0';
    
    if (fileLock != NULL) {
        oldDir = CurrentDir(fileLock);
    }
    
    tags[0].ti_Tag = ICONGETA_IdentifyBuffer;
    tags[0].ti_Data = (ULONG)typeBuffer;
    tags[1].ti_Tag = ICONGETA_IdentifyOnly;
    tags[1].ti_Data = TRUE;
    tags[2].ti_Tag = ICONA_ErrorCode;
    tags[2].ti_Data = (ULONG)&errorCode;
    tags[3].ti_Tag = TAG_DONE;
    
    icon = GetIconTagList(fileName, tags);
    
    if (icon) {
        FreeDiskObject(icon);
    }
    
    if (oldDir != NULL) {
        CurrentDir(oldDir);
    }
    
    if (errorCode == 0 && typeBuffer[0] != '\0') {
        return typeBuffer;
    }
    
    return NULL;
}

/* Get DefIcons default tool */
STRPTR GetDefIconsDefaultTool(STRPTR typeIdentifier)
{
    struct DiskObject *defaultIcon = NULL;
    STRPTR defaultTool = NULL;
    UBYTE defIconName[64];
    BPTR oldDir = NULL;
    BPTR envDir = NULL;
    
    if (!IconBase || !typeIdentifier || *typeIdentifier == '\0') {
        return NULL;
    }
    
    SNPrintf(defIconName, sizeof(defIconName), "def_%s", typeIdentifier);
    
    if ((envDir = Lock("ENV:Sys", SHARED_LOCK)) != NULL) {
        oldDir = CurrentDir(envDir);
        defaultIcon = GetDiskObject(defIconName);
        CurrentDir(oldDir);
        UnLock(envDir);
    }
    
    if (!defaultIcon && (envDir = Lock("ENVARC:Sys", SHARED_LOCK)) != NULL) {
        oldDir = CurrentDir(envDir);
        defaultIcon = GetDiskObject(defIconName);
        CurrentDir(oldDir);
        UnLock(envDir);
    }
    
    if (defaultIcon) {
        if (defaultIcon->do_DefaultTool != NULL) {
            if (defaultIcon->do_DefaultTool[0] != '\0') {
                UBYTE toolBuffer[256];
                ULONG toolLen;
                
                toolLen = strlen(defaultIcon->do_DefaultTool);
                Strncpy(toolBuffer, defaultIcon->do_DefaultTool, 256);
                
                defaultTool = AllocVec(toolLen + 1, MEMF_CLEAR);
                if (defaultTool) {
                    Strncpy((UBYTE *)defaultTool, toolBuffer, toolLen + 1);
                }
            }
        }
        
        FreeDiskObject(defaultIcon);
    }
    
    return defaultTool;
}

/* Get datatypes.library tool */
STRPTR GetDatatypesTool(STRPTR fileName, BPTR fileLock, UWORD preferredTool)
{
    struct DataType *dtn = NULL;
    struct ToolNode *tn = NULL;
    STRPTR tool = NULL;
    UWORD toolOrder[3];
    LONG i;
    
    if (!DataTypesBase || !fileName || !fileLock) {
        return NULL;
    }
    
    /* Obtain datatype for the file */
    dtn = ObtainDataTypeA(DTST_FILE, (APTR)fileLock, NULL);
    if (!dtn) {
        return NULL;
    }
    
    /* Determine tool order based on preference */
    if (preferredTool == TW_BROWSE) {
        toolOrder[0] = TW_BROWSE;
        toolOrder[1] = TW_EDIT;
        toolOrder[2] = TW_INFO;
    } else if (preferredTool == TW_EDIT) {
        toolOrder[0] = TW_EDIT;
        toolOrder[1] = TW_BROWSE;
        toolOrder[2] = TW_INFO;
    } else if (preferredTool == TW_INFO) {
        toolOrder[0] = TW_INFO;
        toolOrder[1] = TW_BROWSE;
        toolOrder[2] = TW_EDIT;
    } else if (preferredTool == TW_PRINT) {
        toolOrder[0] = TW_PRINT;
        toolOrder[1] = TW_BROWSE;
        toolOrder[2] = TW_EDIT;
    } else if (preferredTool == TW_MAIL) {
        toolOrder[0] = TW_MAIL;
        toolOrder[1] = TW_BROWSE;
        toolOrder[2] = TW_EDIT;
    } else {
        /* Default fallback */
        toolOrder[0] = TW_BROWSE;
        toolOrder[1] = TW_EDIT;
        toolOrder[2] = TW_INFO;
    }
    
    /* Try to find a tool in preferred order */
    for (i = 0; i < 3; i++) {
        struct TagItem tags[2];
        tags[0].ti_Tag = TOOLA_Which;
        tags[0].ti_Data = (ULONG)toolOrder[i];
        tags[1].ti_Tag = TAG_DONE;
        
        tn = FindToolNodeA(&dtn->dtn_ToolList, tags);
        if (tn) {
            if (tn->tn_Tool.tn_Program && *tn->tn_Tool.tn_Program) {
                ULONG toolLen = strlen(tn->tn_Tool.tn_Program) + 1;
                tool = AllocVec(toolLen, MEMF_CLEAR);
                if (tool) {
                    Strncpy((UBYTE *)tool, tn->tn_Tool.tn_Program, toolLen);
                    break;
                }
            }
        }
    }
    
    /* If no tool found with FindToolNodeA, try manual search */
    if (!tool) {
        struct Node *node;
        for (node = dtn->dtn_ToolList.lh_Head; node->ln_Succ; node = node->ln_Succ) {
            tn = (struct ToolNode *)node;
            if (tn->tn_Tool.tn_Program && *tn->tn_Tool.tn_Program) {
                /* Use first available tool */
                ULONG toolLen = strlen(tn->tn_Tool.tn_Program) + 1;
                tool = AllocVec(toolLen, MEMF_CLEAR);
                if (tool) {
                    Strncpy((UBYTE *)tool, tn->tn_Tool.tn_Program, toolLen);
                    break;
                }
            }
        }
    }
    
    ReleaseDataType(dtn);
    
    return tool;
}

/* Get datatypes ToolNode (keeps DataType locked - caller must ReleaseDataType) */
struct ToolNode *GetDatatypesToolNode(STRPTR fileName, BPTR fileLock, UWORD preferredTool, struct DataType **dtnOut)
{
    struct DataType *dtn = NULL;
    struct ToolNode *tn = NULL;
    UWORD toolOrder[3];
    LONG i;
    
    if (!DataTypesBase || !fileName || !fileLock || !dtnOut) {
        return NULL;
    }
    
    *dtnOut = NULL;
    
    /* Obtain datatype for the file */
    dtn = ObtainDataTypeA(DTST_FILE, (APTR)fileLock, NULL);
    if (!dtn) {
        return NULL;
    }
    
    /* Determine tool order based on preference */
    if (preferredTool == TW_BROWSE) {
        toolOrder[0] = TW_BROWSE;
        toolOrder[1] = TW_EDIT;
        toolOrder[2] = TW_INFO;
    } else if (preferredTool == TW_EDIT) {
        toolOrder[0] = TW_EDIT;
        toolOrder[1] = TW_BROWSE;
        toolOrder[2] = TW_INFO;
    } else if (preferredTool == TW_INFO) {
        toolOrder[0] = TW_INFO;
        toolOrder[1] = TW_BROWSE;
        toolOrder[2] = TW_EDIT;
    } else if (preferredTool == TW_PRINT) {
        toolOrder[0] = TW_PRINT;
        toolOrder[1] = TW_BROWSE;
        toolOrder[2] = TW_EDIT;
    } else if (preferredTool == TW_MAIL) {
        toolOrder[0] = TW_MAIL;
        toolOrder[1] = TW_BROWSE;
        toolOrder[2] = TW_EDIT;
    } else {
        /* Default fallback */
        toolOrder[0] = TW_BROWSE;
        toolOrder[1] = TW_EDIT;
        toolOrder[2] = TW_INFO;
    }
    
    /* Try to find a tool in preferred order */
    for (i = 0; i < 3; i++) {
        struct TagItem tags[2];
        tags[0].ti_Tag = TOOLA_Which;
        tags[0].ti_Data = (ULONG)toolOrder[i];
        tags[1].ti_Tag = TAG_DONE;
        
        tn = FindToolNodeA(&dtn->dtn_ToolList, tags);
        if (tn) {
            if (tn->tn_Tool.tn_Program && *tn->tn_Tool.tn_Program) {
                /* Found a valid tool - return it and keep DataType locked */
                *dtnOut = dtn;
                return tn;
            }
        }
    }
    
    /* If no tool found with FindToolNodeA, try manual search */
    if (!tn) {
        struct Node *node;
        for (node = dtn->dtn_ToolList.lh_Head; node->ln_Succ; node = node->ln_Succ) {
            tn = (struct ToolNode *)node;
            if (tn->tn_Tool.tn_Program && *tn->tn_Tool.tn_Program) {
                /* Found a valid tool - return it and keep DataType locked */
                *dtnOut = dtn;
                return tn;
            }
        }
    }
    
    /* No tool found - release DataType */
    ReleaseDataType(dtn);
    return NULL;
}

/* Get icon default tool */
STRPTR GetIconDefaultTool(STRPTR fileName, BPTR fileLock)
{
    struct DiskObject *icon = NULL;
    STRPTR defaultTool = NULL;
    BPTR parentLock = NULL;
    STRPTR filePart = NULL;
    BPTR oldDir = NULL;
    
    if (!IconBase || !fileName || !fileLock) {
        return NULL;
    }
    
    filePart = FilePart(fileName);
    parentLock = ParentDir(fileLock);
    
    if (parentLock) {
        oldDir = CurrentDir(parentLock);
        icon = GetDiskObject(filePart);
        CurrentDir(oldDir);
        
        if (icon) {
            if (icon->do_DefaultTool != NULL && icon->do_DefaultTool[0] != '\0') {
                ULONG toolLen = strlen(icon->do_DefaultTool) + 1;
                defaultTool = AllocVec(toolLen, MEMF_CLEAR);
                if (defaultTool) {
                    Strncpy((UBYTE *)defaultTool, icon->do_DefaultTool, toolLen);
                }
            }
            FreeDiskObject(icon);
        }
        
        UnLock(parentLock);
    }
    
    return defaultTool;
}

