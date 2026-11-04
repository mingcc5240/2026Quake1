/*
Copyright (C) 1996-1997 Id Software, Inc.

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  

See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.

*/
// sys_win.c -- Win32 system interface code

#include "quakedef.h"
#include "winquake.h"
#include "errno.h"
#include "resource.h"
#include "conproc.h"

#define MINIMUM_WIN_MEMORY		0x0880000
#define MAXIMUM_WIN_MEMORY		0x1000000

#define CONSOLE_ERROR_TIMEOUT	60.0	// # of seconds to wait on Sys_Error running
										//  dedicated before exiting
#define PAUSE_SLEEP		50				// sleep time on pause or minimization
#define NOT_FOCUS_SLEEP	20				// sleep time when not focus

int			starttime;
qboolean	ActiveApp, Minimized;
qboolean	WinNT;

static double		pfreq;
static double		curtime = 0.0;
static double		lastcurtime = 0.0;
static int			lowshift;
qboolean			isDedicated;
static qboolean		sc_return_on_enter = false;
HANDLE				hinput, houtput;

static char			*tracking_tag = "Clams & Mooses";

static HANDLE	tevent;
static HANDLE	hFile;
static HANDLE	heventParent;
static HANDLE	heventChild;

void MaskExceptions (void);
void Sys_InitFloatTime (void);
void Sys_PushFPCW_SetHigh (void);
void Sys_PopFPCW (void);

volatile int					sys_checksum;


/*
================
Sys_PageIn
================
*/
void Sys_PageIn (void *ptr, int size)
{
	byte	*x;
	int		j, m, n;

// touch all the memory to make sure it's there. The 16-page skip is to
// keep Win 95 from thinking we're trying to page ourselves in (we are
// doing that, of course, but there's no reason we shouldn't)
	x = (byte *)ptr;

	for (n=0 ; n<4 ; n++)
	{
		for (m=0 ; m<(size - 16 * 0x1000) ; m += 4)
		{
			sys_checksum += *(int *)&x[m];
			sys_checksum += *(int *)&x[m + 16 * 0x1000];
		}
	}
}


/*
===============================================================================

FILE IO

===============================================================================
*/

#define	MAX_HANDLES		10
FILE	*sys_handles[MAX_HANDLES];

int		findhandle (void)
{
	int		i;
	
	for (i=1 ; i<MAX_HANDLES ; i++)
		if (!sys_handles[i])
			return i;
	Sys_Error ("out of handles");
	return -1;
}

/*
================
filelength
================
*/
int filelength (FILE *f)
{
	int		pos;
	int		end;
	int		t;

	t = VID_ForceUnlockedAndReturnState ();

	pos = ftell (f);
	fseek (f, 0, SEEK_END);
	end = ftell (f);
	fseek (f, pos, SEEK_SET);

	VID_ForceLockState (t);

	return end;
}

int Sys_FileOpenRead (char *path, int *hndl)
{
	FILE	*f;
	int		i, retval;
	int		t;

	t = VID_ForceUnlockedAndReturnState ();

	i = findhandle ();

	f = fopen(path, "rb");

	if (!f)
	{
		*hndl = -1;
		retval = -1;
	}
	else
	{
		sys_handles[i] = f;
		*hndl = i;
		retval = filelength(f);
	}

	VID_ForceLockState (t);

	return retval;
}

int Sys_FileOpenWrite (char *path)
{
	FILE	*f;
	int		i;
	int		t;

	t = VID_ForceUnlockedAndReturnState ();
	
	i = findhandle ();

	f = fopen(path, "wb");
	if (!f)
		Sys_Error ("Error opening %s: %s", path,strerror(errno));
	sys_handles[i] = f;
	
	VID_ForceLockState (t);

	return i;
}

void Sys_FileClose (int handle)
{
	int		t;

	t = VID_ForceUnlockedAndReturnState ();
	fclose (sys_handles[handle]);
	sys_handles[handle] = NULL;
	VID_ForceLockState (t);
}

void Sys_FileSeek (int handle, int position)
{
	int		t;

	t = VID_ForceUnlockedAndReturnState ();
	fseek (sys_handles[handle], position, SEEK_SET);
	VID_ForceLockState (t);
}

