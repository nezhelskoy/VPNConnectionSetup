#include <Windows.h>
#include <WindowsX.h>
#include <Commctrl.h>
#include <Ras.h>
#include <raserror.h>
#include <ShlObj.h>
#include <Shlwapi.h>
#include <strsafe.h>
#include "resource.h"

void ShowDetailedError(LPTSTR, BOOL, DWORD);
void FixIPSec();
void CreateLnkOnDesktop(const LPWSTR);
BOOL IsUserAdmin(VOID);
void CreateConnection();
void CloseApplication();

bool DlgOnInit(HWND, HWND, LPARAM);
void DlgOnCommand(HWND, int, HWND, UINT);
INT_PTR CALLBACK DlgAboutProc(HWND, UINT, WPARAM, LPARAM);
INT_PTR CALLBACK DialogProc(HWND, UINT, WPARAM, LPARAM);
