#define WIN32_LEAN_AND_MEAN
#pragma comment (lib, "kernel32.lib")
#pragma comment (lib, "gdi32.lib")
#pragma comment (lib, "User32.lib")
#pragma comment (lib, "Comctl32.lib")

#ifdef _WIN64
#pragma comment (lib, "WinGraph64.lib")
#else
#pragma comment (lib, "WinGraph32.lib")
#endif
#pragma comment (lib, "NIDAQmx.lib")

#include <Windows.h>
#include <process.h>
#include "resource.h"
#include <time.h>
#include <stdio.h>

// You can find the latest version available of WinGraph here:
// https://github.com/Alexandre-Carpentier/WinGraph
#include "WinGraph.h"

// Must Install Nationnal Instrument DAQmx Lib freely available here:
// https://www.ni.com/fr-fr/support/downloads/drivers/download.ni-daqmx.html#460239
// Default instalation path is located at :
// C:\Program Files (x86)\National Instruments\Shared\ExternalCompilerSupport\C\include
#include "NIDAQmx.h"

// Must load the last Windows UXTheme (optionnal)
#pragma comment(linker,"\"/manifestdependency:type='win32' \
name='Microsoft.Windows.Common-Controls' version='6.0.0.0' \
processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")

HINSTANCE hInst;                    // Global process instance
HGRAPH hGraph;                      // Graph handle
HANDLE acquireThreadHandle;         // Acquizition thread handle
HANDLE hEvent;                      // Event to stop acquiring
enum { ACQUIRE_FREQ = 100 };          // ms
enum { DISP_FREQ = 250 };           // ms
enum { SIGNB = 1 };                // ms

TaskHandle taskHandle;
int32 DAQret;

inline long long PerformanceFrequency()
{
    LARGE_INTEGER li;
    QueryPerformanceFrequency(&li);
    return li.QuadPart;
}

inline long long PerformanceCounter()
{
    LARGE_INTEGER li;
    QueryPerformanceCounter(&li);
    return li.QuadPart;
}

unsigned int acquireThread(void* data)
{
    HANDLE Event = *(HANDLE*)data;
    long long t1 = PerformanceCounter();
    long long t2;
    long long delta;
    long long freq = PerformanceFrequency();
    while (WaitForSingleObject(hEvent, 0) != WAIT_OBJECT_0)
    {

        t2 = PerformanceCounter();
        delta = (double)((t2 - t1)) / freq * 1000;

        if (delta >= ACQUIRE_FREQ)
        {
            t1 = PerformanceCounter();

            // playing here
            float64 data[SIGNB];
            DAQret = DAQmxReadAnalogScalarF64(taskHandle, 10.0, data, NULL);
            float fdata[SIGNB];
            for (int i = 0; i < SIGNB; i++)
            {
                fdata[i] = (float)data[i];
            }
            AddPoints(hGraph, fdata, SIGNB);

        }
    }
    return 0;
}

