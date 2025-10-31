#pragma once

#include "IRecognition.h"
#include <WinSock2.h>
#include <thread>
#include "Common.h"
#include <vector>
#include <mutex>
#include <condition_variable>
#include <websocketpp/config/asio_client.hpp>
#include <websocketpp/client.hpp>

class ALIRecognition : public IRecognition {
public:
	struct Config : public ConfigBase{
		std::string apikey;
	};

	~ALIRecognition() {}
	// 通过 IRecognition 继承
	virtual int Start(ConfigBase* config, IRecCallback* callback, std::string& sourceLanguage) override;
	virtual void Stop() override;
	virtual int Process(const uint8_t* audio, const size_t len, int samplerate, int channels) override;

private:
	void worker();
	void websocket_runloop();
	std::string generate_run_command();
	void on_message(websocketpp::connection_hdl, websocketpp::client<websocketpp::config::asio_tls_client>::message_ptr msg);
	void on_open(websocketpp::connection_hdl);
	void on_close(websocketpp::connection_hdl);
	void on_fail(websocketpp::connection_hdl);

private:
	IRecCallback* callback_ = nullptr;
	std::string apikey_ = "";
	std::thread worker_thread_;
	bool run_ = false;
	int current_audio_len_ = 0;
	std::mutex mutex_;
	std::condition_variable condition_;
	std::string taskid_ = "";
	std::string source_language_ = "zh";
	char audio_data_[AUDIO_10MS_DATA_LEN * 10];
	int audio_data_len_ = AUDIO_10MS_DATA_LEN * 10;
	websocketpp::client<websocketpp::config::asio_tls_client> client_;
	std::thread websocket_thread_;
	websocketpp::connection_hdl connection_hdl_;
};