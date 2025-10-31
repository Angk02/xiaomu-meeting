#include "ALIRecognition.h"
#include "ProducerLog.h"
#include <functional>
#include <websocketpp/frame.hpp>
#include <nlohmann/json.hpp>
#include "Common.h"

#define USE_paraformer 0

int ALIRecognition::Start(ConfigBase* config, IRecCallback* callback, std::string& sourceLanguage) {
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
    source_language_ = sourceLanguage;
    LogI("source language : %s", source_language_.c_str());
	taskid_ = GenerateGUID();
	LogI("taskid : %s", taskid_.c_str());

	Config* cfg = (Config*)config;
	apikey_ = cfg->apikey;
	websocket_thread_ = std::thread(std::bind(&ALIRecognition::websocket_runloop, this));
	LogI("success");
	return 0;
}

void ALIRecognition::Stop() {
    LogI("begin");

    do {
        std::unique_lock<std::mutex> lock(mutex_);
        run_ = false;
        condition_.notify_one();
    } while (0);

    if (worker_thread_.joinable()) {
        LogI("wait for worker thread exit");
        worker_thread_.join();
        LogI("worker thread exit success");
    }

    client_.close(connection_hdl_, websocketpp::close::status::normal, "task finish");
    if (websocket_thread_.joinable()) {
        LogI("wait for websocket thread exit");
        websocket_thread_.join();
        LogI("websocket thread exit success");
    }

    LogI("success");
}

int ALIRecognition::Process(const uint8_t* audio, const size_t len, int samplerate, int channels) {
    if (len / 2 / channels != AUDIO_10MS_SAMPLES_PER_CHANNEL) {
        LogE("invalid frame size");
        return 0;
    }

    std::unique_lock<std::mutex> lock(mutex_);
    if (current_audio_len_ == audio_data_len_) {
        memmove(&audio_data_[0], &audio_data_[AUDIO_10MS_DATA_LEN], current_audio_len_ - AUDIO_10MS_DATA_LEN);
        current_audio_len_ -= AUDIO_10MS_DATA_LEN;
    }

    memcpy(&audio_data_[current_audio_len_], audio, AUDIO_10MS_DATA_LEN);
    current_audio_len_ += AUDIO_10MS_DATA_LEN;
    if (current_audio_len_ == audio_data_len_) {
        condition_.notify_one();
    }
    return 0;
}

void ALIRecognition::worker() {
    LogI("enter");

    bool first = false;

    while (run_) {
        std::unique_lock<std::mutex> lock(mutex_);
        if (run_ && current_audio_len_ != audio_data_len_) {
            condition_.wait(lock);
        }

        if (!run_) {
            LogI("worker thread finish");
            break;
        }

        if (current_audio_len_ != audio_data_len_) {
            continue;
        }

        std::vector<short> data(audio_data_len_ / 4, 0);
        int j = 0;
        short* src = (short*)(&audio_data_[0]);
        for (int i = 0; i < audio_data_len_ / 2; i += 2) {
            data[j++] = src[i];
        }
        std::error_code err;
        client_.send(connection_hdl_, (const void*)data.data(), data.size(), websocketpp::frame::opcode::BINARY, err);
        if (err.value() != 0) {
            LogE("send audio failed, error code : %d, error message : %s", err.value(), err.message().c_str());
        }
        //LogI("send audio result : %d, %s", err.value(), err.message().c_str());
        current_audio_len_ = 0;
    }

    LogI("exit");
}

void ALIRecognition::websocket_runloop() {
    LogI("enter");
    std::string uri = "wss://dashscope.aliyuncs.com/api-ws/v1/inference";
    try {
        // Set logging to be pretty verbose (everything except message payloads)
        client_.set_access_channels(websocketpp::log::alevel::none);
        client_.clear_access_channels(websocketpp::log::alevel::none);
        client_.set_error_channels(websocketpp::log::elevel::info);

        // Initialize ASIO
        client_.init_asio();

        // Register our message handler
        client_.set_message_handler(websocketpp::lib::bind(&ALIRecognition::on_message, this, websocketpp::lib::placeholders::_1, websocketpp::lib::placeholders::_2));
        client_.set_open_handler(websocketpp::lib::bind(&ALIRecognition::on_open, this, websocketpp::lib::placeholders::_1));
        client_.set_close_handler(websocketpp::lib::bind(&ALIRecognition::on_close, this, websocketpp::lib::placeholders::_1));
        client_.set_fail_handler(websocketpp::lib::bind(&ALIRecognition::on_fail, this, websocketpp::lib::placeholders::_1));
        client_.set_tls_init_handler([this](websocketpp::connection_hdl) {
            auto ctx = websocketpp::lib::make_shared<::asio::ssl::context>(::asio::ssl::context::tlsv12);
            try {
                ctx->set_options(::asio::ssl::context::default_workarounds |
                    ::asio::ssl::context::no_sslv2 |
                    ::asio::ssl::context::no_sslv3 |
                    ::asio::ssl::context::single_dh_use);


                ctx->set_verify_mode(::asio::ssl::verify_none);
            }
            catch (std::exception& e) {
                LogE("set_tls_init_handler exception : %s", e.what());
            }
            return ctx;
            });

        websocketpp::lib::error_code ec;
        websocketpp::client<websocketpp::config::asio_tls_client>::connection_ptr con = client_.get_connection(uri, ec);
        if (ec) {
            LogE("could not create connection, because : %s", ec.message().c_str());
            if (callback_ != nullptr) {
                callback_->OnASRStatus(this, STATUS_ERROR);
            }
            return;
        }
        con->append_header("Authorization", apikey_);
        con->append_header("X-DashScope-DataInspection", "enable");

        // Note that connect here only requests a connection. No network messages are
        // exchanged until the event loop starts running in the next line.
        client_.connect(con);

        client_.get_alog().write(websocketpp::log::alevel::app, "Connecting to " + uri);

        // Start the ASIO io_service run loop
        // this will cause a single connection to be made to the server. c.run()
        // will exit when this connection is closed.
        client_.run();
    }
    catch (websocketpp::exception const& e) {
        LogE("connect to url : %s exception, message : %s", uri.c_str(), e.what());
        if (callback_ != nullptr) {
            callback_->OnASRStatus(this, STATUS_ERROR);
        }
    }
    LogI("exit");
}

