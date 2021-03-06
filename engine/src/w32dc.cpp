/* Copyright (C) 2003-2013 Runtime Revolution Ltd.

This file is part of LiveCode.

LiveCode is free software; you can redistribute it and/or modify it under
the terms of the GNU General Public License v3 as published by the Free
Software Foundation.

LiveCode is distributed in the hope that it will be useful, but WITHOUT ANY
WARRANTY; without even the implied warranty of MERCHANTABILITY or
FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
for more details.

You should have received a copy of the GNU General Public License
along with LiveCode.  If not see <http://www.gnu.org/licenses/>.  */

#include "w32prefix.h"

#include "globdefs.h"
#include "filedefs.h"
#include "objdefs.h"
#include "parsedef.h"

#include "dispatch.h"
#include "osspec.h"
#include "image.h"
#include "stack.h"
#include "util.h"
#include "stacklst.h"
#include "execpt.h"
#include "globals.h"
#include "core.h"
#include "notify.h"

#include "w32dc.h"
#include "w32printer.h"
#include "w32context.h"

#include "mctheme.h"

#include "graphicscontext.h"
#include "graphics_util.h"

////////////////////////////////////////////////////////////////////////////////

static inline POINT MCPointToWin32POINT(const MCPoint &p_point)
{
	POINT t_point;
	t_point.x = p_point.x;
	t_point.y = p_point.y;

	return t_point;
}

////////////////////////////////////////////////////////////////////////////////

MCScreenDC::MCScreenDC()
{
	f_src_dc = NULL;
	f_dst_dc = NULL;

#ifdef FEATURE_TASKBAR_ICON
	f_has_icon = False;
	f_icon_menu = NULL;
#endif

	pendingevents = NULL;
	beeppitch = 440L;                    //440 Hz
	beepduration = 100;                 // 1/100 second
	grabbingclipboard = False;
	exposures = False;
	opened = 0;
	dnddata = NULL;
	taskbarhidden = False;

	backdrop_active = false;
	backdrop_hard = false;
	backdrop_window = NULL;
	memset(&backdrop_colour, 0, sizeof(MCColor));
	backdrop_pattern = NULL;
	backdrop_badge = NULL;

	m_printer_dc = NULL;
	m_printer_dc_locked = false;
	m_printer_dc_changed = false;

	m_clipboard = NULL;

	MCNotifyInitialize();
}

MCScreenDC::~MCScreenDC()
{
	MCNotifyFinalize();

	showtaskbar();
	while (opened)
		close(True);
	if (ncolors != 0)
	{
		uint2 i;
		for (i = 0 ; i < ncolors ; i++)
		{
			if (colornames[i] != NULL)
				delete colornames[i];
		}
		delete colors;
		delete colornames;
		delete allocs;
	}
	while (pendingevents != NULL)
	{
		MCEventnode *tptr =(MCEventnode *)pendingevents->remove(pendingevents);
		delete tptr;
	}
}

bool MCScreenDC::hasfeature(MCPlatformFeature p_feature)
{
	switch(p_feature)
	{
	case PLATFORM_FEATURE_WINDOW_TRANSPARENCY:
		return MCmajorosversion >= 0x0500;
	break;

	case PLATFORM_FEATURE_OS_COLOR_DIALOGS:
	case PLATFORM_FEATURE_OS_FILE_DIALOGS:
	case PLATFORM_FEATURE_OS_PRINT_DIALOGS:
		return true;
	break;
	
	case PLATFORM_FEATURE_TRANSIENT_SELECTION:
		return false;
	break;

	default:
		assert(false);
	break;
	}

	return false;
}
// TD-2013-07-01 [[ DynamicFonts ]]
bool MCScreenDC::loadfont(const char *p_path, bool p_globally, void*& r_loaded_font_handle)
{
	bool t_success = true;
    DWORD t_private = NULL;
    
    if (!p_globally)
        t_private = FR_PRIVATE;
    
	if (t_success)
		t_success = (MCS_exists(p_path, True) == True);
	
	if (t_success)
		t_success = (AddFontResourceExA(p_path, t_private, 0) != 0);
    
	if (t_success && p_globally)
		PostMessage(HWND_BROADCAST, WM_FONTCHANGE, 0, 0);
    
	return t_success;
}


