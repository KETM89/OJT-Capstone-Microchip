// ShiftTimer_USB7002.cpp
// PC-controlled Shift timer using USB7002:
#include <windows.h>
#include <conio.h>     
#include <iostream>
#include <string>
#include <cstdio>
#include "..\\APIHeader\\MPLABConnect.h"
using namespace std;

// DLL function pointer typedefs
// Logging / version / path
typedef BOOL(*pfMchpEnableLogging)(UINT32);
typedef BOOL(*pfMchpSetPath)(PCHAR);
typedef BOOL(*pfMchpUsbGetVersion)(PCHAR);
// Hub enumerate / open / close / error
typedef INT(*pfMchpUsbGetAllHubs)(PCHAR);
typedef HANDLE(*pfMchpUsbOpen)(INT);
typedef BOOL(*pfMchpUsbClose)(HANDLE);
typedef UINT32(*pfMchpUsbGetLastErr)(HANDLE);
// GPIO bridging
typedef BOOL(*pfMchpUsbConfigureGPIO)(HANDLE, INT);
typedef BOOL(*pfMchpUsbGpioSet)(HANDLE, INT, INT);
// I2C bridging
typedef BOOL(*pfMchpUsbI2CSetConfig)(HANDLE, INT, INT);
typedef BOOL(*pfMchpUsbI2CWrite)(HANDLE, INT, UINT8*, UINT8);

// Globals

HMODULE g_hDll = NULL;
HANDLE  g_hDevice = INVALID_HANDLE_VALUE;
// Common
pfMchpEnableLogging   g_fnEnableLogging = NULL;
pfMchpSetPath         g_fnSetPath = NULL;
pfMchpUsbGetVersion   g_fnGetVersion = NULL;
// Hub
pfMchpUsbGetAllHubs   g_fnGetAllHubs = NULL;
pfMchpUsbOpen         g_fnOpen = NULL;
pfMchpUsbClose        g_fnClose = NULL;
pfMchpUsbGetLastErr   g_fnGetLastErr = NULL;
// GPIO
pfMchpUsbConfigureGPIO g_fnConfigureGPIO = NULL;
pfMchpUsbGpioSet       g_fnGpioSet = NULL;
// I2C
pfMchpUsbI2CSetConfig  g_fnI2CSetConfig = NULL;
pfMchpUsbI2CWrite      g_fnI2CWrite = NULL;
// 7-seg GPIO mapping (your pins)
static const int PIN_A = 93; // TOP MIDDLE
static const int PIN_B = 92; // TOP RIGHT
static const int PIN_C = 71; // BOTTOM RIGHT
static const int PIN_D = 78; // BOTTOM MIDDLE
static const int PIN_E = 76; // BOTTOM LEFT
static const int PIN_F = 95; // TOP LEFT
static const int PIN_G = 70; // MIDDLE
// LCD I2C address (change to 0x3F if needed)
static const UINT8 LCD_I2C_ADDR = 0x27;
// PCF8574 backpack bits
static const UINT8 LCD_BACKLIGHT = 0x08; // P3
static const UINT8 LCD_ENABLE = 0x04; // P2
static const UINT8 LCD_RS = 0x01; // P0 (RW is tied low)

// Helpers
void PrintLastError(const char* where)
{
	if (!g_fnGetLastErr || g_hDevice == INVALID_HANDLE_VALUE)
	{
		cerr << where << " failed (no error info)" << endl;
		return;
	}
	DWORD err = g_fnGetLastErr(g_hDevice);
	cerr << "FAIL: " << where << " error = 0x"
		<< std::hex << err << std::dec << endl;
}

