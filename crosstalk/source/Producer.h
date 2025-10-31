#pragma once

#include <string>
#include <vector>

enum class LogLevel {
	LOG_LEVEL_DEBUG = 0,
	LOG_LEVEL_INFO = 1,
	LOG_LEVEL_WARNING = 2,
	LOG_LEVEL_ERROR = 3,
};

#ifdef PRODUCER_EXPORTS
#define PRODUCER_C_API extern "C" __declspec(dllexport)
#define PRODUCER_API extern __declspec(dllexport)
#else
#define PRODUCER_C_API extern "C" __declspec(dllimport)
#define PRODUCER_API extern __declspec(dllimport)
#endif

enum ASRAndTransType {
	DEFAULT = 0
};

class IProducer {
public:

	class ICallback {
	public:
		virtual ~ICallback() {}
		virtual void OnText(std::string& original, std::string& translation) = 0;
	};

	virtual ~IProducer() {}
	virtual int Start(std::string& captureName, std::string& playoutName, ASRAndTransType type, ICallback* callback,
					  std::string sourceLanguage = "zh", std::string targetLanguage = "en") = 0;
	virtual int Stop() = 0;
};

typedef void (*ProducerLogSink)(LogLevel level, const char* msg, void* context);

/*
 * 设置Producer日志回调，如果不设置回调，默认输出到控制台
 */
PRODUCER_C_API void SetProducerLogSink(ProducerLogSink sink, void* context);

PRODUCER_API std::vector<std::string> GetAudioCaptureDevices();

PRODUCER_API std::vector<std::string> GetAudioPlayoutDevices();

PRODUCER_C_API IProducer* CreateProducer();