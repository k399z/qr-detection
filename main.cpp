#include <opencv2/opencv.hpp>
#include <opencv2/objdetect.hpp>
#include <iostream>
#include <chrono>
#include <unordered_map>
#include <termios.h>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h> // signals for clean exit

using namespace std;
using namespace cv;

// Replace CLOCK()/avg* helpers with clearer chrono-based utilities
static inline double nowMs() {
    using clock = std::chrono::steady_clock;
    auto t = clock::now().time_since_epoch();
    return std::chrono::duration<double, std::milli>(t).count();
}

struct FpsStats {
    double avgMs = 0.0;
    double fpsStart = nowMs();
    double avgFps = 0.0;
    double fps1sec = 0.0;

    double updateAvgMs(double frameMs) {
        avgMs = 0.98 * avgMs + 0.02 * frameMs;
        return avgMs;
    }
    double tickFps() {
        double now = nowMs();
        if (now - fpsStart > 1000.0) {
            fpsStart = now;
            avgFps = 0.7 * avgFps + 0.3 * fps1sec;
            fps1sec = 0.0;
        }
        fps1sec += 1.0;
        return avgFps;
    }
};
// ---- End timing helpers ----

// ---- Terminal (Unix) non-blocking input helpers ----
static struct termios orig_termios;
static bool term_raw_enabled = false;

void enableRawTerminal() {
    if (term_raw_enabled) return;
    struct termios raw;
    tcgetattr(STDIN_FILENO, &orig_termios);
    raw = orig_termios;
    raw.c_lflag &= ~(ICANON | ECHO);
    tcsetattr(STDIN_FILENO, TCSANOW, &raw);
    int flags = fcntl(STDIN_FILENO, F_GETFL, 0);
    fcntl(STDIN_FILENO, F_SETFL, flags | O_NONBLOCK);
    term_raw_enabled = true;
}
void disableRawTerminal() {
    if (!term_raw_enabled) return;
    tcsetattr(STDIN_FILENO, TCSANOW, &orig_termios);
    int flags = fcntl(STDIN_FILENO, F_GETFL, 0);
    fcntl(STDIN_FILENO, F_SETFL, flags & ~O_NONBLOCK);
    term_raw_enabled = false;
}
bool stdinKeyPressed(int& ch) {
    unsigned char c;
    ssize_t n = read(STDIN_FILENO, &c, 1);
    if (n == 1) { ch = c; return true; }
    return false;
}
// ---- End terminal helpers ----

// Ensure raw terminal is restored automatically
struct TerminalRawGuard {
    TerminalRawGuard() { enableRawTerminal(); }
    ~TerminalRawGuard() { disableRawTerminal(); }
};

// Add exit key matcher and signal handling
static inline bool isExitKey(int k) {
    if (k < 0) return false;
    // Only consider ASCII range for exit keys; do NOT mask extended codes
    // to 8-bit, otherwise arrow keys (e.g., 0xFF51) would appear as 'Q'.
    if (k > 255) return false;
    switch (k) {
        case 27:            // ESC
        case 'q': case 'Q': // quit
        case 'x': case 'X': // exit
        case 'c': case 'C': // close
        case 3:             // Ctrl+C
        case 4:             // Ctrl+D
        case 17:            // Ctrl+Q
        case 24:            // Ctrl+X
            return true;
        default:
            return false;
    }
}

volatile sig_atomic_t g_signal_exit = 0;
void handleSignal(int) { g_signal_exit = 1; }

// Centralized exit request check (window key, terminal key, or signal)
static inline bool exitRequested(int windowKey) {
    if (isExitKey(windowKey)) return true;
    int ch;
    if (stdinKeyPressed(ch) && isExitKey(ch)) return true;
    if (g_signal_exit) return true;
    return false;
}

// Small helpers to open sources
static bool tryOpenCamera(int index, cv::VideoCapture& cap, int w, int h) {
    cap.release();
    if (!cap.open(index)) return false;
    cap.set(cv::CAP_PROP_FRAME_WIDTH, w);
    cap.set(cv::CAP_PROP_FRAME_HEIGHT, h);
    return cap.isOpened();
}

