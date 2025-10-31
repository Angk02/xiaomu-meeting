#include "Producer.h"
#include "ProducerImpl.h"
#include "ProducerLog.h"
#include "ALIRecognition.h"
#include "TencentTranslation.h"
#include "TencentTTS.h"
#include "AWSRecognition.h"
#include "AzureRecognition.h"
#include "AzureRecAndTrans.h"
#include "AzureTTS.h"
#include <tencentcloud/core/TencentCloud.h>

class AWSLogSink : public Aws::Utils::Logging::LogSystemInterface {
public:
	virtual Aws::Utils::Logging::LogLevel GetLogLevel(void) const override {
		return loglevel_;
	}
	/**
	 * Does a printf style output to the output stream. Don't use this, it's unsafe. See LogStream
	 */
	virtual void Log(Aws::Utils::Logging::LogLevel logLevel, const char* tag, const char* formatStr, ...) override {
		va_list vl;
		va_start(vl, formatStr);
		vaLog(logLevel, tag, formatStr, vl);
		va_end(vl);
	}
	/**
	 * va_list overload for Log, avoid using this as well.
	 */
	virtual void vaLog(Aws::Utils::Logging::LogLevel logLevel, const char* tag, const char* formatStr, va_list args) override {
		char msg[1024];
		vsnprintf(msg, 1024, formatStr, args);
		if (logLevel == Aws::Utils::Logging::LogLevel::Error || logLevel == Aws::Utils::Logging::LogLevel::Fatal) {
			LogE("[%s] %s", tag, msg);
		}
		else if (logLevel == Aws::Utils::Logging::LogLevel::Info) {
			LogI("[%s] %s", tag, msg);
		}
		else if (logLevel == Aws::Utils::Logging::LogLevel::Warn) {
			LogW("[%s] %s", tag, msg);
		}
		else if (logLevel == Aws::Utils::Logging::LogLevel::Debug || logLevel == Aws::Utils::Logging::LogLevel::Trace) {
			LogD("[%s] %s", tag, msg);
		}
	}
	/**
	* Writes the stream to the output stream.
	*/
	virtual void LogStream(Aws::Utils::Logging::LogLevel logLevel, const char* tag, const Aws::OStringStream& messageStream) override {
		std::string str = messageStream.str();
		const char* msg = str.c_str();
		if (logLevel == Aws::Utils::Logging::LogLevel::Error || logLevel == Aws::Utils::Logging::LogLevel::Fatal) {
			LogE("[%s] %s", tag, msg);
		}
		else if (logLevel == Aws::Utils::Logging::LogLevel::Info) {
			LogI("[%s] %s", tag, msg);
		}
		else if (logLevel == Aws::Utils::Logging::LogLevel::Warn) {
			LogW("[%s] %s", tag, msg);
		}
		else if (logLevel == Aws::Utils::Logging::LogLevel::Debug || logLevel == Aws::Utils::Logging::LogLevel::Trace) {
			LogD("[%s] %s", tag, msg);
		}
	}
	/**
	 * Writes any buffered messages to the underlying device if the logger supports buffering.
	 */
	virtual void Flush() override {

	}

private:
	Aws::Utils::Logging::LogLevel loglevel_ = Aws::Utils::Logging::LogLevel::Trace;
};

class AWSCRTLogSink : public Aws::Utils::Logging::CRTLogSystemInterface {
public:
	virtual Aws::Utils::Logging::LogLevel GetLogLevel() const override {
		return loglevel_;
	}
	/**
	 * Set a new log level. This has the immediate effect of changing the log output to the new level.
	 */
	virtual void SetLogLevel(Aws::Utils::Logging::LogLevel logLevel) override {
		loglevel_ = logLevel;
	}

	/**
	 * Handle the logging information from common runtime libraries.
	 * Redirect them to C++ SDK logging system by default.
	 */
	virtual void Log(Aws::Utils::Logging::LogLevel logLevel, const char* subjectName, const char* formatStr, va_list args) override {
		char msg[1024];
		vsnprintf(msg, 1024, formatStr, args);
		if (logLevel == Aws::Utils::Logging::LogLevel::Error || logLevel == Aws::Utils::Logging::LogLevel::Fatal) {
			LogE("[%s] %s", subjectName, msg);
		}
		else if (logLevel == Aws::Utils::Logging::LogLevel::Info) {
			LogI("[%s] %s", subjectName, msg);
		}
		else if (logLevel == Aws::Utils::Logging::LogLevel::Warn) {
			LogW("[%s] %s", subjectName, msg);
		}
		else if (logLevel == Aws::Utils::Logging::LogLevel::Debug || logLevel == Aws::Utils::Logging::LogLevel::Trace) {
			LogD("[%s] %s", subjectName, msg);
		}
	}

private:
	Aws::Utils::Logging::LogLevel loglevel_ = Aws::Utils::Logging::LogLevel::Trace;
};