std::string ALIRecognition::generate_run_command() {
    nlohmann::json run_command;

    nlohmann::json header;
    header["streaming"] = "duplex";
    header["task_id"] = taskid_;
    header["action"] = "run-task";
    run_command["header"] = header;

    nlohmann::json payload;
#if USE_paraformer
    payload["model"] = "paraformer-realtime-v2";
    nlohmann::json parameters;
    parameters["sample_rate"] = AUDIO_SAMPLERATE;
    parameters["format"] = "pcm";
    parameters["source_language"] = source_language_;
    //parameters["semantic_punctuation_enabled"] = true;
    //parameters["max_sentence_silence"] = 300;
    parameters["multi_threshold_mode_enabled"] = true;
    parameters["heartbeat"] = true;
    payload["parameters"] = parameters;
    payload["input"] = nlohmann::json::object();
    payload["task"] = "asr";
    payload["task_group"] = "audio";
    payload["function"] = "recognition";
#else
    payload["model"] = "gummy-realtime-v1";
    nlohmann::json parameters;
    parameters["sample_rate"] = AUDIO_SAMPLERATE;
    parameters["format"] = "pcm";
    parameters["source_language"] = source_language_;
    parameters["transcription_enabled"] = true;
    parameters["translation_enabled"] = false;
    //parameters["translation_target_languages"] = { "en" };
    payload["parameters"] = parameters;
    payload["input"] = nlohmann::json::object();
    payload["task"] = "asr";
    payload["task_group"] = "audio";
    payload["function"] = "recognition";
#endif
    run_command["payload"] = payload;

    return run_command.dump();
}

void ALIRecognition::on_message(websocketpp::connection_hdl, websocketpp::client<websocketpp::config::asio_tls_client>::message_ptr msg) {
    std::string message = msg->get_payload();
    //LogI("%s", message.c_str());
    nlohmann::json response = nlohmann::json::parse(message);
    std::string type = response["header"]["event"];
    if (type == "task-started") {
        LogI("task started");
        if (callback_ != nullptr) {
            callback_->OnASRStatus(this, STATUS_STARTED);
        }

        run_ = true;
        worker_thread_ = std::thread(std::bind(&ALIRecognition::worker, this));
    }
    else if (type == "task-finished") {
        LogI("task finished");
    }
    else if (type == "task-failed") {
        LogE("task failed");
        if (callback_ != nullptr) {
            callback_->OnASRStatus(this, STATUS_ERROR);
        }
    }
    else if (type == "result-generated") {
        nlohmann::json output = response["payload"]["output"];
#if USE_paraformer
        if (output.find("sentence") != output.end() && output["sentence"].is_object()) {
            nlohmann::json sentence = output["sentence"];
            bool sentence_end = false;
            bool end_time = false;

            if (sentence.find("end_time") != sentence.end() && sentence["end_time"].is_number_integer()) {
                end_time = true;
            }

            if (sentence.find("sentence_end") != sentence.end() && sentence["sentence_end"].is_boolean()) {
                sentence_end = sentence["sentence_end"];
            }

            if (sentence_end || end_time) {
                std::string text = sentence["text"];
                if (callback_ != nullptr) {
                    callback_->OnASRResult(this, text);
                }
            }
        }
#else
        auto it = output.find("transcription");
        if (it != output.end()) {
            bool sentence_end = output["transcription"]["sentence_end"];
            if (sentence_end) {
                std::string text = output["transcription"]["text"];
                if (callback_ != nullptr) {
                    callback_->OnASRResult(this, text);
                }
            }
        }
#endif
    }
}

void ALIRecognition::on_open(websocketpp::connection_hdl conn) {
    LogI("websocket open success");
    connection_hdl_ = conn;
    if (callback_ != nullptr) {
        callback_->OnASRStatus(this, STATUS_OPEN);
    }

    std::string run_command = generate_run_command();
    LogI("run command : %s", run_command.c_str());
    client_.send(conn, run_command, websocketpp::frame::opcode::TEXT);
}

void ALIRecognition::on_close(websocketpp::connection_hdl) {
    LogI("");
    if (callback_ != nullptr) {
        callback_->OnASRStatus(this, STATUS_CLOSE);
    }
}

void ALIRecognition::on_fail(websocketpp::connection_hdl) {
    LogI("");
    if (callback_ != nullptr) {
        callback_->OnASRStatus(this, STATUS_ERROR);
    }
}
