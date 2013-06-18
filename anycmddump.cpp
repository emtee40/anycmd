/*
anycmddump.cpp - Total Commander lister plugin text dumper

Copyright (C) 2012-2013 by Serge Lamikhov-Center

MIT License

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
THE SOFTWARE.
*/

#ifdef _MSC_VER
#define _SCL_SECURE_NO_WARNINGS
#endif

#define BUFSIZE                     4096
#define ITERATIONS_BEFORE_DLG_SHOWN   40
#define POLL_TIME_INTERVAL            50

#include <windows.h>
#include <string>

#include "anycmd.h"
#include "resource.h"

extern HINSTANCE hinst;
extern HWND      listWin;
extern char      command_string[];

BOOL         createTargetProcess( const char* cmd, unsigned int streams );
void         closeTargetProcess();
void         waitForCompletionThread();
DWORD WINAPI reader( LPVOID lpParameter );

#ifdef _M_X64
INT_PTR
#else
BOOL
#endif
CALLBACK dlgProc( HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam );

static PROCESS_INFORMATION piProcInfo; 

static HANDLE g_hChildStd_OUT_Rd = 0;
static HANDLE g_hChildStd_OUT_Wr = 0;

static std::string res;


std::string receive_text( const char* cmd, unsigned int streams )
{
    SECURITY_ATTRIBUTES saAttr; 

    saAttr.nLength              = sizeof(SECURITY_ATTRIBUTES); 
    saAttr.bInheritHandle       = TRUE; 
    saAttr.lpSecurityDescriptor = 0; 

    // Create a pipe for the child process's STDOUT
    if ( ! CreatePipe( &g_hChildStd_OUT_Rd, &g_hChildStd_OUT_Wr, &saAttr, 0 ) ) {
        return "Error on CreatePipe"; 
    }

    // Ensure the read handle to the pipe for STDOUT is not inherited.
    if ( ! SetHandleInformation(g_hChildStd_OUT_Rd, HANDLE_FLAG_INHERIT, 0) ) {
        return "StdOut SetHandleInformation";
    }

    // Create the child process and get its output 
    res = "";
    if ( createTargetProcess( cmd, streams ) ) {
        waitForCompletionThread();
    }
    closeTargetProcess();

    // Read the rest of output from the child process's pipe for STDOUT
    reader( 0 );

    return res;
}


BOOL createTargetProcess( const char* cmd, unsigned int streams )
{
    // Set up members of the PROCESS_INFORMATION structure. 
    ZeroMemory( &piProcInfo, sizeof( PROCESS_INFORMATION ) );

    // Set up members of the STARTUPINFO structure. 
    // This structure specifies the STDIN and STDOUT handles for redirection.
    STARTUPINFO siStartInfo;
    ZeroMemory( &siStartInfo, sizeof( STARTUPINFO ) );
    siStartInfo.cb          = sizeof( STARTUPINFO ); 
    siStartInfo.dwFlags    |= STARTF_USESTDHANDLES;
    siStartInfo.hStdInput   = GetStdHandle( STD_INPUT_HANDLE );
    if ( ( streams & ANYCMD_CATCH_STD_OUT ) != 0 ) {
        siStartInfo.hStdOutput = g_hChildStd_OUT_Wr;
    }
    else {
        siStartInfo.hStdOutput = GetStdHandle( STD_OUTPUT_HANDLE );
    }
    if ( ( streams & ANYCMD_CATCH_STD_ERR ) != 0 ) {
        siStartInfo.hStdError = g_hChildStd_OUT_Wr;
    }
    else {
        siStartInfo.hStdError = GetStdHandle( STD_ERROR_HANDLE );
    }

    // Create the child process. 
    BOOL ret = CreateProcess( NULL, 
        (LPSTR)cmd,       // command line 
        NULL,             // process security attributes 
        NULL,             // primary thread security attributes 
        TRUE,             // handles are inherited 
        CREATE_NO_WINDOW, // creation flags 
        NULL,             // use parent's environment 
        NULL,             // use parent's current directory 
        &siStartInfo,     // STARTUPINFO pointer 
        &piProcInfo );    // receives PROCESS_INFORMATION 

    return ret;
}


void closeTargetProcess()
{
    // Close handles to the child process and its primary thread.
    CloseHandle( piProcInfo.hProcess );
    CloseHandle( piProcInfo.hThread );
    CloseHandle( g_hChildStd_OUT_Wr );
}


void waitForCompletionDialogBox()
{
    HWND waitDialog = CreateDialog( hinst,
                                    MAKEINTRESOURCE( IDD_WAIT_DIALOG ),
                                    listWin,
                                    dlgProc );

    int count = 0;
    if( waitDialog != NULL ) {
        MSG msg;
        // To make the GetMessage() loop spin, send idle messages
        PostMessage( waitDialog, WM_ENTERIDLE, MSGF_DIALOGBOX, (LPARAM)waitDialog );
        while( GetMessage( &msg, NULL, 0, 0 ) )
        {
            if( !IsDialogMessage( waitDialog, &msg ) ) {
                TranslateMessage( &msg );
                DispatchMessage( &msg );
            }

            DWORD wait_state = WaitForSingleObject( piProcInfo.hProcess, POLL_TIME_INTERVAL );
            if ( wait_state == WAIT_OBJECT_0 || wait_state == WAIT_FAILED ) {
                break;
            }

            if ( ++count == ITERATIONS_BEFORE_DLG_SHOWN ) {
                ShowWindow( waitDialog, SW_SHOW );
            }
            else if ( count < ITERATIONS_BEFORE_DLG_SHOWN ) {
                PostMessage( waitDialog, WM_ENTERIDLE, MSGF_DIALOGBOX, (LPARAM)waitDialog );
            }
        }

        DestroyWindow( waitDialog );
    }
}


void waitForCompletionThread()
{ 
    DWORD  threadID;
    HANDLE thread     = CreateThread( 0, 0, reader, 0, 0, &threadID );
    
    if ( thread != 0 ) {
        waitForCompletionDialogBox();
#pragma warning(suppress: 6258)
        TerminateThread( thread, 0 );
    }
}


#ifdef _M_X64
INT_PTR
#else
BOOL
#endif
CALLBACK dlgProc( HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam )
{
    switch( msg ) {
    case WM_COMMAND:
        switch( LOWORD( wParam ) ) {
        case IDCANCEL:
            TerminateProcess( piProcInfo.hProcess, 9 );
            break;
        }
        break;
        
    default:
        return FALSE;
    }
    return TRUE;
}


DWORD WINAPI reader( LPVOID lpParameter )
{
    DWORD dwRead; 
    CHAR  chBuf[BUFSIZE]; 
    BOOL  bSuccess = TRUE;

    // Read output from the child process's pipe for STDOUT
    // Stop when there is no more data. 
    while (bSuccess) 
    { 
        bSuccess = ReadFile( g_hChildStd_OUT_Rd, chBuf, BUFSIZE, &dwRead, NULL );
        if( ! bSuccess || dwRead == 0 ) {
            break; 
        }
        res += std::string( chBuf, dwRead );
    }

    return 0;
}