bool MCScreenDC::unloadfont(const char *p_path, bool p_globally, void *r_loaded_font_handle)
{
    bool t_success = true;
    DWORD t_private = NULL;
    
    if (p_globally)
        t_private = FR_PRIVATE;
    
    if (t_success)
		t_success = (RemoveFontResourceExA(p_path, t_private, 0) != 0);
    
	if (t_success && p_globally)
		PostMessage(HWND_BROADCAST, WM_FONTCHANGE, 0, 0);
    
	return t_success;
}

///////////////////////////////////////////////////////////////////////////////

extern int UTF8ToUnicode(const char *p_source_str, int p_source, uint2 *p_dest_str, int p_dest);

LPWSTR MCScreenDC::convertutf8towide(const char *p_utf8_string)
{
	int t_new_length;
	t_new_length = UTF8ToUnicode(p_utf8_string, strlen(p_utf8_string), NULL, 0);

	LPWSTR t_result;
	t_result = new WCHAR[t_new_length + 2];

	t_new_length = UTF8ToUnicode(p_utf8_string, strlen(p_utf8_string), (uint2*)t_result, t_new_length);
	t_result[t_new_length / 2] = 0;

	return t_result;
}

LPCSTR MCScreenDC::convertutf8toansi(const char *p_utf8_string)
{
	LPWSTR t_wide;
	t_wide = convertutf8towide(p_utf8_string);

	int t_length;
	t_length = WideCharToMultiByte(CP_ACP, 0, t_wide, -1, NULL, 0, NULL, NULL);

	LPSTR t_result;
	t_result = new CHAR[t_length];
	WideCharToMultiByte(CP_ACP, 0, t_wide, -1, t_result, t_length, NULL, NULL);

	delete t_wide;

	return t_result;
}

///////////////////////////////////////////////////////////////////////////////

MCPrinter *MCScreenDC::createprinter(void)
{
	return new MCWindowsPrinter;
}

void MCScreenDC::listprinters(MCExecPoint& ep)
{
	ep . clear();

	DWORD t_flags;
	DWORD t_level;
	t_flags = PRINTER_ENUM_LOCAL | PRINTER_ENUM_CONNECTIONS;
	t_level = 4;

	DWORD t_printer_count;
	DWORD t_bytes_needed;
	t_printer_count = 0;
	t_bytes_needed = 0;
	if (EnumPrintersA(t_flags, NULL, t_level, NULL, 0, &t_bytes_needed, &t_printer_count) == 0 && GetLastError() != ERROR_INSUFFICIENT_BUFFER)
		return;

	char *t_printers;
	t_printers = new char[t_bytes_needed];
	if (EnumPrintersA(t_flags, NULL, t_level, (LPBYTE)t_printers, t_bytes_needed, &t_bytes_needed, &t_printer_count) != 0)
	{
		for(uint4 i = 0; i < t_printer_count; ++i)
		{
			const char *t_printer_name;
			t_printer_name = ((PRINTER_INFO_4A *)t_printers)[i] . pPrinterName;
			ep . concatcstring(t_printer_name, EC_RETURN, i == 0);
		}
	}

	delete t_printers;
}

///////////////////////////////////////////////////////////////////////////////

MCStack *MCScreenDC::platform_getstackatpoint(int32_t x, int32_t y)
{
	MCPoint t_loc;
	t_loc = MCPointMake(x, y);

	// IM-2014-01-28: [[ HiDPI ]] Convert logical to screen coordinates
	t_loc = logicaltoscreenpoint(t_loc);

	POINT t_location;
	t_location = MCPointToWin32POINT(t_loc);
	
	HWND t_window;
	t_window = WindowFromPoint(t_location);

	if (t_window == nil)
		return nil;
		
	_Drawable d;
	d . type = DC_WINDOW;
	d . handle . window = (MCSysWindowHandle)t_window;
		
	return MCdispatcher -> findstackd(&d);
}