int Sys_FileRead (int handle, void *dest, int count)
{
	int		t, x;

	t = VID_ForceUnlockedAndReturnState ();
	x = fread (dest, 1, count, sys_handles[handle]);
	VID_ForceLockState (t);
	return x;
}

int Sys_FileWrite (int handle, void *data, int count)
{
	int		t, x;

	t = VID_ForceUnlockedAndReturnState ();
	x = fwrite (data, 1, count, sys_handles[handle]);
	VID_ForceLockState (t);
	return x;
}

int	Sys_FileTime (char *path)
{
	FILE	*f;
	int		t, retval;

	t = VID_ForceUnlockedAndReturnState ();
	
	f = fopen(path, "rb");

	if (f)
	{
		fclose(f);
		retval = 1;
	}
	else
	{
		retval = -1;
	}
	
	VID_ForceLockState (t);
	return retval;
}

void Sys_mkdir (char *path)
{
	_mkdir (path);
}


/*
===============================================================================

SYSTEM IO

===============================================================================
*/

/*
================
Sys_MakeCodeWriteable
================
*/
void Sys_MakeCodeWriteable (unsigned long startaddr, unsigned long length)
{
	DWORD  flOldProtect;

	if (!VirtualProtect((LPVOID)startaddr, length, PAGE_READWRITE, &flOldProtect))
   		Sys_Error("Protection change failed\n");
}


#ifndef _M_IX86

void Sys_SetFPCW (void)
{
}

void Sys_PushFPCW_SetHigh (void)
{
}

void Sys_PopFPCW (void)
{
}

void MaskExceptions (void)
{
}

#endif

/*
================
Sys_Init
================
*/
void Sys_Init (void)
{
	LARGE_INTEGER	PerformanceFreq;
	unsigned int	lowpart, highpart;
	OSVERSIONINFO	vinfo;

	MaskExceptions ();
	Sys_SetFPCW ();

	if (!QueryPerformanceFrequency (&PerformanceFreq))
		Sys_Error ("No hardware timer available");

// get 32 out of the 64 time bits such that we have around
// 1 microsecond resolution
	lowpart = (unsigned int)PerformanceFreq.LowPart;
	highpart = (unsigned int)PerformanceFreq.HighPart;
	lowshift = 0;

	while (highpart || (lowpart > 2000000.0))
	{
		lowshift++;
		lowpart >>= 1;
		lowpart |= (highpart & 1) << 31;
		highpart >>= 1;
	}

	pfreq = 1.0 / (double)lowpart;

	Sys_InitFloatTime ();

	vinfo.dwOSVersionInfoSize = sizeof(vinfo);

	if (!GetVersionEx (&vinfo))
		Sys_Error ("Couldn't get OS info");

	if ((vinfo.dwMajorVersion < 4) ||
		(vinfo.dwPlatformId == VER_PLATFORM_WIN32s))
	{
		Sys_Error ("WinQuake requires at least Win95 or NT 4.0");
	}

	if (vinfo.dwPlatformId == VER_PLATFORM_WIN32_NT)
		WinNT = true;
	else
		WinNT = false;
}