ProducerImpl::~ProducerImpl() {
	//LogI("begin");
	//Stop();
	//LogI("finish");
}

int ProducerImpl::Start(std::string& captureName, std::string& playoutName, ASRAndTransType type, ICallback* callback,
	std::string sourceLanguage, std::string targetLanguage){
	LogI("capture device name : %s, playout device name : %s, sink : %p", captureName.c_str(), playoutName.c_str(), callback);
	//TencentCloud::InitAPI();

	//options_.loggingOptions.logger_create_fn = []() {
	//	return std::make_shared<AWSLogSink>();
	//};

	//options_.loggingOptions.crt_logger_create_fn = []() {
	//	return std::make_shared<AWSCRTLogSink>();
	//};

	//options_.loggingOptions.logLevel = Aws::Utils::Logging::LogLevel::Info;

	//Aws::InitAPI(options_);

	callback_ = callback;

	do {
		buffer_ = new (std::nothrow) LoopbackBuffer();
		if (buffer_ == nullptr) {
			LogE("create loopback buffer failed");
			break;
		}
		if (!buffer_->Initialize()) {
			LogE("loopback buffer initialize failed");
			break;
		}

		render_ = new (std::nothrow) CWASAPIRenderer(playoutName);
		if (render_ == nullptr) {
			LogE("create render failed");
			break;
		}

		if (!render_->Initialize(10)) {
			LogE("render initialize failed");
			break;
		}

		if (!render_->Start(buffer_)) {
			LogE("render start failed");
			break;
		}

		if (type == DEFAULT) {
#if AZURE_ASR
			char* access_key = getenv("AZURE_SECRET_KEY");
			char* region = getenv("AZURE_REGION");

			AzureTTS::Config ttsConfig;
			ttsConfig.name = "AZURETTS";
			ttsConfig.secretkey = access_key == nullptr ? "" : std::string(access_key);
			ttsConfig.region = region == nullptr ? "" : std::string(region);

			tts_ = new(std::nothrow) AzureTTS();
			if (tts_ == nullptr) {
				LogE("create azure tts failed");
				break;
			}

			if (tts_->Start(&ttsConfig, this, targetLanguage) != 0) {
				LogE("azure tts start failed");
				break;
			}

			//char* sid = getenv("TENCENT_SECRET_ID");
			//char* skey = getenv("TECENT_SECRET_KEY");
			//std::string secretid = sid == nullptr ? "" : std::string(sid);
			//std::string secretkey = skey == nullptr ? "" : std::string(skey);

			//TencentTTS::Config ttsConfig;
			//ttsConfig.name = "TENCENTTTS";
			//ttsConfig.appid = "1257169994";
			//ttsConfig.secretid = secretid;
			//ttsConfig.secretkey = secretkey;

			//tts_ = new(std::nothrow) TencentTTS();
			//if (tts_ == nullptr) {
			//	LogE("create tecent tts failed");
			//	break;
			//}

			//if (tts_->Start(&ttsConfig, this, targetLanguage) != 0) {
			//	LogE("tencent tts start failed");
			//	break;
			//}

			AzureRecAndTrans::Config config;
			config.name = "AZURE";
			config.secretkey = access_key == nullptr ? "" : std::string(access_key);
			config.region = region == nullptr ? "" : std::string(region);
			rec_trans_ = new (std::nothrow) AzureRecAndTrans();
			if (rec_trans_ == nullptr) {
				LogE("create rec and trans failed");
				break;
			}

			if (rec_trans_->Start(&config, this, sourceLanguage, targetLanguage) != 0) {
				LogE("rec and trans start failed");
				break;
			}
#else
			char* sid = getenv("TENCENT_SECRET_ID");
			char* skey = getenv("TECENT_SECRET_KEY");
			std::string secretid = sid == nullptr ? "" : std::string(sid);
			std::string secretkey = skey == nullptr ? "" : std::string(skey);

			TencentTTS::Config ttsConfig;
			ttsConfig.name = "TENCENTTTS";
			ttsConfig.appid = "1257169994";
			ttsConfig.secretid = secretid;
			ttsConfig.secretkey = secretkey;

			tts_ = new(std::nothrow) TencentTTS();
			if (tts_ == nullptr) {
				LogE("create tecent tts failed");
				break;
			}

			if (tts_->Start(&ttsConfig, this, targetLanguage) != 0) {
				LogE("tencent tts start failed");
				break;
			}

			TencentTranslation::Config transConfig;
			transConfig.name = "TENCENT";
			transConfig.appid = "1257169994";
			transConfig.secretid = secretid;
			transConfig.secretkey = secretkey;

			trans_ = new(std::nothrow) TencentTranslation();
			if (trans_ == nullptr) {
				LogE("create tecent translation failed");
				break;
			}

			if (trans_->Start(&transConfig, this, sourceLanguage, targetLanguage) != 0) {
				LogE("tecent translation start failed");
				break;
			}
#endif
			
#if DEFAULT_ASR
			IRecognition::ConfigBase* configbase = nullptr;
			char* apikey_c = getenv("ALIYUN_API_KEY");
			std::string apikey = apikey_c == nullptr ? "" : std::string(apikey_c);
			ALIRecognition::Config config;
			config.name = "ALIYUN";
			config.apikey = apikey;
			configbase = &config;
			asr_ = new(std::nothrow) ALIRecognition();
			if (asr_ == nullptr) {
				LogE("create ALIYUN asr failed");
				break;
			}

			if (asr_->Start(configbase, this, sourceLanguage) != 0) {
				LogE("ALIYUN recognition start failed");
				break;
			}
#elif AMAZON_ASR
			IRecognition::ConfigBase* configbase = nullptr;
			char* access_key_id = getenv("AWS_ACCESS_KEY_ID");
			char* access_key = getenv("AWS_SECRET_ACCESS_KEY");
			AWSRecognition::Config config;
			config.name = "AWS";
			config.secretid = access_key_id == nullptr ? "" : std::string(access_key_id);
			config.secretkey = access_key == nullptr ? "" : std::string(access_key);
			configbase = &config;
			asr_ = new (std::nothrow) AWSRecognition();
			if (asr_ == nullptr) {
				LogE("create ALIYUN asr failed");
				break;
			}

			if (asr_->Start(configbase, this, sourceLanguage) != 0) {
				LogE("ALIYUN recognition start failed");
				break;
			}
#elif AZURE_ASR

#endif
		}
		else { //TODO 支持别的asr
			LogE("invalid recognition and translation type");
			break;
		}

		capture_ = new(std::nothrow) AudioCaptrue();
		if (capture_ == nullptr) {
			LogE("create audio capture failed");
			break;
		}

		std::string uniqueName = "";
		if (capture_->Init(captureName, uniqueName, this) != 0) {
			LogE("audio capture init failed");
			break;
		}

		capture_->SetPlayoutDevice(playoutName);

		if (capture_->Start() != 0) {
			LogE("audio capture start failed");
			break;
		}
		LogI("success");
		return 0;
	} while (0);

	if (capture_ != nullptr) {
		capture_->Stop();
		capture_->Release();
		delete capture_;
		capture_ = nullptr;
	}

#if AZURE_ASR
	if (rec_trans_ != nullptr) {
		rec_trans_->Stop();
		delete rec_trans_;
		rec_trans_ = nullptr;
	}
#else
	if (asr_ != nullptr) {
		asr_->Stop();
		delete asr_;
		asr_ = nullptr;
	}

	if (trans_ != nullptr) {
		trans_->Stop();
		delete trans_;
		trans_ = nullptr;
	}
#endif

	if (tts_ != nullptr) {
		tts_->Stop();
		delete tts_;
		tts_ = nullptr;
	}

	if (render_ != nullptr) {
		render_->Stop();
		render_->Shutdown();
		delete render_;
		render_ = nullptr;
	}

	if (buffer_ != nullptr) {
		delete buffer_;
		buffer_ = nullptr;
	}

	LogE("failed");
	return -1;
}

