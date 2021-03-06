#include "stdafx.h"
#include "IRenderingDevice.h"
#include "VideoDecoder.h"
#include "EmulationSettings.h"
#include "DefaultVideoFilter.h"
#include "BisqwitNtscFilter.h"
#include "NtscFilter.h"
#include "HdVideoFilter.h"
#include "ScaleFilter.h"
#include "VideoRenderer.h"
#include "RewindManager.h"
#include "PPU.h"
#include "HdNesPack.h"

unique_ptr<VideoDecoder> VideoDecoder::Instance;

VideoDecoder* VideoDecoder::GetInstance()
{
	if(!Instance) {
		Instance.reset(new VideoDecoder());
	}
	return Instance.get();
}

void VideoDecoder::Release()
{
	if(Instance) {
		Instance.reset();
	}
}

VideoDecoder::VideoDecoder()
{
	_frameChanged = false;
	_stopFlag = false;
	UpdateVideoFilter();
}

VideoDecoder::~VideoDecoder()
{
	StopThread();
}

FrameInfo VideoDecoder::GetFrameInfo()
{
	return _videoFilter->GetFrameInfo();
}

void VideoDecoder::GetScreenSize(ScreenSize &size, bool ignoreScale)
{
	if(_videoFilter) {
		OverscanDimensions overscan = ignoreScale ? _videoFilter->GetOverscan() : EmulationSettings::GetOverscanDimensions();
		FrameInfo frameInfo{ overscan.GetScreenWidth(), overscan.GetScreenHeight(), PPU::ScreenWidth, PPU::ScreenHeight, 4 };
		double aspectRatio = EmulationSettings::GetAspectRatio();
		double scale = (ignoreScale ? 1 : EmulationSettings::GetVideoScale());
		size.Width = (int32_t)(frameInfo.Width * scale);
		size.Height = (int32_t)(frameInfo.Height * scale);
		if(aspectRatio != 0.0) {
			size.Width = (uint32_t)(frameInfo.OriginalHeight * scale * aspectRatio * ((double)frameInfo.Width / frameInfo.OriginalWidth));
		}
		size.Scale = scale;
	}
}

void VideoDecoder::UpdateVideoFilter()
{
	VideoFilterType newFilter = EmulationSettings::GetVideoFilterType();

	if(_videoFilterType != newFilter || _videoFilter == nullptr || (_hdScreenTiles && !_hdFilterEnabled) || (!_hdScreenTiles && _hdFilterEnabled)) {
		_videoFilterType = newFilter;
		_videoFilter.reset(new DefaultVideoFilter());
		_scaleFilter.reset();

		switch(_videoFilterType) {
			case VideoFilterType::None: break;
			case VideoFilterType::NTSC: _videoFilter.reset(new NtscFilter()); break;
			case VideoFilterType::BisqwitNtsc: _videoFilter.reset(new BisqwitNtscFilter(1)); break;
			case VideoFilterType::BisqwitNtscHalfRes: _videoFilter.reset(new BisqwitNtscFilter(2)); break;
			case VideoFilterType::BisqwitNtscQuarterRes: _videoFilter.reset(new BisqwitNtscFilter(4)); break;
			default: _scaleFilter = ScaleFilter::GetScaleFilter(_videoFilterType); break;
		}

		_hdFilterEnabled = false;
		if(_hdScreenTiles) {
			_videoFilter.reset(new HdVideoFilter());
			_hdFilterEnabled = true;
		}
	}
}

