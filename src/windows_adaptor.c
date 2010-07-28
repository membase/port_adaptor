#include <windows.h> 
#include <tchar.h>
#include <conio.h>
#include <stdio.h>
#include <io.h>

#define BUFSIZE 4096 
//#define DEBUG
//#define SHOW_MESSAGE_BOX_ON_ERROR_EXIT

// struct to hold congig information.
typedef struct ConfigType
{
    // The time to wait for process after it is asked to terminate.
    int process_wait_timeout_;
    // The command line to start process.
    CHAR *process_cmd_line_;
} Config;

HANDLE g_child_std_in_rd = NULL;
HANDLE g_child_std_in_wr = NULL;
HANDLE g_child_std_out_rd = NULL;
HANDLE g_child_std_out_wr = NULL;

Config g_config;
PROCESS_INFORMATION* g_pchild_proc_info = NULL; 
FILE* g_log_file;

int ParseParameters(int argc, CHAR* argv[], Config* pconfig);
BOOL CreateChildProcess(Config* pconfig); 
void TearDownChildProcess(PROCESS_INFORMATION* pproc_info, Config* pconfig);
void WriteToPipe(HANDLE input_file); 
void TrackChildProcess(PROCESS_INFORMATION* pchild_proc_info, Config* pconfig); 
BOOL WINAPI OnConsoleCtrlEvent(DWORD ctrl_type);
void ReleaseAllOnExit();
void ErrorExit(PTSTR); 
void PrintDebug(const char* debug_Info, ...);

int _tmain(int argc, CHAR *argv[]) {
    SECURITY_ATTRIBUTES saAttr; 

    if(!SetConsoleCtrlHandler(OnConsoleCtrlEvent, TRUE)) {
        PrintDebug("Parent: Failed to subscribe to console ctrl events");
        return -1;
    }

    if(ParseParameters(argc, argv, &g_config) != 0) {
        PrintDebug("Failed to parse arguments");
        return -1;
    }

    PrintDebug("\n->Start of parent execution.\n");

    // Set the bInheritHandle flag so pipe handles are inherited. 

    saAttr.nLength = sizeof(SECURITY_ATTRIBUTES); 
    saAttr.bInheritHandle = TRUE; 
    saAttr.lpSecurityDescriptor = NULL; 

    // Create a pipe for the child process's STDOUT. 
    if ( ! CreatePipe(&g_child_std_out_rd, &g_child_std_out_wr, &saAttr, 0) ) {
        ErrorExit(TEXT("StdoutRd CreatePipe")); 
    }

    // Ensure the read handle to the pipe for STDOUT is not inherited.
    if ( ! SetHandleInformation(g_child_std_out_rd, HANDLE_FLAG_INHERIT, 0) ) {
        ErrorExit(TEXT("Stdout SetHandleInformation")); 
    }

    // Create a pipe for the child process's STDIN. 
    if (! CreatePipe(&g_child_std_in_rd, &g_child_std_in_wr, &saAttr, 0)) {
        ErrorExit(TEXT("Stdin CreatePipe")); 
    }

    // Ensure the write handle to the pipe for STDIN is not inherited. 
    if ( ! SetHandleInformation(g_child_std_in_wr, HANDLE_FLAG_INHERIT, 0) ) {
        ErrorExit(TEXT("Stdin SetHandleInformation"));
    }

    // Create the child process. 
    if(CreateChildProcess(&g_config)) {
        PrintDebug( "\n->Process contents of child process STDOUT:\n\n", g_config.process_cmd_line_);
        TrackChildProcess(g_pchild_proc_info, &g_config);
        PrintDebug("\n->End of parent execution.\n");

        //// Close handles to the child process and its primary thread.
        //// Some applications might keep these handles to monitor the status
        //// of the child process, for example. 

        //CloseHandle(g_pchild_proc_info->hProcess);
        //CloseHandle(g_pchild_proc_info->hThread);

        ReleaseAllOnExit();
    }
    return 0; 
} 

// Parse command line parameters and store them in given config.
int ParseParameters(int argc, CHAR* argv[], Config* pconfig) {
    if(!argv) return - 1;
    if(!pconfig) return - 2;

    if (argc == 1) 
    ErrorExit(TEXT("Please specify an input file.\n"));

    pconfig->process_cmd_line_ = argv[1];

    pconfig->process_wait_timeout_ = 7000;
    if(argc > 2) {
        pconfig->process_wait_timeout_ = atoi(argv[2]);
    }

    return 0;
}

// Create a child process that uses the previously created pipes for STDIN and STDOUT.
BOOL CreateChildProcess(Config* pconfig) { 
    //wchar_t szCmdline[]= TEXT("JustWritingOutput.exe");
    PROCESS_INFORMATION* pproc_info;
    STARTUPINFOA start_info;
    BOOL success = FALSE; 

    // Set up members of the PROCESS_INFORMATION structure. 

    pproc_info = malloc(sizeof(PROCESS_INFORMATION));
    ZeroMemory( pproc_info, sizeof(PROCESS_INFORMATION) );

    // Set up members of the _STARTUPINFOW structure. 
    // This structure specifies the STDIN and STDOUT handles for redirection.

    ZeroMemory( &start_info, sizeof(STARTUPINFOA) );
    start_info.cb = sizeof(STARTUPINFOA); 
    start_info.hStdError = g_child_std_out_wr;
    start_info.hStdOutput = g_child_std_out_wr;
    start_info.hStdInput = g_child_std_in_rd;
    start_info.dwFlags |= STARTF_USESTDHANDLES;

    // Create the child process. 
    PrintDebug(pconfig->process_cmd_line_);
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
    if ( ! success ) {
        free(pproc_info);
        g_pchild_proc_info = NULL;
        ErrorExit(TEXT("CreateProcess"));
    }
    else {
        g_pchild_proc_info = pproc_info;
    }

    return success;
}

