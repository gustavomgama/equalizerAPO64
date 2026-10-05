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
#include "LoudnessCorrectionFilter.h"
#include "helpers/MemoryHelper.h"

#include "VolumeController.h"

#ifndef _USE_MATH_DEFINES
#define _USE_MATH_DEFINES
#endif
#include <math.h>

LoudnessCorrectionFilter::LoudnessCorrectionFilter(const FilterParameters& fParameters)
{
	_parameters = fParameters;
	if (_parameters.attenuation > 1.0)
	{
		_parameters.attenuation = 1.0;
	}
	if (_parameters.attenuation < 0.0)
	{
		_parameters.attenuation = 0.0;
	}
#ifdef _WIN32
	InitializeCriticalSection(&_parameterUpdateSection);
#endif
}

LoudnessCorrectionFilter::~LoudnessCorrectionFilter()
{
#ifdef _WIN32
	if (_stopParameterUpdateThreadEvent)
	{
		SetEvent(_stopParameterUpdateThreadEvent);
	}
	WaitForSingleObject(_parameterUpdateThreadHandle, INFINITE);
	DeleteCriticalSection(&_parameterUpdateSection);
	CloseHandle(_stopParameterUpdateThreadEvent);
	CloseHandle(_parameterUpdateThreadHandle);
	CloseHandle(_parameterchangedEvent);
#else
	_stopParameterUpdateThread = true;
	if (_parameterUpdateThread.joinable())
		_parameterUpdateThread.join();
#endif
}

std::vector<std::wstring> LoudnessCorrectionFilter::initialize(float sampleRate, unsigned maxFrameCount, std::vector<std::wstring> channelNames)
{
	this->_channelCount = channelNames.size();
	_lowShelfBiquads.resize(_channelCount);
	_highShelfBiquads.resize(_channelCount);
	_sampleRate = sampleRate;
	_attFactor = 1.0;
	_neutral = true;

	double freqLS = 75, qLS = 1, gainLS = 0;
	double freqHS = 10000, qHS = 1, gainHS = 0;
	VolumeController VolumeController;
	double vol;
	HRESULT res = VolumeController.getVolume(vol);
	if (res == S_OK)
	{
		double preAmp;
		getLShelfParamter(vol, freqLS, qLS, gainLS, preAmp);
		_attFactor = exp(preAmp / 6 * log(2));
		getHShelfParamter(vol + (double)preAmp, freqHS, qHS, gainHS);
		_neutral = std::max<double>(std::abs(gainLS), std::abs(gainHS)) < 0.2 ? true : false;
	}

	for (unsigned i = 0; i < _channelCount; i++)
	{
		_lowShelfBiquads[i] = BiQuad(BiQuad::LOW_SHELF, gainLS, freqLS, _sampleRate, qLS, false);
		_highShelfBiquads[i] = BiQuad(BiQuad::HIGH_SHELF, gainHS, freqHS, _sampleRate, qHS, false);
	}
#ifdef _WIN32
	_stopParameterUpdateThreadEvent = CreateEvent(NULL, true, false, NULL);
	_parameterchangedEvent = CreateEvent(NULL, true, false, NULL);
	_parameterUpdateThreadHandle = CreateThread(NULL, 0, &parameterUpdateThread, this, 0, NULL);
#else
	_parameterUpdateThread = std::thread(parameterUpdateThread, this);
#endif

	return channelNames;
}

