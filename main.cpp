#include "main.h"

LPWSTR connTitlePPTP = TEXT("Подключение к интернет (PPTP)");
LPWSTR connTitlePPPoE = TEXT("Подключение к интернет (PPPoE)");
LPWSTR connTitleL2TP = TEXT("Подключение к интернет (L2TP)");

HINSTANCE hInst = NULL; // идентификатор экземпляра программы
HWND      hwnd  = NULL; // хэндл главного окна программы
UINT IDR_STATUSBAR = 605;
OSVERSIONINFOEX os_ver;
int ver = 0;
BOOL isAdmin = false;

HWND hWndComboBox        = NULL;
HWND hWndLogin           = NULL;
HWND hWndPassword        = NULL;
HWND hWndCreateLnk       = NULL;
HWND hWndSaveCredentials = NULL;
HWND hWndRequestLogin    = NULL;

const int maxChars = 20;

//------------------------------------------------
//	Функция детального вывода сообщения об ошибке
//------------------------------------------------
void ShowDetailedError(LPTSTR lpszFunction, BOOL bExit, DWORD errorCode = -1)
/*  lpszFunction - имя функции, которая проверяется на ошибку
 *  bExit - флаг, если установлен, то производится завершение программы
 *  errorCode - если указано значение, отличное от -1,
 *              то используется код ошибки из данного аргумента,
 *              иначе код получается из GetLastError()
 */
{
	TCHAR szBuf[80];
	LPVOID lpMsgBuf;
	DWORD dw;
	if (errorCode == -1) {
		dw = GetLastError();
	}
	else {
		dw = errorCode;
	}
	FormatMessage(
		FORMAT_MESSAGE_ALLOCATE_BUFFER |
		FORMAT_MESSAGE_FROM_SYSTEM,
		NULL, dw,
		MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
		(LPTSTR) &lpMsgBuf, 0, NULL );
	wsprintf(szBuf, TEXT("%s - ошибка %d:\n%s"),
		lpszFunction, dw, lpMsgBuf);
	MessageBox(NULL, szBuf, TEXT("Ошибка"), MB_OK);
	LocalFree(lpMsgBuf);
	if(bExit) ExitProcess(dw);
}

void FixIPSec()
/*
 *  Для Windows XP требуется отключение IPSec при использовании L2TP соединений
 */
{
	HKEY  hKey             = NULL;
	LONG  lhRegSetValueRet = NULL;
	LONG  lhRegOpenRet     = NULL;
	DWORD keyValue         = 1;
	DWORD dwDisposition    = 0;

	/* старая версия кода, изменено на RegCreateKeyEx, т.к. параметр может не существовать.
	lhRegOpenRet = RegOpenKeyEx(
		HKEY_LOCAL_MACHINE,
		TEXT("System\\CurrentControlSet\\Services\\Rasman\\Parameters"),
		0,
		KEY_WRITE,
		&hKey
		);
	*/
	lhRegOpenRet = RegCreateKeyEx(HKEY_LOCAL_MACHINE,
		TEXT("System\\CurrentControlSet\\Services\\Rasman\\Parameters"),
		0, NULL, REG_OPTION_NON_VOLATILE,
		KEY_WRITE, NULL,
		&hKey, &dwDisposition
		);
	if (lhRegOpenRet != ERROR_SUCCESS) {
		ShowDetailedError(TEXT("RegOpenKeyEx"), false, (DWORD)lhRegOpenRet);
	}
	else {
		lhRegSetValueRet = RegSetValueEx(hKey, TEXT("ProhibitIpSec"), 0, REG_DWORD, (const BYTE *)&keyValue, sizeof(DWORD));
		if (lhRegSetValueRet != ERROR_SUCCESS) {
			ShowDetailedError(TEXT("RegSetValueEx"), false, (DWORD)lhRegSetValueRet);
		}
	}
	RegCloseKey(hKey);
}