// Asks child process to shutdown. If it is unable to shutdown after some time - kills it.
void TearDownChildProcess(PROCESS_INFORMATION* pproc_info, Config* pconfig) {
    DWORD wfso_result;

    if(!GenerateConsoleCtrlEvent(CTRL_BREAK_EVENT, pproc_info->dwProcessId)) {
        ErrorExit(TEXT("GenerateConsoleCtrlEvent CTRL_BREAK_EVENT"));
    }

    wfso_result = WaitForSingleObject(pproc_info->hProcess, pconfig->process_wait_timeout_);

    if(!wfso_result == WAIT_OBJECT_0) {
        if(wfso_result == WAIT_TIMEOUT) {
            PrintDebug("\n->Timeout.\n");
        }
        TerminateProcess(pproc_info->hProcess, 0);
    }
}

// Read from a file and write its contents to the pipe for the child's STDIN.
// Stop when there is no more data. 
void WriteToPipe(HANDLE input_file) { 
    DWORD number_of_bytes_read, number_of_bytes_written; 
    CHAR ch_buf[BUFSIZE];
    BOOL success = FALSE;

    for (;;) { 
        success = ReadFile(input_file, ch_buf, BUFSIZE, &number_of_bytes_read, NULL);
        if ( ! success || number_of_bytes_read == 0 ) { break; }

        success = WriteFile(g_child_std_in_wr, ch_buf, number_of_bytes_read, &number_of_bytes_written, NULL);
        if ( ! success ) { break; }
    } 

    // Close the pipe handle so the child process stops reading. 

    if ( ! CloseHandle(g_child_std_in_wr) ) {
        ErrorExit(TEXT("StdInWr CloseHandle"));
    }
} 

// Read output from the child process's pipe for STDOUT
// and write to the parent process's pipe for STDOUT. 
// Stop when there is no more data, child process exited or EOF was sent to parent process. 
void TrackChildProcess(PROCESS_INFORMATION* pchild_proc_info, Config* pconfig) { 
    DWORD number_of_bytes_read, number_of_bytes_written; 
    CHAR ch_buf_small[1]; 
    CHAR ch_buf_large[1000]; 
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
        
        /*if(GetExitCodeProcess(pproc_info->hProcess, &proc_exit_code)) {
            if(proc_exit_code != STILL_ACTIVE) {
                ErrorExit(TEXT("GetExitCodeProcess"));
                break;
            }
        }*/

        /*success = ReadFile(hParentStdIn, ch_buf, 1, &number_of_bytes_read, NULL);
        if ( ! success || number_of_bytes_read == 0 ) break;*/

        success = ReadFile( g_child_std_out_rd, ch_buf_small, 1, &number_of_bytes_read, NULL);
        if( ! success || number_of_bytes_read == 0 ) {
            break;
        }

        success = WriteFile(parent_std_out, ch_buf_small, 
        number_of_bytes_read, &number_of_bytes_written, NULL);

        _flushall();

        if (! success ) break; 
    } 

    if (conout_file) {
        fclose(conout_file);
    }

    if(should_end) {
        TearDownChildProcess(pchild_proc_info, pconfig);

        //success = ReadFile(g_child_std_out_rd, ch_buf_large, 1000, &number_of_bytes_read, NULL);
        //if( success && number_of_bytes_read != 0 ) {
        //    success = WriteFile(parent_std_out, ch_buf_large, 
        //    number_of_bytes_read, &number_of_bytes_written, NULL);
        //}
        //_flushall();
    }
} 

// Fires when our procss s asked to close.
BOOL WINAPI OnConsoleCtrlEvent(DWORD ctrl_type) {
    PrintDebug("Parent OnConsoleCtrlEvent");

    TearDownChildProcess(g_pchild_proc_info, &g_config);
    ReleaseAllOnExit();

    return FALSE;
}

// Free allocated resources.
void ReleaseAllOnExit() {
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
    LPVOID display_buf;
    DWORD last_error = GetLastError(); 

    FormatMessageA(
                FORMAT_MESSAGE_ALLOCATE_BUFFER | 
                FORMAT_MESSAGE_FROM_SYSTEM |
                FORMAT_MESSAGE_IGNORE_INSERTS,
                NULL,
                last_error,
                MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
                (LPSTR) &msg_buf,
                0, NULL );

    display_buf = (LPVOID)LocalAlloc(LMEM_ZEROINIT, (strlen((char*)msg_buf)+strlen((char*)lpszFunction)+40)*sizeof(char)); 
    wsprintfA((char*)display_buf, "%s failed with error %d: %s", lpszFunction, last_error, msg_buf);

    PrintDebug((char*)display_buf);
#ifdef SHOW_MESSAGE_BOX_ON_ERROR_EXIT
    MessageBoxA(NULL, (char*)display_buf, "Error", MB_OK); 
#endif
    LocalFree(msg_buf);
    LocalFree(display_buf);
    ExitProcess(1);
}

void PrintDebug(const char* debug_Info, ...) {
    int j;
    LPVOID display_buf;

#ifdef DEBUG
    //printf(debug_Info);

    if(g_log_file == NULL) {
        g_log_file = fopen("log.txt", "wt+");
    }

    if(g_log_file) {
        display_buf = (LPVOID)LocalAlloc(LMEM_ZEROINIT, 1000);
        wsprintfA((char*)display_buf, debug_Info);
        
        fwrite((char*)display_buf, sizeof(char), strlen((char*)display_buf), g_log_file); 
    }
#endif
}