///////////////////////////////////////////////////////////////////////////////

// IM-2014-01-28: [[ HiDPI ]] Weak-linked IsProcessDPIAware function
typedef BOOL (WINAPI *IsProcessDPIAwarePtr)(VOID);
bool MCWin32IsProcessDPIAware(bool &r_aware)
{
	static IsProcessDPIAwarePtr s_IsProcessDPIAware = NULL;
	static bool s_init = true;

	if (s_init)
	{
		s_IsProcessDPIAware = (IsProcessDPIAwarePtr)GetProcAddress(GetModuleHandleA("user32.dll"), "IsProcessDPIAware");
		s_init = false;
	}

	if (s_IsProcessDPIAware == nil)
		return false;

	r_aware = s_IsProcessDPIAware();

	return true;
}

//////////

typedef enum __MCW32ProcessDPIAwareness
{
	kMCW32ProcessDPIUnaware,
	kMCW32ProcessSystemDPIAware,
	kMCW32ProcessPerMonitorDPIAware,
} MCW32ProcessDPIAwareness;

// IM-2014-01-28: [[ HiDPI ]] Weak-linked GetProcessDPIAwareness function
typedef HRESULT (WINAPI *GetProcessDPIAwarenessPTR)(HANDLE hprocess, MCW32ProcessDPIAwareness *value);
bool MCWin32GetProcessDPIAwareness(MCW32ProcessDPIAwareness &r_awareness)
{
	static GetProcessDPIAwarenessPTR s_GetProcessDPIAwareness = NULL;
	static bool s_init = true;

	if (s_init)
	{
		s_GetProcessDPIAwareness = (GetProcessDPIAwarenessPTR)GetProcAddress(GetModuleHandleA("shcore.dll"), "GetProcessDPIAwareness");
		s_init = false;
	}

	if (s_GetProcessDPIAwareness == nil)
		return false;

	HRESULT t_result;
	t_result = s_GetProcessDPIAwareness(NULL, &r_awareness);

	return t_result == S_OK;
}

//////////

typedef enum __MCW32MonitorDPIType
{
	kMCW32MDTEffectiveDPI,
	kMCW32MDTAngularDPI,
	kMCW32MDTRawDPI,
	kMCW32MDTDefault = kMCW32MDTEffectiveDPI,
} MCW32MonitorDPIType;

// IM-2014-01-28: [[ HiDPI ]] Weak-linked GetDPIForMonitor function
typedef HRESULT (WINAPI *GetDPIForMonitorPTR)(HMONITOR hmonitor, MCW32MonitorDPIType dpiType, UINT *dpiX, UINT *dpiY);
bool MCWin32GetDPIForMonitor(HMONITOR p_monitor, uint32_t &r_xdpi, uint32_t &r_ydpi)
{
	static GetDPIForMonitorPTR s_GetDPIForMonitor = NULL;
	static bool s_init = true;

	if (s_init)
	{
		s_GetDPIForMonitor = (GetDPIForMonitorPTR)GetProcAddress(GetModuleHandleA("shcore.dll"), "GetDPIForMonitor");
		s_init = false;
	}

	if (s_GetDPIForMonitor == nil)
		return false;

	HRESULT t_result;
	UINT t_x, t_y;
	t_result = s_GetDPIForMonitor(p_monitor, kMCW32MDTEffectiveDPI, &t_x, &t_y);

	if (t_result != S_OK)
		return false;

	r_xdpi = t_x;
	r_ydpi = t_y;

	return true;
}

////////////////////////////////////////////////////////////////////////////////

// IM-2014-01-28: [[ HiDPI ]] Return the x & y dpi of the main screen
bool MCWin32GetScreenDPI(uint32_t &r_xdpi, uint32_t &r_ydpi)
{
	HDC t_dc;
	t_dc = GetDC(NULL);

	if (t_dc == NULL)
		return false;

	r_xdpi = GetDeviceCaps(t_dc, LOGPIXELSX);
	r_ydpi = GetDeviceCaps(t_dc, LOGPIXELSY);

	ReleaseDC(NULL, t_dc);

	return true;
}

