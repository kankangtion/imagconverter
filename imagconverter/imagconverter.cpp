// imagconverter.cpp : 定义应用程序的入口点。
//
#define MAGICKCORE_STATIC
#define MAGICKWAND_STATIC

#include "framework.h"
#include "imagconverter.h"
#include <shlobj.h> // 用于文件夹选择对话框
#include "shellapi.h"
#include <commdlg.h> // 用于文件选择对话框
#include <map>
#include <vector>
#include <string>
#include <mutex>
#include <future>
#include <thread>
#include <shlwapi.h> // 用于路径操作
#include <filesystem> // 用于文件操作
#include <iostream>
#include <fstream>
#include <MagickWand/MagickWand.h>

namespace fs = std::filesystem;

#define MAX_LOADSTRING 100

// 全局变量:
HINSTANCE hInst;                                // 当前实例
WCHAR szTitle[MAX_LOADSTRING];                  // 标题栏文本
WCHAR szWindowClass[MAX_LOADSTRING];            // 主窗口类名

std::vector<std::wstring> importedFiles;       // 保存导入的文件路径

std::vector<std::wstring> outportedFiles;       // 保存导出的文件路径

std::wstring outputDirectory = L""; // 保存输出目录路径

// 全局互斥锁，用于日志文件的线程安全写入
std::mutex logMutex;

std::map<int, std::wstring> imgs = {
        {IDM_EXPORT_PNG, L"png"},
        {IDM_EXPORT_JPEG, L"jpeg"},
        {IDM_EXPORT_BMP, L"bmp"},
        {IDM_EXPORT_GIF, L"gif"},

        {IDM_EXPORT_WEBP, L"webp"},
        {IDM_EXPORT_JPG, L"jpg"},
        {IDM_EXPORT_TIF, L"tif"},
        {IDM_EXPORT_TIFF, L"tiff"},

        {IDM_EXPORT_PSD, L"psd"},
        {IDM_EXPORT_PDF, L"pdf"},
        {IDM_EXPORT_SVG, L"svg"},
        {IDM_EXPORT_HDR, L"hdr"},
};



// 此代码模块中包含的函数的前向声明:
ATOM                MyRegisterClass(HINSTANCE hInstance);
BOOL                InitInstance(HINSTANCE, int);
LRESULT CALLBACK    WndProc(HWND, UINT, WPARAM, LPARAM);
INT_PTR CALLBACK    About(HWND, UINT, WPARAM, LPARAM);
INT_PTR CALLBACK    OutputDialogProc(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam);



int APIENTRY wWinMain(_In_ HINSTANCE hInstance,
                     _In_opt_ HINSTANCE hPrevInstance,
                     _In_ LPWSTR    lpCmdLine,
                     _In_ int       nCmdShow)
{
    UNREFERENCED_PARAMETER(hPrevInstance);
    UNREFERENCED_PARAMETER(lpCmdLine);

    // TODO: 在此处放置代码。

    // 初始化全局字符串
    LoadStringW(hInstance, IDS_APP_TITLE, szTitle, MAX_LOADSTRING);
    LoadStringW(hInstance, IDC_IMAGCONVERTER, szWindowClass, MAX_LOADSTRING);
    MyRegisterClass(hInstance);

    // 执行应用程序初始化:
    if (!InitInstance (hInstance, nCmdShow))
    {
        return FALSE;
    }

    HACCEL hAccelTable = LoadAccelerators(hInstance, MAKEINTRESOURCE(IDC_IMAGCONVERTER));

    MSG msg;

    // 主消息循环:
    while (GetMessage(&msg, nullptr, 0, 0))
    {
        if (!TranslateAccelerator(msg.hwnd, hAccelTable, &msg))
        {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
    }

    return (int) msg.wParam;
}

std::string WStringToString(const std::wstring& wstr)
{
    int bufferSize = WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), -1, nullptr, 0, nullptr, nullptr);
    if (bufferSize == 0)
    {
        return "";
    }

    std::string str(bufferSize - 1, '\0');
    WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), -1, &str[0], bufferSize, nullptr, nullptr);
    return str;
}

