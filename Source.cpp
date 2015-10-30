#include <Windows.h>
#include <complex>
#include <vector>
#include <list>
#pragma comment(lib, "WinMM.lib")

#define BytesPerRead (32768)
#define FreqRange (10000)
const double PI = 3.14159265358979323846264338327950;
struct tag_WAVHeader
{
	DWORD FileDesc;
	DWORD FileSize;
	DWORD WAVDesc;
	DWORD FormatDesc;
	DWORD SizeofWaveSectionChunk;
	WORD WAVETypeFormat;
	WORD NumChannels;
	DWORD SamplePerSecond;
	DWORD BytesPerSecond;
	WORD BlockAlignment;
	WORD BitsPerSample;
	DWORD DataDescriptionHeader;
	DWORD SizeofData;
} WAVHeader;


/* Main Window*/
ATOM RegisterCustomClass(HINSTANCE& hInstance);
LRESULT CALLBACK WndProc(HWND hWnd, UINT iMsg, WPARAM wParam, LPARAM lParam);

enum { ID_CHILDWINDOW = 100 };

LPCTSTR Title = L"FreqVisualizer";
HINSTANCE g_hInst;
HWND hMainWnd;

ATOM RegisterCustomClass(HINSTANCE& hInstance)
{
	WNDCLASS wc;
	wc.cbClsExtra = 0;
	wc.cbWndExtra = 0;
	wc.hbrBackground = (HBRUSH)GetStockObject(WHITE_BRUSH);
	wc.hCursor = LoadCursor(NULL, IDC_ARROW);
	wc.hIcon = LoadIcon(NULL, IDI_APPLICATION);
	wc.hInstance = hInstance;
	wc.lpfnWndProc = WndProc;
	wc.lpszClassName = Title;
	wc.lpszMenuName = NULL;
	wc.style = CS_VREDRAW | CS_HREDRAW;
	return RegisterClass(&wc);
}
int APIENTRY WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR, int nCmdShow)
{
	g_hInst = hInstance;
	RegisterCustomClass(hInstance);
	hMainWnd = CreateWindow(Title, Title, WS_OVERLAPPEDWINDOW,0,0,1000,1000, NULL, NULL, hInstance, NULL);
	ShowWindow(hMainWnd, nCmdShow);

	MSG msg;
	while (GetMessage(&msg, 0, 0, 0))
	{
		TranslateMessage(&msg);
		DispatchMessage(&msg);
	}

	return msg.wParam;
}


/* WAV Processing*/
TCHAR* FileName(HWND hWnd, TCHAR* lpstrFile, LPCWSTR lpstrFilter, int MaxLen)
{
	TCHAR InitDir[MAX_PATH];
	OPENFILENAME OFN = {};
	OFN.lStructSize = sizeof(OPENFILENAME);
	OFN.hwndOwner = hWnd;
	OFN.lpstrFilter = lpstrFilter;
	OFN.lpstrFile = lpstrFile;
	OFN.nMaxFile = MaxLen;
	OFN.lpstrTitle = TEXT("PLEASE SELECT FILE");
	//OFN.Flags |= OFN_ALLOWMULTISELECT | OFN_EXPLORER;
	GetWindowsDirectory(InitDir, MAX_PATH);
	OFN.lpstrInitialDir = InitDir;

	if (GetOpenFileName(&OFN))
		return lpstrFile;
	else
	{
		return 0;
	}
} //get File Name

HANDLE WAVFile;
HANDLE WAVFileMap;
short* WAVFilePtr;

HANDLE hThread;
std::list<HBITMAP> MemBitList;

DWORD StartTime;

DWORD CurFilePos; // in 2-bytes unit
LARGE_INTEGER FileSize;

HBITMAP DrawWaveForm(std::vector<std::complex<double>>& left, std::vector<std::complex<double>>& right);
DWORD WINAPI ProcessWave(LPVOID temp);

