// Matrix Screensaver
// Copyright (c) J Brown 2003 (catch22.net)
// Copyright (c) 2011-2021 Henry++

#include "routine.h"

#include "main.h"
#include "rapp.h"

#include "resource.h"

STATIC_DATA config;

#define RND_MAX INT_MAX

VOID ReadSettings ()
{
	config.speed = _r_config_getinteger (L"Speed", SPEED_DEFAULT);
	config.amount = _r_config_getinteger (L"NumGlyphs", AMOUNT_DEFAULT);
	config.density = _r_config_getinteger (L"Density", DENSITY_DEFAULT);
	config.hue = _r_config_getinteger (L"Hue", HUE_DEFAULT);

	config.is_esc_only = _r_config_getboolean (L"IsEscOnly", FALSE);

	config.is_random = _r_config_getboolean (L"Random", HUE_RANDOM);
	config.is_smooth = _r_config_getboolean (L"RandomSmoothTransition", HUE_RANDOM_SMOOTHTRANSITION);
}

VOID SaveSettings ()
{
	_r_config_setinteger (L"Speed", config.speed);
	_r_config_setinteger (L"NumGlyphs", config.amount);
	_r_config_setinteger (L"Density", config.density);
	_r_config_setinteger (L"Hue", config.hue);

	_r_config_setboolean (L"IsEscOnly", config.is_esc_only);

	_r_config_setboolean (L"Random", config.is_random);
	_r_config_setboolean (L"RandomSmoothTransition", config.is_smooth);
}

FORCEINLINE COLORREF HSLtoRGB (WORD h, WORD s, WORD l)
{
	return ColorHLSToRGB (h, l, s);
}

FORCEINLINE VOID RGBtoHSL (COLORREF clr, PWORD h, PWORD s, PWORD l)
{
	ColorRGBToHLS (clr, h, l, s);
}

FORCEINLINE GLYPH GlyphIntensity (GLYPH glyph)
{
	return ((glyph & 0x7F00) >> 8);
}

FORCEINLINE GLYPH RandomGlyph (INT intensity)
{
	return GLYPH_REDRAW | (intensity << 8) | (_r_math_rand (0, RND_MAX) % config.amount);
}

FORCEINLINE GLYPH DarkenGlyph (GLYPH glyph)
{
	GLYPH intensity = GlyphIntensity (glyph);

	if (intensity > 0)
	{
		return GLYPH_REDRAW | ((intensity - 1) << 8) | (glyph & 0x00FF);
	}

	return glyph;
}

FORCEINLINE VOID DrawGlyph (PMATRIX matrix, HDC hdc, INT xpos, INT ypos, GLYPH glyph)
{
	GLYPH intensity = GlyphIntensity (glyph);
	INT glyph_idx = glyph & 0xff;

	BitBlt (hdc, xpos, ypos, GLYPH_WIDTH, GLYPH_HEIGHT, matrix->hdc, glyph_idx * GLYPH_WIDTH, intensity * GLYPH_HEIGHT, SRCCOPY);
}

FORCEINLINE VOID RedrawBlip (PGLYPH glyph_arr, INT blip_pos)
{
	glyph_arr[blip_pos + 0] |= GLYPH_REDRAW;
	glyph_arr[blip_pos + 1] |= GLYPH_REDRAW;
	glyph_arr[blip_pos + 8] |= GLYPH_REDRAW;
	glyph_arr[blip_pos + 9] |= GLYPH_REDRAW;
}