void LoudnessCorrectionFilter::getLShelfParamter(const double& volume, double& frequence, double& q, double& gain, double& preAmp)
{
	frequence = 75;
	q = 0.52;
	double volDiff = _parameters.referenceLevel - _parameters.referenceOffset - volume;
	if (volDiff > 0)
	{
		// old: gain=volDiff*0.55*_parameters.attenuation;
		gain = volDiff * 0.55 / (1 - 0.55) * _parameters.attenuation;
		preAmp = -gain;
	}
	else if (volDiff < 0)
	{
		preAmp = 0.0;
		gain = volDiff * 0.55 * exp(volDiff / 90.0) * _parameters.attenuation;
	}
	else
	{
		gain = 0;
	}
}
void LoudnessCorrectionFilter::getHShelfParamter(const double& volume, double& frequence, double& q, double& gain)
{
	frequence = 10000;
	q = 0.9;
	double volDiff = _parameters.referenceLevel - _parameters.referenceOffset - volume;
	if (volDiff > 0)
	{
		gain = volDiff * 0.225 * exp(-volDiff / 100.0) * _parameters.attenuation;
	}
	else if (volDiff < 0)
	{
		gain = volDiff * 0.175 * exp(volDiff / 80.0) * _parameters.attenuation;
	}
	else
	{
		gain = 0;
	}
}

#ifdef _WIN32
unsigned long __stdcall LoudnessCorrectionFilter::parameterUpdateThread(void* parameter)
{
	LoudnessCorrectionFilter* lCorrection = (LoudnessCorrectionFilter*)parameter;
	VolumeController VolumeController;
	double volOld(lCorrection->_parameters.referenceLevel);
	double vol(lCorrection->_parameters.referenceLevel);
	double freqLS, qLS, gainLS, preAmp;
	double freqHS, qHS, gainHS;
	HRESULT res;
	while (WaitForSingleObject(lCorrection->_stopParameterUpdateThreadEvent, 0) == WAIT_TIMEOUT)
	{
		if (WaitForSingleObject(lCorrection->_parameterchangedEvent, 0) == WAIT_TIMEOUT)
		{
			res = VolumeController.getVolume(vol);
			if (res == S_OK)
			{
				if (vol != volOld)
				{
					lCorrection->getLShelfParamter(vol, freqLS, qLS, gainLS, preAmp);
					lCorrection->_attFactor = exp(preAmp / 6 * log(2));
					lCorrection->getHShelfParamter(vol + (double)preAmp, freqHS, qHS, gainHS);
					lCorrection->upDateBiquadCoefficients(freqHS, qHS, gainHS, true);
					lCorrection->upDateBiquadCoefficients(freqLS, qLS, gainLS, false);
					volOld = vol;

					lCorrection->_neutralUpDate = std::max<double>(std::abs(gainLS), std::abs(gainHS)) < 0.2 ? true : false;

					SetEvent(lCorrection->_parameterchangedEvent);
				}
			}
		}
		Sleep(10);
		// ==========================
	}
	return 0;
}
#else
void LoudnessCorrectionFilter::parameterUpdateThread(LoudnessCorrectionFilter* lCorrection)
{
	VolumeController VolumeController;
	double volOld(lCorrection->_parameters.referenceLevel);
	double vol(lCorrection->_parameters.referenceLevel);
	double freqLS, qLS, gainLS, preAmp;
	double freqHS, qHS, gainHS;
	while (!lCorrection->_stopParameterUpdateThread.load())
	{
		if (!lCorrection->_parameterchanged.load())
		{
			HRESULT res = VolumeController.getVolume(vol);
			if (res == S_OK && vol != volOld)
			{
				lCorrection->getLShelfParamter(vol, freqLS, qLS, gainLS, preAmp);
				lCorrection->_attFactor = exp(preAmp / 6 * log(2));
				lCorrection->getHShelfParamter(vol + (double)preAmp, freqHS, qHS, gainHS);
				lCorrection->upDateBiquadCoefficients(freqHS, qHS, gainHS, true);
				lCorrection->upDateBiquadCoefficients(freqLS, qLS, gainLS, false);
				volOld = vol;

				lCorrection->_neutralUpDate = std::max<double>(std::abs(gainLS), std::abs(gainHS)) < 0.2 ? true : false;

				lCorrection->_parameterchanged.store(true);
			}
		}
		std::this_thread::sleep_for(std::chrono::milliseconds(10));
	}
}
#endif