int ProducerImpl::Stop() {
	LogI("start");
	if (capture_ != nullptr) {
		capture_->Stop();
		capture_->Release();
		delete capture_;
		capture_ = nullptr;
	}

#if AZURE_ASR
	if (rec_trans_ != nullptr) {
		rec_trans_->Stop();
		delete rec_trans_;
		rec_trans_ = nullptr;
	}
#else
	if (asr_ != nullptr) {
		asr_->Stop();
		delete asr_;
		asr_ = nullptr;
	}

	if (trans_ != nullptr) {
		trans_->Stop();
		delete trans_;
		trans_ = nullptr;
	}
#endif

	if (tts_ != nullptr) {
		tts_->Stop();
		delete tts_;
		tts_ = nullptr;
	}

	if (render_ != nullptr) {
		render_->Stop();
		render_->Shutdown();
		delete render_;
		render_ = nullptr;
	}

	if (buffer_ != nullptr) {
		delete buffer_;
		buffer_ = nullptr;
	}

	//TencentCloud::ShutdownAPI();
	//Aws::ShutdownAPI(options_);
	LogI("success");
	return 0;
}

void ProducerImpl::OnAudioFrame(const uint8_t* audio, const size_t len, int samplerate, int channels) {
	//LogD("len : %d, samplerate : %d, channels : %d", len, samplerate, channels);
#if AZURE_ASR
	if (rec_trans_ != nullptr) {
		rec_trans_->Process(audio, len, samplerate, channels);
	}
#else
	if (asr_ != nullptr) {
		asr_->Process(audio, len, samplerate, channels);
	}
#endif
}