INT_PTR CALLBACK WndProc(HWND hWnd, UINT message, WPARAM  wParam, LPARAM  lParam)
{
    static PAINTSTRUCT ps;              // paint struct for rendering
    static RECT client_rect;            // WM_SIZE
    static RECT item_r;                 // WM_SIZE
    static HWND hItem;                  // WM_SIZE
    long long start = 0.0;
    long long finish = 0.0;
    long long frequency = 0.0;
    double freq = 0.0;

    switch (message)
    {
        /// ////////////////////////////
        /// TIMER
        /// ////////////////////////////
    case WM_TIMER:                                                      // Refresh the graph with WM_PAINT
    {
        switch (wParam)
        {
        case IDT_TIMER:
        {
            SendMessage(hWnd, WM_PAINT, 0, 0);
            break;
        }
        }
    }
    /// ////////////////////////////
    /// COMMAND MSG
    /// ////////////////////////////
    case WM_COMMAND:                                                    // Analyze messages from controls
    {
        switch (wParam)
        {
        case IDC_QUIT:
        {
            StopGraph(hGraph);
            PostQuitMessage(0);
            break;
        }

        case IDC_ACQUIRE:
        {
            if (BST_CHECKED == IsDlgButtonChecked(hWnd, IDC_CHECK))
                SetRecordingMode(hGraph, TRUE);

            StartGraph(hGraph);
            EnableWindow(GetDlgItem(hWnd, IDC_ACQUIRE), FALSE);
            EnableWindow(GetDlgItem(hWnd, IDC_STOP_ACQUIRE), TRUE);

            // Setup DAQmx

            taskHandle = NULL;
            DAQret = DAQmxCreateTask("", &taskHandle);
            if (0 != DAQret)
            {
                SetDlgItemTextW(hWnd, IDC_EDIT1, L"DAQmxCreateTask Failed");
                EnableWindow(GetDlgItem(hWnd, IDC_ACQUIRE), TRUE);
                EnableWindow(GetDlgItem(hWnd, IDC_STOP_ACQUIRE), FALSE);
                break;
            }
            DAQret = DAQmxCreateAIVoltageChan(taskHandle, "Dev1/ai0", "Voltage0", DAQmx_Val_RSE /*DAQmx_Val_Cfg_Default*/, -10.0, 10.0, DAQmx_Val_Volts, NULL);
            if (0 != DAQret)
            {
                SetDlgItemTextW(hWnd, IDC_EDIT1, L"DAQmxCreateAIVoltageChan Failed");
                EnableWindow(GetDlgItem(hWnd, IDC_ACQUIRE), TRUE);
                EnableWindow(GetDlgItem(hWnd, IDC_STOP_ACQUIRE), FALSE);
                break;
            }
            DAQret = DAQmxStartTask(taskHandle);
            if (0 != DAQret)
            {
                SetDlgItemTextW(hWnd, IDC_EDIT1, L"DAQmxStartTask Failed");
                EnableWindow(GetDlgItem(hWnd, IDC_ACQUIRE), TRUE);
                EnableWindow(GetDlgItem(hWnd, IDC_STOP_ACQUIRE), FALSE);
                break;
            }

            hEvent = NULL;
            acquireThreadHandle = NULL;

            // create Event and launch the acquisition thread

            hEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
            acquireThreadHandle = (HANDLE)_beginthreadex(0, 0, (_beginthreadex_proc_type)&acquireThread, &hEvent, 0, 0);
            if (acquireThreadHandle)
            {

                SetDlgItemTextW(hWnd, IDC_EDIT1, L"Measure started");
            }
            else
            {
                SetDlgItemTextW(hWnd, IDC_EDIT1, L"Measure Failed");
                EnableWindow(GetDlgItem(hWnd, IDC_ACQUIRE), TRUE);
                EnableWindow(GetDlgItem(hWnd, IDC_STOP_ACQUIRE), FALSE);
            }
            break;
        }

        case IDC_STOP_ACQUIRE:
        {
            if (hEvent)
            {
                SetEvent(hEvent); // signal thread to end
                if (acquireThreadHandle)
                {
                    WaitForSingleObject(acquireThreadHandle, 10000); // wait up to 10 seconds
                    CloseHandle(hEvent);
                    CloseHandle(acquireThreadHandle);
                    acquireThreadHandle = NULL;
                }
                hEvent = NULL;
            }

            DAQmxStopTask(taskHandle);
            DAQmxClearTask(taskHandle);

            if (TRUE == GetGraphState(hGraph))
                StopGraph(hGraph);

            EnableWindow(GetDlgItem(hWnd, IDC_ACQUIRE), TRUE);
            EnableWindow(GetDlgItem(hWnd, IDC_STOP_ACQUIRE), FALSE);
            SetDlgItemText(hWnd, IDC_EDIT1, TEXT("Measure stop"));
            break;
        }


        }
        break;
    }
    /// ////////////////////////////
    /// WINDOWS MSG
    /// ////////////////////////////
    case WM_INITDIALOG:
        printf("[*] WM_INITDIALOG start\n");

        // Display the application icon on the dialog toolbar

        HICON hIcon;
        hIcon = LoadIcon((HINSTANCE)GetWindowLongPtr(hWnd, GWLP_HINSTANCE), MAKEINTRESOURCE(IDI_ICON1));
        SendMessage(hWnd, WM_SETICON, ICON_BIG, (LPARAM)hIcon);

        // Retrieve the client windows size to draw the graph inside

        GetClientRect(hWnd, &client_rect);
        client_rect.bottom -= 50;

        // Create the graph

        hGraph = NULL;
        hGraph = CreateGraph(hWnd,                              // Parent Window of the Graph
            client_rect,                                        // Size of the graph
            SIGNB,                                              // Number of signals to handle [1-16]
            1000                                                // Buffer of 1000 points

        );

        if (hGraph == NULL)
        {
            wprintf(L"[*] Fail to CreateGraph()\n");
            break;
        }

        SetTimer(hWnd, IDT_TIMER, DISP_FREQ, (TIMERPROC)NULL);   // refresh display every ~100ms
        EnableWindow(GetDlgItem(hWnd, IDC_STOP_ACQUIRE), FALSE);

        EnableWindow(GetDlgItem(hWnd, IDC_EDIT1), FALSE);
        SetDlgItemText(hWnd, IDC_EDIT1, L"Init success");
        break;

    case WM_PAINT:
        frequency = PerformanceFrequency();
        start = PerformanceCounter();
        BeginPaint(GetGraphParentWnd(hGraph), &ps);
        Render(hGraph);
        EndPaint(GetGraphParentWnd(hGraph), &ps);
        finish = PerformanceCounter();
        freq = (double)((finish - start)) / frequency * 1000;
        //printf("\r\rAnalyze and rendering data in %lf ms", freq);
        break;

    case WM_DESTROY:
    case WM_CLOSE:

        // Terminate the graph and acquisition loop properly

        SendMessage(hWnd, WM_COMMAND, IDC_STOP_ACQUIRE, NULL);

        // Free the graph lib and stop the application

        StopGraph(hGraph);
        FreeGraph(hGraph);
        PostQuitMessage(0);
        break;

    case WM_SIZE:
        client_rect.right = LOWORD(lParam);
        client_rect.bottom = HIWORD(lParam);

        // Resize OPENGL Area

        ReshapeGraph(hGraph, 0, 0, client_rect.right, client_rect.bottom - 50);

        // Resize WIN32 Controls

        int left_shift;
        left_shift = 0;
        for (int item = IDC_QUIT; item <= IDC_SLIDER; item++)
        {
            left_shift++;
            hItem = GetDlgItem(hWnd, item);
            GetClientRect(hItem, &item_r);
            SetWindowPos(hItem, NULL, client_rect.right - 80 - (left_shift * 120), client_rect.bottom - 30, 0, 0, SWP_NOSIZE);
        }
        break;

    default:
        break;
    }
    return (INT_PTR)FALSE;
}

