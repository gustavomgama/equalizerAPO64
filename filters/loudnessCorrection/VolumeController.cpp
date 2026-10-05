/*
    This file is part of Equalizer APO, a system-wide equalizer.
    Copyright (C) 2017  Alexander Walch

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
#include "VolumeController.h"

#ifdef _WIN32
#include <mmdeviceapi.h>

VolumeController::VolumeController()
{
	HRESULT hr;
	CoInitialize(NULL);
	IMMDeviceEnumerator* deviceEnumerator = NULL;
	hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), NULL, CLSCTX_INPROC_SERVER, __uuidof(IMMDeviceEnumerator), (LPVOID*)&deviceEnumerator);
	IMMDevice* defaultDevice = NULL;

	hr = deviceEnumerator->GetDefaultAudioEndpoint(eRender, eMultimedia,  &defaultDevice);
	deviceEnumerator->Release();
	deviceEnumerator = NULL;

	_endpointVolume = NULL;
	hr = defaultDevice->Activate(__uuidof(IAudioEndpointVolume), CLSCTX_INPROC_SERVER, NULL, (LPVOID*)&_endpointVolume);
	defaultDevice->Release();
	defaultDevice = NULL;
	float inkrement;
	_endpointVolume->GetVolumeRange(&_minVol, &_maxVol, &inkrement);
}

HRESULT VolumeController::getVolume(double& currentVolume)
{
	float vol;
	HRESULT res = _endpointVolume->GetMasterVolumeLevel(&vol);
	currentVolume = vol;
	return res;
}

HRESULT VolumeController::setVolume(double volume)
{
	volume = fmin(volume, _maxVol);
	volume = fmax(volume, _minVol);
	return _endpointVolume->SetMasterVolumeLevel(float(volume), NULL);
}
#else
#include <cstdio>
#include <cmath>
#include <chrono>

static long long nowMs()
{
	return std::chrono::duration_cast<std::chrono::milliseconds>(
		std::chrono::steady_clock::now().time_since_epoch()).count();
}

VolumeController::VolumeController() {}

HRESULT VolumeController::getVolume(double& currentVolume)
{
	// The filter polls frequently; cache for 500 ms so we spawn wpctl at most
	// twice per second instead of on every call.
	long long now = nowMs();
	if (_lastReadMs != 0 && now - _lastReadMs < 500)
	{
		currentVolume = _cachedVolumeDb;
		return S_OK;
	}
	_lastReadMs = now;

	FILE* pipe = popen("wpctl get-volume @DEFAULT_AUDIO_SINK@ 2>/dev/null", "r");
	if (pipe == NULL)
		return -1;
	char buf[128] = {0};
	bool ok = fgets(buf, sizeof(buf), pipe) != NULL;
	pclose(pipe);
	if (!ok)
		return -1;

	const char* colon = strchr(buf, ':');
	if (colon == NULL)
		return -1;
	double linear = 0.0;
	if (sscanf(colon + 1, "%lf", &linear) != 1)
		return -1;

	// wpctl reports a linear amplitude; the filter works in dB.
	currentVolume = linear <= 0.0 ? -96.0 : 20.0 * log10(linear);
	_cachedVolumeDb = currentVolume;
	return S_OK;
}

HRESULT VolumeController::setVolume(double volume)
{
	double linear = pow(10.0, volume / 20.0);
	char cmd[128];
	std::snprintf(cmd, sizeof(cmd), "wpctl set-volume @DEFAULT_AUDIO_SINK@ %.4f >/dev/null 2>&1", linear);
	_lastReadMs = 0; // invalidate cache
	return system(cmd) == 0 ? S_OK : -1;
}
#endif