static void listCameras(int maxIndexToProbe = 10) {
    std::cout << "Probing V4L2 cameras...\n";
    for (int i = 0; i < maxIndexToProbe; ++i) {
        cv::VideoCapture test;
        if (test.open(i)) {
            double w = test.get(cv::CAP_PROP_FRAME_WIDTH);
            double h = test.get(cv::CAP_PROP_FRAME_HEIGHT);
            std::cout << " - /dev/video" << i << " (opened)";
            if (w > 0 && h > 0) std::cout << " default " << (int)w << "x" << (int)h;
            std::cout << "\n";
            test.release();
        }
    }
}

int main(int argc, char** argv) {
    // Constants for clarity
    const int kFrameWidth = 640;
    const int kFrameHeight = 480;
    const char* kWindowTitle = "QR Detect";

    // Parse optional input source
    // Usage: ./detector [--list] [0|1]
    if (argc >= 2 && std::string(argv[1]) == "--list") {
        listCameras(2); // only probe 0 and 1
        return 0;
    }

    int requestedIndex = -1;
    if (argc >= 2) {
        std::string arg = argv[1];
        bool numeric = !arg.empty() &&
                       std::all_of(arg.begin(), arg.end(), [](unsigned char c){ return std::isdigit(c); });
        if (!numeric) {
            std::cerr << "仅支持摄像头索引 0 或 1 (不支持图片/视频路径)." << std::endl;
            return 2;
        }
        requestedIndex = std::stoi(arg);
        if (requestedIndex < 0 || requestedIndex > 1) {
            std::cerr << "无效的摄像头索引 " << requestedIndex
                      << ". 仅支持 0 或 1." << std::endl;
            return 2;
        }
    }

    cv::VideoCapture cap;

    if (requestedIndex >= 0) {
        if (!tryOpenCamera(requestedIndex, cap, 640, 480)) {
            std::cerr << "无法打开摄像头索引 " << requestedIndex
                      << " (仅支持 0 或 1)." << std::endl;
            return 3;
        }
    } else {
        // No argument: try 0 then 1 only
        int indices[2] = {0,1};
        bool opened = false;
        for (int idx : indices) {
            if (tryOpenCamera(idx, cap, 640, 480)) { opened = true; break; }
        }
        if (!opened) {
            std::cerr << "无法打开摄像头 (仅尝试 /dev/video0 与 /dev/video1).\n"
                      << "提示:\n"
                      << "  1) 运行: ./detector --list 查看可用设备 (仅列出 0,1)\n"
                      << "  2) 指定: ./detector 0  或  ./detector 1\n"
                      << "  3) 现在已不支持文件/图片/URL 输入\n";
            return 1;
        }
    }

    // QR code detector
    cv::QRCodeDetector qrDetector;

    Mat frame;

    // Enable terminal key handling with RAII
    TerminalRawGuard terminalGuard;

    // Register signal handlers for clean exit (restores terminal)
    signal(SIGINT, handleSignal);
    signal(SIGTERM, handleSignal);
    signal(SIGHUP, handleSignal);

    FpsStats stats;
    
    while (true) {
        double start = nowMs(); // start timing this frame

        if (!cap.read(frame) || frame.empty()) break;

        // Detect and decode multiple QR codes
        std::vector<cv::Point> poly;
        std::string text = qrDetector.detectAndDecodeCurved(frame, poly);
        int detectedCount = 0;
        if (!text.empty() && poly.size() >= 4) {
            const cv::Point* ptsArr = poly.data();
            int npts = static_cast<int>(poly.size());
            cv::polylines(frame, &ptsArr, &npts, 1, true, cv::Scalar(0, 255, 0), 3, cv::LINE_AA);
            cv::Point center(0,0);
            for (const auto& p : poly) center += p;
            center.x /= (int)poly.size();
            center.y /= (int)poly.size();
            cv::putText(frame, text, center + cv::Point(-20, -10),
                        cv::FONT_HERSHEY_SIMPLEX, 0.6, cv::Scalar(0, 255, 0), 2, cv::LINE_AA);
            detectedCount = 1;
        }
        
        double dur = nowMs() - start;
    std::string statsText = cv::format("avg %.2f ms  fps %.1f  QR %d",
                       stats.updateAvgMs(dur), stats.tickFps(), detectedCount);
        cv::putText(frame, statsText, cv::Point(10, 30),
                    cv::FONT_HERSHEY_SIMPLEX, 0.8, cv::Scalar(0,255,0), 2);

        cv::imshow(kWindowTitle, frame);
        int key = cv::waitKey(1);
        if (exitRequested(key)) break;
    }

    // Terminal restored automatically by TerminalRawGuard
    cap.release();
    cv::destroyAllWindows();
    return 0;
}