#ifdef _DEBUG
int main(void)
{
    HINSTANCE hInstance = GetModuleHandle(NULL);
    printf("[*] Debug start\n");
#else
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow)
{
#endif

    // Setup classic Windows class

    WNDCLASSEX    wc;
    wc.cbSize = sizeof(WNDCLASSEX);
    wc.style = CS_HREDRAW | CS_VREDRAW | CS_OWNDC;
    wc.lpfnWndProc = (WNDPROC)WndProc;
    wc.cbClsExtra = 0;
    wc.cbWndExtra = 0;
    wc.hInstance = hInstance;
    wc.hIcon = LoadIcon(hInstance, MAKEINTRESOURCE(IDI_ICON1));
    wc.hIconSm = LoadIcon(hInstance, MAKEINTRESOURCE(IDI_ICON1));
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)GetStockObject(COLOR_WINDOW + 1);
    wc.lpszMenuName = NULL;
    wc.lpszClassName = L"OpenGL Graph WinClass";
    if (!RegisterClassEx(&wc))
    {
        MessageBox(0, L"Failed To Register The Window Class.", L"Error", MB_OK | MB_ICONERROR);
        return FALSE;
    }
    printf("[*] Class registered\n");
    hInst = hInstance;
    DialogBox(hInstance, MAKEINTRESOURCE(IDD_GRAH_DLG), 0, (DLGPROC)WndProc);
    return EXIT_SUCCESS;
}