void VideoDecoder::DecodeFrame()
{
	UpdateVideoFilter();

	if(_hdFilterEnabled) {
		((HdVideoFilter*)_videoFilter.get())->SetHdScreenTiles(_hdScreenTiles);
	}
	_videoFilter->SendFrame(_ppuOutputBuffer);

	uint32_t* outputBuffer = (uint32_t*)_videoFilter->GetOutputBuffer();
	if(_scaleFilter) {
		outputBuffer = _scaleFilter->ApplyFilter(outputBuffer, _videoFilter->GetFrameInfo().Width, _videoFilter->GetFrameInfo().Height);
	}

	ScreenSize screenSize;
	GetScreenSize(screenSize, true);
	if(_previousScale != EmulationSettings::GetVideoScale() || screenSize.Height != _previousScreenSize.Height || screenSize.Width != _previousScreenSize.Width) {
		MessageManager::SendNotification(ConsoleNotificationType::ResolutionChanged);
	}
	_previousScale = EmulationSettings::GetVideoScale();
	_previousScreenSize = screenSize;
	
	FrameInfo frameInfo = _videoFilter->GetFrameInfo();
	if(_scaleFilter) {
		frameInfo = _scaleFilter->GetFrameInfo(frameInfo);
	}

	_frameChanged = false;
	
	//Rewind manager will take care of sending the correct frame to the video renderer
	RewindManager::SendFrame(outputBuffer, frameInfo.Width, frameInfo.Height);
}

void VideoDecoder::DebugDecodeFrame(uint16_t* inputBuffer, uint32_t* outputBuffer, uint32_t length)
{
	for(uint32_t i = 0; i < length; i++) {
		if(inputBuffer[i] == 0xFFFF) {
			//This pixel is transparent
			outputBuffer[i] = 0;
		} else {
			outputBuffer[i] = EmulationSettings::GetRgbPalette()[inputBuffer[i] & 0x3F];
		}
	}
}

void VideoDecoder::DecodeThread()
{
	//This thread will decode the PPU's output (color ID to RGB, intensify r/g/b and produce a HD version of the frame if needed)
	while(!_stopFlag.load()) {
		//DecodeFrame returns the final ARGB frame we want to display in the emulator window
		while(!_frameChanged) {
			_waitForFrame.Wait();
			if(_stopFlag.load()) {
				return;
			}
		}

		DecodeFrame();
	}
}

uint32_t VideoDecoder::GetFrameCount()
{
	return _frameCount;
}

void VideoDecoder::UpdateFrameSync(void *ppuOutputBuffer, HdPpuPixelInfo *hdPixelInfo)
{
	_hdScreenTiles = hdPixelInfo;
	_ppuOutputBuffer = (uint16_t*)ppuOutputBuffer;
	DecodeFrame();
	_frameCount++;
}

void VideoDecoder::UpdateFrame(void *ppuOutputBuffer, HdPpuPixelInfo *hdPixelInfo)
{
	if(_frameChanged) {
		//Last frame isn't done decoding yet - sometimes Signal() introduces a 25-30ms delay
		while(_frameChanged) {
			//Spin until decode is done
		}
		//At this point, we are sure that the decode thread is no longer busy
	}

	_hdScreenTiles = hdPixelInfo;
	_ppuOutputBuffer = (uint16_t*)ppuOutputBuffer;
	_frameChanged = true;
	_waitForFrame.Signal();

	_frameCount++;
}

void VideoDecoder::StartThread()
{
	if(!_decodeThread) {	
		_stopFlag = false;
		_frameChanged = false;
		_frameCount = 0;
		_waitForFrame.Reset();

		_decodeThread.reset(new thread(&VideoDecoder::DecodeThread, this));
	}
}

void VideoDecoder::StopThread()
{
	_stopFlag = true;
	if(_decodeThread) {
		_waitForFrame.Signal();
		_decodeThread->join();

		_decodeThread.reset();

		_hdScreenTiles = nullptr;
		EmulationSettings::SetPpuModel(PpuModel::Ppu2C02);
		UpdateVideoFilter();
		if(_ppuOutputBuffer != nullptr) {
			//Clear whole screen
			for(uint32_t i = 0; i < PPU::PixelCount; i++) {
				_ppuOutputBuffer[i] = 14; //Black
			}
			DecodeFrame();
		}
		_ppuOutputBuffer = nullptr;
	}
}

bool VideoDecoder::IsRunning()
{
	return _decodeThread != nullptr;
}

void VideoDecoder::TakeScreenshot()
{
	if(_videoFilter) {
		_videoFilter->TakeScreenshot(_videoFilterType);
	}
}

void VideoDecoder::TakeScreenshot(std::stringstream &stream)
{
	if(_videoFilter) {
		_videoFilter->TakeScreenshot(_videoFilterType, "", &stream);
	}
}
