#include "stdafx.h"
#include "RewindManager.h"
#include "MessageManager.h"
#include "Console.h"
#include "VideoRenderer.h"
#include "SoundMixer.h"

RewindManager* RewindManager::_instance = nullptr;

RewindManager::RewindManager()
{
	_instance = this;
	_rewindState = RewindState::Stopped;
	_framesToFastForward = 0;
	AddHistoryBlock();

	MessageManager::RegisterNotificationListener(this);
}

RewindManager::~RewindManager()
{
	if(_instance == this) {
		_instance = nullptr;
	}
	MessageManager::UnregisterNotificationListener(this);
}

void RewindManager::ClearBuffer()
{
	if(_instance) {
		_instance->_history.clear();
		_instance->_historyBackup.clear();
		_instance->_currentHistory = RewindData();
		_instance->_framesToFastForward = 0;
		_instance->_videoHistory.clear();
		_instance->_videoHistoryBuilder.clear();
		_instance->_audioHistory.clear();
		_instance->_audioHistoryBuilder.clear();
		_instance->_rewindState = RewindState::Stopped;
		_instance->AddHistoryBlock();
	}
}

void RewindManager::ProcessNotification(ConsoleNotificationType type, void * parameter)
{
	if(type == ConsoleNotificationType::PpuFrameDone) {
		if(EmulationSettings::GetRewindBufferSize() > 0) {
			switch(_rewindState) {
				case RewindState::Starting:
				case RewindState::Started:
				case RewindState::Debugging:
					_currentHistory.FrameCount--;
					break;

				case RewindState::Stopping:
					_framesToFastForward--;
					_currentHistory.FrameCount++;
					if(_framesToFastForward == 0) {
						for(int i = 0; i < 4; i++) {
							size_t numberToRemove = _currentHistory.InputLogs[i].size();
							_currentHistory.InputLogs[i] = _historyBackup.front().InputLogs[i];
							for(size_t j = 0; j < numberToRemove; j++) {
								_currentHistory.InputLogs[i].pop_back();
							}
						}
						_historyBackup.clear();
						_rewindState = RewindState::Stopped;
						EmulationSettings::ClearFlags(EmulationFlags::ForceMaxSpeed);
					}
					break;
			
				case RewindState::Stopped:
					_currentHistory.FrameCount++;
					break;
			}
		}
	}
}

void RewindManager::AddHistoryBlock()
{
	uint32_t maxHistorySize = EmulationSettings::GetRewindBufferSize() * 120;	
	while(_history.size() > maxHistorySize) {
		_history.pop_front();
	}

	if(_currentHistory.FrameCount > 0) {
		_history.push_back(_currentHistory);
	}
	_currentHistory = RewindData();
	_currentHistory.SaveState();
}

void RewindManager::PopHistory()
{
	if(_history.empty() && _currentHistory.FrameCount <= 0) {
		StopRewinding();
	} else {
		if(_currentHistory.FrameCount <= 0) {
			_currentHistory = _history.back();
			_history.pop_back();
		}

		_historyBackup.push_front(_currentHistory);
		_currentHistory.LoadState();
		if(!_audioHistoryBuilder.empty()) {
			_audioHistory.insert(_audioHistory.begin(), _audioHistoryBuilder.begin(), _audioHistoryBuilder.end());
			_audioHistoryBuilder.clear();
		}
	}
}

void RewindManager::Start(bool forDebugger)
{
	if(_rewindState == RewindState::Stopped && EmulationSettings::GetRewindBufferSize() > 0) {
		Console::Pause();

		_rewindState = forDebugger ? RewindState::Debugging : RewindState::Starting;
		_videoHistoryBuilder.clear();
		_videoHistory.clear();
		_audioHistoryBuilder.clear();
		_audioHistory.clear();
		_historyBackup.clear();

		PopHistory();
		SoundMixer::StopAudio(true);
		EmulationSettings::SetFlags(EmulationFlags::ForceMaxSpeed);

		Console::Resume();
	}
}

void RewindManager::ForceStop()
{
	if(_rewindState != RewindState::Stopped) {
		while(_historyBackup.size() > 1) {
			_history.push_back(_historyBackup.front());
			_historyBackup.pop_front();
		}
		_currentHistory = _historyBackup.front();
		_historyBackup.clear();
		_rewindState = RewindState::Stopped;
		EmulationSettings::ClearFlags(EmulationFlags::ForceMaxSpeed);
	}
}

void RewindManager::Stop()
{
	if(_rewindState >= RewindState::Starting) {
		Console::Pause();
		if(_rewindState == RewindState::Started) {
			//Move back to the save state containing the frame currently shown on the screen
			if(_historyBackup.size() > 1) {
				_framesToFastForward = (uint32_t)_videoHistory.size() + _historyBackup.front().FrameCount;
				do {
					_history.push_back(_historyBackup.front());
					_framesToFastForward -= _historyBackup.front().FrameCount;
					_historyBackup.pop_front();

					_currentHistory = _historyBackup.front();
				}
				while(_framesToFastForward > RewindManager::BufferSize && _historyBackup.size() > 1);
			}
		} else {
			//We started rewinding, but didn't actually visually rewind anything yet
			//Move back to the save state containing the frame currently shown on the screen
			while(_historyBackup.size() > 1) {
				_history.push_back(_historyBackup.front());
				_historyBackup.pop_front();
			}
			_currentHistory = _historyBackup.front();
			_framesToFastForward = _historyBackup.front().FrameCount;
		}

		_currentHistory.LoadState();
		if(_framesToFastForward > 0) {
			_rewindState = RewindState::Stopping;
			_currentHistory.FrameCount = 0;
			EmulationSettings::SetFlags(EmulationFlags::ForceMaxSpeed);
		} else {
			_rewindState = RewindState::Stopped;
			_historyBackup.clear();
			EmulationSettings::ClearFlags(EmulationFlags::ForceMaxSpeed);
		}

		_videoHistoryBuilder.clear();
		_videoHistory.clear();
		_audioHistoryBuilder.clear();
		_audioHistory.clear();

		Console::Resume();
	}
}

