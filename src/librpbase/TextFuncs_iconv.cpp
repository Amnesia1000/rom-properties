/***************************************************************************
 * ROM Properties Page shell extension. (librpbase)                        *
 * TextFuncs_iconv.cpp: Text encoding functions. (iconv version)           *
 *                                                                         *
 * Copyright (c) 2009-2018 by David Korth.                                 *
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

#include "config.librpbase.h"
#include "TextFuncs.hpp"
#include "TextFuncs_NULL.hpp"

// C includes. (C++ namespace)
#include <cstdio>

#if defined(_WIN32)
#error TextFuncs_iconv.cpp is not supported on Windows.
#elif !defined(HAVE_ICONV)
#error TextFuncs_iconv.cpp requires the iconv() function.
#endif

// Determine the system encodings.
#include "byteorder.h"
#if SYS_BYTEORDER == SYS_BIG_ENDIAN
# define RP_ICONV_UTF16_ENCODING "UTF-16BE"
#else
# define RP_ICONV_UTF16_ENCODING "UTF-16LE"
#endif

// iconv
#include <iconv.h>

// C includes.
#include <stdlib.h>

// C includes. (C++ namespace)
#include <cassert>
#include <cstring>

// C++ includes.
#include <string>
using std::basic_string;
using std::string;
using std::u16string;

namespace LibRpBase {

/** OS-specific text conversion functions. **/

/**
 * Convert a string from one character set to another.
 * @param src 		[in] Source string.
 * @param len           [in] Source length, in bytes.
 * @param src_charset	[in] Source character set.
 * @param dest_charset	[in] Destination character set.
 * @return malloc()'d UTF-8 string, or nullptr on error.
 */
static char *rp_iconv(const char *src, int len,
		const char *src_charset, const char *dest_charset)
{
#ifdef RP_WIS16
	static_assert(sizeof(wchar_t) == sizeof(char16_t), "RP_WIS16 is defined, but wchar_t is not 16-bit!");
#else /* !RP_WIS16 */
	static_assert(sizeof(wchar_t) != sizeof(char16_t), "RP_WIS16 is not defined, but wchar_t is 16-bit!");
#endif /* RP_WIS16 */

	if (!src || len <= 0)
		return nullptr;

	if (!src_charset)
		src_charset = "";
	if (!dest_charset)
		dest_charset = "";

	// Based on examples from:
	// * http://www.delorie.com/gnu/docs/glibc/libc_101.html
	// * http://www.codase.com/search/call?name=iconv

	// Open an iconv descriptor.
	iconv_t cd;
	cd = iconv_open(dest_charset, src_charset);
	if (cd == (iconv_t)(-1)) {
		// Error opening iconv.
		return nullptr;
	}

	// Allocate the output buffer.
	// UTF-8 is variable length, and the largest UTF-8 character is 4 bytes long.
	size_t src_bytes_len = (size_t)len;
	const size_t out_bytes_len = (src_bytes_len * 4) + 4;
	size_t out_bytes_remaining = out_bytes_len;
	char *outbuf = static_cast<char*>(malloc(out_bytes_len));

	// Input and output pointers.
	char *inptr = const_cast<char*>(src);	// Input pointer.
	char *outptr = &outbuf[0];		// Output pointer.

	bool success = true;
	while (src_bytes_len > 0) {
		if (iconv(cd, &inptr, &src_bytes_len, &outptr, &out_bytes_remaining) == (size_t)(-1)) {
			// An error occurred while converting the string.
			// FIXME: Flag to indicate that we want to have
			// a partial Shift-JIS conversion?
			// Madou Monogatari I (MD) has a broken Shift-JIS
			// code point, which breaks conversion.
			// (Reported by andlabs.)
			success = false;
			break;
		}
	}

	// Close the iconv descriptor.
	iconv_close(cd);

	if (success) {
		// The string was converted successfully.

		// Make sure the string is null-terminated.
		size_t null_bytes = (out_bytes_remaining > 4 ? 4 : out_bytes_remaining);
		for (size_t i = null_bytes; i > 0; i--) {
			*outptr++ = 0x00;
		}

		// Return the output buffer.
		return outbuf;
	}

	// The string was not converted successfully.
	free(outbuf);
	return nullptr;
}

/** Generic code page functions. **/

/**
 * Convert a code page to an encoding name.
 * @param enc_name	[out] Buffer for encoding name.
 * @param len		[in] Length of enc_name.
 * @param cp		[in] Code page number.
 * @param flags		[in] Flags. (See TextConv_Flags_e.)
 */