VOID ScrollMatrixColumn (PMATRIX_COLUMN column)
{
	GLYPH last_glyph;
	GLYPH current_glyph;
	GLYPH current_glyph_intensity;

	// wait until we are allowed to scroll
	if (!column->is_started)
	{
		if (--column->countdown <= 0)
			column->is_started = TRUE;

		return;
	}

	// "seed" the glyph-run
	last_glyph = column->state ? 0 : (MAX_INTENSITY << 8);

	//
	// loop over the entire length of the column, looking for changes
	// in intensity/darkness. This change signifies the start/end
	// of a run of glyphs.
	//
	for (INT y = 0; y < column->length; y++)
	{
		current_glyph = column->glyph[y];

		current_glyph_intensity = GlyphIntensity (current_glyph);

		// bottom-most part of "run". Insert a new character (glyph)
		// at the end to lengthen the run down the screen..gives the
		// impression that the run is "falling" down the screen
		if (current_glyph_intensity < GlyphIntensity (last_glyph) && current_glyph_intensity == 0)
		{
			column->glyph[y] = RandomGlyph (MAX_INTENSITY - 1);
			y += 1;
		}
		// top-most part of "run". Delete a character off the top by
		// darkening the glyph until it eventually disappears (turns black). 
		// this gives the effect that the run as dropped downwards
		else if (current_glyph_intensity > GlyphIntensity (last_glyph))
		{
			column->glyph[y] = DarkenGlyph (current_glyph);

			// if we've just darkened the last bit, skip on so
			// the whole run doesn't go dark
			if (current_glyph_intensity == MAX_INTENSITY - 1)
				y++;
		}

		last_glyph = column->glyph[y];
	}

	// change state from blanks <-> runs when the current run as expired
	if (--column->run_length <= 0)
	{
		INT density = DENSITY_MAX - config.density + DENSITY_MIN;

		if (column->state ^= 1)
		{
			column->run_length = _r_math_rand (0, RND_MAX) % (3 * density / 2) + DENSITY_MIN;
		}
		else
		{
			column->run_length = _r_math_rand (0, RND_MAX) % (DENSITY_MAX + 1 - density) + (DENSITY_MIN * 2);
		}
	}

	// mark current blip as redraw so it gets "erased"
	if (column->blip_pos >= 0 && column->blip_pos < column->length)
		RedrawBlip (column->glyph, column->blip_pos);

	// advance down screen at double-speed
	column->blip_pos += 2;

	// if the blip gets to the end of a run, start it again (for a random
	// length so that the blips never get synched together)
	if (column->blip_pos >= column->blip_length)
	{
		column->blip_length = column->length + (_r_math_rand (0, RND_MAX) % 50);
		column->blip_pos = 0;
	}

	// now redraw blip at new position
	if (column->blip_pos >= 0 && column->blip_pos < column->length)
		RedrawBlip (column->glyph, column->blip_pos);
}

//
// randomly change a small collection glyphs in a column
//
VOID RandomMatrixColumn (PMATRIX_COLUMN column)
{
	ULONG rand;

	for (INT i = 1, y = 0; i < 16; i++)
	{
		// find a run
		while (y < column->length && GlyphIntensity (column->glyph[y]) < (MAX_INTENSITY - 1))
			y += 1;

		if (y >= column->length)
			break;

		rand = _r_math_rand (0, RND_MAX);

		column->glyph[y] = (column->glyph[y] & 0xFF00) | (rand % config.amount);
		column->glyph[y] |= GLYPH_REDRAW;

		y += rand % 10;
	}
}

VOID RedrawMatrixColumn (PMATRIX_COLUMN column, PMATRIX matrix, HDC hdc, INT xpos)
{
	// loop down the length of the column redrawing only what needs doing
	for (INT y = 0; y < column->length; y++)
	{
		GLYPH glyph = column->glyph[y];

		// does this glyph (character) need to be redrawn?
		if (glyph & GLYPH_REDRAW)
		{
			if ((GlyphIntensity (glyph) >= MAX_INTENSITY - 1) && (y == column->blip_pos + 0 || y == column->blip_pos + 1 || y == column->blip_pos + 8 || y == column->blip_pos + 9))
				glyph |= MAX_INTENSITY << 8;

			DrawGlyph (matrix, hdc, xpos, y * GLYPH_HEIGHT, glyph);

			// clear redraw state
			column->glyph[y] &= ~GLYPH_REDRAW;
		}
	}
}