/* Maths */
static inline std::complex<double> cis(double val)
{
	return std::complex < double >(cos(val), sin(val));
}
static std::vector<std::complex<double>> dft(std::vector<std::complex<double>>& input)
{
	int n = input.size();
	std::vector<std::complex<double>> out(input.size());
	for (int k = 0; k < n; ++k)
	{
		for (int t = 0; t < n; ++t)
		{
			out[k] += input[t] * cis(-2 * PI*k*t / n);
		}
	}

	return out;
}
static void FFT(std::vector<std::complex<double>>& array, int n)
{
	if (n == 1) {
		return;
	}

	std::vector<std::complex<double>> even(n / 2);
	std::vector<std::complex<double>>  odd(n / 2);

	for (int i = 0; i < n / 2; ++i) {
		even[i] = array[2 * i];
		odd[i] = array[2 * i + 1];
	}

	FFT(even, n / 2);
	FFT(odd, n / 2);

	std::complex<double> w = 1.0;
	// The conjugate principal nth root of unity
	std::complex<double> wn = std::exp(std::complex<double>(0.0, -2.0*PI / n));

	for (int i = 0; i < n / 2; ++i) {
		array[i] = even[i] + w*odd[i];
		array[i + n / 2] = even[i] - w*odd[i];
		w = w * wn;
	}
}

/* Screen Info */
int ScreenWidth;
int ScreenHeight;