//DLL load and hardware init
bool LoadMplabConnectDll()
{
	if (g_hDll) return true;
	CHAR basepath[MAX_PATH] = { 0 };
	GetCurrentDirectoryA(MAX_PATH, basepath);
	string dllPath = basepath;
#ifdef _WIN64
	dllPath += "\\APILib\\64-bit\\MPLABConnect.dll";
#else
	dllPath += "\\APILib\\32-bit\\MPLABConnect.dll";
#endif
	g_hDll = LoadLibraryA(dllPath.c_str());
	if (!g_hDll)
	{
		cerr << "Failed to load MPLABConnect.dll from: " << dllPath << endl;
		return false;
	}
	// Bind functions
	g_fnEnableLogging = (pfMchpEnableLogging)GetProcAddress(g_hDll, "MchpEnableLogging");
	g_fnSetPath = (pfMchpSetPath)GetProcAddress(g_hDll, "MchpSetPath");
	g_fnGetVersion = (pfMchpUsbGetVersion)GetProcAddress(g_hDll, "MchpUsbGetVersion");
	g_fnGetAllHubs = (pfMchpUsbGetAllHubs)GetProcAddress(g_hDll, "MchpUsbGetAllHubs");
	g_fnOpen = (pfMchpUsbOpen)GetProcAddress(g_hDll, "MchpUsbOpen");
	g_fnClose = (pfMchpUsbClose)GetProcAddress(g_hDll, "MchpUsbClose");
	g_fnGetLastErr = (pfMchpUsbGetLastErr)GetProcAddress(g_hDll, "MchpUsbGetLastErr");
	g_fnConfigureGPIO = (pfMchpUsbConfigureGPIO)GetProcAddress(g_hDll, "MchpUsbConfigureGPIO");
	g_fnGpioSet = (pfMchpUsbGpioSet)GetProcAddress(g_hDll, "MchpUsbGpioSet");
	g_fnI2CSetConfig = (pfMchpUsbI2CSetConfig)GetProcAddress(g_hDll, "MchpUsbI2CSetConfig");
	g_fnI2CWrite = (pfMchpUsbI2CWrite)GetProcAddress(g_hDll, "MchpUsbI2CWrite");
	if (!g_fnSetPath || !g_fnGetVersion || !g_fnGetAllHubs ||
		!g_fnOpen || !g_fnClose || !g_fnGetLastErr ||
		!g_fnConfigureGPIO || !g_fnGpioSet ||
		!g_fnI2CSetConfig || !g_fnI2CWrite)
	{
		cerr << "Failed to bind one or more DLL functions." << endl;
		return false;
	}
	if (g_fnEnableLogging)
		g_fnEnableLogging(3); // detailed logging
	return true;
}
bool InitHardware()
{
	if (!LoadMplabConnectDll())
		return false;
	// Set INI path (to APILib)
	CHAR basepath[MAX_PATH] = { 0 };
	GetCurrentDirectoryA(MAX_PATH, basepath);
	string iniPath = string(basepath) + "\\APILib\\";
	if (g_fnSetPath)
		g_fnSetPath((PCHAR)iniPath.c_str());
	// Optional: print version
	char ver[256] = { 0 };
	if (g_fnGetVersion && g_fnGetVersion(ver))
		cout << "MPLABConnect version: " << ver << endl;
	// Enumerate hubs
	char hubList[2048] = { 0 };
	int hubCount = g_fnGetAllHubs ? g_fnGetAllHubs(hubList) : 0;
	cout << "Hubs found: " << hubCount << endl;
	if (hubCount <= 0)
	{
		cerr << "No USB hubs detected." << endl;
		return false;
	}
	// Open hub index 0
	g_hDevice = g_fnOpen ? g_fnOpen(0) : INVALID_HANDLE_VALUE;
	if (g_hDevice == INVALID_HANDLE_VALUE)
	{
		PrintLastError("MchpUsbOpen");
		return false;
	}
	// Configure I2C for LCD (use preset 1)
	INT clockRate = 0;
	INT preset = 1;
	if (!g_fnI2CSetConfig(g_hDevice, clockRate, preset))
	{
		PrintLastError("MchpUsbI2CSetConfig");
		return false;
	}
	cout << "I2C configured OK." << endl;
	// Configure 7-seg GPIO pins
	int pins[7] = { PIN_A, PIN_B, PIN_C, PIN_D, PIN_E, PIN_F, PIN_G };
	for (int i = 0; i < 7; ++i)
	{
		if (!g_fnConfigureGPIO(g_hDevice, pins[i]))
		{
			cerr << "Failed to configure GPIO " << pins[i] << endl;
			PrintLastError("MchpUsbConfigureGPIO");
			return false;
		}
	}
	cout << "7-segment GPIO configured." << endl;
	return true;
}
void ShutdownHardware()
{
	if (g_hDevice != INVALID_HANDLE_VALUE && g_fnClose)
	{
		g_fnClose(g_hDevice);
		g_hDevice = INVALID_HANDLE_VALUE;
	}
	if (g_hDll)
	{
		FreeLibrary(g_hDll);
		g_hDll = NULL;
	}
}

