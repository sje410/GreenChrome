﻿#include "GreenChrome.h"

//构造新命令行
void NewCommand(const wchar_t *iniPath,const wchar_t *exePath,const wchar_t *fullPath)
{
    std::vector <std::wstring> command_line;

    int nArgs;
    LPWSTR *szArglist = CommandLineToArgvW(GetCommandLineW(), &nArgs);
    for(int i=0; i<nArgs; i++)
    {
        // 保留原来参数
        command_line.push_back( QuotePathIfNeeded(szArglist[i]) );

        if(i==0) //在进程路径后面追加参数
        {
            wchar_t additional_parameter[MAX_SIZE];
            GetPrivateProfileSectionW(L"追加参数", additional_parameter, MAX_SIZE, iniPath);

            wchar_t *parameter_ptr = additional_parameter;
            while (parameter_ptr && *parameter_ptr)
            {
                std::wstring parameter_str = parameter_ptr;
                std::size_t equal = parameter_str.find(L"=");
                if (equal != std::wstring::npos)
                {
                    //含有等号
                    std::wstring parameter = parameter_str.substr(0, equal);

                    //扩展环境变量
                    std::wstring parameter_path = ExpandEnvironmentPath(parameter_str.substr(equal + 1));

                    //扩展%app%
                    ReplaceStringInPlace(parameter_path, L"%app%", exePath);

                    //组合参数
                    command_line.push_back( parameter + L"=" + QuotePathIfNeeded(parameter_path) );
                }
                else
                {
                    //添加到参数
                    command_line.push_back( parameter_str );
                }

                parameter_ptr += wcslen(parameter_ptr) + 1;
            }
        }

    }
    LocalFree(szArglist);

    //打开网页
    if(GetPrivateProfileInt(L"其它设置", L"首次运行", 1, iniPath)==1)
    {
        WritePrivateProfileString(L"其它设置", L"首次运行", L"0", iniPath);

        command_line.push_back(L"http://www.shuax.com/?from=greenchrome");
    }

    //是否是首先启动的dll
    int first_dll = false;
    CreateMutex(NULL, TRUE, L"{56A17F97-9F89-4926-8415-446649F25EB5}");
    if (GetLastError() != ERROR_ALREADY_EXISTS)
    {
         first_dll = true;
    }

    //启动的外部程序句柄
    std::vector <HANDLE> program_handles;

    //是否启用了老板键
    bool bosskey_start = false;

    if(first_dll)
    {
        wchar_t start_program[MAX_SIZE];
        GetPrivateProfileSectionW(L"启动时运行", start_program, MAX_SIZE, iniPath);

        wchar_t *program_ptr = start_program;
        while (program_ptr && *program_ptr)
        {
            //扩展环境变量
            std::wstring program = ExpandEnvironmentPath(program_ptr);

            //扩展%app%
            ReplaceStringInPlace(program, L"%app%", exePath);

            //运行程序
            HANDLE program_handle = RunExecute(program.c_str());
            if (program_handle)
            {
                program_handles.push_back(program_handle);
            }

            program_ptr += wcslen(program_ptr) + 1;
        }

        //老板键
        bosskey_start = Bosskey(iniPath);

        //检查更新
        wchar_t updater_path[MAX_PATH];
        GetPrivateProfileString(L"自动更新", L"更新器地址", L"", updater_path, MAX_PATH, iniPath);
        if(updater_path[0])
        {
            //扩展环境变量
            std::wstring updater = ExpandEnvironmentPath(updater_path);

            //扩展%app%
            ReplaceStringInPlace(updater, L"%app%", exePath);

            wchar_t check_version[MAX_PATH];
            GetPrivateProfileString(L"自动更新", L"检查版本", L"", check_version, MAX_PATH, iniPath);

            std::wstring parameters = QuotePathIfNeeded(updater) + L" " + QuotePathIfNeeded(exePath) + L" " + check_version;

            //运行程序
            RunExecute(parameters.c_str());
        }
    }

    //启动进程
    STARTUPINFOW si = {0};
    PROCESS_INFORMATION pi = {0};
    si.cb = sizeof(STARTUPINFO);

    std::wstring my_ommand_line;
    for(auto str : command_line)
    {
        my_ommand_line += str;
        my_ommand_line += L" ";
    }

    if (CreateProcessW(fullPath, (LPWSTR)my_ommand_line.c_str(), NULL, NULL, false, CREATE_NEW_CONSOLE | CREATE_UNICODE_ENVIRONMENT | CREATE_DEFAULT_ERROR_MODE, NULL, 0, &si, &pi))
    {
        wchar_t close_program[MAX_SIZE];
        GetPrivateProfileSectionW(L"关闭时运行", close_program, MAX_SIZE, iniPath);

        if(first_dll && (program_handles.size()!=0 || close_program[0] || bosskey_start))
        {
            //启动了外部程序时，首个进程不立刻退出，需要检测Chrome的关闭，然后杀掉外部程序
            //需要结束外部程序，也需要等待处理
            WaitForSingleObject(pi.hProcess, INFINITE);

            //结束附加启动程序
            if(GetPrivateProfileInt(L"其它设置", L"自动关闭", 0, iniPath)==1)
            {
                for(auto rogram_handle : program_handles)
                {
                    TerminateProcess(rogram_handle, 0);
                }
            }

            //退出时运行额外程序
            wchar_t *program_ptr = close_program;
            while (program_ptr && *program_ptr)
            {
                //扩展环境变量
                std::wstring program = ExpandEnvironmentPath(program_ptr);

                //扩展%app%
                ReplaceStringInPlace(program, L"%app%", exePath);

                //运行程序
                RunExecute(program.c_str(), SW_HIDE);

                program_ptr += wcslen(program_ptr) + 1;
            }
        }

        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
        ExitProcess(0);
    }
}

