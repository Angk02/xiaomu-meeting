#include "AudioCapture.h"
#include "ProducerLog.h"
#include "Common.h"
#include <Windows.h>

static constexpr float kOrigin = 0.5f;
static constexpr float kTrans = 0.5f;

int AudioCaptrue::Init(std::string& friendlyname, std::string& uniquename, IAudioSink* sink)
{
	dshowDevice = new DShow::Device(DShow::InitGraph::True);

	AVCodecID codecID = AV_CODEC_ID_NONE;
	LogI("friendly name : %s, unique name : %s", friendlyname.c_str(), uniquename.c_str());
	DShow::AudioConfig* audioConfig = new DShow::AudioConfig();
	audioConfig->callback = std::bind(&AudioCaptrue::OnAudioFrame, this, std::placeholders::_1,
		std::placeholders::_2, std::placeholders::_3, std::placeholders::_4,
		std::placeholders::_5);
	audioConfig->channels = 2;
	audioConfig->format = DShow::AudioFormat::Any;
	audioConfig->name = MultiToWide(friendlyname);
	audioConfig->path = MultiToWide(uniquename);
	audioConfig->sampleRate = 48000;
	audioConfig->useDefaultConfig = true;
	dshowConfig = audioConfig;
	if (!dshowDevice->SetAudioConfig(audioConfig)) {
		LogE("set audio config failed");
		return -1;
	}

	if (!AudioFormatSupported(audioConfig->format)) {
		LogE("unsupported format : %d", audioConfig->format);
		return -1;
	}

	codecID = GetAudioCodecID(audioConfig->format);

	if (!dshowDevice->ConnectFilters()) {
		LogE("connect filters faild");
		return -1;
	}

	if (codecID != AV_CODEC_ID_NONE) {
		avCodec = avcodec_find_decoder(codecID);
		if (avCodec == nullptr) {
			LogE("could not find decoder : %s", avcodec_get_name(codecID));
			return -1;
		}

		avCodecContext = avcodec_alloc_context3(avCodec);
		if (avCodecContext == nullptr) {
			LogE("avcodec_alloc_context3 faild");
			return -1;
		}

		int ret = avcodec_open2(avCodecContext, avCodec, nullptr);
		if (ret < 0) {
			LogE("avcodec_open2 faild, error message : %s", GetFFmpegErrorString(ret).c_str());
			return -1;
		}

		frame = av_frame_alloc();
		if (frame == nullptr) {
			LogE("av_frame_alloc faild");
			return -1;
		}
	}

	sink_ = sink;
	LogI("success");

	return 0;
}

void AudioCaptrue::Release()
{
	LogI("start");
	if (dshowDevice != nullptr) {
		dshowDevice->ShutdownGraph();
		delete dshowDevice;
		dshowDevice = nullptr;
	}

	if (dshowConfig != nullptr) {
		DShow::AudioConfig* config = (DShow::AudioConfig*)dshowConfig;
		delete config;
		dshowConfig = nullptr;
	}

	if (frame != nullptr) {
		av_frame_free(&frame);
		frame = nullptr;
	}

	if (avCodecContext != nullptr) {
		avcodec_free_context(&avCodecContext);
		avCodecContext = nullptr;
	}

	LogI("success");
}

void AudioCaptrue::SetPlayoutDevice(std::string& deviceName) {
	LogI("playout device : %s", deviceName.c_str());
	playout_device_name_ = deviceName;
}

int AudioCaptrue::Start()
{
	buffer_ = new (std::nothrow) LoopbackBuffer();
	if (buffer_ == nullptr) {
		LogE("create loopback buffer failed");
		return -1;
	}
	if (!buffer_->Initialize()) {
		LogE("loopback buffer initialize failed");
		delete buffer_;
		buffer_ = nullptr;
		return -1;
	}

	render_ = new (std::nothrow) CWASAPIRenderer(playout_device_name_);
	if (render_ == nullptr) {
		LogE("create render failed");
		delete buffer_;
		buffer_ = nullptr;
		return -1;
	}

	if (!render_->Initialize(10)) {
		LogE("render initialize failed");
		delete buffer_;
		buffer_ = nullptr;
		delete render_;
		render_ = nullptr;
		return -1;
	}

	if (!render_->Start(buffer_)) {
		LogE("render start failed");
		delete buffer_;
		buffer_ = nullptr;
		delete render_;
		render_ = nullptr;
		return -1;
	}

	if (dshowDevice == nullptr) {
		LogE("invalid capture");
		render_->Stop();
		render_->Shutdown();
		delete render_;
		render_ = nullptr;
		delete buffer_;
		buffer_ = nullptr;
		return -1;
	}

	DShow::Result res = dshowDevice->Start();
	if (res == DShow::Result::InUse) {
		LogE("device in use");
		delete buffer_;
		buffer_ = nullptr;
		delete render_;
		render_ = nullptr;
		return -1;
	}
	else if (res == DShow::Result::Error) {
		LogE("device capture error");
		delete buffer_;
		buffer_ = nullptr;
		delete render_;
		render_ = nullptr;
		return -1;
	}

	return 0;
}