// LCD1602 I2C driver
bool LcdExpanderWrite(UINT8 data)
{
	if (!g_fnI2CWrite || g_hDevice == INVALID_HANDLE_VALUE)
		return false;
	if (!g_fnI2CWrite(g_hDevice, 1, &data, LCD_I2C_ADDR))
	{
		PrintLastError("MchpUsbI2CWrite");
		return false;
	}
	return true;
}
void LcdPulseEnable(UINT8 data)
{
	LcdExpanderWrite(data | LCD_ENABLE);
	Sleep(1);
	LcdExpanderWrite(data & ~LCD_ENABLE);
	Sleep(1);
}
void LcdWrite4Bits(UINT8 nibble)
{
	LcdExpanderWrite(nibble | LCD_BACKLIGHT);
	LcdPulseEnable(nibble | LCD_BACKLIGHT);
}
void LcdSendByte(UINT8 value, bool isData)
{
	UINT8 highNib = value & 0xF0;
	UINT8 lowNib = (value << 4) & 0xF0;
	UINT8 mode = (isData ? LCD_RS : 0x00); // RW=0
	LcdWrite4Bits(highNib | mode);
	LcdWrite4Bits(lowNib | mode);
}
void LcdCommand(UINT8 cmd)
{
	LcdSendByte(cmd, false);
}
void LcdWriteChar(char c)
{
	LcdSendByte((UINT8)c, true);
}
void LcdClear()
{
	LcdCommand(0x01);
	Sleep(2);
}
void LcdSetCursor(UINT8 col, UINT8 row)
{
	static const UINT8 rowOffsets[2] = { 0x00, 0x40 };
	if (row > 1) row = 1;
	LcdCommand(0x80 | (col + rowOffsets[row]));
}
void LcdPrint(const string& s)
{
	for (size_t i = 0; i < s.size(); ++i)
		LcdWriteChar(s[i]);
}

// Prints exactly 16 characters (pad / truncate) to avoid leftover text 
void LcdPrintPadded(const string& s)
{
	string out = s;
	if (out.size() < 16)
		out += string(16 - out.size(), ' ');
	else if (out.size() > 16)
		out = out.substr(0, 16);
	LcdPrint(out);
}
void LcdShowLine(const string& line1, const string& line2)
{
	LcdClear();
	LcdSetCursor(0, 0);
	LcdPrintPadded(line1);
	LcdSetCursor(0, 1);
	LcdPrintPadded(line2);
}
void LcdInit()
{
	Sleep(50);
	// 4-bit init sequence
	LcdWrite4Bits(0x30); Sleep(5);
	LcdWrite4Bits(0x30); Sleep(5);
	LcdWrite4Bits(0x30); Sleep(5);
	LcdWrite4Bits(0x20);
	LcdCommand(0x28); // Function set: 4-bit, 2-line
	LcdCommand(0x0C); // Display on, cursor off
	LcdClear();
	LcdCommand(0x06); // Entry mode
}

//7-segment driver
bool SetGpio(int gpio, bool high)
{
	if (!g_fnGpioSet || g_hDevice == INVALID_HANDLE_VALUE)
		return false;
	if (!g_fnGpioSet(g_hDevice, gpio, high ? 1 : 0))
	{
		PrintLastError("MchpUsbGpioSet");
		return false;
	}
	return true;
}
void SevenSeg_AllOff()
{
	SetGpio(PIN_A, false);
	SetGpio(PIN_B, false);
	SetGpio(PIN_C, false);
	SetGpio(PIN_D, false);
	SetGpio(PIN_E, false);
	SetGpio(PIN_F, false);
	SetGpio(PIN_G, false);
}

// Digit patterns for common-cathode 7-seg (HIGH = segment ON)
// Order: A, B, C, D, E, F, G
static const bool DIGIT_MAP[10][7] =
{
   { true,  true,  true,  true,  true,  true,  false }, // 0
   { false, true,  true,  false, false, false, false }, // 1
   { true,  true,  false, true,  true,  false, true  }, // 2
   { true,  true,  true,  true,  false, false, true  }, // 3
   { false, true,  true,  false, false, true,  true  }, // 4
   { true,  false, true,  true,  false, true,  true  }, // 5
   { true,  false, true,  true,  true,  true,  true  }, // 6
   { true,  true,  true,  false, false, false, false }, // 7
   { true,  true,  true,  true,  true,  true,  true  }, // 8
   { true,  true,  true,  true,  false, true,  true  }  // 9
};
void SevenSeg_DisplayDigit(int digit)
{
	if (digit < 0 || digit > 9)
	{
		SevenSeg_AllOff();
		return;
	}
	const bool* p = DIGIT_MAP[digit];
	SetGpio(PIN_A, p[0]);
	SetGpio(PIN_B, p[1]);
	SetGpio(PIN_C, p[2]);
	SetGpio(PIN_D, p[3]);
	SetGpio(PIN_E, p[4]);
	SetGpio(PIN_F, p[5]);
	SetGpio(PIN_G, p[6]);
}
void SevenSeg_BlinkEnd(int digit, int blinks)
{
	for (int i = 0; i < blinks; ++i)
	{
		SevenSeg_AllOff();
		Sleep(200);
		SevenSeg_DisplayDigit(digit);
		Sleep(200);
	}
}