void CreateLnkOnDesktop(const LPWSTR connTitle)
{
	IShellLink   *SLink;
	IPersistFile *PF;
	HRESULT HRes;
	TCHAR desktop_path[MAX_PATH] = TEXT("");
	TCHAR pszFullLnkPath[MAX_PATH]; 

	CoInitialize(NULL);

	ITEMIDLIST* pidl1 = NULL;
    SHGetFolderLocation(NULL, CSIDL_CONNECTIONS, NULL, 0, &pidl1);
    IShellFolder *desktop, *ncfolder;
    SHGetDesktopFolder(&desktop);
    desktop->BindToObject(pidl1, NULL, IID_IShellFolder, (void**)&ncfolder);

    IEnumIDList *items;
    ncfolder->EnumObjects(NULL, SHCONTF_NONFOLDERS, &items);
    ITEMIDLIST* pidl2 = NULL;
    while (S_OK == items->Next(1, &pidl2, NULL))
    {
        STRRET sr = {STRRET_WSTR};
        ncfolder->GetDisplayNameOf(pidl2, SHGDN_NORMAL, &sr);

        TCHAR buf[MAX_PATH] = TEXT("");
        StrRetToBuf(&sr, pidl2, buf, MAX_PATH);

        if (0 == StrCmpI(buf, connTitle))
        {
            ITEMIDLIST* pidl3 = ILCombine(pidl1, pidl2);
			HRESULT HRes = CoCreateInstance(CLSID_ShellLink, 0, CLSCTX_INPROC_SERVER, IID_IShellLink, ( LPVOID*)&SLink);
            SLink->SetIDList(pidl3);
			SHGetFolderPath(NULL, CSIDL_DESKTOP, NULL, 0, desktop_path);
			StringCbPrintf(pszFullLnkPath, MAX_PATH * sizeof(TCHAR), TEXT("%s\\%s.lnk"), desktop_path, connTitle);
			HRes = SLink->QueryInterface(IID_IPersistFile, (LPVOID*)&PF);
			HRes = PF->Save((LPCOLESTR)pszFullLnkPath, TRUE);
			PF->Release();
			SLink->Release();
            ILFree(pidl3);
            ILFree(pidl2);
            break;
        }

        ILFree(pidl2);
        pidl2 = NULL;
    }
	ncfolder->Release();
	desktop->Release();

    ILFree(pidl1);

	CoUninitialize();
}