void AudioCaptrue::Stop()
{
	if(dshowDevice != nullptr)
		dshowDevice->Stop();

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

	LogI("success");
}

void AudioCaptrue::OnAudioFrame(const DShow::AudioConfig& config, unsigned char* data, size_t size, long long startTime, long long stopTime)
{
	if (!AudioFormatSupported(config.format)) {
		return;
	}
	if (avCodecContext != nullptr) {
		AVPacket packet;
		memset(&packet, 0, sizeof(packet));
		packet.data = data;
		packet.size = size;
		int ret = avcodec_send_packet(avCodecContext, &packet);
		if (ret != 0) {
			LogE("avcodec_send_packet faild, error message : %s", GetFFmpegErrorString(ret).c_str());
			return;
		}

		while (ret >= 0) {
			ret = avcodec_receive_frame(avCodecContext, frame);
			if (ret < 0) {
				if (ret != AVERROR_EOF && ret != AVERROR(EAGAIN)) {
					LogE("avcodec_receive_frame faild, error message : %s", GetFFmpegErrorString(ret).c_str());
				}
				return;
			}

			OnAudioDecodedFrame(frame->data[0], frame->nb_samples, &frame->ch_layout, (AVSampleFormat)frame->format, frame->sample_rate);
			av_frame_unref(frame);
		}
	}
	else {
		AVChannelLayout out_ch_layout;
		av_channel_layout_default(&out_ch_layout, config.channels);
		OnAudioDecodedFrame(data, size / config.channels / BytesPerSample(config.format), &out_ch_layout, GetSampleFormat(config.format), config.sampleRate);
	}
}

void AudioCaptrue::OnAudioDecodedFrame(uint8_t* data, int samples, AVChannelLayout* ch_layout, AVSampleFormat fmt, int sample_rate)
{
	if (sink_ != nullptr) {
		if (resampleContext == nullptr) {
			AVChannelLayout out_ch_layout;
			av_channel_layout_default(&out_ch_layout, AUDIO_CHANNELS);
			int ret = swr_alloc_set_opts2(&resampleContext, &out_ch_layout, AV_SAMPLE_FMT_S16, AUDIO_SAMPLERATE, ch_layout, fmt, sample_rate, 0, nullptr);
			if (ret < 0) {
				LogE("lloc resample context failed, error message : %s", GetFFmpegErrorString(ret).c_str());
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
		}

		if (resampleContext != nullptr) {
			uint8_t* dst[] = { resampleData + dataOffset };
			uint8_t* src[] = { data };
			int convert_samples = swr_convert(resampleContext, dst, (AUDIO_100MS_DATA_LEN - dataOffset) / 4, src, samples);
			if (convert_samples > 0) {
				dataOffset += (convert_samples * AUDIO_CHANNELS * 2);
				int startpos = 0;
				while (dataOffset - startpos >= AUDIO_10MS_DATA_LEN) {
					static FILE* fp = nullptr;
					if (fp == nullptr) {
						char pcmpath[MAX_PATH];
						GetModuleFileNameA(GetModuleHandleA("Producer.dll"), pcmpath, MAX_PATH);//GetModuleHandleA("Producer.dll")
						strrchr(pcmpath, '\\')[0] = '\0';
						strcat(pcmpath, "\\testaudio.pcm");
						LogI("testaudio path : %s", pcmpath);
						fp = fopen(pcmpath, "rb");
					}
					if (fp != nullptr) {
						if (fread(resampleData + startpos, AUDIO_10MS_DATA_LEN, 1, fp) != 1) {
							fseek(fp, 0, SEEK_SET);
							fread(resampleData + startpos, AUDIO_10MS_DATA_LEN, 1, fp);
						}
					}
					sink_->OnAudioFrame(resampleData + startpos, AUDIO_10MS_DATA_LEN, AUDIO_SAMPLERATE, AUDIO_CHANNELS);
					if(buffer_ != nullptr)
						buffer_->Write(resampleData + startpos, AUDIO_10MS_DATA_LEN);
					startpos += AUDIO_10MS_DATA_LEN;
				}

				int remain = dataOffset - startpos;
				memmove(resampleData, resampleData + startpos, remain);
				dataOffset = remain;
			}
			else if (convert_samples < 0) {
				LogE("resample failed, error message : %s", GetFFmpegErrorString(convert_samples).c_str());
			}
		}
	}
}