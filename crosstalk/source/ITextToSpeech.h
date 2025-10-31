#pragma once

#include "Common.h"

class ITextToSpeech {
public:

	struct ConfigBase {
		std::string name;
	};

	class ITTSCallback {
	public:
		virtual ~ITTSCallback() {}
		virtual void OnTTSStatus(ITextToSpeech* obj, Status status) = 0;
		virtual void OnTTSResult(ITextToSpeech* obj, const uint8_t* audio, const size_t len, int samplerate, int channels) = 0;
	};

	virtual ~ITextToSpeech() {}
	virtual int Start(ConfigBase* config, ITTSCallback* callback, std::string& language) = 0;
	virtual void Stop() = 0;
	virtual int Process(std::string& text) = 0;
};