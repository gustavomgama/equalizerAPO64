/*
    This file is part of Equalizer APO, a system-wide equalizer.

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

// Linux implementation of DeviceAPOInfo. There is no APO concept on Linux;
// this model only enumerates PipeWire sinks/sources so the editor can offer a
// device list and channel configuration. APO installation related methods are
// inert. Devices are read by shelling out to `pw-dump`, so no PipeWire link
// dependency is required for the GUI.

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QProcess>

#include "DeviceAPOInfo.h"

using namespace std;

namespace
{
QString propString(const QJsonObject& props, const char* key)
{
	return props.value(QLatin1String(key)).toString();
}

QJsonArray dumpObjects()
{
	QProcess process;
	process.start(QStringLiteral("pw-dump"), QStringList());
	if (!process.waitForStarted(3000))
		return QJsonArray();
	if (!process.waitForFinished(8000))
	{
		process.kill();
		process.waitForFinished(1000);
		return QJsonArray();
	}

	QJsonParseError error;
	QJsonDocument document = QJsonDocument::fromJson(process.readAllStandardOutput(), &error);
	if (error.error != QJsonParseError::NoError || !document.isArray())
		return QJsonArray();

	return document.array();
}

shared_ptr<DeviceAPOInfo> makeDevice(const QString& connectionName, const QString& deviceName,
	const QString& guid, bool input, bool defaultDevice, unsigned channelCount,
	unsigned sampleRate, unsigned long channelMask)
{
	shared_ptr<DeviceAPOInfo> device = make_shared<DeviceAPOInfo>();
	device->linuxInit(connectionName.toStdWString(), deviceName.toStdWString(), guid.toStdWString(),
		input, defaultDevice, channelCount, sampleRate, channelMask);
	return device;
}
}

// Small helper used by makeDevice above; declared here to keep the header
// free of Qt types.
void DeviceAPOInfo::linuxInit(const std::wstring& connectionName, const std::wstring& deviceName,
	const std::wstring& deviceGuid, bool input, bool defaultDevice, unsigned channelCount,
	unsigned sampleRate, unsigned long channelMask)
{
	this->connectionName = connectionName;
	this->deviceName = deviceName;
	this->deviceGuid = deviceGuid;
	this->input = input;
	this->defaultDevice = defaultDevice;
	this->channelCount = channelCount == 0 ? 2 : channelCount;
	this->sampleRate = sampleRate == 0 ? 48000 : sampleRate;
	this->channelMask = channelMask;
}

vector<shared_ptr<AbstractAPOInfo>> DeviceAPOInfo::loadAllInfos(bool input)
{
	vector<shared_ptr<AbstractAPOInfo>> result;

	const QJsonArray objects = dumpObjects();
	bool foundDefault = false;
	for (const QJsonValue& value : objects)
	{
		QJsonObject object = value.toObject();
		if (object.value(QStringLiteral("type")).toString() != QStringLiteral("PipeWire:Interface:Node"))
			continue;

		QJsonObject info = object.value(QStringLiteral("info")).toObject();
		QJsonObject props = info.value(QStringLiteral("props")).toObject();

		const QString mediaClass = propString(props, "media.class");
		const bool isSink = mediaClass == QStringLiteral("Audio/Sink");
		const bool isSource = mediaClass == QStringLiteral("Audio/Source");
		if (input ? !isSource : !isSink)
			continue;

		QString nodeName = propString(props, "node.name");
		if (nodeName.isEmpty())
			nodeName = propString(props, "object.path");
		QString description = propString(props, "node.description");
		if (description.isEmpty())
			description = nodeName;

		unsigned channelCount = 2;
		QJsonValue channels = props.value(QStringLiteral("audio.channels"));
		if (channels.isDouble())
			channelCount = (unsigned)channels.toInt();
		unsigned sampleRate = 48000;

		const bool isDefault = !foundDefault;
		foundDefault = true;

		result.push_back(makeDevice(description, nodeName, nodeName, input, isDefault,
			channelCount, sampleRate, 0));
	}

	if (result.empty())
	{
		// No PipeWire nodes visible: expose one inert default device so the
		// editor remains usable (with analysis based on the fallback audio
		// parameters).
		result.push_back(makeDevice(input ? QStringLiteral("Default capture")
		                                  : QStringLiteral("Default playback"),
			input ? QStringLiteral("default-input") : QStringLiteral("default-output"),
			input ? QStringLiteral("{default-input}") : QStringLiteral("{default-output}"),
			input, true, 2, 48000, 0));
	}

	return result;
}

wstring DeviceAPOInfo::getDefaultDevice(bool input, int role)
{
	vector<shared_ptr<AbstractAPOInfo>> devices = loadAllInfos(input);
	return devices.empty() ? L"" : devices.front()->getDeviceGuid();
}

bool DeviceAPOInfo::checkProtectedAudioDG(bool fix)
{
	return true;
}

bool DeviceAPOInfo::checkAPORegistration(bool fix)
{
	return true;
}

wstring DeviceAPOInfo::getConnectionName() const
{
	return connectionName;
}

wstring DeviceAPOInfo::getDeviceName() const
{
	return deviceName;
}

wstring DeviceAPOInfo::getDeviceGuid() const
{
	return deviceGuid;
}

wstring DeviceAPOInfo::getDeviceString() const
{
	return getConnectionName() + L" " + getDeviceName() + L" " + getDeviceGuid();
}

unsigned DeviceAPOInfo::getChannelCount() const
{
	return channelCount;
}

unsigned DeviceAPOInfo::getSampleRate() const
{
	return sampleRate;
}

unsigned long DeviceAPOInfo::getChannelMask() const
{
	return channelMask;
}

bool DeviceAPOInfo::isInput() const
{
	return input;
}

bool DeviceAPOInfo::isInstalled() const
{
	return true;
}

bool DeviceAPOInfo::canBeUpgraded() const
{
	return false;
}

bool DeviceAPOInfo::hasChanges() const
{
	return false;
}

bool DeviceAPOInfo::isExperimental() const
{
	return false;
}

bool DeviceAPOInfo::isEnhancementsDisabled() const
{
	return false;
}

bool DeviceAPOInfo::isDefaultDevice() const
{
	return defaultDevice;
}

bool DeviceAPOInfo::isDisabled() const
{
	return false;
}

bool DeviceAPOInfo::isUnplugged() const
{
	return false;
}

void DeviceAPOInfo::install()
{
}

void DeviceAPOInfo::uninstall()
{
}

void DeviceAPOInfo::reinstall()
{
}