// Shift session logic (GUI-only version, no keyboard stop)
void RunSession(const string& modeName, int minutes)
{
	if (minutes <= 0)
	{
		LcdShowLine(modeName, "Duration is 0");
		Sleep(1500);
		return;
	}
	int totalSecs = minutes * 60;
	// Initial screen
	LcdShowLine(modeName, "MODE");
	DWORD startTick = GetTickCount();
	int   lastElapsed = -1;
	// Short name for line 1 so it fits "NAME mm:ss" in 16 chars
	std::string shortName = modeName;
	if (shortName.size() > 10)
		shortName = shortName.substr(0, 10);
	while (true)
	{
		DWORD now = GetTickCount();
		int   elapsed = (int)((now - startTick) / 1000);
		// Finished?
		if (elapsed > totalSecs)
			break;
		// Update once per second
		if (elapsed != lastElapsed)
		{
			lastElapsed = elapsed;
			// 1) Progress step for 7-seg (9 ? 0)
			double stepSize = totalSecs / 10.0;
			int stepIndex = (int)(elapsed / stepSize);  // 0..9
			if (stepIndex > 9) stepIndex = 9;
			int digit = 9 - stepIndex;                    // 9..0
			SevenSeg_DisplayDigit(digit);
			// 2) Remaining time
			int remaining = totalSecs - elapsed;
			if (remaining < 0) remaining = 0;
			int mm = remaining / 60;
			int ss = remaining % 60;
			// 3) Build LCD lines (safe with sizeof)
			char buf1[17];
			sprintf_s(buf1, sizeof(buf1), "%s %02d:%02d",
				shortName.c_str(), mm, ss);
			char buf2[17];
			sprintf_s(buf2, sizeof(buf2), "Progress %d/9", 9 - digit);
			// 4) Print padded lines
			LcdSetCursor(0, 0);
			LcdPrintPadded(string(buf1));
			LcdSetCursor(0, 1);
			LcdPrintPadded(string(buf2));
		}
		Sleep(20); // avoid busy loop
	}
	// Session finished normally
	LcdShowLine(modeName + " Done!", " ");
	SevenSeg_BlinkEnd(0, 6);
	SevenSeg_DisplayDigit(0);
	Sleep(1500);
}

// MAIN
int main(int argc, char* argv[])
{
	if (!InitHardware())
	{
		cerr << "InitHardware failed." << endl;
		ShutdownHardware();
		return -1;
	}
	LcdInit();
	SevenSeg_AllOff();
	// ----------------------------------------------------
	// GUI / command-line mode
	// Examples:
	//   ShiftTimer_USB7002.exe phone 30
	//   ShiftTimer_USB7002.exe onchat 15
	//   ShiftTimer_USB7002.exe break 5
	//   ShiftTimer_USB7002.exe stop
	// ----------------------------------------------------
	if (argc >= 2)
	{
		std::string mode = argv[1];
		// 1) STOP command from GUI
		if (mode == "stop")
		{
			LcdShowLine("This Person", "Is Available");
			SevenSeg_AllOff();
			Sleep(1500);
			ShutdownHardware();
			return 0;
		}
		// 2) Normal timer sessions
		int minutes = 0;
		if (argc >= 3)
			minutes = atoi(argv[2]);
		if (minutes <= 0) minutes = 1;
		if (mode == "phone")
		{
			RunSession("PhoneShift", minutes);
		}
		else if (mode == "onchat")
		{
			RunSession("On-chat/call", minutes);
		}
		else if (mode == "break")
		{
			RunSession("Break", minutes);
		}
		else
		{
			LcdShowLine("Invalid mode", mode);
			Sleep(2000);
		}
		// After any session, show availability screen
		LcdShowLine("This Person", "Is Available");
		SevenSeg_AllOff();
		ShutdownHardware();
		return 0;
	}
	// If launched with no arguments (e.g., by accident)
	LcdShowLine("Shift Timer", "Waiting for GUI");
	Sleep(1500);
	SevenSeg_AllOff();
	ShutdownHardware();
	return 0;
}