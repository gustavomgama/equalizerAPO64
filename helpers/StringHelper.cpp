/*
    This file is part of Equalizer APO, a system-wide equalizer.
    Copyright (C) 2013  Jonas Thedering

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License along
    with this program; if not, write to the Free Software Foundation, Inc.,
    51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
*/

#include "stdafx.h"
#include <string>
#include <sstream>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <cwctype>
#endif
#include "StringHelper.h"

using namespace std;

wstring StringHelper::replaceCharacters(const wstring& s, const wstring& chars, const wstring& replacement)
{
	wstring result;
	result.reserve(s.length());

	for (unsigned i = 0; i < s.length(); i++)
	{
		wchar_t c = s[i];
		if (chars.find(c) == -1)
			result += c;
		else
			result += replacement;
	}

	return result;
}

wstring StringHelper::replaceIllegalCharacters(const wstring& filename)
{
	return replaceCharacters(filename, L"<>:\"/\\|?*", L"_");
}

wstring StringHelper::trim(const wstring& s)
{
	int firstNonSpace = -1;
	int lastNonSpace = -1;

	for (unsigned i = 0; i < s.length(); i++)
	{
		wchar_t c = s[i];
		if (!iswspace(c))
		{
			if (firstNonSpace == -1)
				firstNonSpace = i;
			lastNonSpace = i;
		}
	}

	if (firstNonSpace == -1)
		return L"";
	else
		return s.substr(firstNonSpace, lastNonSpace - firstNonSpace + 1);
}

vector<wstring> StringHelper::split(const wstring& s, wchar_t splitChar, bool skipEmpty)
{
	vector<wstring> result;
	size_t prevPos = 0;
	size_t pos = 0;
	while ((pos = s.find(splitChar, pos)) != wstring::npos)
	{
		wstring part = s.substr(prevPos, pos - prevPos);
		if (part.length() > 0 || !skipEmpty)
			result.push_back(part);
		prevPos = ++pos;
	}

	wstring part = s.substr(prevPos);
	if (part.length() > 0 || !skipEmpty)
		result.push_back(part);

	return result;
}

wstring StringHelper::join(const vector<wstring>& strings, const wstring& separator)
{
	wstringstream stream;

	bool first = true;

	for (vector<wstring>::const_iterator it = strings.cbegin(); it != strings.cend(); it++)
	{
		if (first)
			first = false;
		else
			stream << separator;
		stream << *it;
	}

	return stream.str();
}

vector<wstring> StringHelper::splitQuoted(const wstring& s, wchar_t splitChar, wchar_t quoteChar)
{
	vector<wstring> result;
	bool inQuotes = false;
	wstring current;
	for (size_t i = 0; i < s.length(); i++)
	{
		wchar_t c = s[i];
		if (c == splitChar && !inQuotes)
		{
			if (current != L"")
			{
				result.push_back(current);
				current = L"";
			}
		}
		else if (c == quoteChar)
		{
			inQuotes = !inQuotes;
			if (inQuotes && i > 0 && s[i - 1] == quoteChar)
				current += quoteChar;
		}
		else
		{
			current += c;
		}
	}

	if (current != L"")
		result.push_back(current);

	return result;
}

#ifdef _WIN32
wstring StringHelper::toWString(const string& s, unsigned codepage)
{
	int length = MultiByteToWideChar(codepage, 0, s.c_str(), -1, NULL, 0);
	wchar_t* charBuf = new wchar_t[length];
	MultiByteToWideChar(codepage, 0, s.c_str(), -1, charBuf, length);
	wstring result = charBuf;
	delete charBuf;

	return result;
}

string StringHelper::toString(const wstring& s, unsigned codepage)
{
	int length = WideCharToMultiByte(codepage, 0, s.c_str(), -1, NULL, 0, NULL, NULL);
	char* charBuf = new char[length];
	WideCharToMultiByte(codepage, 0, s.c_str(), -1, charBuf, length, NULL, NULL);
	string result = charBuf;
	delete charBuf;

	return result;
}

wstring StringHelper::toLowerCase(const wstring& s)
{
	wchar_t* charBuf = new wchar_t[s.length() + 1];
	memcpy(charBuf, s.c_str(), (s.length() + 1) * sizeof(wchar_t));
	errno_t err = _wcslwr_s(charBuf, s.length() + 1);

	wstring result = charBuf;
	delete charBuf;

	if (err == 0)
		return result;
	else
		return s;
}

