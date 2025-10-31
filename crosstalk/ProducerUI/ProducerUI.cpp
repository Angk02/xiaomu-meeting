#include "Producer.h"

#include <nana/gui.hpp>
#include <nana/gui/widgets/label.hpp>
#include <nana/gui/widgets/combox.hpp>
#include <nana/gui/widgets/button.hpp>
#include <nana/gui/widgets/textbox.hpp>
#include <nana/gui/filebox.hpp>
#include <nana/gui/widgets/picture.hpp>
#include <fstream>
#include <memory>
using namespace nana;
#include <Windows.h>
#include <thread>
#include <mutex>
#include <set>
#include <map>

#pragma comment(lib, "libpng16.lib")
#pragma comment(lib, "nana_v143_Release_x64.lib")

class circle_button : public nana::picture
{
public:
    circle_button(nana::window wd, const nana::rectangle& r, const std::string& img_path)
        : nana::picture(wd, r), radius_(r.width / 2)
    {
        load(nana::paint::image(img_path));
        transparent(true);

        this->events().click([this](const nana::arg_click& arg) {
            if (!arg.mouse_args) return;

            auto pos = arg.mouse_args->pos;
            int dx = pos.x - radius_;
            int dy = pos.y - radius_;
            if (dx * dx + dy * dy <= radius_ * radius_)
            {
                if (on_click_) on_click_();
            }
        });
    }

    void set_image(const std::string& img_path) {
        load(nana::paint::image(img_path));
    }

    void set_on_click(std::function<void()> cb)
    {
        on_click_ = std::move(cb);
    }

private:
    int radius_;
    std::function<void()> on_click_;
};

struct Language {
    std::string name;
    std::string abbrev;
};

bool operator<(const Language& lhs, const Language& rhs) {
    if (lhs.name != rhs.name)
        return lhs.name < rhs.name;
    return lhs.abbrev < rhs.abbrev;
}

std::map<std::string, std::string> g_language_map = {
    {"Chinese", "zh"},
    {"English", "en"},
    {"Japanese", "ja"},
    {"Korean", "ko"},
    {"French", "fr"},
    {"Spanish", "es"},
    {"Italian", "it"},
    {"German", "de"},
    {"Russian", "ru"}
};

static std::map<Language, std::vector<Language>> g_default_language = {
    {{"Chinese", "zh"}, {
        {"English", "en"}, {"Japanese", "ja"}, {"Korean", "ko"},
        {"French", "fr"}, {"Spanish", "es"}, {"Italian", "it"},
        {"German", "de"}, {"Russian", "ru"}
    }},
    {{"English", "en"}, {
        {"Chinese", "zh"}, {"Japanese", "ja"}, {"Korean", "ko"},
        {"French", "fr"}, {"Spanish", "es"}, {"Italian", "it"},
        {"German", "de"}, {"Russian", "ru"}
    }},
    {{"Japanese", "ja"}, {
        {"Chinese", "zh"}, {"English", "en"}, {"Korean", "ko"}
    }},
    {{"Korean", "ko"}, {
        {"Chinese", "zh"}, {"English", "en"}, {"Japanese", "ja"}
    }},
    {{"French", "fr"}, {
        {"Chinese", "zh"}, {"English", "en"}, {"Spanish", "es"},
        {"Italian", "it"}, {"German", "de"}, {"Russian", "ru"}
    }},
    {{"Spanish", "es"}, {
        {"Chinese", "zh"}, {"English", "en"}, {"French", "fr"},
        {"Italian", "it"}, {"German", "de"}, {"Russian", "ru"}
    }},
    {{"Italian", "it"}, {
        {"Chinese", "zh"}, {"English", "en"}, {"French", "fr"},
        {"Spanish", "es"}, {"German", "de"}, {"Russian", "ru"}
    }},
    {{"German", "de"}, {
        {"Chinese", "zh"}, {"English", "en"}, {"French", "fr"},
        {"Spanish", "es"}, {"Italian", "it"}, {"Russian", "ru"}
    }},
    {{"Russian", "ru"}, {
        {"Chinese", "zh"}, {"English", "en"},{"French", "fr"}, 
        {"Spanish", "es"}, {"Italian", "it"},{"German", "de"}
    }}
};

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