HBITMAP MakeBitmap (HDC hdc, HINSTANCE hinst, UINT type, INT hue)
{
	DIBSECTION dib = {0};
	LPBITMAPINFOHEADER lpbih;
	PULONG dest = NULL;
	PBYTE src;

	RGBQUAD pal[256] = {0};

	// load the 8bit image
	HBITMAP hglyph = LoadImage (hinst, MAKEINTRESOURCE (IDR_GLYPH), IMAGE_BITMAP, 0, 0, LR_CREATEDIBSECTION);

	// extract the colour table
	HDC hdc_c = CreateCompatibleDC (hdc);
	HANDLE hbitmap_old = SelectObject (hdc_c, hglyph);
	GetDIBColorTable (hdc_c, 0, RTL_NUMBER_OF (pal), pal);
	SelectObject (hdc_c, hbitmap_old);

	GetObject (hglyph, sizeof (dib), &dib);

	src = dib.dsBm.bmBits;
	lpbih = &dib.dsBmih;

	// change to a 32bit bitmap
	dib.dsBm.bmBitsPixel = 32;
	dib.dsBm.bmPlanes = 1;
	dib.dsBm.bmWidthBytes = dib.dsBm.bmWidth * 4;
	dib.dsBmih.biBitCount = 32;
	dib.dsBmih.biPlanes = 1;
	dib.dsBmih.biCompression = BI_RGB;
	dib.dsBmih.biSizeImage = (dib.dsBmih.biWidth * dib.dsBmih.biHeight) * 4;

	// create a new (blank) 32bit DIB section
	HBITMAP hDIB = CreateDIBSection (hdc_c, (LPBITMAPINFO)&dib.dsBmih, DIB_RGB_COLORS, &dest, 0, 0);

	// copy each pixel
	for (LONG i = 0; i < lpbih->biWidth * lpbih->biHeight; i++)
	{
		// convert 8bit palette entry to 32bit colour
		RGBQUAD rgb = pal[*src++];
		COLORREF clr = RGB (rgb.rgbRed, rgb.rgbGreen, rgb.rgbBlue);

		// convert the RGB colour to H,S,L values
		WORD h, s, l;
		RGBtoHSL (clr, &h, &s, &l);

		// create the new colour
		*dest++ = HSLtoRGB ((WORD)hue, s, l);
	}

	DeleteObject (hglyph);
	DeleteDC (hdc_c);

	return hDIB;
}

VOID SetMatrixBitmap (HDC hdc, PMATRIX matrix, INT hue)
{
	HBITMAP hbitmap;
	HBITMAP hbitmap_old;

	hbitmap = MakeBitmap (hdc, _r_sys_getimagebase (), IDR_GLYPH, hue);

	if (!hbitmap)
		return;

	hbitmap_old = SelectObject (matrix->hdc, hbitmap);

	if (hbitmap_old)
		DeleteObject (hbitmap_old);

	matrix->hbitmap = hbitmap;

	SelectObject (matrix->hdc, matrix->hbitmap);
}

VOID DecodeMatrix (HWND hwnd, PMATRIX matrix)
{
	PMATRIX_COLUMN column;
	HDC hdc;
	static INT new_hue = 0;

	hdc = GetDC (hwnd);

	if (!hdc)
		return;

	if (!new_hue)
		new_hue = config.hue;

	for (INT x = 0; x < matrix->numcols; x++)
	{
		column = &matrix->column[x];

		RandomMatrixColumn (column);
		ScrollMatrixColumn (column);
		RedrawMatrixColumn (column, matrix, hdc, x * GLYPH_WIDTH);
	}

	if (config.is_random)
	{
		if (config.is_smooth)
		{
			new_hue = (new_hue >= HUE_MAX) ? HUE_MIN : new_hue + 1;
		}
		else
		{
			if (_r_sys_gettickcount () % 2)
				new_hue = (INT)_r_math_rand (HUE_MIN, HUE_MAX);
		}
	}
	else
	{
		new_hue = config.hue;
	}

	SetMatrixBitmap (hdc, matrix, new_hue);

	ReleaseDC (hwnd, hdc);
}