wstring StringHelper::toUpperCase(const wstring& s)
{
	wchar_t* charBuf = new wchar_t[s.length() + 1];
	memcpy(charBuf, s.c_str(), (s.length() + 1) * sizeof(wchar_t));
	errno_t err = _wcsupr_s(charBuf, s.length() + 1);

	wstring result = charBuf;
	delete charBuf;

	if (err == 0)
		return result;
	else
		return s;
}

wstring StringHelper::getSystemErrorString(long status)
{
	wchar_t* buf;

	if (FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, NULL, status, 0, (LPTSTR)&buf, 0, NULL) != 0)
	{
		wstring result(buf);
		LocalFree(buf);

		// remove trailing newline
		if (result.back() == L'\n')
			result.erase(prev(result.end()));
		if (result.back() == L'\r')
			result.erase(prev(result.end()));
		return result;
	}
	else
		return L"";
}
#else
// Linux / POSIX implementations. wchar_t is 32-bit here, so a code point maps
// directly to one wchar_t.
static wstring utf8ToWide(const string& s)
{
	wstring out;
	for (size_t i = 0; i < s.size(); )
	{
		unsigned char c = (unsigned char)s[i];
		unsigned cp;
		size_t n;
		if (c < 0x80) { cp = c; n = 1; }
		else if ((c & 0xE0) == 0xC0) { cp = c & 0x1F; n = 2; }
		else if ((c & 0xF0) == 0xE0) { cp = c & 0x0F; n = 3; }
		else if ((c & 0xF8) == 0xF0) { cp = c & 0x07; n = 4; }
		else { out.push_back(0xFFFD); ++i; continue; }
		if (i + n > s.size()) { out.push_back(0xFFFD); break; }
		for (size_t k = 1; k < n; ++k)
			cp = (cp << 6) | ((unsigned char)s[i + k] & 0x3F);
		out.push_back((wchar_t)cp);
		i += n;
	}
	return out;
}

static string wideToUtf8(const wstring& s)
{
	string out;
	for (wchar_t wc : s)
	{
		unsigned cp = (unsigned)wc;
		if (cp < 0x80)
			out.push_back((char)cp);
		else if (cp < 0x800)
		{
			out.push_back((char)(0xC0 | (cp >> 6)));
			out.push_back((char)(0x80 | (cp & 0x3F)));
		}
		else if (cp < 0x10000)
		{
			out.push_back((char)(0xE0 | (cp >> 12)));
			out.push_back((char)(0x80 | ((cp >> 6) & 0x3F)));
			out.push_back((char)(0x80 | (cp & 0x3F)));
		}
		else
		{
			out.push_back((char)(0xF0 | (cp >> 18)));
			out.push_back((char)(0x80 | ((cp >> 12) & 0x3F)));
			out.push_back((char)(0x80 | ((cp >> 6) & 0x3F)));
			out.push_back((char)(0x80 | (cp & 0x3F)));
		}
	}
	return out;
}

static wstring latin1ToWide(const string& s)
{
	wstring out;
	out.reserve(s.size());
	for (unsigned char c : s)
		out.push_back((wchar_t)c);
	return out;
}

static string wideToLatin1(const wstring& s)
{
	string out;
	out.reserve(s.size());
	for (wchar_t wc : s)
		out.push_back((char)(wc & 0xFF));
	return out;
}

wstring StringHelper::toWString(const string& s, unsigned codepage)
{
	if (codepage == CP_UTF8)
		return utf8ToWide(s);
	return latin1ToWide(s);
}

string StringHelper::toString(const wstring& s, unsigned codepage)
{
	if (codepage == CP_UTF8)
		return wideToUtf8(s);
	return wideToLatin1(s);
}

wstring StringHelper::toLowerCase(const wstring& s)
{
	wstring result = s;
	for (wchar_t& c : result)
		c = (wchar_t)towlower(c);
	return result;
}

wstring StringHelper::toUpperCase(const wstring& s)
{
	wstring result = s;
	for (wchar_t& c : result)
		c = (wchar_t)towupper(c);
	return result;
}

wstring StringHelper::getSystemErrorString(long status)
{
	const char* msg = strerror((int)status);
	return latin1ToWide(msg ? msg : "");
}
#endif