static inline void codePageToEncName(char *enc_name, size_t len, unsigned int cp, unsigned int flags)
{
	// If TEXTCONV_FLAG_CP1252_FALLBACK is set, this is the
	// primary code page, so we should fail on error.
	// Otherwise, this is the fallback codepage.
	const char *const ignore = (flags & TEXTCONV_FLAG_CP1252_FALLBACK) ? "" : "//IGNORE";

	// Check for "special" code pages.
	switch (cp) {
		case CP_ACP:
		case CP_LATIN1:
			// NOTE: Handling "ANSI" as Latin-1 for now.
			snprintf(enc_name, len, "LATIN1%s", ignore);
			break;
		case CP_UTF8:
			snprintf(enc_name, len, "UTF-8%s", ignore);
			break;
		default:
			snprintf(enc_name, len, "CP%u%s", cp, ignore);
			break;
	}
}

/**
 * Convert 8-bit text to UTF-8.
 * Trailing NULL bytes will be removed.
 *
 * The specified code page number will be used.
 *
 * @param cp	[in] Code page number.
 * @param str	[in] 8-bit text.
 * @param len	[in] Length of str, in bytes. (-1 for NULL-terminated string)
 * @param flags	[in] Flags. (See TextConv_Flags_e.)
 * @return UTF-8 string.
 */
string cpN_to_utf8(unsigned int cp, const char *str, int len, unsigned int flags)
{
	len = check_NULL_terminator(str, len);

	// Get the encoding name for the primary code page.
	char cp_name[20];
	codePageToEncName(cp_name, sizeof(cp_name), cp, flags);

	// Attempt to convert the text to UTF-8.
	// NOTE: "//IGNORE" sometimes doesn't work, so we won't
	// check for TEXTCONV_FLAG_CP1252_FALLBACK here.
	string ret;
	char *mbs = reinterpret_cast<char*>(rp_iconv((char*)str, len*sizeof(*str), cp_name, "UTF-8"));
	if (!mbs /*&& (flags & TEXTCONV_FLAG_CP1252_FALLBACK)*/) {
		// Try cp1252 fallback.
		if (cp != 1252) {
			mbs = reinterpret_cast<char*>(rp_iconv((char*)str, len*sizeof(*str), "CP1252//IGNORE", "UTF-8"));
		}
		if (!mbs) {
			// Try Latin-1 fallback.
			if (cp != CP_LATIN1) {
				mbs = reinterpret_cast<char*>(rp_iconv((char*)str, len*sizeof(*str), "LATIN1//IGNORE", "UTF-8"));
			}
		}
	}

	if (mbs) {
		ret.assign(mbs);
		free(mbs);

#ifdef HAVE_ICONV_LIBICONV
		if (cp == 932) {
			// libiconv's cp932 maps Shift-JIS 8160 to U+301C. This is expected
			// behavior for Shift-JIS, but cp932 should map it to U+FF5E.
			for (auto p = ret.begin(); p != ret.end(); ++p) {
				if ((uint8_t)p[0] == 0xE3 && (uint8_t)p[1] == 0x80 && (uint8_t)p[2] == 0x9C) {
					// Found a wave dash.
					p[0] = (dest_type)0xEF;
					p[1] = (dest_type)0xBD;
					p[2] = (dest_type)0x9E;
					p += 2;
				}
			}
		}
#endif /* HAVE_ICONV_LIBICONV */
	}
	return ret;
}

/**
 * Convert 8-bit text to UTF-16.
 * Trailing NULL bytes will be removed.
 *
 * The specified code page number will be used.
 *
 * @param cp	[in] Code page number.
 * @param str	[in] 8-bit text.
 * @param len	[in] Length of str, in bytes. (-1 for NULL-terminated string)
 * @param flags	[in] Flags. (See TextConv_Flags_e.)
 * @return UTF-16 string.
 */
u16string cpN_to_utf16(unsigned int cp, const char *str, int len, unsigned int flags)
{
	len = check_NULL_terminator(str, len);

	// Get the encoding name for the primary code page.
	char cp_name[20];
	codePageToEncName(cp_name, sizeof(cp_name), cp, flags);

	// Attempt to convert the text to UTF-16.
	// NOTE: "//IGNORE" sometimes doesn't work, so we won't
	// check for TEXTCONV_FLAG_CP1252_FALLBACK here.
	u16string ret;
	char16_t *wcs = reinterpret_cast<char16_t*>(rp_iconv((char*)str, len*sizeof(*str), cp_name, RP_ICONV_UTF16_ENCODING));
	if (!wcs /*&& (flags & TEXTCONV_FLAG_CP1252_FALLBACK)*/) {
		// Try cp1252 fallback.
		if (cp != 1252) {
			wcs = reinterpret_cast<char16_t*>(rp_iconv((char*)str, len*sizeof(*str), "CP1252//IGNORE", RP_ICONV_UTF16_ENCODING));
		}
		if (!wcs) {
			// Try Latin-1 fallback.
			if (cp != CP_LATIN1) {
				wcs = reinterpret_cast<char16_t*>(rp_iconv((char*)str, len*sizeof(*str), "LATIN1//IGNORE", RP_ICONV_UTF16_ENCODING));
			}
		}
	}

	if (wcs) {
		ret.assign(wcs);
		free(wcs);

#ifdef HAVE_ICONV_LIBICONV
		if (cp == 932) {
			// libiconv's cp932 maps Shift-JIS 8160 to U+301C. This is expected
			// behavior for Shift-JIS, but cp932 should map it to U+FF5E.
			for (auto p = ret.begin(); p != ret.end(); ++p) {
				if (*p == 0x301C) {
					// Found a wave dash.
					*p = (char16_t)0xFF5E;
				}
			}
		}
#endif /* HAVE_ICONV_LIBICONV */
	}
	return ret;
}