class MainProducer : public IProducer::ICallback {
public:
    MainProducer(std::string& icon_folder) :
    mainWindow_(API::make_center(360, 660), nana::appearance{true, true, false, false, true, false, false}),
    captureComb_(mainWindow_, rectangle{ 130, 20, 200, 25 }),
    playoutComb_(mainWindow_, rectangle{ 130, 60, 200, 25 }),
    startBtn_(mainWindow_, rectangle{ 152, 180, 55, 55 }, icon_folder + "\\start.png"),
    transcripts_(mainWindow_, rectangle{ 20, 240, 100, 25 }),
    clearBtn_(mainWindow_, rectangle{147, 240, 22, 22}, icon_folder + "\\clear.png"),
    saveBtn_(mainWindow_, rectangle{ 120, 240, 22, 22 }, icon_folder + "\\download.png"),
    output_(mainWindow_, rectangle{ 20, 270, 320, 380 }),
    captureLabel_(mainWindow_, rectangle{20, 20, 80, 25}),
    playoutLabel_(mainWindow_, rectangle{20, 60, 120, 25}),
    inputLanguageLabel_(mainWindow_, rectangle{20, 100, 140, 25}),
    inputLanguageComb_(mainWindow_, rectangle{130, 100, 200, 25}),
    outputLanguageLabel_(mainWindow_, rectangle{20, 140, 140, 25}),
    outputLanguageComb_(mainWindow_, rectangle{130, 140, 200, 25}),
    icon_folder_(icon_folder){
        mainWindow_.caption("CrossTalk");
        mainWindow_.bgcolor(colors::white);
        output_.multi_lines(true);
        output_.editable(false);
        output_.line_wrapped(false);
        clearBtn_.caption("clear");
        clearBtn_.bgcolor(colors::white);
        saveBtn_.caption("save");
        saveBtn_.bgcolor(colors::white);
        captureLabel_.caption("Input Device:");
        playoutLabel_.caption("Virtual Speaker:");
        captureLabel_.bgcolor(colors::white);
        playoutLabel_.bgcolor(colors::white);
        captureComb_.bgcolor(colors::white);
        playoutComb_.bgcolor(colors::white);
        inputLanguageLabel_.caption("Input Language:");
        inputLanguageLabel_.bgcolor(colors::white);
        outputLanguageLabel_.caption("Output Language:");
        outputLanguageLabel_.bgcolor(colors::white);
        transcripts_.caption("Transcripts");
        transcripts_.bgcolor(colors::white);
        nana::paint::font bold_font("微软雅黑", 14, { true }); // 第三个参数：true = 粗体
        transcripts_.typeface(bold_font);

        inputLanguageComb_.events().selected([this]() {
            auto idx = inputLanguageComb_.option();
            if (idx != nana::npos) {
                auto l = inputLanguageComb_.text(idx);
                selectedSourceLanguage_ = g_language_map[l];
                Language src = { l, g_language_map[l] };
                auto targets = g_default_language[src];
                outputLanguageComb_.clear();
                for (auto& t : targets) {
                    outputLanguageComb_.push_back(t.name);
                }
                outputLanguageComb_.option(0);
            }
            else {
                selectedSourceLanguage_ = "";
            }
        });

        outputLanguageComb_.events().selected([this]() {
            auto idx = outputLanguageComb_.option();
            if (idx != nana::npos) {
                selectedTargetLanguage_ = g_language_map[outputLanguageComb_.text(idx)];
            }
            else {
                selectedTargetLanguage_ = "";
            }
        });

        inputLanguageComb_.clear();
        for (auto& item : g_default_language) {
            inputLanguageComb_.push_back(item.first.name);
        }
        inputLanguageComb_.option(0);
    }

    ~MainProducer() {
        if (update_.joinable()) {
            update_.join();
        }
    }

    virtual void OnText(std::string& original, std::string& translation) override {
        std::string text = original + "\n" + translation + "\n\n";
        output_.append(text, false);
    }

