/* -*- Mode: C; tab-width: 4; c-basic-offset: 4; indent-tabs-mode: nil -*- */
#include <windows.h>
#include <winuser.h>
#include <tchar.h>
#include <conio.h>
#include <stdio.h>
#include <io.h>

const int MILLISECONDS_IN_SECOND = 1000;

// Struct to hold config information.
typedef struct ConfigType
{
    // The time to wait for process after it is asked to terminate in milliseconds.
    int process_wait_timeout_msec_;
    // The command line to start process.
    CHAR *process_cmd_line_;
} Config;

static HANDLE g_child_std_in_rd;
static HANDLE g_child_std_in_wr;
static HANDLE g_child_std_out_rd;
static HANDLE g_child_std_out_wr;

static Config g_config;
static PROCESS_INFORMATION* g_pchild_proc_info;
static HANDLE g_hchild_tracking_thread;
static CRITICAL_SECTION g_tear_down_crit;

static int ParseParameters(int argc, CHAR* argv[], Config* pconfig);
static BOOL CreateChildProcess(Config* pconfig);
static void TearDownChildProcess(Config* pconfig);
static void TrackChildProcess(Config* pconfig);
static BOOL WINAPI OnConsoleCtrlEvent(DWORD ctrl_type);
static DWORD WINAPI WaitUntillChildProcessExits(LPVOID param);
static void ErrorExit(PTSTR);

int _tmain(int argc, CHAR *argv[]) {
    SECURITY_ATTRIBUTES saAttr;

    InitializeCriticalSection(&g_tear_down_crit);

    if (!SetConsoleCtrlHandler(OnConsoleCtrlEvent, TRUE)) {
        fprintf(stderr, "Parent: Failed to subscribe to console ctrl events\n");
        return -1;
    }

    if (ParseParameters(argc, argv, &g_config) != 0) {
        fprintf(stderr, "Failed to parse arguments\n");
        return -1;
    }

    // Set the bInheritHandle flag so pipe handles are inherited.
    saAttr.nLength = sizeof(SECURITY_ATTRIBUTES);
    saAttr.bInheritHandle = TRUE;
    saAttr.lpSecurityDescriptor = NULL;

    // Create a pipe for the child process's STDOUT.
    if (!CreatePipe(&g_child_std_out_rd, &g_child_std_out_wr, &saAttr, 0)) {
        ErrorExit(TEXT("StdoutRd CreatePipe"));
    }

    // Ensure the read handle to the pipe for STDOUT is not inherited.
    if (!SetHandleInformation(g_child_std_out_rd, HANDLE_FLAG_INHERIT, 0)) {
        ErrorExit(TEXT("Stdout SetHandleInformation"));
    }

    // Create a pipe for the child process's STDIN.
    if (!CreatePipe(&g_child_std_in_rd, &g_child_std_in_wr, &saAttr, 0)) {
        ErrorExit(TEXT("Stdin CreatePipe"));
    }

    // Ensure the write handle to the pipe for STDIN is not inherited.
    if (!SetHandleInformation(g_child_std_in_wr, HANDLE_FLAG_INHERIT, 0)) {
        ErrorExit(TEXT("Stdin SetHandleInformation"));
    }

    // Create the child process.
    if (CreateChildProcess(&g_config)) {
        TrackChildProcess(&g_config);
    }
    return 0;
}

// Parse command line parameters and store them in given config.
int ParseParameters(int argc, CHAR* argv[], Config* pconfig) {
    size_t currsize;
    int i;
    if (argc < 3) {
        return -1;
    }

    if (!pconfig) {
        return -2;
    }

    pconfig->process_wait_timeout_msec_ = atoi(argv[1]) * MILLISECONDS_IN_SECOND;
    pconfig->process_cmd_line_ = strdup(argv[2]);
    currsize = strlen(pconfig->process_cmd_line_) + 1;

    for (i = 3; i < argc; ++i) {
        size_t newsize = currsize + strlen(argv[i]) + 1;
        pconfig->process_cmd_line_ = realloc(pconfig->process_cmd_line_,
                                             newsize);
        if (pconfig->process_cmd_line_ == NULL) {
            fprintf(stderr, "Failed to allocate buffer\n");
            return -3;
        }
        sprintf(pconfig->process_cmd_line_ + currsize - 1, " %s",
                argv[i]);
        currsize = newsize;
    }

    pconfig->process_cmd_line_[currsize - 1] = '\0';

    return 0;
}