void Sys_Error (char *error, ...)
{
	va_list		argptr;
	char		text[1024], text2[1024];
	char		*text3 = "Press Enter to exit\n";
	char		*text4 = "***********************************\n";
	char		*text5 = "\n";
	DWORD		dummy;
	double		starttime;
	static int	in_sys_error0 = 0;
	static int	in_sys_error1 = 0;
	static int	in_sys_error2 = 0;
	static int	in_sys_error3 = 0;

	if (!in_sys_error3)
	{
		in_sys_error3 = 1;
		VID_ForceUnlockedAndReturnState ();
	}

	va_start (argptr, error);
	vsprintf (text, error, argptr);
	va_end (argptr);

	if (isDedicated)
	{
		va_start (argptr, error);
		vsprintf (text, error, argptr);
		va_end (argptr);

		sprintf (text2, "ERROR: %s\n", text);
		WriteFile (houtput, text5, strlen (text5), &dummy, NULL);
		WriteFile (houtput, text4, strlen (text4), &dummy, NULL);
		WriteFile (houtput, text2, strlen (text2), &dummy, NULL);
		WriteFile (houtput, text3, strlen (text3), &dummy, NULL);
		WriteFile (houtput, text4, strlen (text4), &dummy, NULL);


		starttime = Sys_FloatTime ();
		sc_return_on_enter = true;	// so Enter will get us out of here

		while (!Sys_ConsoleInput () &&
				((Sys_FloatTime () - starttime) < CONSOLE_ERROR_TIMEOUT))
		{
		}
	}
	else
	{
	// switch to windowed so the message box is visible, unless we already
	// tried that and failed
		if (!in_sys_error0)
		{
			in_sys_error0 = 1;
			VID_SetDefaultMode ();
			MessageBox(NULL, text, "Quake Error",
					   MB_OK | MB_SETFOREGROUND | MB_ICONSTOP);
		}
		else
		{
			MessageBox(NULL, text, "Double Quake Error",
					   MB_OK | MB_SETFOREGROUND | MB_ICONSTOP);
		}
	}

	if (!in_sys_error1)
	{
		in_sys_error1 = 1;
		Host_Shutdown ();
	}

// shut down QHOST hooks if necessary
	if (!in_sys_error2)
	{
		in_sys_error2 = 1;
		DeinitConProc ();
	}

	//exit (1);
}

void Sys_Printf (char *fmt, ...)
{
	va_list		argptr;
	char		text[1024];
	DWORD		dummy;
	
	if (isDedicated)
	{
		va_start (argptr,fmt);
		vsprintf (text, fmt, argptr);
		va_end (argptr);

		WriteFile(houtput, text, strlen (text), &dummy, NULL);	
	}
}

void Sys_Quit (void)
{

	VID_ForceUnlockedAndReturnState ();

	Host_Shutdown();

	if (tevent)
		CloseHandle (tevent);

	if (isDedicated)
		FreeConsole ();

// shut down QHOST hooks if necessary
	DeinitConProc ();

	//exit (0);
}


/*
================
Sys_FloatTime
================
*/
double Sys_FloatTime (void)
{
	static int			sametimecount;
	static unsigned int	oldtime;
	static int			first = 1;
	LARGE_INTEGER		PerformanceCount;
	unsigned int		temp, t2;
	double				time;

	Sys_PushFPCW_SetHigh ();

	QueryPerformanceCounter (&PerformanceCount);

	temp = ((unsigned int)PerformanceCount.LowPart >> lowshift) |
		   ((unsigned int)PerformanceCount.HighPart << (32 - lowshift));

	if (first)
	{
		oldtime = temp;
		first = 0;
	}
	else
	{
	// check for turnover or backward time
		if ((temp <= oldtime) && ((oldtime - temp) < 0x10000000))
		{
			oldtime = temp;	// so we can't get stuck
		}
		else
		{
			t2 = temp - oldtime;

			time = (double)t2 * pfreq;
			oldtime = temp;

			curtime += time;

			if (curtime == lastcurtime)
			{
				sametimecount++;

				if (sametimecount > 100000)
				{
					curtime += 1.0;
					sametimecount = 0;
				}
			}
			else
			{
				sametimecount = 0;
			}

			lastcurtime = curtime;
		}
	}

	Sys_PopFPCW ();

    return curtime;
}


/*
================
Sys_InitFloatTime
================
*/
void Sys_InitFloatTime (void)
{
	int		j;

	Sys_FloatTime ();

	j = COM_CheckParm("-starttime");

	if (j)
	{
		curtime = (double) (Q_atof(com_argv[j+1]));
	}
	else
	{
		curtime = 0.0;
	}

	lastcurtime = curtime;
}


