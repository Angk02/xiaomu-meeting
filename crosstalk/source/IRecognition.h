#pragma once

#include "Common.h"

class IRecognition {
public:

	struct ConfigBase {
		std::string name;
	};

	class IRecCallback {
	public:
		virtual ~IRecCallback() {}
		virtual void OnASRStatus(IRecognition* obj, Status status) = 0;
		virtual void OnASRResult(IRecognition* obj, std::string& result) = 0;
	};

	virtual ~IRecognition() {}
	virtual int Start(ConfigBase* config, IRecCallback* callback, std::string& sourceLanguage) = 0;
	virtual void Stop() = 0;
	virtual int Process(const uint8_t* audio, const size_t len, int samplerate, int channels) = 0;
};