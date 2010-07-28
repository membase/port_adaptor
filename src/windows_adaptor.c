/* -*- Mode: C; tab-width: 4; c-basic-offset: 4; indent-tabs-mode: nil -*- */
#include <windows.h> 
#include <tchar.h>
#include <conio.h>
#include <stdio.h>
#include <io.h>

#define BUFSIZE 4096 

// struct to hold congig information.
typedef struct ConfigType
{
    // The time to wait for process after it is asked to terminate.
    int process_wait_timeout_;
    // The command line to start process.
    CHAR *process_cmd_line_;
} Config;

static HANDLE g_child_std_in_rd = NULL;
static HANDLE g_child_std_in_wr = NULL;
static HANDLE g_child_std_out_rd = NULL;
static HANDLE g_child_std_out_wr = NULL;

static Config g_config;
static PROCESS_INFORMATION* g_pchild_proc_info = NULL;
static FILE* g_log_file;

static int ParseParameters(int argc, CHAR* argv[], Config* pconfig);
static BOOL CreateChildProcess(Config* pconfig);
static void TearDownChildProcess(PROCESS_INFORMATION* pproc_info, Config* pconfig);
static void TrackChildProcess(PROCESS_INFORMATION* pchild_proc_info, Config* pconfig);
static BOOL WINAPI OnConsoleCtrlEvent(DWORD ctrl_type);
static void ReleaseAllOnExit(void);
static void ErrorExit(PTSTR);

int _tmain(int argc, CHAR *argv[]) {
    SECURITY_ATTRIBUTES saAttr; 

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
        TrackChildProcess(g_pchild_proc_info, &g_config);
        ReleaseAllOnExit();
    }
    return 0; 
} 

// Parse command line parameters and store them in given config.
int ParseParameters(int argc, CHAR* argv[], Config* pconfig) {
    if (argc < 3) {
        return -1;
    }

    if (!pconfig) {
        return -2;
    }

    pconfig->process_wait_timeout_ = atoi(argv[1]);
    pconfig->process_cmd_line_ = strdup(argv[2]);
    size_t currsize = strlen(pconfig->process_cmd_line_) + 1;

    for (int i = 3; i < argc; ++i) {
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

    // Set up members of the PROCESS_INFORMATION structure. 
    pproc_info = malloc(sizeof(PROCESS_INFORMATION));
    ZeroMemory( pproc_info, sizeof(PROCESS_INFORMATION));

    // Set up members of the _STARTUPINFOW structure. 
    // This structure specifies the STDIN and STDOUT handles for redirection.

    ZeroMemory( &start_info, sizeof(STARTUPINFOA));
    start_info.cb = sizeof(STARTUPINFOA); 
    start_info.hStdError = g_child_std_out_wr;
    start_info.hStdOutput = g_child_std_out_wr;
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

// Asks child process to shutdown. If it is unable to shutdown after some time - kills it.
void TearDownChildProcess(PROCESS_INFORMATION* pproc_info, Config* pconfig) {
    DWORD wfso_result;

    if (!GenerateConsoleCtrlEvent(CTRL_BREAK_EVENT, pproc_info->dwProcessId)) {
        ErrorExit(TEXT("GenerateConsoleCtrlEvent CTRL_BREAK_EVENT"));
    }

    wfso_result = WaitForSingleObject(pproc_info->hProcess,
                                      pconfig->process_wait_timeout_);

    if (!wfso_result == WAIT_OBJECT_0) {
        if(wfso_result == WAIT_TIMEOUT) {
            fprintf(stderr, "\n->Timeout.\n");
        }
        TerminateProcess(pproc_info->hProcess, 0);
    }
}

// Read output from the child process's pipe for STDOUT
// and write to the parent process's pipe for STDOUT. 
// Stop when there is no more data, child process exited or EOF was sent to parent process. 
void TrackChildProcess(PROCESS_INFORMATION* pchild_proc_info, Config* pconfig) { 
    DWORD number_of_bytes_read, number_of_bytes_written; 
    CHAR ch_buf_small[1]; 
    /* CHAR ch_buf_large[1000];  */
    BOOL success = FALSE;
    HANDLE parent_std_out = GetStdHandle(STD_OUTPUT_HANDLE);
    FILE* conout_file;
    BOOL should_end = FALSE;
    //DWORD proc_exit_code;
    //HANDLE hParentStdIn = GetStdHandle(STD_INPUT_HANDLE);

    // Close the write end of the pipe before reading from the 
    // read end of the pipe, to control child process execution.
    // The pipe is assumed to have enough buffer space to hold the
    // data the child process has already written to it.

    if (!CloseHandle(g_child_std_out_wr)) {
        ErrorExit(TEXT("StdOutWr CloseHandle")); 
    }

    conout_file = fopen("CONOUT$", "r");
    
    for (;;) {
        if(_kbhit()) {
            int r = getc(conout_file);
            if(r == EOF) {
                should_end = TRUE;
                break;
            }
        }

        success = ReadFile( g_child_std_out_rd, ch_buf_small, 1, &number_of_bytes_read, NULL);
        if(!success || number_of_bytes_read == 0) {
            break;
        }

        success = WriteFile(parent_std_out, ch_buf_small, 
        number_of_bytes_read, &number_of_bytes_written, NULL);

        _flushall();

        if (! success) break;
    } 

    if (conout_file) {
        fclose(conout_file);
    }

    if (should_end) {
        TearDownChildProcess(pchild_proc_info, pconfig);
    }
} 

// Fires when our procss s asked to close.
BOOL WINAPI OnConsoleCtrlEvent(DWORD ctrl_type) {
    (void)ctrl_type;

    TearDownChildProcess(g_pchild_proc_info, &g_config);
    ReleaseAllOnExit();

    return FALSE;
}

// Free allocated resources.
void ReleaseAllOnExit(void) {
    if(g_pchild_proc_info) {
        free(g_pchild_proc_info);
        g_pchild_proc_info = NULL;
    }

    if(g_log_file) {
        fclose(g_log_file);
        g_log_file = NULL;
    }
}

// Format a readable error message, display a message box, 
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
    ExitProcess(1);
}