char *Sys_ConsoleInput (void)
{
	static char	text[256];
	static int		len;
	INPUT_RECORD	recs[1024];
	int		count;
	int		i, dummy;
	int		ch, numread, numevents;

	if (!isDedicated)
		return NULL;


	for ( ;; )
	{
		if (!GetNumberOfConsoleInputEvents (hinput, &numevents))
			Sys_Error ("Error getting # of console events");

		if (numevents <= 0)
			break;

		if (!ReadConsoleInput(hinput, recs, 1, &numread))
			Sys_Error ("Error reading console input");

		if (numread != 1)
			Sys_Error ("Couldn't read console input");

		if (recs[0].EventType == KEY_EVENT)
		{
			if (!recs[0].Event.KeyEvent.bKeyDown)
			{
				ch = recs[0].Event.KeyEvent.uChar.AsciiChar;

				switch (ch)
				{
					case '\r':
						WriteFile(houtput, "\r\n", 2, &dummy, NULL);	

						if (len)
						{
							text[len] = 0;
							len = 0;
							return text;
						}
						else if (sc_return_on_enter)
						{
						// special case to allow exiting from the error handler on Enter
							text[0] = '\r';
							len = 0;
							return text;
						}

						break;

					case '\b':
						WriteFile(houtput, "\b \b", 3, &dummy, NULL);	
						if (len)
						{
							len--;
						}
						break;

					default:
						if (ch >= ' ')
						{
							WriteFile(houtput, &ch, 1, &dummy, NULL);	
							text[len] = ch;
							len = (len + 1) & 0xff;
						}

						break;

				}
			}
		}
	}

	return NULL;
}

void Sys_Sleep (void)
{
	Sleep (1);
}


void Sys_SendKeyEvents (void)
{
    MSG        msg;

	while (PeekMessage (&msg, NULL, 0, 0, PM_NOREMOVE))
	{
	// we always update if there are any event, even if we're paused
		scr_skipupdate = 0;

		if (!GetMessage (&msg, NULL, 0, 0))
			Sys_Quit ();

      	TranslateMessage (&msg);
      	DispatchMessage (&msg);
	}
}


/*
==============================================================================

 WINDOWS CRAP

==============================================================================
*/


/*
==================
WinMain
==================
*/
void SleepUntilInput (int time)
{

	MsgWaitForMultipleObjects(1, &tevent, FALSE, time, QS_ALLINPUT);
}


/*
==================
WinMain
==================
*/
HINSTANCE	global_hInstance;
int			global_nCmdShow;
char		*argv[MAX_NUM_ARGVS];
static char	*empty_string = "";
HWND		hwnd_dialog;