LRESULT CALLBACK WndProc(HWND hWnd, UINT iMsg, WPARAM wParam, LPARAM lParam)
{
	switch (iMsg)
	{
	case WM_CREATE:
	{
		RECT R = { 0, 0, 1000, 1000 };
		AdjustWindowRect(&R, WS_OVERLAPPEDWINDOW, false);
		SetWindowPos(hWnd, NULL, 0, 0, R.right - R.left, R.bottom - R.top, SWP_NOMOVE);


		TCHAR Path[MAX_PATH] = {};
		//TCHAR Filter[] = TEXT("WAV File(*.WAV*)\0*.WAV*\0");
		TCHAR Filter[] = { NULL };
		if (!FileName(hWnd, Path, Filter, MAX_PATH))
			return -1;


		WAVFile = CreateFile(Path, GENERIC_READ,FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
	
		 if (WAVFile == INVALID_HANDLE_VALUE)
		{
			//handle error
			GetLastError();
		}

		WAVFileMap = CreateFileMapping(WAVFile, NULL, PAGE_READONLY, NULL, NULL, NULL);
		WAVFilePtr = (short*)MapViewOfFile(WAVFileMap, FILE_MAP_READ, 0, 0, 0);
		WAVHeader = *(tag_WAVHeader*)WAVFilePtr;
		CurFilePos += sizeof(WAVHeader) / sizeof(*WAVFilePtr);
		GetFileSizeEx(WAVFile, &FileSize);
		CloseHandle(WAVFile);
		hThread = CreateThread(NULL, 0, ProcessWave, NULL, NULL, NULL);
		//while (MemBitList.size() < 200); //wait


		SetTimer(hWnd, 0, 1000 * BytesPerRead/WAVHeader.BytesPerSecond, 0);
		StartTime = GetTickCount();
		PlaySound(Path, NULL, SND_FILENAME | SND_ASYNC);
		SendMessage(hWnd, WM_TIMER, 0, 0);

		break;
	}
	case WM_TIMER:
	{
		if (!MemBitList.empty())
		{
			HDC hdc = GetDC(hWnd);
			HDC MemDC = CreateCompatibleDC(hdc);
			HBITMAP CurBit = MemBitList.front();MemBitList.pop_front();
			HBITMAP OldBit = (HBITMAP)SelectObject(MemDC, CurBit);

			BitBlt(hdc, 0, 0, 1000, 1000, MemDC, 0, 0, SRCCOPY);

			SelectObject(MemDC, OldBit);
			DeleteDC(MemDC);
			DeleteObject(CurBit);
			ReleaseDC(hWnd, hdc);
		}
		break;
	}
	case WM_PAINT:
	{
		PAINTSTRUCT ps;
		HDC hdc = BeginPaint(hWnd, &ps);
		EndPaint(hWnd, &ps);
		break;
	}
	case WM_SIZE:
	{
		RECT R;
		GetClientRect(hWnd, &R);
		ScreenWidth = R.right - R.left;
		ScreenHeight = R.bottom - R.top;
		break;
	}
	case WM_DESTROY:

		UnmapViewOfFile(WAVFilePtr);
		CloseHandle(WAVFileMap);
		CloseHandle(hThread);

		PostQuitMessage(0);
		break;
	default:
		return DefWindowProc(hWnd, iMsg, wParam, lParam);
	}
	return 0;
}

//Draws WaveForm on Bitmap
DWORD WINAPI ProcessWave(LPVOID temp)
{
	int SamplePerRead = BytesPerRead * 8 / WAVHeader.BitsPerSample;
	int ValidSamplePerChannel = SamplePerRead / WAVHeader.NumChannels / 2;

	std::vector<std::complex<double>> left(ValidSamplePerChannel*2); //latter half invalid.
	std::vector<std::complex<double>> right(ValidSamplePerChannel*2);

	while (1)
	{
		if (WAVHeader.NumChannels == 2)
		{
			std::fill(left.begin(), left.end(), 0);
			std::fill(right.begin(), right.end(), 0);

			for (int i = 0; i <SamplePerRead; ++i, ++CurFilePos)
			{
				double HannValue = 0.5*(1 - cos(2 * PI*i / SamplePerRead / 2));

				if (CurFilePos > FileSize.QuadPart/2)
					goto end;
				if (i & 1) right[i / 2] = WAVFilePtr[CurFilePos]* HannValue /BytesPerRead;
				else left[i / 2] = WAVFilePtr[CurFilePos] * HannValue / BytesPerRead;
			}
			FFT(left, ValidSamplePerChannel * 2);
			FFT(right, ValidSamplePerChannel * 2);

			//PROCESSING OVER (EXCEPT AMPLITUDE = done during DrawWaveForm
			MemBitList.push_back(DrawWaveForm(left, right));

			if (MemBitList.size() > 50) //prevent too much memory eh... but was threading necessary??
				Sleep(30 * 1000 * BytesPerRead / WAVHeader.BytesPerSecond);
		}

	}
end:
	return 0;
}
HBITMAP DrawWaveForm(std::vector<std::complex<double>>& left, std::vector<std::complex<double>>& right)
{
	int SamplePerRead = BytesPerRead * 8 / WAVHeader.BitsPerSample;
	int ValidSamplePerChannel = SamplePerRead / WAVHeader.NumChannels / 2;

	//CREATE HBITMAP & DC
	HDC hdc = GetDC(hMainWnd);

	HBITMAP SaveBit = CreateCompatibleBitmap(hdc, 1000, 1000);

	RECT R{ 0, 0, 1000, 1000 };
	HDC MemDC = CreateCompatibleDC(hdc);
	HBITMAP OldBit = (HBITMAP)SelectObject(MemDC, SaveBit);
	FillRect(MemDC, &R, (HBRUSH)GetStockObject(WHITE_BRUSH));

	ReleaseDC(hMainWnd, hdc);



	//REPORT TIME
	WCHAR Strbuf[256];
	double CurTime = 2.00*(CurFilePos - sizeof(WAVHeader) / sizeof(*WAVFilePtr)) / WAVHeader.BytesPerSecond;
	swprintf_s(Strbuf, L"[(MUSIC)%f:(PROCESS)%f]Seconds Passed",CurTime,(GetTickCount() - StartTime)/1000.0);
	TextOutW(MemDC, 0, 0, Strbuf, lstrlen(Strbuf));

	//DRAW CURVE
	int MaxAmplitude;
	if (WAVHeader.BitsPerSample == 16)
		MaxAmplitude = 256;

	int FreqBinSize = WAVHeader.SamplePerSecond/2/ValidSamplePerChannel;
	for (int i = 0; i < FreqRange/FreqBinSize; i++)
	{
		LineTo(MemDC, i, ScreenHeight/2 - abs(right[i])/MaxAmplitude * ScreenHeight); //abs to get amplitude

		// each 'i' corresponds to a "frequency bin"
		// size of frequency bin is obtainable by
		// # Valid Sample Per Channel = # bins
		// total range = Samples / sec / 2 (Nyquist)
		// thus freq size per bin = Samplespersec /2 /ValidSamplePerChannel
		// so if we are interested in Freq 0 ~ 10,000,
		// then <10000 / bin size> number of i's are what we want.
	}

	MoveToEx(MemDC, 0, 1000, NULL);

	for (int i = 0; i < ValidSamplePerChannel; i++)
	{
		LineTo(MemDC, i, ScreenHeight / 2 + abs(left[i]) / MaxAmplitude*ScreenHeight);
	}


	//DELETE DC & RETURN HBITMAP
	SelectObject(MemDC, OldBit);
	DeleteDC(MemDC);

	return SaveBit;
}