// Create a child process that uses the previously created pipes for STDIN and STDOUT.
BOOL CreateChildProcess(Config* pconfig) {
    PROCESS_INFORMATION* pproc_info;
    STARTUPINFOA start_info;
    BOOL success = FALSE;
    HANDLE parent_std_out = GetStdHandle(STD_OUTPUT_HANDLE);

    // Set up members of the PROCESS_INFORMATION structure.
    pproc_info = calloc(1, sizeof(PROCESS_INFORMATION));

    // Set up members of the _STARTUPINFOW structure.
    // This structure specifies the STDIN and STDOUT handles for redirection.

    ZeroMemory( &start_info, sizeof(STARTUPINFOA));
    start_info.cb = sizeof(STARTUPINFOA);
    start_info.hStdError = g_child_std_out_wr;
    start_info.hStdOutput = parent_std_out;
    start_info.hStdInput = g_child_std_in_rd;
    start_info.dwFlags |= STARTF_USESTDHANDLES;

    // Create the child process.
    success = CreateProcessA(NULL,
        pconfig->process_cmd_line_,     // command line
        NULL,          // process security attributes
        NULL,          // primary thread security attributes
        TRUE,          // handles are inherited
        CREATE_NEW_PROCESS_GROUP,             // creation flags
        NULL,          // use parent's environment
        NULL,          // use parent's current directory
        &start_info,  // _STARTUPINFOW pointer
        pproc_info);  // receives PROCESS_INFORMATION

    // If an error occurs, exit the application.
    if (!success) {
        free(pproc_info);
        g_pchild_proc_info = NULL;
        ErrorExit(TEXT("CreateProcess"));
    } else {
        g_pchild_proc_info = pproc_info;
    }

    return success;
}

// Posts WM_CLOSE to given hwnd if its process it matches child_pid.
BOOL CALLBACK TerminateAppEnum(HWND hwnd, LPARAM child_pid) {
    DWORD process_id;

    GetWindowThreadProcessId(hwnd, &process_id);

    if(process_id == (DWORD)child_pid) {
        PostMessage(hwnd, WM_CLOSE, 0, 0);
    }

    return TRUE;
}

// Asks child process to shutdown. If it is unable to shutdown after some time - kills it.
void TearDownChildProcess(Config* pconfig) {
    DWORD wfso_result;

    EnterCriticalSection(&g_tear_down_crit);
    if(g_pchild_proc_info) {
        // Post WM_CLOSE to all windows whose PID matches child process's.
        EnumWindows((WNDENUMPROC)TerminateAppEnum, (LPARAM)g_pchild_proc_info->dwProcessId);
        // Send ctrl + break event to ask app to close.
        GenerateConsoleCtrlEvent(CTRL_BREAK_EVENT, g_pchild_proc_info->dwProcessId);
        // Wait for process to exit.
        wfso_result = WaitForSingleObject(g_pchild_proc_info->hProcess,
                                          pconfig->process_wait_timeout_msec_);

        if (!wfso_result == WAIT_OBJECT_0) {
            if(wfso_result == WAIT_TIMEOUT) {
                fprintf(stderr, "\n->Timeout.\n");
            }
            fprintf(stderr, "Waiting for child process failed. Terminating child process.");
            if(!TerminateProcess(g_pchild_proc_info->hProcess, 1)) {
                ErrorExit(TEXT("Unable to terminate child process."));
            }
        }
        WaitForSingleObject(g_hchild_tracking_thread, INFINITE);
    }
    g_pchild_proc_info = NULL;
    LeaveCriticalSection(&g_tear_down_crit);
}

// Start a thread to track child process exit and read input.
// Stop when there is no more data, child process exited or EOF was sent to parent process.
void TrackChildProcess(Config* pconfig) {
    DWORD thread_id;
    int ch;

    // Close the write end of the pipe before reading from the
    // read end of the pipe, to control child process execution.
    // The pipe is assumed to have enough buffer space to hold the
    // data the child process has already written to it.
    if (!CloseHandle(g_child_std_out_wr)) {
        ErrorExit(TEXT("StdOutWr CloseHandle"));
    }

    // Create a thread
    g_hchild_tracking_thread = CreateThread(
                 NULL,         // default security attributes
                 0,            // default stack size
                 (LPTHREAD_START_ROUTINE) WaitUntillChildProcessExits,
                 NULL,         // no thread function arguments
                 0,            // default creation flags
                 &thread_id); // receive thread identifier

    if(g_hchild_tracking_thread == NULL) {
        ErrorExit(TEXT("CreateThread error"));
    }

    do {
        ch = getc(stdin);
    } while (ch != EOF && ch != '\n');

    TearDownChildProcess(pconfig);
}

// Fires when our process is asked to close.
BOOL WINAPI OnConsoleCtrlEvent(DWORD ctrl_type) {
    (void)ctrl_type;
    TearDownChildProcess(&g_config);
    return FALSE;
}

// Waits until process exits.
DWORD WINAPI WaitUntillChildProcessExits(LPVOID param) {
    DWORD exit_code;

    WaitForSingleObject(g_pchild_proc_info->hProcess, INFINITE);
    if(!GetExitCodeProcess(g_pchild_proc_info->hProcess, &exit_code)) {
        exit_code = 1;
    }
    ExitProcess(exit_code);
    return 1;
}

// Format a readable error message, write to stderr,
// and exit from the application.
void ErrorExit(PTSTR lpszFunction) {
    LPVOID msg_buf;
    DWORD last_error = GetLastError();

    FormatMessageA(FORMAT_MESSAGE_ALLOCATE_BUFFER |
                   FORMAT_MESSAGE_FROM_SYSTEM |
                   FORMAT_MESSAGE_IGNORE_INSERTS,
                   NULL,
                   last_error,
                   MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
                   (LPSTR) &msg_buf,
                   0, NULL);

    fprintf(stderr, "%s failed with error %d: %s", lpszFunction,
            (int)last_error, (const char*)msg_buf);

    LocalFree(msg_buf);
    ExitProcess(-1);
}