int WINAPI WinMain (HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow)
{
    MSG				msg;
	quakeparms_t	parms;
	double			time, oldtime, newtime;
	MEMORYSTATUS	lpBuffer;
	static	char	cwd[1024];
	int				t;
	RECT			rect;

    /* previous instances do not exist in Win32 */
    if (hPrevInstance)
        return 0;

	global_hInstance = hInstance;
	global_nCmdShow = nCmdShow;

	lpBuffer.dwLength = sizeof(MEMORYSTATUS);
	GlobalMemoryStatus (&lpBuffer);

	if (!GetCurrentDirectory (sizeof(cwd), cwd))
		Sys_Error ("Couldn't determine current directory");

	if (cwd[Q_strlen(cwd)-1] == '/')
		cwd[Q_strlen(cwd)-1] = 0;

	parms.basedir = cwd;
	parms.cachedir = NULL;

	parms.argc = 1;
	argv[0] = empty_string;

	while (*lpCmdLine && (parms.argc < MAX_NUM_ARGVS))
	{
		while (*lpCmdLine && ((*lpCmdLine <= 32) || (*lpCmdLine > 126)))
			lpCmdLine++;

		if (*lpCmdLine)
		{
			argv[parms.argc] = lpCmdLine;
			parms.argc++;

			while (*lpCmdLine && ((*lpCmdLine > 32) && (*lpCmdLine <= 126)))
				lpCmdLine++;

			if (*lpCmdLine)
			{
				*lpCmdLine = 0;
				lpCmdLine++;
			}
			
		}
	}

	parms.argv = argv;

	COM_InitArgv (parms.argc, parms.argv);

	parms.argc = com_argc;
	parms.argv = com_argv;

	isDedicated = (COM_CheckParm ("-dedicated") != 0);

	if (!isDedicated)
	{
		hwnd_dialog = CreateDialog(hInstance, MAKEINTRESOURCE(IDD_DIALOG1), NULL, NULL);

		if (hwnd_dialog)
		{
			if (GetWindowRect (hwnd_dialog, &rect))
			{
				if (rect.left > (rect.top * 2))
				{
					SetWindowPos (hwnd_dialog, 0,
						(rect.left / 2) - ((rect.right - rect.left) / 2),
						rect.top, 0, 0,
						SWP_NOZORDER | SWP_NOSIZE);
				}
			}

			ShowWindow (hwnd_dialog, SW_SHOWDEFAULT);
			UpdateWindow (hwnd_dialog);
			SetForegroundWindow (hwnd_dialog);
		}
	}

// take the greater of all the available memory or half the total memory,
// but at least 8 Mb and no more than 16 Mb, unless they explicitly
// request otherwise
	parms.memsize = lpBuffer.dwAvailPhys;

	if (parms.memsize < MINIMUM_WIN_MEMORY)
		parms.memsize = MINIMUM_WIN_MEMORY;

	if (parms.memsize < (lpBuffer.dwTotalPhys >> 1))
		parms.memsize = lpBuffer.dwTotalPhys >> 1;

	if (parms.memsize > MAXIMUM_WIN_MEMORY)
		parms.memsize = MAXIMUM_WIN_MEMORY;

	if (COM_CheckParm ("-heapsize"))
	{
		t = COM_CheckParm("-heapsize") + 1;

		if (t < com_argc)
			parms.memsize = Q_atoi (com_argv[t]) * 1024;
	}

	parms.membase = malloc (parms.memsize);

	if (!parms.membase)
		Sys_Error ("Not enough memory free; check disk space\n");

	Sys_PageIn (parms.membase, parms.memsize);

	tevent = CreateEvent(NULL, FALSE, FALSE, NULL);

	if (!tevent)
		Sys_Error ("Couldn't create event");

	if (isDedicated)
	{
		if (!AllocConsole ())
		{
			Sys_Error ("Couldn't create dedicated server console");
		}

		hinput = GetStdHandle (STD_INPUT_HANDLE);
		houtput = GetStdHandle (STD_OUTPUT_HANDLE);

	// give QHOST a chance to hook into the console
		if ((t = COM_CheckParm ("-HFILE")) > 0)
		{
			if (t < com_argc)
				hFile = (HANDLE)Q_atoi (com_argv[t+1]);
		}
			
		if ((t = COM_CheckParm ("-HPARENT")) > 0)
		{
			if (t < com_argc)
				heventParent = (HANDLE)Q_atoi (com_argv[t+1]);
		}
			
		if ((t = COM_CheckParm ("-HCHILD")) > 0)
		{
			if (t < com_argc)
				heventChild = (HANDLE)Q_atoi (com_argv[t+1]);
		}

		InitConProc (hFile, heventParent, heventChild);
	}

	Sys_Init ();

// because sound is off until we become active
	S_BlockSound ();

	Sys_Printf ("Host_Init\n");
	Host_Init (&parms);

	oldtime = Sys_FloatTime ();

    /* main window message loop */
	while (1)
	{
		if (isDedicated)
		{
			newtime = Sys_FloatTime ();
			time = newtime - oldtime;

			while (time < sys_ticrate.value )
			{
				Sys_Sleep();
				newtime = Sys_FloatTime ();
				time = newtime - oldtime;
			}
		}
		else
		{
		// yield the CPU for a little while when paused, minimized, or not the focus
			if ((cl.paused && (!ActiveApp && !DDActive)) || Minimized || block_drawing)
			{
				SleepUntilInput (PAUSE_SLEEP);
				scr_skipupdate = 1;		// no point in bothering to draw
			}
			else if (!ActiveApp && !DDActive)
			{
				SleepUntilInput (NOT_FOCUS_SLEEP);
			}

			newtime = Sys_FloatTime ();
			time = newtime - oldtime;
		}

		Host_Frame (time);
		oldtime = newtime;
	}

    /* return success of application */
    return TRUE;
}

// sys_wina.s 어셈블리 대체용 빈 함수 (GLQuake 및 순수 C 빌드용)
void MaskExceptions(void){}
void Sys_SetFPCW(void){}
void Sys_PushFPCW_SetHigh(void){}
void Sys_PopFPCW(void){}
void Sys_LowFPPrecision(void) {}
void Sys_HighFPPrecision(void) {}


// =========================================================================
// [여기부터 sys_win.c 맨 아래에 붙여넣기]
// (주의: 바로 위 원본 WinMain 함수의 닫는 중괄호 '}'가 정상적으로 있는지 확인하세요)
// =========================================================================


