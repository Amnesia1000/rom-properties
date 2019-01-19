/***************************************************************************
 * ROM Properties Page shell extension. (libi18n)                          *
 * i18n.c: Internationalization support code.                              *
 *                                                                         *
 * Copyright (c) 2017-2019 by David Korth.                                 *
 *                                                                         *
 * This program is free software; you can redistribute it and/or modify it *
 * under the terms of the GNU General Public License as published by the   *
 * Free Software Foundation; either version 2 of the License, or (at your  *
 * option) any later version.                                              *
 *                                                                         *
 * This program is distributed in the hope that it will be useful, but     *
 * WITHOUT ANY WARRANTY; without even the implied warranty of              *
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the           *
 * GNU General Public License for more details.                            *
 *                                                                         *
 * You should have received a copy of the GNU General Public License       *
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.   *
 ***************************************************************************/

// NOTE: We need to have at least one file compiled in order to
// build a static library. Otherwise, CMake will complain.

// Since we don't want any code actually compiled here if NLS is disabled,
// wrap the whole thing in #ifdef ENABLE_NLS.

#include "libi18n/config.libi18n.h"
#ifdef ENABLE_NLS

#include "i18n.h"
#ifdef _WIN32
# include "libwin32common/RpWin32_sdk.h"
#endif

/**
 * Number of elements in an array.
 * (from librpbase/common.h)
 *
 * Includes a static check for pointers to make sure
 * a dynamically-allocated array wasn't specified.
 * Reference: http://stackoverflow.com/questions/8018843/macro-definition-array-size
 */
#define ARRAY_SIZE(x) \
	((int)(((sizeof(x) / sizeof(x[0]))) / \
		(size_t)(!(sizeof(x) % sizeof(x[0])))))

#ifdef _WIN32
// Architecture name.
#if defined(_M_X64) || defined(__amd64__)
# define ARCH_NAME _T("amd64")
#elif defined(_M_IX86) || defined(__i386__)
# define ARCH_NAME _T("i386")
#else
# error Unsupported CPU architecture.
#endif

/**
 * Initialize the internationalization subsystem.
 * (Windows version)
 *
 * @return 0 on success; non-zero on error.
 */
int rp_i18n_init(void)
{
	// Windows: Use the application-specific locale directory.
	DWORD dwResult, dwAttrs;
	int ret;
	TCHAR tpathname[MAX_PATH+16];
#ifndef UNICODE
	wchar_t wpathname[MAX_PATH+16];
#else
# define wpathname tpathname
#endif
	char u8pathname[MAX_PATH+16];

	TCHAR *bs;
	const char *base;

	// Get the current module filename.
	// NOTE: Delay-load only supports ANSI module names.
	// We'll assume it's ASCII and do a simple conversion to Unicode.
	SetLastError(ERROR_SUCCESS);
	dwResult = GetModuleFileName(HINST_THISCOMPONENT,
		tpathname, ARRAY_SIZE(tpathname));
	if (dwResult == 0 || GetLastError() != ERROR_SUCCESS) {
		// Cannot get the current module filename.
		// TODO: Windows XP doesn't SetLastError() if the
		// filename is too big for the buffer.
		return -1;
	}

	// Find the last backslash in tpathname[].
	bs = _tcsrchr(tpathname, _T('\\'));
	if (!bs) {
		// No backslashes...
		return -1;
	}

	// Append the "locale" subdirectory.
	_tcscpy(bs+1, _T("locale"));
	dwAttrs = GetFileAttributes(tpathname);
	if (dwAttrs == INVALID_FILE_ATTRIBUTES ||
	    !(dwAttrs & FILE_ATTRIBUTE_DIRECTORY))
	{
		// Not found, or not a directory.
		// Try one level up.
		*bs = 0;
		bs = _tcsrchr(tpathname, _T('\\'));
		if (!bs) {
			// No backslashes...
			return -1;
		}

		// Make sure the current subdirectory matches
		// the DLL architecture.
		if (_tcscmp(bs+1, ARCH_NAME) != 0) {
			// Not a match.
			return -1;
		}

		// Append the "locale" subdirectory.
		_tcscpy(bs+1, _T("locale"));
		dwAttrs = GetFileAttributes(tpathname);
		if (dwAttrs == INVALID_FILE_ATTRIBUTES ||
		    !(dwAttrs & FILE_ATTRIBUTE_DIRECTORY))
		{
			// Not found, or not a subdirectory.
			return -1;
		}
	}

	// Found the locale subdirectory.
	// Bind the gettext domain.
	// NOTE: The bundled copy of gettext supports UTF-8 paths.
	// Results with other versions may vary.

#ifndef UNICODE
	// Convert the pathname from ANSI to UTF-16 first.
	ret = MultiByteToWideChar(CP_ACP, 0, tpathname, -1, wpathname, ARRAY_SIZE(wpathname));
	if (ret <= 0) {
		// Error converting the pathname.
		return -1;
	}
#endif /* !UNICODE */
	// Convert the pathname from UTF-16 to UTF-8.
	ret = WideCharToMultiByte(CP_UTF8, 0, wpathname, -1, u8pathname, ARRAY_SIZE(u8pathname), NULL, NULL);
	if (ret <= 0) {
		// Error converting the pathname.
		return -1;
	}

	base = bindtextdomain(RP_I18N_DOMAIN, u8pathname);
	if (!base) {
		// bindtextdomain() failed.
		return -1;
	}

	// Initialized.
	return 0;
}

#else /* !_WIN32 */

/**
 * Initialize the internationalization subsystem.
 * (Unix/Linux version)
 *
 * @return 0 on success; non-zero on error.
 */
int rp_i18n_init(void)
{
	// Unix/Linux: Use the system-wide locale directory.
	const char *const base = bindtextdomain(RP_I18N_DOMAIN, DIR_INSTALL_LOCALE);
	if (!base) {
		// bindtextdomain() failed.
		return -1;
	}
	return 0;
}
#endif /* _WIN32 */

#endif /* ENABLE_NLS */
