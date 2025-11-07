// Interactive QR code generator with GUI and hotkeys (libqrencode backend)

#include <opencv2/highgui.hpp>
#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>

#include <qrencode.h>

#include <cstdlib>
#include <ctime>
#include <string>
#include <vector>
#include <algorithm>

using namespace cv;

static inline int clampi(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }

struct State {
    std::string text;          // payload
    int version;               // 0 = auto
    int eclIdx;                // 0=L, 1=M, 2=Q, 3=H
    int scale;                 // px per module
    int quietZone;             // modules
    bool showHelp;
    std::string defaultOut;    // optional save path

    State()
        : text("Hello, QR!"), version(0), eclIdx(1), scale(15), quietZone(7), showHelp(false) {}
};

static inline QRecLevel eclFromIdx(int idx) {
    switch (clampi(idx, 0, 3)) {
        case 0: return QR_ECLEVEL_L;
        case 1: return QR_ECLEVEL_M;
        case 2: return QR_ECLEVEL_Q;
        default: return QR_ECLEVEL_H;
    }
}
static inline std::string eclName(int idx) {
    static const char* names[] = {"L","M","Q","H"};
    return names[clampi(idx,0,3)];
}

static Mat renderQR(const State& s) {
    if (s.text.empty()) {
        return Mat(240, 240, CV_8UC1, Scalar(255));
    }

    QRcode* code = QRcode_encodeString(s.text.c_str(), s.version, eclFromIdx(s.eclIdx), QR_MODE_8, 1);
    if (!code) {
        return Mat(240, 240, CV_8UC1, Scalar(200));
    }
    const int w = code->width; // modules
    const unsigned char* data = code->data;

    Mat modules(w, w, CV_8UC1, Scalar(255));
    for (int y = 0; y < w; ++y) {
        for (int x = 0; x < w; ++x) {
            // data bit: LSB of each byte indicates dark module
            unsigned char b = data[y * w + x];
            bool dark = (b & 0x01) != 0;
            modules.at<unsigned char>(y, x) = dark ? 0 : 255;
        }
    }
    QRcode_free(code);

    int qz = std::max(0, s.quietZone);
    Mat bordered(w + 2*qz, w + 2*qz, CV_8UC1, Scalar(255));
    modules.copyTo(bordered(Rect(qz, qz, w, w)));

    int sc = std::max(1, s.scale);
    Mat up;
    resize(bordered, up, Size(bordered.cols*sc, bordered.rows*sc), 0, 0, INTER_NEAREST);
    return up;
}

static void overlayInfo(Mat& canvas, const State& s) {
    if (!s.showHelp) return; // only show overlay when toggled on
    if (canvas.channels() == 1) cvtColor(canvas, canvas, COLOR_GRAY2BGR);
    int y = 20;
    const auto put = [&](const std::string& line, const Scalar& col = Scalar(0,255,0)) {
        putText(canvas, line, Point(10, y), FONT_HERSHEY_SIMPLEX, 0.6, Scalar(0,0,0), 2, LINE_AA);
        putText(canvas, line, Point(10, y), FONT_HERSHEY_SIMPLEX, 0.6, col, 1, LINE_AA);
        y += 22;
    };
    put("QR Code Generator (GUI)", Scalar(255,255,255));
    put("Text: " + (s.text.empty() ? std::string("<empty>") : s.text));
    put(format("Version: %d (v/V)  ECL: %s (e/E)", s.version, eclName(s.eclIdx).c_str()));
    put(format("Scale: %d (+/- or =/_)  QuietZone: %d ([/ ] or {/})", s.scale, s.quietZone));
    if (!s.defaultOut.empty()) put("Save: s -> " + s.defaultOut); else put("Save: s -> auto name");
    if (s.showHelp) {UVWXYZabcdefghijklmnopqrstuvwxyz";
    int len = 12 + (std::rand() % 13);
    std::string s; s.reserve(len);
    for (int i = 0; i < len; ++i) s.push_back(alnum[std::rand() % (sizeof(alnum)-1)]);
    return s;
}

static std::string autoFileName(const State& s) {
    return format("qrcode_v%d_ecl%s_sc%d_qz%d.png", s.version, eclName(s.eclIdx).c_str(), s.scale, s.quietZone);
}

int main(int argc, char** argv) {
    std::srand((unsigned)std::time(nullptr));
    State s;
        y += 8;
        put("Keys:", Scalar(200,200,200));
        put("  Type to append, Backspace to delete", Scalar(200,200,200));
        put("  v/V version, e/E error correction", Scalar(200,200,200));
        put("  +/- or =/_ scale, [/ ] or {/} quiet zone", Scalar(200,200,200));
        put("  r random, c clear, s save, h help, q/ESC quit", Scalar(200,200,200));
    }
}