void CreateConnection()
{
	const size_t NeedRebootMsgChars = 64;
	bool doDisableIPSec = false;
	LPWSTR connTitle = NULL;
	int typeConnection = (int)SendMessage(hWndComboBox, CB_GETCURSEL, 0, 0);

	WORD  cLoginLength                 = 0;
	TCHAR lpszLoginText[maxChars+1]    = {0};
	WORD  cPasswordLength              = 0;
	TCHAR lpszPasswordText[maxChars+1] = {0};
	// Получение текста логина и пароля
	cLoginLength    = (WORD)SendMessage(hWndLogin,    EM_LINELENGTH, 0, 0);
	cPasswordLength = (WORD)SendMessage(hWndPassword, EM_LINELENGTH, 0, 0);
	if (cLoginLength > maxChars) {
		MessageBox(hwnd, TEXT("Значение поля \"Пользователь\" не должно превышать 20-ти символов."), TEXT("Ошибка"), MB_OK);
		return;
	}
	if (cPasswordLength > maxChars) {
		MessageBox(hwnd, TEXT("Значение поля \"Пароль\" не должно превышать 20-ти символов."), TEXT("Ошибка"), MB_OK);
		return;
	}
	*((LPWORD)lpszLoginText)    = cLoginLength;
	*((LPWORD)lpszPasswordText) = cPasswordLength;
	SendMessage(hWndLogin,    EM_GETLINE, 0, (LPARAM)lpszLoginText);
	SendMessage(hWndPassword, EM_GETLINE, 0, (LPARAM)lpszPasswordText);
	// Корректное окончание строк (null-terminated)
	lpszLoginText[cLoginLength]       = 0;
	lpszPasswordText[cPasswordLength] = 0;

	// Первичная проверка версии ОС
	if (os_ver.dwPlatformId != VER_PLATFORM_WIN32_NT) {
		MessageBox(hwnd, TEXT("Устаревшая или нераспознанная версия операционной системы"), TEXT("Ошибка"), MB_OK);
		CloseApplication();
	}

	// Произвести отключение IPSec при успешном создании L2TP соединения
	if (ver == 51) { // Windows XP?
		if (typeConnection == 0) { // L2TP?
			if (isAdmin) { // Администратор?
				doDisableIPSec = true;
			}
			else {
				MessageBox(hwnd, TEXT("Для создания L2TP соединения в Windows XP\nнеобходимо запустить программу с правами администратора."), TEXT("Уведомление"), MB_OK);
				return;
			}
		}
	}

	// Получение размера структуры, независящее от версии ОС.
	int rasentry_struct_size = sizeof(RASENTRY);
	/* кусок старого кода, может, пригодится?
	if ((os_ver.dwMajorVersion == 5 && os_ver.dwMinorVersion == 0) || os_ver.dwMajorVersion <= 4) {
		// указание для старых ОС размера структуры RASENTRY
		rasentry_struct_size = 2088;
	}
	*/
	DWORD dwDeviceInfoSize = 0;
	DWORD dwRasEntryRet = RasGetEntryProperties(NULL, NULL, NULL, (LPDWORD)&rasentry_struct_size, NULL, &dwDeviceInfoSize);
	if (dwRasEntryRet == ERROR_RASMAN_CANNOT_INITIALIZE) {
		MessageBox(hwnd, TEXT("Не запущена служба \"Диспетчер подключений удалённого доступа\" (RasMan)\nЗапустите службу и повторите операцию."), TEXT("Уведомление"), MB_OK);
		return;
	}

	// Заполнение структуры настроек соединения
	RASENTRY rasEntry;
	ZeroMemory(&rasEntry, sizeof(RASENTRY));
	rasEntry.dwSize = rasentry_struct_size;
	rasEntry.dwfOptions = RASEO_RemoteDefaultGateway | RASEO_ModemLights |
			RASEO_SecureLocalFiles | RASEO_RequireMsEncryptedPw | RASEO_RequireDataEncryption |
			RASEO_RequireMsCHAP2 | RASEO_ShowDialingProgress;
	if (BST_CHECKED == SendMessage(hWndRequestLogin, BM_GETSTATE, 0, 0)) {
		rasEntry.dwfOptions = rasEntry.dwfOptions | RASEO_PreviewUserPw;
	}
	rasEntry.dwfOptions2 = RASEO2_DisableNbtOverIP | RASEO2_ReconnectIfDropped | RASEO2_Internet |
			RASEO2_DontNegotiateMultilink | RASEO2_SecureClientForMSNet | RASEO2_SecureFileAndPrint;
	rasEntry.dwRedialCount = 3;
	rasEntry.dwRedialPause = 60;
	rasEntry.dwFramingProtocol = RASFP_Ppp;
	rasEntry.dwfNetProtocols = RASNP_Ip;
	rasEntry.dwEncryptionType = ET_Optional; // ET_Require
	switch (typeConnection) {
	case 1: //PPPoE
		connTitle = connTitlePPPoE;
		wcscpy_s(rasEntry.szLocalPhoneNumber, 9, TEXT("in-doors"));
		wcscpy_s(rasEntry.szDeviceType, 6, RASDT_PPPoE);
		rasEntry.dwVpnStrategy = VS_Default;
		rasEntry.dwType = RASET_Broadband;
		break;
	case 2: // PPTP
		connTitle = connTitlePPTP;
		wcscpy_s(rasEntry.szLocalPhoneNumber, 21, TEXT("internet.in-doors.ru"));
		wcscpy_s(rasEntry.szDeviceType, 4, RASDT_Vpn);
		rasEntry.dwVpnStrategy = VS_PptpOnly;
		rasEntry.dwType = RASET_Vpn;
		break;
	case 0: // L2TP
	default:
		connTitle = connTitleL2TP;
		wcscpy_s(rasEntry.szLocalPhoneNumber, 17, TEXT("l2tp.in-doors.ru"));
		wcscpy_s(rasEntry.szDeviceType, 4, RASDT_Vpn);
		rasEntry.dwVpnStrategy = VS_L2tpOnly;
		rasEntry.dwType = RASET_Vpn;
		break;
	}
	//rasEntry.dwDialMode = RASEDM_DialAll;
	//wcscpy_s(rasEntry.szDeviceName, 21, TEXT("VPN"));

	//RASDIALPARAMS ras_param;
	RASCREDENTIALS ras_cred;

	// Непосредственно - создание соединения.
	dwRasEntryRet = RasSetEntryProperties(NULL, connTitle, &rasEntry, rasentry_struct_size, NULL, 0);
	switch (dwRasEntryRet) {
	case ERROR_ACCESS_DENIED:
		MessageBox(hwnd, TEXT("Не удалось создать подключение\nRasSetEntryProperties() - ERROR_ACCESS_DENIED"), TEXT("Ошибка"), MB_OK);
		break;
	case ERROR_BUFFER_INVALID:
		MessageBox(hwnd, TEXT("Не удалось создать подключение\nRasSetEntryProperties() - ERROR_BUFFER_INVALID"), TEXT("Ошибка"), MB_OK);
		break;
	case ERROR_CANNOT_OPEN_PHONEBOOK:
		MessageBox(hwnd, TEXT("Не удалось создать подключение\nRasSetEntryProperties() - ERROR_CANNOT_OPEN_PHONEBOOK"), TEXT("Ошибка"), MB_OK);
		break;
	case ERROR_INVALID_PARAMETER:
		MessageBox(hwnd, TEXT("Не удалось создать подключение\nRasSetEntryProperties() - ERROR_INVALID_PARAMETER"), TEXT("Ошибка"), MB_OK);
		break;
	case ERROR_SUCCESS:
		// Указание логина и пароля соединения. Необязательное действие, если используется RasSetCredentials().
		/*
		ZeroMemory(&ras_param, sizeof(RASDIALPARAMS));
		ras_param.dwSize = sizeof(RASDIALPARAMS);
		wcscpy_s(ras_param.szEntryName, wcslen(connTitle)+1, connTitle);
		wcscpy_s(ras_param.szUserName, 256, lpszLoginText);
		wcscpy_s(ras_param.szPassword, 256, lpszPasswordText);
		DWORD dwRasEntryParamsRet = RasSetEntryDialParams(0, &ras_param, FALSE);
		switch (dwRasEntryParamsRet) {
		case ERROR_BUFFER_INVALID:
			MessageBox(hwnd, TEXT("Не удалось задать логин и пароль\nRasSetEntryDialParams() - ERROR_BUFFER_INVALID"), TEXT("Ошибка"), MB_OK);
			break;
		case ERROR_CANNOT_OPEN_PHONEBOOK:
			MessageBox(hwnd, TEXT("Не удалось задать логин и пароль\nRasSetEntryDialParams() - ERROR_CANNOT_OPEN_PHONEBOOK"), TEXT("Ошибка"), MB_OK);
			break;
		case ERROR_CANNOT_FIND_PHONEBOOK_ENTRY:
			MessageBox(hwnd, TEXT("Не удалось задать логин и пароль\nRasSetEntryDialParams() - ERROR_CANNOT_FIND_PHONEBOOK_ENTRY"), TEXT("Ошибка"), MB_OK);
			break;
		case ERROR_SUCCESS:
		default:
			break;
		}
		*/

		// Управление логином и паролем соединения.
		ZeroMemory(&ras_cred, sizeof(RASCREDENTIALS));
		ras_cred.dwSize = sizeof(RASCREDENTIALS);
		ras_cred.dwMask = RASCM_UserName | RASCM_Password; // | RASCM_DefaultCreds;
		if (BST_CHECKED == SendMessage(hWndSaveCredentials, BM_GETSTATE, 0, 0)) {
			// сохранить логин и пароль
			wcscpy_s(ras_cred.szUserName, 256, lpszLoginText);
			wcscpy_s(ras_cred.szPassword, 256, lpszPasswordText);
			DWORD dwRasCredRet = RasSetCredentials(NULL, connTitle, &ras_cred, FALSE);
			switch (dwRasCredRet) {
			case ERROR_CANNOT_OPEN_PHONEBOOK:
				MessageBox(hwnd, TEXT("Не удалось сохранить логин и пароль\nRasSetCredentials() - ERROR_CANNOT_OPEN_PHONEBOOK"), TEXT("Ошибка"), MB_OK);
				break;
			case ERROR_CANNOT_FIND_PHONEBOOK_ENTRY:
				MessageBox(hwnd, TEXT("Не удалось сохранить логин и пароль\nRasSetCredentials() - ERROR_CANNOT_FIND_PHONEBOOK_ENTRY"), TEXT("Ошибка"), MB_OK);
				break;
			case ERROR_INVALID_PARAMETER:
				MessageBox(hwnd, TEXT("Не удалось сохранить логин и пароль\nRasSetCredentials() - ERROR_INVALID_PARAMETER"), TEXT("Ошибка"), MB_OK);
				break;
			case ERROR_INVALID_SIZE:
				MessageBox(hwnd, TEXT("Не удалось сохранить логин и пароль\nRasSetCredentials() - ERROR_INVALID_SIZE"), TEXT("Ошибка"), MB_OK);
				break;
			case ERROR_ACCESS_DENIED:
				MessageBox(hwnd, TEXT("Не удалось сохранить логин и пароль\nRasSetCredentials() - ERROR_ACCESS_DENIED"), TEXT("Ошибка"), MB_OK);
				break;
			case ERROR_SUCCESS:
			default:
				break;
			}
		}
		else {
			// очистить логин и пароль
			RasSetCredentials(NULL, connTitle, &ras_cred, TRUE);
		}

		TCHAR strNeedRebootStr[NeedRebootMsgChars];
		ZeroMemory(strNeedRebootStr, NeedRebootMsgChars * sizeof(TCHAR));

		if (doDisableIPSec) {
			FixIPSec();
			StringCbPrintf(strNeedRebootStr, NeedRebootMsgChars * sizeof(TCHAR), TEXT("\nТребуется перезагрузка компьютера."));
		}

		if (BST_CHECKED == SendMessage(hWndCreateLnk, BM_GETSTATE, 0, 0)) {
			CreateLnkOnDesktop(connTitle);
		}

		TCHAR lptstrSuccessMsg[256];
		StringCbPrintf(lptstrSuccessMsg, 256 * sizeof(TCHAR), TEXT("VPN соединение \"%s\" создано.%s"), connTitle, strNeedRebootStr);
		MessageBox(hwnd, lptstrSuccessMsg, TEXT("Уведомление"), MB_OK);
		break;
	default:
		int const arraysize = 254;
		TCHAR lptstrStatupOSPaert[arraysize];
		StringCbPrintf(lptstrStatupOSPaert, arraysize * sizeof(TCHAR), TEXT("Не удалось создать подключение\nRasSetEntryProperties() - Код ошибки: %d"), dwRasEntryRet);
		MessageBox(hwnd, lptstrStatupOSPaert, TEXT("Ошибка"), MB_OK);
		break;
	}
}

