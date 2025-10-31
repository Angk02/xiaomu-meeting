#include "ProducerLog.h"
#include "Producer.h"
#include "Common.h"
#include <stdarg.h>
#include <stdio.h>
#include <string>

static ProducerLogSink g_ProducerLogSink = nullptr;
static void* g_log_context = nullptr;

void SetProducerLogSink(ProducerLogSink sink, void* context) {
	g_ProducerLogSink = sink;
	g_log_context = context;
}

void LogMessage(const char* fmt, ...) {
	va_list vl;
	va_start(vl, fmt);
	int len = vsnprintf(nullptr, 0, fmt, vl);
	std::string msg;
	msg.resize(len + 1);
	vsnprintf((char*)msg.data(), msg.length(), fmt, vl);
	va_end(vl);
	if (g_ProducerLogSink != nullptr) {
		g_ProducerLogSink(LogLevel::LOG_LEVEL_INFO, msg.c_str(), g_log_context);
	}
	else {
		printf("%s\n", msg.c_str());
	}
}

void LogError(const char* fmt, ...) {
	va_list vl;
	va_start(vl, fmt);
	int len = vsnprintf(nullptr, 0, fmt, vl);
	std::string msg;
	msg.resize(len + 1);
	vsnprintf((char*)msg.data(), msg.length(), fmt, vl);
	va_end(vl);
	if (g_ProducerLogSink != nullptr) {
		g_ProducerLogSink(LogLevel::LOG_LEVEL_ERROR, msg.c_str(), g_log_context);
	}
	else {
		printf("%s\n", msg.c_str());
	}
}

void LogWarning(const char* fmt, ...) {
	va_list vl;
	va_start(vl, fmt);
	int len = vsnprintf(nullptr, 0, fmt, vl);
	std::string msg;
	msg.resize(len + 1);
	vsnprintf((char*)msg.data(), msg.length(), fmt, vl);
	va_end(vl);
	if (g_ProducerLogSink != nullptr) {
		g_ProducerLogSink(LogLevel::LOG_LEVEL_WARNING, msg.c_str(), g_log_context);
	}
	else {
		printf("%s\n", msg.c_str());
	}
}

void LogDebug(const char* fmt, ...) {
	va_list vl;
	va_start(vl, fmt);
	int len = vsnprintf(nullptr, 0, fmt, vl);
	std::string msg;
	msg.resize(len + 1);
	vsnprintf((char*)msg.data(), msg.length(), fmt, vl);
	va_end(vl);
	if (g_ProducerLogSink != nullptr) {
		g_ProducerLogSink(LogLevel::LOG_LEVEL_DEBUG, msg.c_str(), g_log_context);
	}
	else {
		printf("%s\n", msg.c_str());
	}
}

void DshowLogCallback(DShow::LogType type, const wchar_t* msg, void* param) {
	std::string msg_multi = WideToMulti(msg);
	if (type == DShow::LogType::Debug) {
		LogDebug("%s", msg_multi.c_str());
	}
	else if (type == DShow::LogType::Info) {
		LogMessage("%s", msg_multi.c_str());
	}
	else if (type == DShow::LogType::Warning) {
		LogWarning("%s", msg_multi.c_str());
	}
	else if (type == DShow::LogType::Error) {
		LogError("%s", msg_multi.c_str());
	}
}