PMATRIX CreateMatrix (INT width, INT height)
{
	PMATRIX matrix;
	INT numcols = width / GLYPH_WIDTH + 1;
	INT numrows = height / GLYPH_HEIGHT + 1;

	matrix = _r_mem_allocatezero (sizeof (MATRIX) + (sizeof (MATRIX_COLUMN) * numcols));

	matrix->numcols = numcols;
	matrix->numrows = numrows;
	matrix->width = width;
	matrix->height = height;

	for (INT x = 0; x < numcols; x++)
	{
		matrix->column[x].length = numrows;
		matrix->column[x].countdown = _r_math_rand (0, RND_MAX) % 100;
		matrix->column[x].state = _r_math_rand (0, RND_MAX) % 2;
		matrix->column[x].run_length = _r_math_rand (0, RND_MAX) % 20 + 3;

		matrix->column[x].glyph = _r_mem_allocatezero (sizeof (GLYPH) * (numrows + 16));
	}

	HDC hdc = GetDC (NULL);

	if (hdc)
	{
		matrix->hdc = CreateCompatibleDC (hdc);
		matrix->hbitmap = MakeBitmap (hdc, _r_sys_getimagebase (), IDR_GLYPH, config.hue);

		SelectObject (matrix->hdc, matrix->hbitmap);

		ReleaseDC (NULL, hdc);
	}

	return matrix;
}

VOID DestroyMatrix (PMATRIX matrix)
{
	DeleteDC (matrix->hdc);
	DeleteObject (matrix->hbitmap);

	for (INT x = 0; x < matrix->numcols; x++)
	{
		PGLYPH glyph = matrix->column[x].glyph;

		if (glyph)
		{
			matrix->column[x].glyph = NULL;

			_r_mem_free (glyph);
		}
	}

	_r_mem_free (matrix);
}

LRESULT CALLBACK ScreensaverProc (HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam)
{
	PMATRIX matrix;

	static POINT pt_last = {0};
	static POINT pt_cursor = {0};
	static BOOLEAN is_firsttime = TRUE;

	switch (msg)
	{
		case WM_NCCREATE:
		{
			LPCREATESTRUCT pcs = (LPCREATESTRUCT)lparam;

			if (!config.hmatrix)
				config.hmatrix = hwnd;

			matrix = CreateMatrix (pcs->cx, pcs->cy);

			if (!matrix)
				return FALSE;

			SetWindowLongPtr (hwnd, GWLP_USERDATA, (LONG_PTR)matrix);
			SetTimer (hwnd, UID, ((SPEED_MAX - config.speed) + SPEED_MIN) * 10, 0);

			return TRUE;
		}

		case WM_NCDESTROY:
		{
			KillTimer (hwnd, UID);

			matrix = (PMATRIX)GetWindowLongPtr (hwnd, GWLP_USERDATA);

			if (matrix)
			{
				SetWindowLongPtr (hwnd, GWLP_USERDATA, 0);

				DestroyMatrix (matrix);
			}

			if (config.is_preview && !GetParent (hwnd))
				return FALSE;

			PostQuitMessage (0);

			return FALSE;
		}

		case WM_CLOSE:
		{
			config.hmatrix = NULL;

			KillTimer (hwnd, UID);
			DestroyWindow (hwnd);

			return FALSE;
		}

		case WM_TIMER:
		{
			matrix = (PMATRIX)GetWindowLongPtr (hwnd, GWLP_USERDATA);

			if (matrix)
				DecodeMatrix (hwnd, matrix);

			return FALSE;
		}

		case WM_KEYDOWN:
		case WM_SYSKEYDOWN:
		{
			if (wparam != VK_ESCAPE && config.is_esc_only)
				return FALSE;

			PostMessage (hwnd, WM_CLOSE, 0, 0);
			return FALSE;
		}

		case WM_MOUSEMOVE:
		{
			INT icon_size;

			if (GetParent (hwnd) || config.is_esc_only)
				return FALSE;

			if (is_firsttime)
			{
				GetCursorPos (&pt_last);
				is_firsttime = FALSE;
			}

			GetCursorPos (&pt_cursor);

			icon_size = _r_dc_getsystemmetrics (hwnd, SM_CXSMICON);

			if (abs (pt_cursor.x - pt_last.x) >= (icon_size / 2) || abs (pt_cursor.y - pt_last.y) >= (icon_size / 2))
			{
				PostMessage (hwnd, WM_CLOSE, 0, 0);
			}

			pt_last = pt_cursor;

			return FALSE;
		}

		case WM_LBUTTONDOWN:
		case WM_RBUTTONDOWN:
		case WM_MBUTTONDOWN:
		{
			if (GetParent (hwnd) || config.is_esc_only)
				return FALSE;

			PostMessage (hwnd, WM_CLOSE, 0, 0);

			break;
		}
	}

	return DefWindowProc (hwnd, msg, wparam, lparam);
}