bool DlgOnInit(HWND hWndDlg, HWND hwndFocus, LPARAM lParam)
{
	hwnd = hWndDlg;

	hWndComboBox        = GetDlgItem(hwnd, IDC_VPN_TYPE);
	hWndLogin           = GetDlgItem(hwnd, IDC_LOGIN);
	hWndPassword        = GetDlgItem(hwnd, IDC_PASSWORD);
	hWndCreateLnk       = GetDlgItem(hwnd, IDC_CHECK_CREATE_LNK);
	hWndSaveCredentials = GetDlgItem(hwnd, IDC_SAVE_CREDENTIALS);
	hWndRequestLogin    = GetDlgItem(hwnd, IDC_REQUEST_LOGIN);

	ZeroMemory(&os_ver, sizeof(OSVERSIONINFOEX));
	os_ver.dwOSVersionInfoSize = sizeof(OSVERSIONINFOEX);

	// получение информации о версии ОС
	BOOL bOsVersionInfoEx = GetVersionEx((OSVERSIONINFO*) &os_ver);
	if (!bOsVersionInfoEx) {
		MessageBox(hwnd, TEXT("Проблема с определением версии операционной системы"), TEXT("Ошибка"), MB_OK);
		CloseApplication();
	}
	ver = (int)os_ver.dwMajorVersion * 10 + (int)os_ver.dwMinorVersion;

	HWND hwndStatus = CreateWindowEx(0, STATUSCLASSNAME, NULL,
        WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, hwnd, (HMENU)IDR_STATUSBAR, hInst, NULL);

	int statusParts[2] = {150, 300};
	SendMessage(hwndStatus, SB_SETPARTS, (WPARAM)2, (LPARAM)&statusParts);
	switch (ver) {
	case 50:
		SendMessage(hwndStatus, SB_SETTEXT, MAKEWPARAM(0, 0), (LPARAM)TEXT("Windows 2000"));
		break;
	case 51:
		SendMessage(hwndStatus, SB_SETTEXT, MAKEWPARAM(0, 0), (LPARAM)TEXT("Windows XP"));
		break;
	case 52:
		SendMessage(hwndStatus, SB_SETTEXT, MAKEWPARAM(0, 0), (LPARAM)TEXT("Windows Server 2003"));
		break;
	case 60:
		SendMessage(hwndStatus, SB_SETTEXT, MAKEWPARAM(0, 0), (LPARAM)TEXT("Windows Vista"));
		break;
	case 61:
		SendMessage(hwndStatus, SB_SETTEXT, MAKEWPARAM(0, 0), (LPARAM)TEXT("Windows 7"));
		break;
	default:
		SendMessage(hwndStatus, SB_SETTEXT, MAKEWPARAM(0, 0), (LPARAM)TEXT("Нераспознанная Windows"));
		break;
	}
	if (isAdmin) {
		SendMessage(hwndStatus, SB_SETTEXT, MAKEWPARAM(1, 0), (LPARAM)TEXT("Администратор"));
	}
	else {
		SendMessage(hwndStatus, SB_SETTEXT, MAKEWPARAM(1, 0), (LPARAM)TEXT("Пользователь"));
	}

	// определение иконки программы
	SetClassLong(hwnd, GCL_HICON, (LONG)LoadIcon(hInst, MAKEINTRESOURCE(IDI_LOGO)));

	// заполнение выпадающего списка
	SendMessage(hWndComboBox,(UINT)CB_ADDSTRING,(WPARAM)0,(LPARAM)TEXT("L2TP"));
	if (ver > 50) { // за исключением Windows 2000
		SendMessage(hWndComboBox,(UINT)CB_ADDSTRING,(WPARAM)0,(LPARAM)TEXT("PPPoE"));
	}
	SendMessage(hWndComboBox,(UINT)CB_ADDSTRING,(WPARAM)0,(LPARAM)TEXT("PPTP"));

	// выбран по умочанию первый элемент выпадающего списка
	SendMessage(hWndComboBox, CB_SETCURSEL, (WPARAM)0, (LPARAM)0);

	// ограничение на вводимое количество символов для логина и пароля
	SendMessage(hWndLogin, EM_SETLIMITTEXT, (WPARAM)maxChars, (LPARAM)0);
	SendMessage(hWndPassword, EM_SETLIMITTEXT, (WPARAM)maxChars, (LPARAM)0);

	// чекбокс отмечен по умолчанию
	CheckDlgButton(hwnd, IDC_CHECK_CREATE_LNK, 1);
	CheckDlgButton(hwnd, IDC_SAVE_CREDENTIALS, 1);

	return TRUE;
}

