#include "Common.h"
#include <Windows.h>
#include <random>
#include <combaseapi.h>
#include <map>

std::string WideToMulti(const std::wstring input) {
	if (input.empty()) return std::string();
	int size_needed = WideCharToMultiByte(CP_UTF8, 0, &input[0], (int)input.size(), NULL, 0, NULL, NULL);
	std::string multi(size_needed, 0);
	WideCharToMultiByte(CP_UTF8, 0, &input[0], (int)input.size(), &multi[0], size_needed, NULL, NULL);
	return multi;
}

std::wstring MultiToWide(const std::string input) {
	if (input.empty()) return std::wstring();
	int size_needed = MultiByteToWideChar(CP_UTF8, 0, &input[0], (int)input.size(), NULL, 0);
	std::wstring wide(size_needed, 0);
	MultiByteToWideChar(CP_UTF8, 0, &input[0], (int)input.size(), &wide[0], size_needed);
	return wide;
}

bool AudioFormatSupported(DShow::AudioFormat format) {
	return format == DShow::AudioFormat::AAC ||
		format == DShow::AudioFormat::AC3 ||
		format == DShow::AudioFormat::MPGA ||
		format == DShow::AudioFormat::Wave16bit ||
		format == DShow::AudioFormat::WaveFloat;
}

AVCodecID GetAudioCodecID(DShow::AudioFormat format) {
	if (format == DShow::AudioFormat::AAC) {
		return AV_CODEC_ID_AAC;
	}
	else if (format == DShow::AudioFormat::AC3) {
		return AV_CODEC_ID_AC3;
	}
	else if (format == DShow::AudioFormat::MPGA) {
		return AV_CODEC_ID_MP3;
	}
	return AV_CODEC_ID_NONE;
}

std::string GetFFmpegErrorString(int errnum) {
	char errbuf[AV_ERROR_MAX_STRING_SIZE];
	av_strerror(errnum, errbuf, AV_ERROR_MAX_STRING_SIZE);
	return std::string(errbuf);
}

AVSampleFormat GetSampleFormat(DShow::AudioFormat format) {
	if (format == DShow::AudioFormat::Wave16bit) {
		return AVSampleFormat::AV_SAMPLE_FMT_S16;
	}
	else if (format == DShow::AudioFormat::WaveFloat) {
		return AVSampleFormat::AV_SAMPLE_FMT_FLT;
	}
	return AVSampleFormat::AV_SAMPLE_FMT_NONE;
}

int BytesPerSample(DShow::AudioFormat format) {
	if (format == DShow::AudioFormat::WaveFloat) {
		return 4;
	}
	return 2;
}

std::string GenerateGUID() {
	unsigned long data1;
	unsigned short data2;
	unsigned short data3;
	unsigned char data4[8];
	GUID guid;
	HRESULT hr = CoCreateGuid(&guid);
	if (hr == S_OK) {
		data1 = guid.Data1;
		data2 = guid.Data2;
		data3 = guid.Data3;
		memcpy(data4, guid.Data4, 8);
	}
	else {
		std::default_random_engine engine;
		std::uniform_int_distribution<unsigned long> d1;
		std::uniform_int_distribution<unsigned short> d2;
		std::uniform_int_distribution<unsigned int> d3(0, 255);
		data1 = d1(engine);
		data2 = d2(engine);
		data3 = d2(engine);
		for (int i = 0; i < 8; i++) {
			data4[i] = d3(engine);
		}
	}

	char data[64];
	snprintf(data, 64, "%08x-%04x-%04x-%02x%02x-%02x%02x%02x%02x%02x%02x", data1, data2, data3,
		data4[0], data4[1], data4[2], data4[3], data4[4], data4[5], data4[6], data4[7]);
	return std::string(data);
}

static std::map<std::string, Aws::TranscribeStreamingService::Model::LanguageCode> g_aws_language_map = {
	{"zh", Aws::TranscribeStreamingService::Model::LanguageCode::zh_CN},
	{"en", Aws::TranscribeStreamingService::Model::LanguageCode::en_US},
	{"ja", Aws::TranscribeStreamingService::Model::LanguageCode::ja_JP},
	{"ko", Aws::TranscribeStreamingService::Model::LanguageCode::ko_KR},
	{"fr", Aws::TranscribeStreamingService::Model::LanguageCode::fr_FR},
	{"es", Aws::TranscribeStreamingService::Model::LanguageCode::es_ES},
	{"it", Aws::TranscribeStreamingService::Model::LanguageCode::it_IT},
	{"de", Aws::TranscribeStreamingService::Model::LanguageCode::de_DE},
	{"ru", Aws::TranscribeStreamingService::Model::LanguageCode::ru_RU}
};

static std::map<std::string, std::string> g_language_map = {
	{"zh", "zh-CN"},
	{"en", "en-US"},
	{"ja", "ja-JP"},
	{"ko", "ko-KR"},
	{"fr", "fr-FR"},
	{"es", "es-ES"},
	{"it", "it-IT"},
	{"de", "de-DE"},
	{"ru", "ru-RU"}
};

Aws::TranscribeStreamingService::Model::LanguageCode GetLanguageCode(std::string& lang) {
	auto it = g_aws_language_map.find(lang);
	if (it != g_aws_language_map.end()) {
		return it->second;
	}
	return Aws::TranscribeStreamingService::Model::LanguageCode::NOT_SET;
}

std::string GetLanguageBCP47(std::string& lang) {
	auto it = g_language_map.find(lang);
	if (it != g_language_map.end()) {
		return it->second;
	}
	return "";
}

static std::map<std::string, std::string> g_default_voice_name = {
	{"zh", "zh-CN-YunjianNeural"},
	{"en", "en-US-DavisNeural"},
	{"ja", "ja-JP-KeitaNeural"},
	{"ko", "ko-KR-InJoonNeural"},
	{"fr", "fr-FR-HenriNeural"},
	{"es", "es-ES-AlvaroNeural"},
	{"it", "it-IT-DiegoNeural"},
	{"de", "de-DE-ConradNeural"},
	{"ru", "ru-RU-DmitryNeural"}
};

std::string GetVoiceName(std::string& lang) {
	auto it = g_default_voice_name.find(lang);
	if (it != g_default_voice_name.end()) {
		return it->second;
	}
	return "en-US-DavisNeural";
}