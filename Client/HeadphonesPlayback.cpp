// Playback controller: play/pause/track keys, headphone volume and the names of
// what's playing (PLAY 0xA0-0xA9, table 1). Layouts are in docs/PROTOCOL.md.

#include "Headphones.h"

#include <algorithm>

namespace
{
	enum : unsigned char
	{
		PLAY_GET_CAPABILITY = 0xa0, PLAY_RET_CAPABILITY = 0xa1, PLAY_GET_STATUS = 0xa2,
		PLAY_RET_STATUS = 0xa3, PLAY_SET_STATUS = 0xa4, PLAY_NTFY_STATUS = 0xa5,
		PLAY_GET_PARAM = 0xa6, PLAY_RET_PARAM = 0xa7, PLAY_SET_PARAM = 0xa8, PLAY_NTFY_PARAM = 0xa9,
	};

	constexpr unsigned char PLAYBACK_CONTROLLER = 0x01; // PlayInquiredType
	constexpr unsigned char F_PLAYBACK_CONTROLLER = 0xa1; // FunctionType

	// PlaybackDetailedDataType
	constexpr unsigned char DETAIL_TRACK = 0x00;
	constexpr unsigned char DETAIL_ALBUM = 0x01;
	constexpr unsigned char DETAIL_ARTIST = 0x02;
	constexpr unsigned char DETAIL_GENRE = 0x03;
	constexpr unsigned char DETAIL_PLAYER = 0x10;
	constexpr unsigned char DETAIL_VOLUME = 0x20;

	constexpr unsigned char NAME_STATUS_SETTLED = 0x02;
	constexpr unsigned char COMMON_STATUS_ENABLE = 0x00;

	constexpr unsigned char ALL_NAMES[] = { DETAIL_TRACK, DETAIL_ALBUM, DETAIL_ARTIST, DETAIL_PLAYER };
}

PlaybackCapability Headphones::getPlaybackCapability()
{
	std::lock_guard guard(this->_propertyMtx);
	return this->_playbackCapability;
}

PlaybackState Headphones::getPlaybackState()
{
	std::lock_guard guard(this->_propertyMtx);
	PlaybackState state = this->_playbackState;
	state.volume = this->_volume.desired;
	return state;
}

void Headphones::setVolume(int volume)
{
	std::lock_guard guard(this->_propertyMtx);
	if (this->_volume.current < 0)
		return; // not reported yet
	// Like EQ steps, the count includes 0: the XM4 reports 31 steps and a maximum of 30.
	int max = this->_playbackCapability.volumeSteps > 0 ? this->_playbackCapability.volumeSteps - 1 : 255;
	this->_volume.desired = std::clamp(volume, 0, max);
}

void Headphones::sendPlaybackControl(PLAYBACK_CONTROL control)
{
	this->_conn.sendCommand({ static_cast<char>(PLAY_SET_STATUS), PLAYBACK_CONTROLLER,
		COMMON_STATUS_ENABLE, static_cast<char>(control) });
}

bool Headphones::hasStalePlaybackMetadata()
{
	std::lock_guard guard(this->_propertyMtx);
	return !this->_stalePlaybackNames.empty() && this->_playbackCapability.metadata;
}

void Headphones::refreshPlaybackMetadata()
{
	std::vector<unsigned char> types;
	{
		std::lock_guard guard(this->_propertyMtx);
		if (!this->_playbackCapability.metadata)
			return;
		types.swap(this->_stalePlaybackNames);
	}
	for (unsigned char type : types)
		this->_conn.sendCommand({ static_cast<char>(PLAY_GET_PARAM), PLAYBACK_CONTROLLER, static_cast<char>(type) });
}

void Headphones::_resetPlayback()
{
	this->_playbackCapability = PlaybackCapability{};
	this->_playbackState = PlaybackState{};
	this->_volume.setState(-1);
	this->_stalePlaybackNames.clear();
}

void Headphones::_queryPlayback()
{
	if (!this->supportsFunction(F_PLAYBACK_CONTROLLER))
		return;
	// Names are fetched once the capability says the device has them (see RET_CAPABILITY).
	this->_conn.sendCommand({ static_cast<char>(PLAY_GET_CAPABILITY), PLAYBACK_CONTROLLER });
	this->_conn.sendCommand({ static_cast<char>(PLAY_GET_STATUS), PLAYBACK_CONTROLLER });
	this->_conn.sendCommand({ static_cast<char>(PLAY_GET_PARAM), PLAYBACK_CONTROLLER, static_cast<char>(DETAIL_VOLUME) });
}

bool Headphones::_handlePlaybackReply(const Buffer& bytes)
{
	auto at = [&bytes](size_t i) { return static_cast<unsigned char>(bytes[i]); };
	const size_t n = bytes.size();
	if (n < 3 || at(1) != PLAYBACK_CONTROLLER)
		return false;

	auto markStale = [this](unsigned char type) {
		if (std::find(this->_stalePlaybackNames.begin(), this->_stalePlaybackNames.end(), type) == this->_stalePlaybackNames.end())
			this->_stalePlaybackNames.push_back(type);
	};
	auto reportVolume = [this](int volume) {
		if (this->_volume.current < 0)
			this->_volume.setState(volume);
		else
			this->_volume.updateFromDevice(volume);
	};

	std::lock_guard guard(this->_propertyMtx);
	switch (at(0))
	{
	case PLAY_RET_CAPABILITY:
		// [A1, 01, volumeSteps, playbackControlType, metaDataDisplayType]
		if (n < 5)
			return false;
		this->_playbackCapability.available = true;
		this->_playbackCapability.volumeSteps = at(2);
		this->_playbackCapability.transport = at(3) == 0x01;
		this->_playbackCapability.metadata = at(4) == 0x01;
		if (this->_playbackCapability.metadata)
			for (unsigned char type : ALL_NAMES)
				markStale(type);
		return true;

	case PLAY_RET_STATUS:
	case PLAY_NTFY_STATUS:
		// [A3|A5, 01, CommonStatus, PlaybackStatus]
		if (n < 4)
			return false;
		this->_playbackState.status = at(3) <= 0x03 ? static_cast<PLAYBACK_STATUS>(at(3)) : PLAYBACK_STATUS::UNSETTLED;
		return true;

	case PLAY_RET_PARAM:
	case PLAY_NTFY_PARAM:
	{
		unsigned char type = at(2);
		if (type == DETAIL_VOLUME)
		{
			// [A7|A9, 01, 20, volume]
			if (n < 4)
				return false;
			reportVolume(at(3));
			return true;
		}
		std::string* name = type == DETAIL_TRACK ? &this->_playbackState.track
			: type == DETAIL_ALBUM ? &this->_playbackState.album
			: type == DETAIL_ARTIST ? &this->_playbackState.artist
			: type == DETAIL_PLAYER ? &this->_playbackState.player
			: nullptr;
		if (!name)
			return type == DETAIL_GENRE; // not shown
		if (at(0) == PLAY_NTFY_PARAM)
		{
			// [A9, 01, type]: the name changed; fetch it with GET_PARAM.
			markStale(type);
			return true;
		}
		// [A7, 01, type, PlaybackNameStatus, len, UTF-8 name]
		if (n < 5)
			return false;
		size_t len = std::min<size_t>(at(4), n - 5);
		if (at(3) == NAME_STATUS_SETTLED && len > 0)
			name->assign(bytes.begin() + 5, bytes.begin() + 5 + len);
		else
			name->clear();
		return true;
	}
	default:
		return false;
	}
}
