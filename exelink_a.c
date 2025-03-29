#include <windows.h>

//----------------------------------------------------------
// 標準エラーに文字列を出力するためのユーティリティ
//----------------------------------------------------------
static VOID OutputErrorMessageA(LPCSTR pMessage)
{
    HANDLE hError = GetStdHandle(STD_ERROR_HANDLE);
    if (hError == INVALID_HANDLE_VALUE) {
        return;
    }
    DWORD written = 0;
    // lstrlenA(pMessage) で長さを取得し、WriteFile で書き込み
    WriteFile(hError, pMessage, lstrlenA(pMessage), &written, NULL);
}

//----------------------------------------------------------
// エラーコードを表示するためのユーティリティ
//----------------------------------------------------------
static VOID OutputErrorMessageWithCode(LPCSTR pMessage, DWORD errorCode)
{
    CHAR buffer[128];
    // wsprintfA は Windows API で、CRT を使わずに文字列フォーマットを行える
    wsprintfA(buffer, "%s (ErrorCode=%lu)\r\n", pMessage, errorCode);
    OutputErrorMessageA(buffer);
}

//----------------------------------------------------------
// argv[0] 部分を読み飛ばしてコマンドラインを返す
//----------------------------------------------------------
static LPCSTR ShiftCommandLine(LPCSTR pCmdLine)
{
    BOOL isBackslashPreceding = FALSE;
    BOOL isInsideDoubleQuote  = FALSE;
    BOOL isAfterArgv0         = FALSE;

    for (int i = 0;; i++) {
        const CHAR ch = pCmdLine[i];
        if (isAfterArgv0) {
            switch (ch) {
            case ' ':
            case '\t':
                // argv[0] が終わった後の空白は飛ばす
                break;
            default:
                return pCmdLine + i;
            }
        } else if (isInsideDoubleQuote) {
            switch (ch) {
            case '\\':
                isBackslashPreceding = !isBackslashPreceding;
                break;
            case '"':
                if (!isBackslashPreceding) {
                    isInsideDoubleQuote = FALSE;
                } else {
                    isBackslashPreceding = FALSE;
                }
                break;
            case '\0':
                return pCmdLine + i;
            default:
                isBackslashPreceding = FALSE;
                break;
            }
        } else {
            switch (ch) {
            case '\\':
                isBackslashPreceding = !isBackslashPreceding;
                break;
            case '"':
                if (!isBackslashPreceding) {
                    isInsideDoubleQuote = TRUE;
                } else {
                    isBackslashPreceding = FALSE;
                }
                break;
            case ' ':
            case '\t':
                isAfterArgv0 = TRUE;
                break;
            case '\0':
                return pCmdLine + i;
            default:
                isBackslashPreceding = FALSE;
                break;
            }
        }
    }
}

//----------------------------------------------------------
// リソースの読み込み
//----------------------------------------------------------
static INT LoadEmbeddedResource(INT resourceId, LPVOID* ppResourceData, DWORD* pResourceSize)
{
    // リソースを検索
    HRSRC hResourceInfo = FindResourceA(NULL, MAKEINTRESOURCEA(resourceId), RT_RCDATA);
    if (!hResourceInfo) {
        OutputErrorMessageA("Failed to find resource.\r\n");
        return -1;
    }

    // リソースをロード
    HGLOBAL hResourceData = LoadResource(NULL, hResourceInfo);
    if (!hResourceData) {
        OutputErrorMessageA("Failed to load resource.\r\n");
        return -1;
    }

    // リソースサイズを取得
    DWORD resourceSize = SizeofResource(NULL, hResourceInfo);
    if (resourceSize == 0) {
        OutputErrorMessageA("Resource size is zero.\r\n");
        return -1;
    }
    *pResourceSize = resourceSize;

    // リソースデータへのポインタを得る
    LPVOID pLockRes = LockResource(hResourceData);
    if (!pLockRes) {
        OutputErrorMessageA("Failed to lock resource.\r\n");
        return -1;
    }
    *ppResourceData = pLockRes;

    return 0;
}

//----------------------------------------------------------
// エントリーポイント
//----------------------------------------------------------
int mainCRTStartup(void)
{
    // リソースから prefix 文字列を取得
    LPVOID pResource          = NULL;
    DWORD prefixLength        = 0;
    if (LoadEmbeddedResource(101, &pResource, &prefixLength) != 0) {
        OutputErrorMessageA("LoadEmbeddedResource failed.\r\n");
        return -1;
    }
    // ここで pResource は NUL終端されていない場合もあるので注意
    // （埋め込み時に NUL を含めておくなど要調整）

    // コマンドライン関連
    LPCSTR pCmdLine       = GetCommandLineA();
    LPCSTR pShiftedCmdLine = ShiftCommandLine(pCmdLine);
    SIZE_T shiftedCmdLineLength = (SIZE_T)lstrlenA(pShiftedCmdLine);

    // 新しいコマンドラインを作るバッファを確保
    // prefix + " " + (shift後のコマンドライン) + NUL
    SIZE_T newCmdLineSize = prefixLength + 1 + shiftedCmdLineLength + 1;
    HANDLE hProcessHeap   = GetProcessHeap();
    LPSTR pNewCmdLineBuffer = (LPSTR)HeapAlloc(
        hProcessHeap, HEAP_ZERO_MEMORY, newCmdLineSize
    );

    if (!pNewCmdLineBuffer) {
        OutputErrorMessageA("HeapAlloc failed.\r\n");
        return -1;
    }

    // prefix をコピー
    CopyMemory(pNewCmdLineBuffer, pResource, prefixLength);
    // スペースを挿入
    pNewCmdLineBuffer[prefixLength] = ' ';
    // 残りのコマンドラインをコピー
    CopyMemory(
        pNewCmdLineBuffer + prefixLength + 1,
        pShiftedCmdLine,
        shiftedCmdLineLength
    );
    // NUL終端
    pNewCmdLineBuffer[prefixLength + 1 + shiftedCmdLineLength] = '\0';

    // プロセス起動準備
    STARTUPINFOA startupInfo;
    ZeroMemory(&startupInfo, sizeof(startupInfo));
    startupInfo.cb = sizeof(startupInfo);

    PROCESS_INFORMATION processInfo;
    ZeroMemory(&processInfo, sizeof(processInfo));

    // プロセス起動
    BOOL isSuccess = CreateProcessA(
        NULL,
        pNewCmdLineBuffer,
        NULL,
        NULL,
        FALSE,
        0,
        NULL,
        NULL,
        &startupInfo,
        &processInfo
    );

    // もう不要なバッファは解放
    HeapFree(hProcessHeap, 0, pNewCmdLineBuffer);

    if (!isSuccess) {
        DWORD lastError = GetLastError();
        OutputErrorMessageWithCode("CreateProcess failed.", lastError);
        return -1;
    }

    // Ctrl+C ハンドラは無効化しておく（任意）
    SetConsoleCtrlHandler(NULL, TRUE);

    // 子プロセス終了待ち
    WaitForSingleObject(processInfo.hProcess, INFINITE);

    // Ctrl+C ハンドラを戻す（任意）
    SetConsoleCtrlHandler(NULL, FALSE);

    // 子プロセスの終了コードを取得して終了
    DWORD exitCode = 0;
    GetExitCodeProcess(processInfo.hProcess, &exitCode);
    CloseHandle(processInfo.hProcess);
    CloseHandle(processInfo.hThread);
    ExitProcess(exitCode);
    return (int)exitCode;
}
