// Test.cpp : 此文件包含 "main" 函数。程序执行将在此处开始并结束。
//

#include "Producer.h"
#include <iostream>
#include <Windows.h>

static char GetLogLevel(LogLevel level) {
    switch (level) {
    case LogLevel::LOG_LEVEL_DEBUG:
        return 'D';
    case LogLevel::LOG_LEVEL_INFO:
        return 'I';
    case LogLevel::LOG_LEVEL_WARNING:
        return 'D';
    case LogLevel::LOG_LEVEL_ERROR:
        return 'E';
    }
    return '-';
}

static FILE* g_flog = nullptr;

static void LogSink(LogLevel leve, const char* msg, void* context) {
    char logtime[30];
    SYSTEMTIME lpsystime;
    GetLocalTime(&lpsystime);
    sprintf(logtime, "%u:%02u:%02u %02u:%02u:%02u:%03u", lpsystime.wYear, lpsystime.wMonth, lpsystime.wDay, lpsystime.wHour, lpsystime.wMinute, lpsystime.wSecond, lpsystime.wMilliseconds);
    logtime[strlen(logtime)] = '\0';
    DWORD tid = GetCurrentThreadId();
    printf("[%s] [%c] tid[% 5lu]", logtime, GetLogLevel(leve), tid);
    printf(" %s\n", msg);
    if (g_flog != nullptr) {
        fprintf(g_flog, "[%s] [%c] tid[% 5lu]", logtime, GetLogLevel(leve), tid);
        fprintf(g_flog, " %s\n", msg);
        fflush(g_flog);
    }
}

static std::string ANSIToUTF8(std::string& ansi) {
    int nUnicodeSize = MultiByteToWideChar(CP_ACP, 0, ansi.c_str(), ansi.length(), nullptr, 0);
    std::wstring unicode(nUnicodeSize + 1, 0);
    MultiByteToWideChar(CP_ACP, 0, ansi.c_str(), ansi.length(), (LPWSTR)unicode.c_str(), unicode.length());

    int nUtf8Size = WideCharToMultiByte(CP_UTF8, 0, unicode.c_str(), unicode.length(), nullptr, 0, nullptr, FALSE);
    std::string utf8(nUtf8Size + 1, 0);
    WideCharToMultiByte(CP_UTF8, 0, unicode.c_str(), unicode.length(), (char*)utf8.c_str(), utf8.length(), nullptr, FALSE);
    return utf8;
}

static std::string WideToMulti(const std::wstring input) {
    if (input.empty()) return std::string();
    int size_needed = WideCharToMultiByte(CP_UTF8, 0, &input[0], (int)input.size(), NULL, 0, NULL, NULL);
    std::string multi(size_needed, 0);
    WideCharToMultiByte(CP_UTF8, 0, &input[0], (int)input.size(), &multi[0], size_needed, NULL, NULL);
    return multi;
}

int main(){
    char logpath[MAX_PATH];
    GetModuleFileNameA(NULL, logpath, MAX_PATH);
    strrchr(logpath, '\\')[0] = '\0';
    strcat(logpath, "\\Test.log");
    g_flog = fopen(logpath, "wb");

    SetConsoleOutputCP(CP_UTF8);
    CoInitializeEx(nullptr, 0);

    std::vector<std::string> captureDevices = GetAudioCaptureDevices();
    if (captureDevices.empty()) {
        printf("no audio capture device\n");
        system("pause");
        exit(1);
    }
    for (auto& e : captureDevices) {
        printf("[AudioCapture] name : %s\n", e.c_str());
    }

    std::vector<std::string> playoutDevices = GetAudioPlayoutDevices();
    for (auto& e : playoutDevices) {
        printf("[AudioPlayout] name : %s\n", e.c_str());
    }

    SetProducerLogSink(LogSink, nullptr);

    IProducer* producer = CreateProducer();
    if (producer == nullptr) {
        LogSink(LogLevel::LOG_LEVEL_ERROR, "create producer failed", nullptr);
        return -1;
    }

    std::string playoutdevice = "CABLE Input (VB-Audio Virtual Cable)";
    std::string capturedevice = captureDevices[0];//WideToMulti(audio_devices[0].name);
    if (producer->Start(capturedevice, playoutdevice, DEFAULT, nullptr) != 0) {
        LogSink(LogLevel::LOG_LEVEL_ERROR, "producer start failed", nullptr);
        return -1;
    }

    getchar();

    delete producer;
    return 0;
}