bool LoudnessCorrectionFilter::upDateNeutral()
{
	return _neutralUpDate;
}

#pragma AVRT_CODE_BEGIN
void LoudnessCorrectionFilter::process(double** output, double** input, unsigned frameCount)
{
	if (_parameters.state == false)
	{
		for (unsigned int j = 0; j < frameCount; j++)
		{
			for (unsigned i = 0; i < _channelCount; i++)
			{
				output[i][j] = input[i][j];
			}
		}
		output = input;
		return;
	}
#ifdef _WIN32
	if (WaitForSingleObject(_parameterchangedEvent, 0) == WAIT_OBJECT_0)
#else
	if (_parameterchanged.load())
#endif
	{
		for (unsigned i = 0; i < _channelCount; i++)
		{
			_lowShelfBiquads[i].setCoefficients(_aLS, _a0LS);
			_highShelfBiquads[i].setCoefficients(_aHS, _a0HS);
		}
		_neutral = upDateNeutral();
#ifdef _WIN32
		ResetEvent(_parameterchangedEvent);
#else
		_parameterchanged.store(false);
#endif
	}
	for (unsigned i = 0; i < _channelCount; i++)
	{
		double* inputChannel = input[i];
		double* outputChannel = output[i];
		for (unsigned j = 0; j < frameCount; j++)
		{
			_tempResult = _lowShelfBiquads[i].process(inputChannel[j]);
			_lowShelfBiquads[i].removeDenormals();
			_tempResult *= _attFactor;
			outputChannel[j] = (double)_highShelfBiquads[i].process(_tempResult);
			_highShelfBiquads[i].removeDenormals();

			// if there is nearly no loudness correction necessary => set output=input to achive best quality
			if (_neutral)
			{
				outputChannel[j] = inputChannel[j];
			}
		}
	}
}

void LoudnessCorrectionFilter::upDateBiquadCoefficients(const double& freq, const double& bandwidthOrQOrS, const double& dbGain, bool highshelf)
{
	double A;
	A = pow(10, dbGain / 40);

	double omega = 2 * M_PI * freq / _sampleRate;
	double sn = sin(omega);
	double cs = cos(omega);
	double alpha;
	double beta = -1;

	alpha = sn / 2 * sqrt((A + 1 / A) * (1 / bandwidthOrQOrS - 1) + 2);
	beta = 2 * sqrt(A) * alpha;

	double a0;
#ifdef _WIN32
	TryEnterCriticalSection(&_parameterUpdateSection);
#else
	std::unique_lock<std::mutex> _lock(_parameterUpdateSection, std::try_to_lock);
#endif
	if (highshelf)
	{
		a0 = (A + 1) - (A - 1) * cs + beta;
		_a0HS = (double)((A * ((A + 1) + (A - 1) * cs + beta)) / a0);
		_aHS[0] = (double)((-2 * A * ((A - 1) + (A + 1) * cs)) / a0);
		_aHS[1] = (double)((A * ((A + 1) + (A - 1) * cs - beta)) / a0);

		_aHS[2] = (double)((2 * ((A - 1) - (A + 1) * cs)) / a0);
		_aHS[3] = (double)(((A + 1) - (A - 1) * cs - beta) / a0);
	}
	else
	{
		a0 = (A + 1) + (A - 1) * cs + beta;
		_a0LS = (double)((A * ((A + 1) - (A - 1) * cs + beta)) / a0);
		_aLS[0] = (double)((2 * A * ((A - 1) - (A + 1) * cs)) / a0);
		_aLS[1] = (double)((A * ((A + 1) - (A - 1) * cs - beta)) / a0);

		_aLS[2] = (double)((-2 * ((A - 1) + (A + 1) * cs)) / a0);
		_aLS[3] = (double)(((A + 1) + (A - 1) * cs - beta) / a0);
	}
#ifdef _WIN32
	LeaveCriticalSection(&_parameterUpdateSection);
#endif
}
#pragma AVRT_CODE_END