///////////////////////////////////////////////////////////////////////////////

#define NORMAL_DENSITY (96.0)

// IM-2014-01-28: [[ HiDPI ]] Return the DPI scale factor of the main screen.
//   This gives us the pixelscale for system-DPI-aware applications.
MCGFloat MCWin32GetLogicalToScreenScale(void)
{
	// TODO - determine the correct value on Win8.1 - this may depend on the display in question

	uint32_t t_x, t_y;
	/* UNCHECKED */ MCWin32GetScreenDPI(t_x, t_y);

	return (MCGFloat) MCMax(t_x, t_y) / NORMAL_DENSITY;
}

////////////////////////////////////////////////////////////////////////////////

// IM-2014-01-28: [[ HiDPI ]] Return the DPI scale factor of the given monitor.
//   For system-DPI-aware applications this will be the global system DPI value.
//   For Per-Monitor-DPI-aware applications, this will be the effective DPI scale of the given monitor
bool MCWin32GetMonitorPixelScale(HMONITOR p_monitor, MCGFloat &r_pixel_scale)
{
	uint32_t t_xdpi, t_ydpi;

	// try to get per-monitor DPI setting
	if (!MCWin32GetDPIForMonitor(p_monitor, t_xdpi, t_ydpi) &&
		// fallback to the global system DPI setting
		!MCWin32GetScreenDPI(t_xdpi, t_ydpi))
			return false;

	r_pixel_scale = (MCGFloat)MCMax(t_xdpi, t_ydpi) / NORMAL_DENSITY;

	return true;
}

///////////////////////////////////////////////////////////////////////////////

// IM-2014-01-27: [[ HiDPI ]] Return whether the application is DPI-aware
bool MCResPlatformSupportsPixelScaling(void)
{
	bool t_aware;
	if (MCWin32IsProcessDPIAware(t_aware))
		return t_aware;

	return false;
}

// IM-2014-01-27: [[ HiDPI ]] On Windows, DPI-awareness can only be set at app startup or in the app manifest
bool MCResPlatformCanChangePixelScaling(void)
{
	return false;
}

// IM-2014-01-30: [[ HiDPI ]] Pixel scale cannot be set on Windows
bool MCResPlatformCanSetPixelScale(void)
{
	return false;
}

// IM-2014-01-30: [[ HiDPI ]] Pixel scale is 1.0 on Windows
MCGFloat MCResPlatformGetDefaultPixelScale(void)
{
	return 1.0;
}

// IM-2014-03-14: [[ HiDPI ]] UI scale is 1.0 on Windows
MCGFloat MCResPlatformGetUIDeviceScale(void)
{
	return 1.0;
}

// IM-2014-01-30: [[ HiDPI ]] No-op as this cannot be modified at runtime on Windows
void MCResPlatformHandleScaleChange(void)
{
}

///////////////////////////////////////////////////////////////////////////////

extern MCGFloat MCWin32GetLogicalToScreenScale(void);

MCPoint MCScreenDC::logicaltoscreenpoint(const MCPoint &p_point)
{
	MCGFloat t_scale;
	t_scale = MCWin32GetLogicalToScreenScale();
	return MCPointTransform(p_point, MCGAffineTransformMakeScale(t_scale, t_scale));
}

MCPoint MCScreenDC::screentologicalpoint(const MCPoint &p_point)
{
	MCGFloat t_scale;
	t_scale = 1 / MCWin32GetLogicalToScreenScale();
	return MCPointTransform(p_point, MCGAffineTransformMakeScale(t_scale, t_scale));
}

MCRectangle MCScreenDC::logicaltoscreenrect(const MCRectangle &p_rect)
{
	return MCRectangleGetScaledInterior(p_rect, MCWin32GetLogicalToScreenScale());
}

MCRectangle MCScreenDC::screentologicalrect(const MCRectangle &p_rect)
{
	return MCRectangleGetScaledBounds(p_rect, 1 / MCWin32GetLogicalToScreenScale());
}

///////////////////////////////////////////////////////////////////////////////
