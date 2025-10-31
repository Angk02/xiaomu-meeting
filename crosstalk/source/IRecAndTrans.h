#pragma once

#include "ITranslation.h"

class IRecAndTrans : public ITranslation {
public:
	virtual ~IRecAndTrans() {}
	virtual int Process(const uint8_t* audio, const size_t len, int samplerate, int channels) = 0;
	virtual int Start(ITranslation::ConfigBase* config, ITransCallback* callback, std::string& sourceLanguage, std::string targetLanguage) = 0;
	virtual void Stop() = 0;
	virtual int Process(std::string& original) { return -1; }
};