BOOL CALLBACK MonitorEnumProc (HMONITOR hmonitor, HDC hdc, PRECT rect, LPARAM lparam)
{
	HWND hparent = (HWND)lparam;
	ULONG style = hparent ? WS_CHILD : WS_POPUP;

	HWND hwnd = CreateWindowEx (WS_EX_TOPMOST | WS_EX_TOOLWINDOW, hparent ? CLASS_PREVIEW : CLASS_FULLSCREEN, APP_NAME, WS_VISIBLE | style, rect->left, rect->top, _r_calc_rectwidth (rect), _r_calc_rectheight (rect), hparent, NULL, _r_sys_getimagebase (), NULL);

	if (hwnd)
		SetWindowPos (hwnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOOWNERZORDER | SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_SHOWWINDOW | SWP_FRAMECHANGED);

	return TRUE;
}

VOID StartScreensaver (HWND hparent)
{
	UINT state = 0;

	if (!hparent)
	{
		SystemParametersInfo (SPI_SETSCREENSAVERRUNNING, TRUE, &state, SPIF_SENDWININICHANGE);

		EnumDisplayMonitors (NULL, NULL, &MonitorEnumProc, 0);

		SystemParametersInfo (SPI_SETSCREENSAVERRUNNING, FALSE, &state, SPIF_SENDWININICHANGE);
	}
	else
	{
		RECT rect;

		if (GetClientRect (hparent, &rect))
			MonitorEnumProc (NULL, NULL, &rect, (LPARAM)hparent);
	}
}

