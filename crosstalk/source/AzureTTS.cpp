#include "AzureTTS.h"
#include "ProducerLog.h"
#include <Windows.h>

int AzureTTS::Start(ConfigBase* config, ITTSCallback* callback, std::string& language){
	LogI("begin");
	if (config == nullptr) {
		LogE("invalid config");
		return -1;
	}
	if (callback == nullptr) {
		LogE("invalid callback");
		return -1;
	}

	callback_ = callback;
	Config* cfg = (Config*)config;
	secretkey_ = cfg->secretkey;
	region_ = cfg->region;
	target_language_ = language;
	LogI("region : %s", region_.c_str());

	AVChannelLayout out_ch_layout;
	av_channel_layout_default(&out_ch_layout, 2);
	AVChannelLayout in_ch_layout;
	av_channel_layout_default(&in_ch_layout, 1);
	int ret = swr_alloc_set_opts2(&resampleContext, &out_ch_layout, AV_SAMPLE_FMT_S16, AUDIO_SAMPLERATE, &in_ch_layout, AV_SAMPLE_FMT_S16, 16000, 0, nullptr);
	if (ret < 0) {
		LogE("alloc resample context failed, error message : %s", GetFFmpegErrorString(ret).c_str());
		resampleContext = nullptr;
	}
	else {
		ret = swr_init(resampleContext);
		if (ret < 0) {
			LogE("resample init failed, error message : %s", GetFFmpegErrorString(ret).c_str());
			swr_free(&resampleContext);
			resampleContext = nullptr;
		}
	}

	if (resampleContext == nullptr) {
		LogE("invalid resample context");
		if (callback_ != nullptr) {
			callback_->OnTTSStatus(this, STATUS_ERROR);
		}
		return -1;
	}

	auto speech_config = SpeechConfig::FromSubscription(secretkey_, region_);
	if (speech_config == nullptr) {
		LogE("create speech config failed");
		if (callback_ != nullptr) {
			callback_->OnTTSStatus(this, STATUS_ERROR);
		}
		swr_free(&resampleContext);
		resampleContext = nullptr;
		return -1;
	}
	LogI("create speech config success");

	//std::string filename = "D:\\speech.log";
	//speech_config->SetProperty(PropertyId::Speech_LogFilename, filename);
	//speech_config->SetProperty(PropertyId::SpeechServiceConnection_EnableAudioLogging, "true");

	speech_config->SetSpeechSynthesisLanguage(GetLanguageBCP47(target_language_));
	speech_config->SetSpeechSynthesisVoiceName(GetVoiceName(target_language_));
	speech_config->SetSpeechSynthesisOutputFormat(SpeechSynthesisOutputFormat::Raw16Khz16BitMonoPcm);

	auto audio_config = AudioConfig::FromStreamOutput(AudioOutputStream::CreatePullStream());
	synthesizer_ = SpeechSynthesizer::FromConfig(speech_config, audio_config);
	if (synthesizer_ == nullptr) {
		LogE("create synthesizer failed");
		if (callback_ != nullptr) {
			callback_->OnTTSStatus(this, STATUS_ERROR);
		}
		swr_free(&resampleContext);
		resampleContext = nullptr;
		return -1;
	}

	connection_ = Connection::FromSpeechSynthesizer(synthesizer_);
	connection_->Open(true);

	//auto result = synthesizer_->GetVoicesAsync().get();

	//if (result->Reason == ResultReason::VoicesListRetrieved) {
	//	LogI("available voice name : ");
	//	for (const auto& voice : result->Voices) {
	//		LogI("name : %s, lang : %s, sex : %d, type : %d",
	//			voice->Name.c_str(), voice->Locale.c_str(),
	//			voice->Gender, voice->VoiceType);
	//	}
	//}

	run_ = true;
	work_thread_ = std::thread(std::bind(&AzureTTS::worker, this));

	if (callback_ != nullptr) {
		callback_->OnTTSStatus(this, STATUS_STARTED);
	}

	LogI("success");
	return 0;
}

void AzureTTS::Stop(){
	LogI("begin");

	if (synthesizer_ != nullptr) {
		synthesizer_->StopSpeakingAsync();
	}

	do {
		std::unique_lock<std::mutex> lock(mutex_);
		run_ = false;
		condition_.notify_one();
	} while (0);
	
	if (work_thread_.joinable()) {
		LogI("wait for work thread exit");
		work_thread_.join();
		LogI("work thread exit success");
	}

	if (resampleContext != nullptr) {
		swr_free(&resampleContext);
		resampleContext = nullptr;
	}

	LogI("success");
}

int AzureTTS::Process(std::string& text) {
	std::unique_lock<std::mutex> lock(mutex_);
	texts_.push_back(text);
	condition_.notify_one();
	return 0;
}

void AzureTTS::worker() {
	LogI("enter");

	std::shared_ptr<std::vector<uint8_t>> audio = std::make_shared<std::vector<uint8_t>>(960);
	while (run_) {
		std::string text = "";
		do {
			std::unique_lock<std::mutex> lock(mutex_);
			if (texts_.empty() && run_) {
				condition_.wait(lock);
			}

			if (!texts_.empty()) {
				text = texts_.front();
				texts_.pop_front();
			}
		} while (0);

		if (!run_) {
			LogI("worker finish");
			break;
		}

		if (text.empty()) {
			continue;
		}

		auto result = synthesizer_->StartSpeakingTextAsync(text).get();
		if (result->Reason == ResultReason::Canceled) {
			auto cancellation = SpeechSynthesisCancellationDetails::FromResult(result);
			LogW("task canceled for text: [%s], reason: %d", text.c_str(), (int)cancellation->Reason);

			if (cancellation->Reason == CancellationReason::Error) {
				LogE("task canceled on error for text: [%s], errorcode: %d, details: %s, did you update the subscription info?",
					text.c_str(),
					(int)cancellation->ErrorCode,
					cancellation->ErrorDetails.c_str());
			}
			break;
		}

		//ProcessAudio(result->GetAudioData()->data(), result->GetAudioLength());
		
		auto audioDataStream = AudioDataStream::FromResult(result);
		uint8_t buffer[960];
		uint32_t bytesRead = 0;
		uint32_t pos = 0;
		do {
			bytesRead = audioDataStream->ReadData(audioDataStream->GetPosition(), buffer, 960);
			pos += bytesRead;
			ProcessAudio(buffer, bytesRead);
		} while (bytesRead > 0 && run_);		
	}

	LogI("exit");
}

void AzureTTS::ProcessAudio(const uint8_t* audio, size_t len) {
	if (len == 0) return;
	//LogI("audio len : %d", len);

	int outsamples = swr_get_out_samples(resampleContext, len / 2);
	if (outsamples > 0) {
		std::vector<uint8_t> buffer(outsamples * 4, 0);

		uint8_t* dst[] = { buffer.data() };
		const uint8_t* src[] = { audio };
		int convert_samples = swr_convert(resampleContext, dst, outsamples, src, len / 2);
		if (convert_samples > 0) {
			if (callback_ != nullptr) {
				callback_->OnTTSResult(this, buffer.data(), convert_samples * 4, AUDIO_SAMPLERATE, AUDIO_CHANNELS);
			}
		}
		else {
			LogE("resample failed");
		}
	}
	else {
		LogE("get output samples failed");
	}
}