// 2. gl_vidnt.c 외부 변수 및 함수 참조
extern HWND    g_vb_hwnd;
extern int     g_vb_width;
extern int     g_vb_height;
extern WNDPROC g_old_vb_wndproc;
extern int     window_x, window_y;
extern void    VID_UpdateWindowStatus(void);

static quakeparms_t vb_parms;
static double       vb_oldtime = 0;
static qboolean     vb_quake_initialized = false;
static char         vb_basedir[MAX_OSPATH];

// 3. VB6 초기화 함수 (해상도 동적 지정 포함)
__declspec(dllexport) int __stdcall VB_QuakeInit(HWND hWnd, int width, int height, const char* basedir)
{
	static char str_width[16];
	static char str_height[16];
	static char* argv[10];

	if (vb_quake_initialized) return 1;

	g_vb_hwnd = hWnd;
	g_vb_width = (width > 0) ? width : 800;
	g_vb_height = (height > 0) ? height : 600;
	global_hInstance = GetModuleHandle(NULL);

	if (basedir && basedir[0])
	{
		strncpy(vb_basedir, basedir, sizeof(vb_basedir) - 1);
		vb_basedir[sizeof(vb_basedir) - 1] = '\0';
		SetCurrentDirectoryA(vb_basedir);
	}
	else
	{
		GetCurrentDirectoryA(sizeof(vb_basedir), vb_basedir);
	}

	sprintf(str_width, "%d", g_vb_width);
	sprintf(str_height, "%d", g_vb_height);

	argv[0] = "glquake";
	argv[1] = "-window";
	argv[2] = "-nodinput";
	argv[3] = "-nocdaudio";
	argv[4] = "-width";
	argv[5] = str_width;
	argv[6] = "-height";
	argv[7] = str_height;
	argv[8] = NULL;

	memset(&vb_parms, 0, sizeof(vb_parms));
	vb_parms.argc = 8;
	vb_parms.argv = argv;
	vb_parms.basedir = vb_basedir;
	vb_parms.memsize = 16 * 1024 * 1024;
	vb_parms.membase = malloc(vb_parms.memsize);

	COM_InitArgv(vb_parms.argc, vb_parms.argv);
	vb_parms.argc = com_argc;
	vb_parms.argv = com_argv;

	Sys_InitFloatTime();
	Sys_Init();

	Host_Init(&vb_parms);

	ActiveApp = 1;
	Minimized = false;
	S_UnblockSound();

	Cbuf_AddText("+mlook\n_windowed_mouse 0\n");

	vb_oldtime = Sys_FloatTime();
	vb_quake_initialized = true;
	return 1;
}

// 4. VB6 프레임 렌더링 함수
__declspec(dllexport) void __stdcall VB_QuakeFrame(void)
{
	POINT pt = { 0, 0 };
	double newtime, time;

	if (!vb_quake_initialized) return;

	ClientToScreen(mainwindow, &pt);
	if (pt.x != window_x || pt.y != window_y)
	{
		window_x = pt.x;
		window_y = pt.y;
		VID_UpdateWindowStatus();
	}

	newtime = Sys_FloatTime();
	time = newtime - vb_oldtime;
	vb_oldtime = newtime;

	if (time > sys_ticrate.value * 2)
		Host_Frame(sys_ticrate.value);
	else
		Host_Frame(time);
}

// 5. 마우스 캡처 잠금/해제 함수
__declspec(dllexport) void __stdcall VB_QuakeSetMouseGrab(int grab)
{
	if (!vb_quake_initialized) return;
	Cvar_SetValue("_windowed_mouse", grab ? 1.0f : 0.0f);
	if (grab)
		IN_ActivateMouse();
	else
		IN_DeactivateMouse();
}

// 6. 엔진 종료 함수
__declspec(dllexport) void __stdcall VB_QuakeShutdown(void)
{
	if (!vb_quake_initialized) return;
	vb_quake_initialized = false;

	if (g_old_vb_wndproc && g_vb_hwnd)
	{
		SetWindowLong(g_vb_hwnd, GWL_WNDPROC, (LONG)g_old_vb_wndproc);
		g_old_vb_wndproc = NULL;
	}

	Host_Shutdown();
	if (vb_parms.membase)
	{
		free(vb_parms.membase);
		vb_parms.membase = NULL;
	}
}