INT_PTR CALLBACK SettingsProc (HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam)
{
	switch (msg)
	{
		case WM_INITDIALOG:
		{
			HWND hpreview = GetDlgItem (hwnd, IDC_PREVIEW);

			// localize window
			_r_ctrl_settextformat (hwnd, IDC_AMOUNT_RANGE, L"%d-%d", AMOUNT_MIN, AMOUNT_MAX);
			_r_ctrl_settextformat (hwnd, IDC_DENSITY_RANGE, L"%d-%d", DENSITY_MIN, DENSITY_MAX);
			_r_ctrl_settextformat (hwnd, IDC_SPEED_RANGE, L"%d-%d", SPEED_MIN, SPEED_MAX);
			_r_ctrl_settextformat (hwnd, IDC_HUE_RANGE, L"%d-%d", HUE_MIN, HUE_MAX);

			SendDlgItemMessage (hwnd, IDC_AMOUNT, UDM_SETRANGE32, AMOUNT_MIN, AMOUNT_MAX);
			SendDlgItemMessage (hwnd, IDC_AMOUNT, UDM_SETPOS32, 0, (LPARAM)config.amount);

			SendDlgItemMessage (hwnd, IDC_DENSITY, UDM_SETRANGE32, DENSITY_MIN, DENSITY_MAX);
			SendDlgItemMessage (hwnd, IDC_DENSITY, UDM_SETPOS32, 0, (LPARAM)config.density);

			SendDlgItemMessage (hwnd, IDC_SPEED, UDM_SETRANGE32, SPEED_MIN, SPEED_MAX);
			SendDlgItemMessage (hwnd, IDC_SPEED, UDM_SETPOS32, 0, (LPARAM)config.speed);

			SendDlgItemMessage (hwnd, IDC_HUE, UDM_SETRANGE32, HUE_MIN, HUE_MAX);
			SendDlgItemMessage (hwnd, IDC_HUE, UDM_SETPOS32, 0, (LPARAM)config.hue);

			CheckDlgButton (hwnd, IDC_RANDOMIZECOLORS_CHK, config.is_random);
			CheckDlgButton (hwnd, IDC_RANDOMIZESMOOTH_CHK, config.is_smooth);
			CheckDlgButton (hwnd, IDC_ISCLOSEONESC_CHK, config.is_esc_only);

			SendMessage (hwnd, WM_COMMAND, MAKEWPARAM (IDC_RANDOMIZECOLORS_CHK, 0), 0);
			SendMessage (hwnd, WM_COMMAND, MAKEWPARAM (IDC_ISCLOSEONESC_CHK, 0), 0);

			_r_ctrl_settextformat (hwnd, IDC_ABOUT, L"<a href=\"%s\">Website</a> | <a href=\"%s\">Github</a>", _r_app_getwebsite_url (), _r_app_getsources_url ());

			BOOLEAN is_classic = _r_app_isclassicui ();

			_r_wnd_addstyle (hwnd, IDC_SHOW, is_classic ? WS_EX_STATICEDGE : 0, WS_EX_STATICEDGE, GWL_EXSTYLE);
			_r_wnd_addstyle (hwnd, IDC_RESET, is_classic ? WS_EX_STATICEDGE : 0, WS_EX_STATICEDGE, GWL_EXSTYLE);
			_r_wnd_addstyle (hwnd, IDC_CLOSE, is_classic ? WS_EX_STATICEDGE : 0, WS_EX_STATICEDGE, GWL_EXSTYLE);

			StartScreensaver (hpreview);

			break;
		}

		case WM_NCCREATE:
		{
			_r_wnd_enablenonclientscaling (hwnd);
			break;
		}

		case WM_CLOSE:
		{
			DestroyWindow (hwnd);
			break;
		}

		case WM_DESTROY:
		{
			SaveSettings ();
			PostQuitMessage (0);

			break;
		}

		case WM_CTLCOLORSTATIC:
		{
			INT ctrl_id = GetDlgCtrlID ((HWND)lparam);

			if (
				ctrl_id == IDC_AMOUNT_RANGE ||
				ctrl_id == IDC_DENSITY_RANGE ||
				ctrl_id == IDC_SPEED_RANGE ||
				ctrl_id == IDC_HUE_RANGE
				)
			{
				SetBkMode ((HDC)wparam, TRANSPARENT); // background-hack
				SetTextColor ((HDC)wparam, GetSysColor (COLOR_GRAYTEXT));

				return (INT_PTR)GetSysColorBrush (COLOR_BTNFACE);
			}

			break;
		}

		case WM_VSCROLL:
		case WM_HSCROLL:
		{
			INT ctrl_id = GetDlgCtrlID ((HWND)lparam);

			SendMessage (hwnd, WM_COMMAND, MAKEWORD (ctrl_id - 1, 0), 0);

			break;
		}

		case WM_LBUTTONDOWN:
		{
			PostMessage (hwnd, WM_SYSCOMMAND, SC_MOVE | HTCAPTION, 0);
			break;
		}

		case WM_ENTERSIZEMOVE:
		case WM_EXITSIZEMOVE:
		case WM_CAPTURECHANGED:
		{
			LONG_PTR exstyle = GetWindowLongPtr (hwnd, GWL_EXSTYLE);

			if ((exstyle & WS_EX_LAYERED) == 0)
				SetWindowLongPtr (hwnd, GWL_EXSTYLE, exstyle | WS_EX_LAYERED);

			SetLayeredWindowAttributes (hwnd, 0, (msg == WM_ENTERSIZEMOVE) ? 100 : 255, LWA_ALPHA);
			SetCursor (LoadCursor (NULL, (msg == WM_ENTERSIZEMOVE) ? IDC_SIZEALL : IDC_ARROW));

			break;
		}

		case WM_NOTIFY:
		{
			LPNMHDR nmlp = (LPNMHDR)lparam;

			switch (nmlp->code)
			{
				case NM_CLICK:
				case NM_RETURN:
				{
					PNMLINK nmlink = (PNMLINK)lparam;

					if (!_r_str_isempty (nmlink->item.szUrl))
						ShellExecute (hwnd, NULL, nmlink->item.szUrl, NULL, NULL, SW_SHOWDEFAULT);

					break;
				}
			}

			break;
		}

		case WM_COMMAND:
		{
			INT ctrl_id = LOWORD (wparam);

			switch (ctrl_id)
			{
				//case IDCANCEL: // process Esc key
				case IDC_CLOSE:
				{
					PostMessage (hwnd, WM_CLOSE, 0, 0);
					break;
				}

				case IDC_RESET:
				{
					if (_r_show_message (hwnd, MB_YESNO | MB_ICONEXCLAMATION | MB_DEFBUTTON2, APP_NAME, NULL, L"Are you really sure you want to reset all application settings?") != IDYES)
						break;

					ReadSettings ();

					CheckDlgButton (hwnd, IDC_RANDOMIZECOLORS_CHK, config.is_random ? BST_CHECKED : BST_UNCHECKED);
					CheckDlgButton (hwnd, IDC_RANDOMIZESMOOTH_CHK, config.is_smooth ? BST_CHECKED : BST_UNCHECKED);
					CheckDlgButton (hwnd, IDC_ISCLOSEONESC_CHK, config.is_esc_only ? BST_CHECKED : BST_UNCHECKED);

					SendDlgItemMessage (hwnd, IDC_AMOUNT, UDM_SETPOS32, 0, AMOUNT_DEFAULT);
					SendDlgItemMessage (hwnd, IDC_DENSITY, UDM_SETPOS32, 0, DENSITY_DEFAULT);
					SendDlgItemMessage (hwnd, IDC_SPEED, UDM_SETPOS32, 0, SPEED_DEFAULT);
					SendDlgItemMessage (hwnd, IDC_HUE, UDM_SETPOS32, 0, HUE_DEFAULT);

					KillTimer (config.hmatrix, UID);
					SetTimer (config.hmatrix, UID, ((SPEED_MAX - SPEED_DEFAULT) + SPEED_MIN) * 10, 0);

					PostMessage (hwnd, WM_COMMAND, MAKEWPARAM (IDC_RANDOMIZECOLORS_CHK, 0), 0);
					PostMessage (hwnd, WM_COMMAND, MAKEWPARAM (IDC_ISCLOSEONESC_CHK, 0), 0);

					break;
				}

				case IDC_SHOW:
				{
					RECT rect;

					MONITORINFO monitor_info = {0};
					monitor_info.cbSize = sizeof (monitor_info);

					HMONITOR hmonitor = MonitorFromWindow (hwnd, MONITOR_DEFAULTTONEAREST);

					if (hmonitor)
					{
						if (GetMonitorInfo (hmonitor, &monitor_info))
							CopyRect (&rect, &monitor_info.rcMonitor);
					}
					else
					{
						SetRect (&rect,
								 0,
								 0,
								 _r_dc_getsystemmetrics (hwnd, SM_CXFULLSCREEN),
								 _r_dc_getsystemmetrics (hwnd, SM_CYFULLSCREEN)
						);
					}

					MonitorEnumProc (NULL, NULL, &rect, 0);

					break;
				}

				case IDC_AMOUNT_CTRL:
				{
					config.amount = (INT)SendDlgItemMessage (hwnd, IDC_AMOUNT, UDM_GETPOS32, 0, 0);
					break;
				}

				case IDC_DENSITY_CTRL:
				{
					config.density = (INT)SendDlgItemMessage (hwnd, IDC_DENSITY, UDM_GETPOS32, 0, 0);
					break;
				}

				case IDC_SPEED_CTRL:
				{
					INT new_value = (INT)SendDlgItemMessage (hwnd, IDC_SPEED, UDM_GETPOS32, 0, 0);

					KillTimer (config.hmatrix, UID);
					SetTimer (config.hmatrix, UID, ((SPEED_MAX - new_value) + SPEED_MIN) * 10, 0);

					config.speed = new_value;

					break;
				}

				case IDC_HUE_CTRL:
				{
					config.hue = (INT)SendDlgItemMessage (hwnd, IDC_HUE, UDM_GETPOS32, 0, 0);
					break;
				}

				case IDC_RANDOMIZECOLORS_CHK:
				{
					BOOLEAN is_enabled = (IsDlgButtonChecked (hwnd, ctrl_id) == BST_CHECKED);

					_r_ctrl_enable (hwnd, IDC_HUE_CTRL, !is_enabled);
					_r_ctrl_enable (hwnd, IDC_HUE, !is_enabled);
					_r_ctrl_enable (hwnd, IDC_RANDOMIZESMOOTH_CHK, is_enabled);

					config.is_random = is_enabled;

					break;
				}

				case IDC_RANDOMIZESMOOTH_CHK:
				{
					config.is_smooth = (IsDlgButtonChecked (hwnd, ctrl_id) == BST_CHECKED);
					break;
				}

				case IDC_ISCLOSEONESC_CHK:
				{
					config.is_esc_only = (IsDlgButtonChecked (hwnd, ctrl_id) == BST_CHECKED);
					break;
				}
			}

			break;
		}
	}

	return FALSE;
}