void ConvertImage(const std::wstring& inputFile, const std::wstring& outputFile, const std::wstring& targetDirectory, std::ofstream& logFile, HWND hWnd)
{
    // 每个线程独立的 MagickWand 实例
    MagickWand* wand = NewMagickWand();

    try
    {
        // 将输入文件路径从 std::wstring 转换为 std::string
        std::string inputFilePath = WStringToString(inputFile);

        // 加载输入图片
        if (MagickReadImage(wand, inputFilePath.c_str()) == MagickFalse)
        {
            char* description;
            ExceptionType severity;
            description = MagickGetException(wand, &severity);

            // 写入错误信息到日志文件
            std::lock_guard<std::mutex> lock(logMutex);
            logFile << "Error loading image: " << inputFilePath << std::endl;
            logFile << "Error description: " << description << std::endl;

            description = (char*)MagickRelinquishMemory(description);
            MessageBox(hWnd, L"加载图片失败！", L"错误", MB_OK | MB_ICONERROR);
            return;
        }

        // 设置输出格式
        std::wstring extension = fs::path(outputFile).extension().wstring();
        if (!extension.empty())
        {
            MagickSetImageFormat(wand, WStringToString(extension.substr(1)).c_str()); // 去掉扩展名前的点
        }

        // 确定目标路径
        std::wstring targetPath = targetDirectory + L"\\" + fs::path(outputFile).filename().wstring();
        std::string targetPathStr = WStringToString(targetPath);

        // 保存图片到目标路径
        if (MagickWriteImage(wand, targetPathStr.c_str()) == MagickFalse)
        {
            std::lock_guard<std::mutex> lock(logMutex);
            logFile << "Error saving image: " << targetPathStr << std::endl;
            MessageBox(hWnd, L"保存图片失败！", L"错误", MB_OK | MB_ICONERROR);
            return;
        }
    }
    catch (...)
    {
        MessageBox(hWnd, L"图片转换过程中发生错误！", L"错误", MB_OK | MB_ICONERROR);
    }

    // 清理资源
    wand = DestroyMagickWand(wand);
}

void StartConversion(HWND hWnd)
{
    if (outportedFiles.empty())
    {
        MessageBox(hWnd, L"请先选择导出格式并生成输出文件列表！", L"提示", MB_OK | MB_ICONWARNING);
        return;
    }

    // 确定输出目录
    std::wstring targetDirectory = outputDirectory.empty() ? L"" : outputDirectory;
    if (targetDirectory.empty())
    {
        MessageBox(hWnd, L"请先设置输出目录！", L"提示", MB_OK | MB_ICONWARNING);
        return;
    }

    // 初始化 ImageMagick
    MagickWandGenesis();

    // 日志文件路径
    std::wstring logFilePath = targetDirectory + L"\\conversion_log.txt";
    auto tmp = WStringToString(logFilePath); // 将 std::wstring 转换为 std::string
    std::ofstream logFile(tmp, std::ios::app); // 以追加模式打开日志文件

    // 创建线程池
    std::vector<std::future<void>> futures;

    for (size_t i = 0; i < importedFiles.size(); ++i)
    {
        const auto& inputFile = importedFiles[i];
        const auto& outputFile = outportedFiles[i];

        // 使用 std::async 启动异步任务
        futures.push_back(std::async(std::launch::async, ConvertImage, inputFile, outputFile, targetDirectory, std::ref(logFile), hWnd));
    }

    // 等待所有线程完成
    for (auto& future : futures)
    {
        future.get();
    }

    // 清理资源
    MagickWandTerminus();

    // 关闭日志文件
    logFile.close();

    MessageBox(hWnd, L"图片转换完成！", L"提示", MB_OK | MB_ICONINFORMATION);
}

void ImportImages(HWND hWnd)
{
    // 配置文件选择对话框
    OPENFILENAME ofn;
    WCHAR szFile[MAX_PATH * 100] = { 0 }; // 用于存储选择的文件路径
    ZeroMemory(&ofn, sizeof(ofn));
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = hWnd;
    ofn.lpstrFilter = L"Image Files (*.jpg;*.jpeg;*.gif;*.png;*.bmp)\0*.jpg;*.jpeg;*.gif;*.png;*.bmp\0All Files (*.*)\0*.*\0";
    ofn.lpstrFile = szFile;
    ofn.nMaxFile = sizeof(szFile) / sizeof(WCHAR);
    ofn.Flags = OFN_ALLOWMULTISELECT | OFN_EXPLORER;
    ofn.lpstrTitle = L"导入图片";
    importedFiles.clear();
    // 显示文件选择对话框
    if (GetOpenFileName(&ofn))
    {
        // 解析选择的文件
        WCHAR* p = ofn.lpstrFile;
        std::wstring directory = p; // 第一个是目录
        p += directory.length() + 1;

        if (*p == 0) // 如果只选择了一个文件
        {
            importedFiles.push_back(directory);
        }
        else // 如果选择了多个文件
        {
            while (*p)
            {
                importedFiles.push_back(directory + L"\\" + p);
                p += wcslen(p) + 1;
            }
        }

        // 刷新主窗口显示
        InvalidateRect(hWnd, NULL, TRUE);
    }
}