    void Exec() {
        char exe_path[MAX_PATH];
        GetModuleFileNameA(NULL, exe_path, MAX_PATH);
        strrchr(exe_path, '\\')[0] = '\0';
        std::string resource_folder = exe_path;
        resource_folder = resource_folder + "\\" + "resources\\";
        std::string big_image_path = resource_folder + "256x256.ico";
        std::string small_image_path = resource_folder + "128x128.ico";
        paint::image big_image(big_image_path);
        paint::image small_image(small_image_path);
        
        API::window_icon(mainWindow_.handle(), small_image, big_image);

        run_ = true;
        std::thread([this]() {
            while (run_) {
                std::vector<std::string> captureDevices = GetAudioCaptureDevices();
                //for (auto& e : captureDevices) {
                //    std::string msg = "[AudioCapture] name : " + e;
                //    LogSink(LogLevel::LOG_LEVEL_INFO, msg.c_str(), nullptr);
                //}

                bool change = false;
                if (captureDevices.size() != captureComb_.the_number_of_options()) {
                    change = true;
                }
                else {
                    std::set<std::string> a;
                    std::set<std::string> b;

                    for (auto& e : captureDevices) {
                        a.insert(e);
                    }

                    for (std::size_t i = 0; i < captureComb_.the_number_of_options(); ++i) {
                        b.insert(captureComb_.text(i));
                    }

                    for (auto it1 = a.begin(), it2 = b.begin(); it1 != a.end(); it1++, it2++) {
                        if (*it1 != *it2) {
                            change = true;
                            break;
                        }
                    }
                }
                if (change) {
                    captureComb_.clear();
                    for (auto& e : captureDevices) {
                        captureComb_.push_back(e);
                    }
                }

                int index = -1;
                for (std::size_t i = 0; i < captureComb_.the_number_of_options(); ++i) {
                    std::string text = captureComb_.text(i);
                    if (selectedCaptureDevice == text) {
                        index = i;
                        break;
                    }
                }

                if (index == -1 && !captureComb_.empty()) {
                    index = 0;
                }

                if (index >= 0) {
                    captureComb_.option(index);
                }

                std::vector<std::string> playoutDevices = GetAudioPlayoutDevices();
                //for (auto& e : playoutDevices) {
                //    std::string msg = "[AudioPlayout] name : " + e;
                //    LogSink(LogLevel::LOG_LEVEL_INFO, msg.c_str(), nullptr);
                //}

                change = false;
                if (playoutDevices.size() != playoutComb_.the_number_of_options()) {
                    change = true;
                }
                else {
                    std::set<std::string> a;
                    std::set<std::string> b;

                    for (auto& e : playoutDevices) {
                        a.insert(e);
                    }

                    for (std::size_t i = 0; i < playoutComb_.the_number_of_options(); ++i) {
                        b.insert(playoutComb_.text(i));
                    }

                    for (auto it1 = a.begin(), it2 = b.begin(); it1 != a.end(); it1++, it2++) {
                        if (*it1 != *it2) {
                            change = true;
                            break;
                        }
                    }
                }
                if (change) {
                    playoutComb_.clear();
                    for (auto& e : playoutDevices) {
                        playoutComb_.push_back(e);
                    }
                }

                index = -1;
                for (std::size_t i = 0; i < playoutComb_.the_number_of_options(); ++i) {
                    std::string text = playoutComb_.text(i);
                    if (selectedPlayoutDevice == text) {
                        index = i;
                        break;
                    }
                }
                if (index == -1 && !playoutComb_.empty()) {
                    index = 0;
                }

                if (index >= 0) {
                    playoutComb_.option(index);
                }
                
                int count = 100;
                while (run_ && count > 0) {
                    Sleep(10);
                    count--;
                }
            }
        }).detach();

        captureComb_.events().selected([this]() {
            auto idx = captureComb_.option();
            if (idx != nana::npos) {
                selectedCaptureDevice = captureComb_.text(idx);
            }
            else {
                selectedCaptureDevice = "";
            }
        });

        playoutComb_.events().selected([this]() {
            auto idx = playoutComb_.option();
            if (idx != nana::npos) {
                selectedPlayoutDevice = playoutComb_.text(idx);
            }
            else {
                selectedPlayoutDevice = "";
            }
        });

        clearBtn_.set_on_click([this]() {
            output_.reset();
        });

        saveBtn_.set_on_click([this]() {
            // 创建保存文件对话框
            filebox saver(mainWindow_, true);  // true 表示保存对话框
            saver.title("save");
            saver.add_filter("Text", "*.txt");
            saver.add_filter("All Files", "*.*");
            saver.init_file("result.txt");
            
            if (saver.show()) {
                auto filepath = saver.file();
                std::string text = output_.caption();
                if (!filepath.empty()) {
                    std::ofstream ofs(filepath);
                    if (ofs) {
                        ofs << text;
                        ofs.close();
                        msgbox mb(mainWindow_, "Info");
                        mb << "save file success";
                        mb.show();
                    }
                }
            }
        });

        startBtn_.set_on_click([this]() {
            if (!started_) {
                if (producer_ != nullptr) {
                    delete producer_;
                    producer_ = nullptr;
                }

                size_t index = playoutComb_.option();
                std::string playoutdevice = index == nana::npos ? "" : playoutComb_.text(index);
                index = captureComb_.option();
                std::string capturedevice = index == nana::npos ? "" : captureComb_.text(index);

                if (playoutdevice.empty()) {
                    LogSink(LogLevel::LOG_LEVEL_ERROR, "invalid playout device", nullptr);
                    return;
                }

                if (capturedevice.empty()) {
                    LogSink(LogLevel::LOG_LEVEL_ERROR, "invalid capture device", nullptr);
                    return;
                }

                producer_ = CreateProducer();
                if (producer_ == nullptr) {
                    LogSink(LogLevel::LOG_LEVEL_ERROR, "create producer failed", nullptr);
                    return;
                }

                if (producer_->Start(capturedevice, playoutdevice, DEFAULT, this, selectedSourceLanguage_, selectedTargetLanguage_) != 0) {
                    LogSink(LogLevel::LOG_LEVEL_ERROR, "producer start failed", nullptr);
                    delete producer_;
                    return;
                }

                started_ = true;
                startBtn_.set_image(icon_folder_ + "\\stop.png");
            }
            else {
                if (producer_ != nullptr) {
                    producer_->Stop();
                    delete producer_;
                    producer_ = nullptr;
                }

                started_ = false;
                startBtn_.set_image(icon_folder_ + "\\start.png");
            }
        });

        mainWindow_.events().unload([this] {
            run_ = false;
            if (update_.joinable()) {
                update_.join();
            }
        });

        mainWindow_.show();
        exec();
    }

private:
    form mainWindow_;
    label captureLabel_;
    combox captureComb_;
    label playoutLabel_;
    combox playoutComb_;
    circle_button startBtn_;
    circle_button clearBtn_;
    circle_button saveBtn_;
    textbox output_;
    label inputLanguageLabel_;
    label transcripts_;
    combox inputLanguageComb_;
    label outputLanguageLabel_;
    combox outputLanguageComb_;
    std::thread update_;
    std::string selectedCaptureDevice = "";
    std::string selectedPlayoutDevice = "";
    std::string selectedSourceLanguage_ = "";
    std::string selectedTargetLanguage_ = "";
    std::string icon_folder_ = "";
    bool started_ = false;
    IProducer* producer_ = nullptr;
    bool run_ = false;
};

int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int)
{
    char logpath[MAX_PATH];
    GetModuleFileNameA(NULL, logpath, MAX_PATH);
    strrchr(logpath, '\\')[0] = '\0';

    std::string icon_folder = logpath;
    icon_folder += "\\resources";

    strcat(logpath, "\\Log.log");
    g_flog = fopen(logpath, "wb");

    SetConsoleOutputCP(CP_UTF8);
    //CoInitializeEx(nullptr, 0);

    SetProducerLogSink(LogSink, nullptr);

    MainProducer producer(icon_folder);
    producer.Exec();
    return 0;
}