EXPORT ReleaseIni(const wchar_t *exePath, wchar_t *iniPath)
{
    //ini路径
    wcscpy(iniPath, exePath);
    wcscat(iniPath, L"\\GreenChrome.ini");

    //生成默认ini文件
    DWORD attribs = GetFileAttributes(iniPath);
    if(attribs == INVALID_FILE_ATTRIBUTES || (attribs & FILE_ATTRIBUTE_DIRECTORY) )
    {
        FILE *fp = _wfopen(iniPath, L"wb");
        if(fp)
        {
            //从资源中读取默认配置文件
            HRSRC res = FindResource(hInstance, L"CONFIG", L"INI");
            if (res)
            {
                HGLOBAL header = LoadResource(hInstance, res);
                if (header)
                {
                    const char *data = (const char*)LockResource(header);
                    DWORD size = SizeofResource(hInstance, res);
                    if (data)
                    {
                        fwrite(data, size, 1, fp);
                        UnlockResource(header);
                    }
                }
                FreeResource(header);
            }
            fclose(fp);
        }
        else
        {
            MessageBox(0, L"无法释放GreenChrome.ini配置文件，当前目录可能不可写！", L"警告", MB_ICONWARNING);
        }
    }
}

void GreenChrome()
{
    //exe全路径
    wchar_t fullPath[MAX_PATH];
    GetModuleFileNameW(NULL, fullPath, MAX_PATH);

    //exe路径
    wchar_t exePath[MAX_PATH];
    wcscpy(exePath, fullPath);
    PathRemoveFileSpecW(exePath);

    //生成默认ini文件
    wchar_t iniPath[MAX_PATH];
    ReleaseIni(exePath, iniPath);

    // 不让chrome使用SetAppIdForWindow
    // chromium/ui/base/win/shell.cc
    // 防止任务栏双图标
    if(GetPrivateProfileInt(L"其它设置", L"修复双图标", 0, iniPath)==1)
    {
        HMODULE shell32 = LoadLibrary(L"shell32.dll");
        if(shell32)
        {
            PBYTE SHGetPropertyStoreForWindow = (PBYTE)GetProcAddress(shell32, "SHGetPropertyStoreForWindow");
            if(SHGetPropertyStoreForWindow)
            {
                #ifdef _WIN64
                BYTE patch[] = {0xB8, 0x01, 0x00, 0x00, 0x00, 0xC3};//return S_FALSE);
                #else
                BYTE patch[] = {0xB8, 0x01, 0x00, 0x00, 0x00, 0xC2, 0x0C, 0x00};//return S_FALSE);
                #endif
                WriteMemory(SHGetPropertyStoreForWindow, patch, sizeof(patch));
            }
        }
    }

    // 不让chrome使用GetComputerNameW，GetVolumeInformationW
    // chromium/rlz/win/lib/machine_id_win.cc
    // 打造便携版chrome
    if(GetPrivateProfileInt(L"其它设置", L"便携化", 0, iniPath)==1)
    {
        HMODULE kernel32 = LoadLibrary(L"kernel32.dll");
        if(kernel32)
        {
            PBYTE GetComputerNameW = (PBYTE)GetProcAddress(kernel32, "GetComputerNameW");
            PBYTE GetVolumeInformationW = (PBYTE)GetProcAddress(kernel32, "GetVolumeInformationW");
            if(GetComputerNameW)
            {
                #ifdef _WIN64
                BYTE patch[] = {0x31, 0xC0, 0xC3};//return 0);
                #else
                BYTE patch[] = {0x31, 0xC0, 0xC2, 0x08, 0x00};//return 0);
                #endif
                WriteMemory(GetComputerNameW, patch, sizeof(patch));
            }
            if(GetVolumeInformationW)
            {
                #ifdef _WIN64
                BYTE patch[] = {0x31, 0xC0, 0xC3};//return 0);
                #else
                BYTE patch[] = {0x31, 0xC0, 0xC2, 0x20, 0x00};//return 0);
                #endif
                WriteMemory(GetVolumeInformationW, patch, sizeof(patch));
            }
        }
    }

    // 让IsChromeMetroSupported强制返回false
    // chromium/chrome/installer/util/shell_util.cc
    // 修复没有注册类错误
    if(GetPrivateProfileInt(L"其它设置", L"修复没有注册类", 0, iniPath)==1)
    {
        HMODULE kernel32 = LoadLibrary(L"kernel32.dll");
        if (kernel32)
        {
            PBYTE VerifyVersionInfoW = (PBYTE)GetProcAddress(kernel32, "VerifyVersionInfoW");
            if (VerifyVersionInfoW)
            {
            #ifdef _WIN64
                BYTE patch[] = {0x31, 0xC0, 0xC3};//return 0);
            #else
                BYTE patch[] = {0x31, 0xC0, 0xC2, 0x10, 0x00};//return 0);
            #endif
                WriteMemory(VerifyVersionInfoW, patch, sizeof(patch));
            }
        }
    }

    //标签页，书签，地址栏增强
    TabBookmark(iniPath);

    //父进程不是Chrome，则需要启动GreenChrome功能
    wchar_t parentPath[MAX_PATH];
    if(GetParentPath(parentPath) && _wcsicmp(parentPath, fullPath)!=0)
    {
        //根据配置文件插入额外的命令行参数
        NewCommand(iniPath, exePath, fullPath);
    }
}

EXTERNC BOOL WINAPI DllMain(HINSTANCE hModule, DWORD dwReason, LPVOID pv)
{
    if (dwReason == DLL_PROCESS_ATTACH)
    {
        hInstance = hModule;

        //保持系统dll原有功能
        LoadSysDll(hModule);

        //修改程序入口点为 GreenChrome
        InstallLoader();
    }
    return TRUE;
}