// 7. 콘솔 명령어 및 치트키 전송 함수
__declspec(dllexport) void __stdcall VB_QuakeCommand(const char* cmd)
{
	if (!vb_quake_initialized || !cmd) return;
	Cbuf_AddText((char*)cmd);
	Cbuf_AddText("\n");
}

/* 8. 특정 엔티티(플레이어=1 등) 정보 읽기 */
__declspec(dllexport) int __stdcall VB_QuakeGetEntityInfo(int index, char* outClassname, float* x, float* y, float* z, float* health)
{
	edict_t* ent;
	if (!vb_quake_initialized || !sv.active) return 0;
	if (index < 0 || index >= sv.num_edicts) return 0;

	ent = EDICT_NUM(index);
	if (ent->free) return 0;

	if (outClassname && ent->v.classname)
		strcpy(outClassname, pr_strings + (int)ent->v.classname);

	*x = ent->v.origin[0];
	*y = ent->v.origin[1];
	*z = ent->v.origin[2];
	*health = ent->v.health;
	return 1;
}

/* 9. 모든 살아있는 몬스터에게 속도(충격량) 부여 */
__declspec(dllexport) int __stdcall VB_QuakePushAllMonsters(float vx, float vy, float vz)
{
	int i, count = 0;
	if (!vb_quake_initialized || !sv.active) return 0;

	for (i = 1; i < sv.num_edicts; i++)
	{
		edict_t* ent = EDICT_NUM(i);
		if (ent->free) continue;

		if (((int)ent->v.flags & FL_MONSTER) && ent->v.health > 0)
		{
			ent->v.flags = (int)ent->v.flags & ~FL_ONGROUND;
			ent->v.velocity[0] += vx;
			ent->v.velocity[1] += vy;
			ent->v.velocity[2] += vz;
			count++;
		}
	}
	return count;
}

/* 10. 모든 몬스터 AI 일시 정지 / 재개 */
__declspec(dllexport) void __stdcall VB_QuakeFreezeMonsters(int freeze)
{
	int i;
	if (!vb_quake_initialized || !sv.active) return;

	for (i = 1; i < sv.num_edicts; i++)
	{
		edict_t* ent = EDICT_NUM(i);
		if (ent->free) continue;

		if (((int)ent->v.flags & FL_MONSTER) && ent->v.health > 0)
		{
			if (freeze)
			{
				ent->v.nextthink = sv.time + 99999.0f;
				ent->v.velocity[0] = ent->v.velocity[1] = ent->v.velocity[2] = 0;
			}
			else
			{
				ent->v.nextthink = sv.time + 0.1f;
			}
		}
	}
}

/* 11. 동적 구조물(문, 플랫폼) 강제 이동/개방 */
__declspec(dllexport) int __stdcall VB_QuakeControlDoors(float vz)
{
	int i, count = 0;
	if (!vb_quake_initialized || !sv.active) return 0;

	for (i = 1; i < sv.num_edicts; i++)
	{
		edict_t* ent = EDICT_NUM(i);
		const char* cname;
		if (ent->free || !ent->v.classname) continue;

		cname = pr_strings + (int)ent->v.classname;
		if (strncmp(cname, "func_door", 9) == 0 || strncmp(cname, "func_plat", 9) == 0)
		{
			ent->v.velocity[2] = vz;
			ent->v.origin[2] += vz * 0.2f;
			SV_LinkEdict(ent, false);
			count++;
		}
	}
	return count;
}

/* 12. 특정 엔티티의 3차원 속도 직접 제어 */
__declspec(dllexport) void __stdcall VB_QuakeSetEntityPhysics(int index, float vx, float vy, float vz)
{
	edict_t* ent;
	if (!vb_quake_initialized || !sv.active) return;
	if (index < 1 || index >= sv.num_edicts) return;

	ent = EDICT_NUM(index);
	if (ent->free) return;

	ent->v.flags = (int)ent->v.flags & ~FL_ONGROUND;
	ent->v.velocity[0] = vx;
	ent->v.velocity[1] = vy;
	ent->v.velocity[2] = vz;
}