void ExportImages(HWND hWnd, const std::wstring& format)
{
    outportedFiles.clear(); // 清空之前的输出文件列表

    for (const auto& file : importedFiles)
    {
        // 构造输出文件路径
        std::wstring outputFile = file;
        size_t pos = outputFile.find_last_of(L'.');
        if (pos != std::wstring::npos)
        {
            outputFile = outputFile.substr(0, pos); // 去掉原来的扩展名
        }
        outputFile += L"." + format; // 添加新的扩展名

        outportedFiles.push_back(outputFile);
    }

    // 刷新主窗口显示
    InvalidateRect(hWnd, NULL, TRUE);
}

//
//  函数: MyRegisterClass()
//
//  目标: 注册窗口类。
//
ATOM MyRegisterClass(HINSTANCE hInstance)
{
    WNDCLASSEXW wcex;

    wcex.cbSize = sizeof(WNDCLASSEX);

    wcex.style          = CS_HREDRAW | CS_VREDRAW;
    wcex.lpfnWndProc    = WndProc;
    wcex.cbClsExtra     = 0;
    wcex.cbWndExtra     = 0;
    wcex.hInstance      = hInstance;
    wcex.hIcon          = LoadIcon(hInstance, MAKEINTRESOURCE(IDI_IMAGCONVERTER));
    wcex.hCursor        = LoadCursor(nullptr, IDC_ARROW);
    wcex.hbrBackground  = (HBRUSH)(COLOR_WINDOW+1);
    wcex.lpszMenuName   = MAKEINTRESOURCEW(IDC_IMAGCONVERTER);
    wcex.lpszClassName  = szWindowClass;
    wcex.hIconSm        = LoadIcon(wcex.hInstance, MAKEINTRESOURCE(IDI_SMALL));

    return RegisterClassExW(&wcex);
}

//
//   函数: InitInstance(HINSTANCE, int)
//
//   目标: 保存实例句柄并创建主窗口
//
//   注释:
//
//        在此函数中，我们在全局变量中保存实例句柄并
//        创建和显示主程序窗口。
//
BOOL InitInstance(HINSTANCE hInstance, int nCmdShow)
{
   hInst = hInstance; // 将实例句柄存储在全局变量中

   HWND hWnd = CreateWindowW(szWindowClass, szTitle, WS_OVERLAPPEDWINDOW,
      CW_USEDEFAULT, 0, CW_USEDEFAULT, 0, nullptr, nullptr, hInstance, nullptr);

   if (!hWnd)
   {
      return FALSE;
   }

   ShowWindow(hWnd, nCmdShow);
   UpdateWindow(hWnd);

   return TRUE;
}

//
//  函数: WndProc(HWND, UINT, WPARAM, LPARAM)
//
//  目标: 处理主窗口的消息。
//
//  WM_COMMAND  - 处理应用程序菜单
//  WM_PAINT    - 绘制主窗口
//  WM_DESTROY  - 发送退出消息并返回
//
//
LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    std::wstring imgKey = L"";
    switch (message)
    {
    case WM_COMMAND:
        {
            int wmId = LOWORD(wParam);
            // 分析菜单选择:
            switch (wmId)
            {
            case IDM_ABOUT:
                DialogBox(hInst, MAKEINTRESOURCE(IDD_ABOUTBOX), hWnd, About);
                break;
                // 在主窗口中调用对话框
            case IDM_OUTPUT_DIRECTORY:
                DialogBox(hInst, MAKEINTRESOURCE(IDD_OUTPUT_DIALOG), hWnd, OutputDialogProc);
                break;
            case IDM_IMPORT_IMAGES:
                ImportImages(hWnd);
                break;
            case IDM_EXPORT_PNG:
            case IDM_EXPORT_JPEG:
            case IDM_EXPORT_BMP:
            case IDM_EXPORT_GIF:
            case IDM_EXPORT_WEBP:
            case IDM_EXPORT_JPG:
            case IDM_EXPORT_TIF:
            case IDM_EXPORT_TIFF:
            case IDM_EXPORT_PSD:
            case IDM_EXPORT_PDF:
            case IDM_EXPORT_SVG:
            case IDM_EXPORT_HDR:
                imgKey = imgs[wmId];
                ExportImages(hWnd, imgKey);
                break;
            case IDM_START_CONVERSION:
                StartConversion(hWnd);
                break;
            case IDM_EXIT:
                DestroyWindow(hWnd);
                break;
            default:
                return DefWindowProc(hWnd, message, wParam, lParam);
            }
        }
        break;
    case WM_PAINT:
        {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hWnd, &ps);

            // TODO: 在此处添加使用 hdc 的任何绘图代码...
            // 
            int y = 10; // 起始 Y 坐标
            auto str = L"输入的图片列表:";
            TextOut(hdc, 10, y, str, wcslen(str));
            y += 20;

            // 显示导入的文件列表
            for (const auto& file : importedFiles)
            {
                TextOut(hdc, 10, y, file.c_str(), file.length());
                y += 20;
            }

            // 显示导出的文件列表
            if (!outportedFiles.empty())
            {
                auto outStr = L"输出的图片列表:";
                TextOut(hdc, 10, y, outStr, wcslen(outStr));
                y += 20;

                for (const auto& file : outportedFiles)
                {
                    TextOut(hdc, 10, y, file.c_str(), file.length());
                    y += 20;
                }
            }

            EndPaint(hWnd, &ps);
        }
        break;
    case WM_DESTROY:
        PostQuitMessage(0);
        break;
    default:
        return DefWindowProc(hWnd, message, wParam, lParam);
    }
    return 0;
}