static std::string randomText() {
    static const char alnum[] = "0123456789ABCDEFGHIJKLMNOPQRST
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if ((a == "-t" || a == "--text") && i + 1 < argc) s.text = argv[++i];
        else if ((a == "-o" || a == "--output") && i + 1 < argc) s.defaultOut = argv[++i];
    }

    const std::string kWin = "QR Code Generator";
    namedWindow(kWin, WINDOW_AUTOSIZE);
    bool needRedraw = true;

    for (;;) {
        if (needRedraw) {
            Mat qr = renderQR(s);
            Mat canvas(qr.rows + 120, std::max(qr.cols, 640), CV_8UC3, Scalar(255,255,255));
            int x = (canvas.cols - qr.cols) / 2;
            Mat qrBgr; cvtColor(qr, qrBgr, COLOR_GRAY2BGR);
            qrBgr.copyTo(canvas(Rect(x, 10, qr.cols, qr.rows)));
            overlayInfo(canvas, s);
            imshow(kWin, canvas);
            needRedraw = false;
        }

        int key = waitKeyEx(0);
        if (key < 0) continue;
        int ch = key & 0xFF; // normalize to ASCII; fixes shifted keys not being detected

        if (ch == 27 || ch == 'q' || ch == 'Q') break;
        if (ch == 'h' || ch == 'H') { s.showHelp = !s.showHelp; needRedraw = true; continue; }
        if (ch == 'r' || ch == 'R') { s.text = randomText(); needRedraw = true; continue; }
        if (ch == 'c' || ch == 'C') { s.text.clear(); needRedraw = true; continue; }

        // Scale: accept '+' or '=' to increase; '-' or '_' to decrease
        if (ch == '+' || ch == '=') { s.scale = clampi(s.scale + 1, 1, 64); needRedraw = true; continue; }
        if (ch == '-' || ch == '_') { s.scale = clampi(s.scale - 1, 1, 64); needRedraw = true; continue; }

        // Quiet zone: accept '[' or '{' to decrease; ']' or '}' to increase
        if (ch == '[' || ch == '{') { s.quietZone = clampi(s.quietZone - 1, 0, 16); needRedraw = true; continue; }
        if (ch == ']' || ch == '}') { s.quietZone = clampi(s.quietZone + 1, 0, 16); needRedraw = true; continue; }

        if (ch == 'e') { s.eclIdx = clampi(s.eclIdx - 1, 0, 3); needRedraw = true; continue; }
        if (ch == 'E') { s.eclIdx = clampi(s.eclIdx + 1, 0, 3); needRedraw = true; continue; }
        if (ch == 'v') { s.version = clampi(s.version - 1, 0, 40); needRedraw = true; continue; }
        if (ch == 'V') { s.version = clampi(s.version + 1, 0, 40); needRedraw = true; continue; }

        if (ch == 's' || ch == 'S') {
            Mat qr = renderQR(s);
            std::string path = s.defaultOut.empty() ? autoFileName(s) : s.defaultOut;
            imwrite(path, qr);
            Mat canvas(qr.rows + 60, std::max(qr.cols, 480), CV_8UC3, Scalar(255,255,255));
            Mat qrBgr; cvtColor(qr, qrBgr, COLOR_GRAY2BGR);
            int x = (canvas.cols - qr.cols) / 2;
            qrBgr.copyTo(canvas(Rect(x, 10, qr.cols, qr.rows)));
            putText(canvas, "Saved: " + path, Point(10, canvas.rows - 15), FONT_HERSHEY_SIMPLEX, 0.6, Scalar(0,0,0), 2, LINE_AA);
            putText(canvas, "Saved: " + path, Point(10, canvas.rows - 15), FONT_HERSHEY_SIMPLEX, 0.6, Scalar(0,128,255), 2, LINE_AA);
            imshow(kWin, canvas);
            continue;
        }

        // Append printable ASCII characters
        if (ch >= 32 && ch <= 126) { s.text.push_back(static_cast<char>(ch)); needRedraw = true; continue; }
        // Backspace (8 or 127)
        if (ch == 8 || ch == 127) { if (!s.text.empty()) { s.text.pop_back(); needRedraw = true; } continue; }
    }

    destroyAllWindows();
    return 0;
}