BOOLEAN RegisterClasses (HINSTANCE hinst)
{
	// register window class
	WNDCLASSEX wcex = {0};

	wcex.cbSize = sizeof (wcex);
	wcex.hInstance = hinst;
	wcex.style = CS_VREDRAW | CS_HREDRAW | CS_SAVEBITS | CS_PARENTDC;
	wcex.lpfnWndProc = &ScreensaverProc;
	wcex.hbrBackground = CreateSolidBrush (RGB (0, 0, 0));
	wcex.cbWndExtra = sizeof (PMATRIX);

	wcex.lpszClassName = CLASS_PREVIEW;
	wcex.hCursor = LoadCursor (NULL, IDC_ARROW);

	if (!RegisterClassEx (&wcex))
	{
		_r_show_errormessage (NULL, NULL, GetLastError (), NULL, NULL);
		return FALSE;
	}

	wcex.lpszClassName = CLASS_FULLSCREEN;
	wcex.hCursor = LoadCursor (hinst, MAKEINTRESOURCE (IDR_CURSOR));

	if (!RegisterClassEx (&wcex))
	{
		_r_show_errormessage (NULL, NULL, GetLastError (), NULL, NULL);
		return FALSE;
	}

	return TRUE;
}

INT APIENTRY wWinMain (_In_ HINSTANCE hinst, _In_opt_ HINSTANCE prev_hinst, _In_ LPWSTR cmdline, _In_ INT show_cmd)
{
	MSG msg;

	RtlSecureZeroMemory (&config, sizeof (config));

	if (!_r_app_initialize ())
		return ERROR_NOT_READY;

	// read settings
	ReadSettings ();

	// register classes
	if (!RegisterClasses (hinst))
		goto CleanupExit;

	// parse arguments
	if (_r_str_compare_length (cmdline, L"/s", 2) == 0)
	{
		StartScreensaver (NULL);
	}
	else if (_r_str_compare_length (cmdline, L"/p", 2) == 0)
	{
		HWND hctrl = (HWND)_r_str_tolong64 (cmdline + 3);

		if (hctrl)
			StartScreensaver (hctrl);
	}
	else
	{
		config.is_preview = TRUE;

		if (!_r_app_createwindow (IDD_SETTINGS, IDI_MAIN, &SettingsProc))
			goto CleanupExit;
	}

	while (GetMessage (&msg, NULL, 0, 0) > 0)
	{
		HWND hwnd = _r_app_gethwnd ();

		if (config.is_preview && IsDialogMessage (hwnd, &msg))
			continue;

		TranslateMessage (&msg);
		DispatchMessage (&msg);
	}

CleanupExit:

	UnregisterClass (CLASS_PREVIEW, hinst);
	UnregisterClass (CLASS_FULLSCREEN, hinst);

	return ERROR_SUCCESS;
}