void DlgOnCommand(HWND hwnd, int id, HWND hwndCtl, UINT codeNotify)
{
	switch (id) {
	case IDOK:
		CreateConnection();
		break;
	case IDR_MENUITEM_ABOUT:
        if (DialogBox(hInst, MAKEINTRESOURCE(IDD_ABOUT), hwnd, (DLGPROC)DlgAboutProc) == IDOK) {

		}
        else {

		}
		break;
	case IDR_MENUITEM_EXIT:
	case IDCANCEL:
		CloseApplication();
		break;
	default:
		break;
	}
}

BOOL IsUserAdmin(VOID)
/*++ 
Routine Description: This routine returns TRUE if the caller's
process is a member of the Administrators local group. Caller is NOT
expected to be impersonating anyone and is expected to be able to
open its own process and process token. 
Arguments: None. 
Return Value: 
   TRUE - Caller has Administrators local group. 
   FALSE - Caller does not have Administrators local group. --
*/
{
	BOOL b;
	SID_IDENTIFIER_AUTHORITY NtAuthority = SECURITY_NT_AUTHORITY;
	PSID AdministratorsGroup; 
	b = AllocateAndInitializeSid(
		&NtAuthority,
		2,
		SECURITY_BUILTIN_DOMAIN_RID,
		DOMAIN_ALIAS_RID_ADMINS,
		0, 0, 0, 0, 0, 0,
		&AdministratorsGroup); 
	if(b) 
	{
		if (!CheckTokenMembership( NULL, AdministratorsGroup, &b)) 
		{
			 b = FALSE;
		} 
		FreeSid(AdministratorsGroup); 
	}
	return(b);
}

void CloseApplication()
{
	EndDialog(hwnd, 0);
}

INT_PTR CALLBACK DlgAboutProc(HWND hwndDlg, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
	switch (uMsg) {
	case WM_COMMAND:
		switch (LOWORD(wParam)) {
		case IDOK:
		case IDCANCEL:
			EndDialog(hwndDlg, wParam);
			return TRUE;
		default:
			return FALSE;
		}
	default:
		return FALSE;
	}
	return FALSE;
}

INT_PTR CALLBACK DialogProc(HWND hwndDlg, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
	switch (uMsg) {
		HANDLE_MSG(hwndDlg, WM_INITDIALOG, DlgOnInit);
		HANDLE_MSG(hwndDlg, WM_COMMAND, DlgOnCommand);
	default:
		return FALSE;
	}
}

int APIENTRY WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow)
{
	hInst = hInstance;
	InitCommonControls();
	isAdmin = IsUserAdmin();
	DialogBox(hInstance, MAKEINTRESOURCE(IDD_DIALOG_MAIN), NULL, DialogProc);
	return 0;
}
