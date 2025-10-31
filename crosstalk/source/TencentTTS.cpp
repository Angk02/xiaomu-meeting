#include "TencentTTS.h"
#include "ProducerLog.h"
#include "Common.h"
#include "base64.h"

int TencentTTS::Start(ConfigBase* config, ITTSCallback* callback, std::string& language) {
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
	taskid_ = GenerateGUID();
	LogI("task id : %s", taskid_.c_str());
	Config* cfg = (Config*)config;
	appid_ = cfg->appid;
	secretid_ = cfg->secretid;
	secretkey_ = cfg->secretkey;

	TencentCloud::Credential cred = TencentCloud::Credential(secretid_, secretkey_);
	TencentCloud::HttpProfile httpProfile = TencentCloud::HttpProfile();
	httpProfile.SetKeepAlive(true);  // 状态保持，默认是False
	//httpProfile.SetEndpoint("tts.cloud.tencent.com/stream");  // 指定接入地域域名(默认就近接入) 
	httpProfile.SetReqTimeout(30);  // 请求超时时间，单位为秒(默认60秒)
	httpProfile.SetConnectTimeout(30); // 响应超时时间，单位是秒(默认是60秒)
	TencentCloud::ClientProfile clientProfile = TencentCloud::ClientProfile(httpProfile);

	ttsClient_ = new(std::nothrow) TtsClient(cred, "ap-beijing", httpProfile);
	if (ttsClient_ == nullptr) {
		LogE("create tts client failed");
		return -1;
	}

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
		delete ttsClient_;
		ttsClient_ = nullptr;
		return false;
	}
	started_.store(true);
	LogI("success");
	return 0;
}

void TencentTTS::Stop() {
	LogI("begin");
	started_.store(false);
	if (ttsClient_ != nullptr) {
		delete ttsClient_;
		ttsClient_ = nullptr;
	}

	if (resampleContext != nullptr) {
		swr_free(&resampleContext);
		resampleContext = nullptr;
	}
	LogI("success");
}

int TencentTTS::Process(std::string& text) {
	if (!started_.load()) {
		LogW("task not start");
		return -1;
	}
	if (ttsClient_ == nullptr) {
		LogE("invalid tts client");
		return -1;
	}
	Model::TextToVoiceRequest request;
	request.SetCodec("pcm");
	request.SetModelType(1);
	request.SetPrimaryLanguage(2);
	request.SetProjectId(0);
	request.SetSampleRate(16000);
	std::string sessionid = GenerateGUID();
	request.SetSessionId(sessionid);
	request.SetText(text);
	request.SetVoiceType(501000);
	request.SetSpeed(0);
	request.SetVolume(10);

	ttsClient_->TextToVoiceAsync(request,
		std::bind(&TencentTTS::OnResponse,
			this, std::placeholders::_1,
			std::placeholders::_2,
			std::placeholders::_3,
			std::placeholders::_4));

	return 0;
}

void TencentTTS::ProcessAudio(const uint8_t* data, int len) {
	static bool first = false;
	if (!first) {
		first = true;
		LogI("receive first audio frame from server");
	}

	std::vector<uint8_t> resampleData(12 * len, 0);
	uint8_t* dst[] = { resampleData.data() };
	const uint8_t* src[] = { data };
	int convert_samples = swr_convert(resampleContext, dst, 3 * len, src, len / 2);
	if (convert_samples > 0) {
		int resultlen = convert_samples * 4;
		int srcpos = 0;
		while (resultlen > 0) {
			int copylen = std::min(AUDIO_10MS_DATA_LEN - audio_len_, resultlen);
			memcpy(&audio_data_[audio_len_], resampleData.data() + srcpos, copylen);
			audio_len_ += copylen;
			srcpos += copylen;
			resultlen -= copylen;

			if (audio_len_ == AUDIO_10MS_DATA_LEN) {
				callback_->OnTTSResult(this, (const uint8_t*)audio_data_, audio_len_, AUDIO_SAMPLERATE, AUDIO_CHANNELS);
				audio_len_ = 0;
			}
		}
	}
}

void TencentTTS::OnResponse(const TtsClient* client,
	const TencentCloud::Tts::V20190823::Model::TextToVoiceRequest& request,
	TtsClient::TextToVoiceOutcome outcome,
	const std::shared_ptr<const TencentCloud::AsyncCallerContext>& context) {
	if (outcome.IsSuccess()) {
		auto result = outcome.GetResult();
		auto audio = result.GetAudio();
		uint8_t* data = (uint8_t*)audio.data();
		int len = audio.size();

		int outlen = Base64::DecodedLength((const char*)data, len);
		std::vector<char> out(outlen, 0);
		Base64::Decode((const char*)data, len, out.data(), out.size());

		ProcessAudio((const uint8_t*)out.data(), out.size());
	}
	else {
		LogE("text to speech error : %s", outcome.GetError().PrintAll().c_str());
	}
}