/**
 * Convert UTF-8 to 8-bit text.
 * Trailing NULL bytes will be removed.
 *
 * The specified code page number will be used.
 * Invalid characters will be ignored.
 *
 * @param cp	[in] Code page number.
 * @param str	[in] UTF-8 text.
 * @param len	[in] Length of str, in bytes. (-1 for NULL-terminated string)
 * @return 8-bit text.
 */
string utf8_to_cpN(unsigned int cp, const char *str, int len)
{
	len = check_NULL_terminator(str, len);

	// Get the encoding name for the primary code page.
	char cp_name[20];
	codePageToEncName(cp_name, sizeof(cp_name), cp, TEXTCONV_FLAG_CP1252_FALLBACK);

	// Attempt to convert the text from UTF-8.
	string ret;
	char *mbs = reinterpret_cast<char*>(rp_iconv((char*)str, len*sizeof(*str), "UTF-8", cp_name));
	if (mbs) {
		ret.assign(mbs);
		free(mbs);
	}
	return ret;
}

/**
 * Convert UTF-16 to 8-bit text.
 * Trailing NULL bytes will be removed.
 *
 * The specified code page number will be used.
 * Invalid characters will be ignored.
 *
 * @param cp	[in] Code page number.
 * @param wcs	[in] UTF-16 text.
 * @param len	[in] Length of str, in bytes. (-1 for NULL-terminated string)
 * @return 8-bit text.
 */
string utf16_to_cpN(unsigned int cp, const char16_t *wcs, int len)
{
	len = check_NULL_terminator(wcs, len);

	// Get the encoding name for the primary code page.
	char cp_name[20];
	const unsigned int flags = (cp == CP_UTF8 ? 0 : TEXTCONV_FLAG_CP1252_FALLBACK);
	codePageToEncName(cp_name, sizeof(cp_name), cp, flags);

	// Attempt to convert the text from UTF-8.
	string ret;
	char *mbs = reinterpret_cast<char*>(rp_iconv((char*)wcs, len*sizeof(*wcs), RP_ICONV_UTF16_ENCODING, cp_name));
	if (mbs) {
		ret.assign(mbs);
		free(mbs);
	}
	return ret;
}

/** Specialized UTF-16 conversion functions. **/

/**
 * Convert UTF-16LE text to UTF-8.
 * Trailing NULL bytes will be removed.
 * @param wcs	[in] UTF-16LE text.
 * @param len	[in] Length of wcs, in characters. (-1 for NULL-terminated string)
 * @return UTF-8 string.
 */
string utf16le_to_utf8(const char16_t *wcs, int len)
{
	len = check_NULL_terminator(wcs, len);

	// Attempt to convert the text from UTF-16LE to UTF-8.
	string ret;
	char *mbs = reinterpret_cast<char*>(rp_iconv((char*)wcs, len*sizeof(*wcs), "UTF-16LE", "UTF-8"));
	if (mbs) {
		ret.assign(mbs);
		free(mbs);
	}
	return ret;
}

/**
 * Convert UTF-16BE text to UTF-8.
 * Trailing NULL bytes will be removed.
 * @param wcs	[in] UTF-16BE text.
 * @param len	[in] Length of wcs, in characters. (-1 for NULL-terminated string)
 * @return UTF-8 string.
 */
string utf16be_to_utf8(const char16_t *wcs, int len)
{
	len = check_NULL_terminator(wcs, len);

	// Attempt to convert the text from UTF-16BE to UTF-8.
	string ret;
	char *mbs = reinterpret_cast<char*>(rp_iconv((char*)wcs, len*sizeof(*wcs), "UTF-16BE", "UTF-8"));
	if (mbs) {
		ret.assign(mbs);
		free(mbs);
	}
	return ret;
}

}
