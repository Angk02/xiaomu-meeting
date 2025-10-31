#pragma once

#include "Common.h"

class ITranslation {
public:

	struct ConfigBase {
		std::string name;
	};

	class ITransCallback {
	public:
		virtual ~ITransCallback() {}
		virtual void OnTransStatus(ITranslation* obj, Status status) = 0;
		virtual void OnTransResult(ITranslation* obj, std::string& original, std::string& translation) = 0;
	};

	virtual ~ITranslation() {}
	virtual int Start(ConfigBase* config, ITransCallback* callback, std::string& sourceLanguage, std::string targetLanguage) = 0;
	virtual void Stop() = 0;
	virtual int Process(std::string& original) = 0;
};