void RewindManager::ProcessEndOfFrame()
{
	if(_rewindState >= RewindState::Starting) {
		if(_currentHistory.FrameCount <= 0) {
			if(_rewindState == RewindState::Debugging) {
				Stop();
			} else {
				PopHistory();
			}
		}
	} else if(_currentHistory.FrameCount >= RewindManager::BufferSize) {
		AddHistoryBlock();
	}
}

void RewindManager::ProcessFrame(void * frameBuffer, uint32_t width, uint32_t height)
{
	if(_rewindState == RewindState::Starting || _rewindState == RewindState::Started) {
		_videoHistoryBuilder.push_back(vector<uint32_t>((uint32_t*)frameBuffer, (uint32_t*)frameBuffer + width*height));

		if(_videoHistoryBuilder.size() == _historyBackup.front().FrameCount) {
			for(int i = (int)_videoHistoryBuilder.size() - 1; i >= 0; i--) {
				_videoHistory.push_front(_videoHistoryBuilder[i]);
			}
			_videoHistoryBuilder.clear();
		}

		if(_rewindState == RewindState::Started || _videoHistory.size() >= RewindManager::BufferSize) {
			_rewindState = RewindState::Started;
			EmulationSettings::ClearFlags(EmulationFlags::ForceMaxSpeed);
			if(!_videoHistory.empty()) {
				VideoRenderer::GetInstance()->UpdateFrame(_videoHistory.back().data(), width, height);
				_videoHistory.pop_back();
			}
		}
	} else if(_rewindState == RewindState::Stopping || _rewindState == RewindState::Debugging) {
		//Display nothing while resyncing
	} else {
		VideoRenderer::GetInstance()->UpdateFrame(frameBuffer, width, height);
	}
}

bool RewindManager::ProcessAudio(int16_t * soundBuffer, uint32_t sampleCount, uint32_t sampleRate)
{
	if(_rewindState == RewindState::Starting || _rewindState == RewindState::Started) {
		_audioHistoryBuilder.insert(_audioHistoryBuilder.end(), soundBuffer, soundBuffer + sampleCount * 2);

		if(_rewindState == RewindState::Started && _audioHistory.size() > sampleCount * 2) {
			for(uint32_t i = 0; i < sampleCount * 2; i++) {
				soundBuffer[i] = _audioHistory.back();
				_audioHistory.pop_back();
			}

			return true;
		} else {
			//Mute while we prepare to rewind
			return false;
		}
	} else if(_rewindState == RewindState::Stopping || _rewindState == RewindState::Debugging) {
		//Mute while we resync
		return false;
	} else {
		return true;
	}
}

void RewindManager::RecordInput(uint8_t port, uint8_t input)
{
	if(EmulationSettings::GetRewindBufferSize() > 0 && _instance && _instance->_rewindState == RewindState::Stopped) {
		_instance->_currentHistory.InputLogs[port].push_back(input);
	}
}

uint8_t RewindManager::GetInput(uint8_t port)
{
	if(!_instance->_currentHistory.InputLogs[port].empty()) {
		uint8_t value = _instance->_currentHistory.InputLogs[port].front();
		_instance->_currentHistory.InputLogs[port].pop_front();
		return value;
	} else {
		return 0;
	}
}

void RewindManager::StartRewinding(bool forDebugger)
{
	if(_instance) {
		_instance->Start(forDebugger);
	}
}

void RewindManager::StopRewinding(bool forDebugger)
{
	if(_instance) {
		if(forDebugger) {
			_instance->ForceStop();
		} else {
			_instance->Stop();
		}
	}
}

bool RewindManager::IsRewinding()
{
	return _instance ? _instance->_rewindState != RewindState::Stopped : false;
}

bool RewindManager::IsStepBack()
{
	return _instance ? _instance->_rewindState == RewindState::Debugging : false;
}

void RewindManager::RewindSeconds(uint32_t seconds)
{
	if(_instance && _instance->_rewindState == RewindState::Stopped) {
		uint32_t removeCount = (seconds * 60 / RewindManager::BufferSize) + 1;
		Console::Pause();
		for(uint32_t i = 0; i < removeCount; i++) {
			if(!_instance->_history.empty()) {
				_instance->_currentHistory = _instance->_history.back();
				_instance->_history.pop_back();
			} else {
				break;
			}
		}
		_instance->_currentHistory.LoadState();
		Console::Resume();
	}
}

void RewindManager::SendFrame(void * frameBuffer, uint32_t width, uint32_t height)
{
	if(_instance) {
		_instance->ProcessFrame(frameBuffer, width, height);
	} else {
		VideoRenderer::GetInstance()->UpdateFrame(frameBuffer, width, height);
	}
}

bool RewindManager::SendAudio(int16_t * soundBuffer, uint32_t sampleCount, uint32_t sampleRate)
{
	if(_instance) {
		return _instance->ProcessAudio(soundBuffer, sampleCount, sampleRate);
	}
	return true;
}