void ProducerImpl::OnASRStatus(IRecognition* obj, Status status) {
	LogI("asr status : %d", status);
}

void ProducerImpl::OnASRResult(IRecognition* obj, std::string& result) {
	//LogI("asr result : %s", result.c_str());
#if !AZURE_ASR
	if (trans_ != nullptr) {
		trans_->Process(result);
	}
#endif
}

void ProducerImpl::OnTransStatus(ITranslation* obj, Status status) {
	LogI("trans status : %d", status);
}

void ProducerImpl::OnTransResult(ITranslation* obj, std::string& original, std::string& translation) {
	LogI("trans result, original : %s, translation : %s", original.c_str(), translation.c_str());
	if (tts_ != nullptr) {
		tts_->Process(translation);
	}

	if (callback_ != nullptr) {
		callback_->OnText(original, translation);
	}
}

void ProducerImpl::OnTTSStatus(ITextToSpeech* obj, Status status) {
	LogI("tts status : %d", status);
}

void ProducerImpl::OnTTSResult(ITextToSpeech* obj, const uint8_t* audio, const size_t len, int samplerate, int channels) {
	if (buffer_ != nullptr) {
		buffer_->Write((PUCHAR)audio, len);
	}
}

Aws::SDKOptions g_options;

BOOL APIENTRY DllMain(HMODULE hModule,
	DWORD  ul_reason_for_call,
	LPVOID lpReserved
)
{
	switch (ul_reason_for_call)
	{
	case DLL_PROCESS_ATTACH:
	{
		CoInitializeEx(nullptr, 0);
		TencentCloud::InitAPI();

		//g_options.loggingOptions.logger_create_fn = []() {
		//	return std::make_shared<AWSLogSink>();
		//	};

		//g_options.loggingOptions.crt_logger_create_fn = []() {
		//	return std::make_shared<AWSCRTLogSink>();
		//	};

		//g_options.loggingOptions.logLevel = Aws::Utils::Logging::LogLevel::Info;

		//Aws::InitAPI(g_options);
	}
	break;

	case DLL_THREAD_ATTACH:
		break;
	case DLL_THREAD_DETACH:
		break;
	case DLL_PROCESS_DETACH:
		break;
	}
	return TRUE;
}

IProducer* CreateProducer() {
	return new(std::nothrow) ProducerImpl();
}

std::vector<std::string> GetAudioCaptureDevices() {
	std::vector<std::string> ret;

	std::vector<DShow::AudioDevice> audio_devices;
	DShow::Device::EnumAudioDevices(audio_devices);
	for (auto& adev : audio_devices) {
		ret.push_back(WideToMulti(adev.name));
	}
	return ret;
}

std::vector<std::string> GetAudioPlayoutDevices() {
	return CWASAPIRenderer::RenderDevices();
}