// 输出目录对话框的消息处理函数
INT_PTR CALLBACK OutputDialogProc(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam)
{
    static WCHAR szPath[MAX_PATH] = L""; // 保存自定义路径

    switch (message)
    {
    case WM_INITDIALOG:
        CheckRadioButton(hDlg, IDC_RADIO_ORIGINAL, IDC_RADIO_CUSTOM, IDC_RADIO_ORIGINAL);
        EnableWindow(GetDlgItem(hDlg, IDC_EDIT_PATH), FALSE);
        EnableWindow(GetDlgItem(hDlg, IDC_BUTTON_BROWSE), FALSE);
        return (INT_PTR)TRUE;

    case WM_COMMAND:
        switch (LOWORD(wParam))
        {
        case IDC_RADIO_ORIGINAL:
            EnableWindow(GetDlgItem(hDlg, IDC_EDIT_PATH), FALSE);
            EnableWindow(GetDlgItem(hDlg, IDC_BUTTON_BROWSE), FALSE);
            GetDlgItemText(hDlg, IDC_EDIT_PATH, szPath, MAX_PATH);
            outputDirectory = szPath; // 保存自定义路径
            break;

        case IDC_RADIO_CUSTOM:
            EnableWindow(GetDlgItem(hDlg, IDC_EDIT_PATH), TRUE);
            EnableWindow(GetDlgItem(hDlg, IDC_BUTTON_BROWSE), TRUE);
            break;

        case IDC_BUTTON_BROWSE:
        {
            BROWSEINFO bi = { 0 };
            bi.lpszTitle = L"选择输出目录";
            LPITEMIDLIST pidl = SHBrowseForFolder(&bi);
            if (pidl != NULL)
            {
                SHGetPathFromIDList(pidl, szPath);
                SetDlgItemText(hDlg, IDC_EDIT_PATH, szPath);
                CoTaskMemFree(pidl);
            }
        }
        break;

        case IDC_BUTTON_OPEN:
            GetDlgItemText(hDlg, IDC_EDIT_PATH, szPath, MAX_PATH);
            ShellExecute(NULL, L"open", szPath, NULL, NULL, SW_SHOWNORMAL);
            break;
        case IDOK:
            GetDlgItemText(hDlg, IDC_EDIT_PATH, szPath, MAX_PATH);
            outputDirectory = szPath; // 保存自定义路径
            EndDialog(hDlg, LOWORD(wParam));
            return (INT_PTR)TRUE;
        case IDCANCEL:
            outputDirectory = szPath; // 保存自定义路径
            EndDialog(hDlg, LOWORD(wParam));
            return (INT_PTR)TRUE;
        }
        break;
    }
    return (INT_PTR)FALSE;
}


// “关于”框的消息处理程序。
INT_PTR CALLBACK About(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam)
{
    UNREFERENCED_PARAMETER(lParam);
    switch (message)
    {
    case WM_INITDIALOG:
        return (INT_PTR)TRUE;

    case WM_COMMAND:
        if (LOWORD(wParam) == IDOK || LOWORD(wParam) == IDCANCEL)
        {
            EndDialog(hDlg, LOWORD(wParam));
            return (INT_PTR)TRUE;
        }
        break;
    }
    return (INT_PTR)FALSE;
}
