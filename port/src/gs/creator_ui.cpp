// The Custom Character screen in the game (main menu item 13): the creator of port/tools/creator, played with the
// controller (or the keyboard and the mouse) inside the game's window, laid out like the game's own menus: the title,
// the tabs WARRIOR / PARTS / MOVES / FORMS / SAVE & PLAY (L1 / R1), a list on the left (d-pad, cross, circle), the
// character grid, an on-screen keyboard, the tip box with a host character, the button prompts, and on the right the
// warrior itself on a platform - the game's own 3D model of it (src/menu/port_creator.c draws it).
//
// Everything a warrior is made of is worked out by the creator's engine (port/tools/creator/creator.py: a local HTTP
// server, 127.0.0.1): this file is one more client of its JSON API, as its web page (index.html) is. When no creator
// answers /api/version the game starts one, hidden (Python through `py` / `python`; the script's place from the setting
// creator_path, else found next to the game), which ends with the game. The requests run on worker threads (WinHTTP on
// Windows, a plain socket elsewhere); the screen never waits for them.
//
// The model: the creator builds the warrior's current look into a folder (/api/preview) which the game serves as a
// package's preview for the disc character it is shown as (PortCharas_Preview, on the game's thread) while the
// Character Reference's viewer shows that character (Mm_ViewWantPreview). "Add to roster" writes the character package
// (/api/package) and has the game read the characters folder again (PortCharas_Reload), so the new cell is on the
// character select without starting the game again.
//
// Drawn on the render thread into ImGui's background list (ui.cpp calls Creator_Frame), under the settings window.
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <winhttp.h>
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <signal.h>
#include <spawn.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <unistd.h>
extern char **environ;
#endif
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <algorithm>
#include <deque>
#include <functional>
#include <string>
#include <vector>
#include <SDL3/SDL.h>
#include "imgui.h"
#include "imgui_internal.h"
#include "ui.h"
#include "creator_look.h"
extern "C" {
#include "../../include/plat_charas.h"
}

SDL_GPUTexture *Ui_Texture(uint8_t *pix, uint32_t w, uint32_t h); // ui.cpp (takes pix)
void Ui_TextureFree(SDL_GPUTexture *tex);
ImFont *Ui_FontBold(void);
extern "C" {
int Port_Setting(const char *name, int def);
void Port_SettingText(const char *name, const char *def, char *out, int size);
int PortCharas_Preview(const char *path, int as);
int Port_NetSession(void);
}

// =============================================================================================== JSON
// Just what the creator's answers need: a value tree, read and written.
struct J {
    enum T { NUL, BOOL, NUM, STR, ARR, OBJ };
    T t = NUL;
    bool b = false;
    double n = 0;
    std::string s;
    std::vector<J> a;            // ARR: the items; OBJ: the values
    std::vector<std::string> k;  // OBJ: the keys

    static J str(const std::string &v) { J j; j.t = STR; j.s = v; return j; }
    static J num(double v) { J j; j.t = NUM; j.n = v; return j; }
    static J boolean(bool v) { J j; j.t = BOOL; j.b = v; return j; }
    static J arr() { J j; j.t = ARR; return j; }
    static J obj() { J j; j.t = OBJ; return j; }
    bool nul() const { return t == NUL; }
    bool isObj() const { return t == OBJ; }
    bool isArr() const { return t == ARR; }
    bool isStr() const { return t == STR; }
    bool isNum() const { return t == NUM; }
    size_t size() const { return t == ARR || t == OBJ ? a.size() : 0; }
    const J &operator[](size_t i) const { static J none; return (t == ARR || t == OBJ) && i < a.size() ? a[i] : none; }
    const J &operator[](int i) const { return (*this)[(size_t)(i < 0 ? 0 : i)]; }
    const J &operator[](const char *key) const {
        static J none;
        if (t == OBJ) {
            for (size_t i = 0; i < k.size(); i++) {
                if (k[i] == key) { return a[i]; }
            }
        }
        return none;
    }
    J *find(const char *key) {
        if (t == OBJ) {
            for (size_t i = 0; i < k.size(); i++) {
                if (k[i] == key) { return &a[i]; }
            }
        }
        return NULL;
    }
    bool has(const char *key) const { return !(*this)[key].nul(); }
    J &set(const char *key, const J &v) {
        if (t != OBJ) { *this = obj(); }
        J *f = find(key);
        if (f != NULL) { *f = v; return *f; }
        k.push_back(key);
        a.push_back(v);
        return a.back();
    }
    J &at(const char *key) { // the member (made an object if it is not one)
        J *f = find(key);
        if (f == NULL || !f->isObj()) { return set(key, obj()); }
        return *f;
    }
    void erase(const char *key) {
        for (size_t i = 0; t == OBJ && i < k.size(); i++) {
            if (k[i] == key) { k.erase(k.begin() + i); a.erase(a.begin() + i); return; }
        }
    }
    void push(const J &v) { if (t != ARR) { *this = arr(); } a.push_back(v); }
    std::string text(const char *def = "") const { return t == STR ? s : t == NUM ? std::to_string((long long)n) : std::string(def); }
    double number(double def = 0) const { return t == NUM ? n : t == BOOL ? (b ? 1 : 0) : def; }
    bool truthy() const { return t == BOOL ? b : t == NUM ? n != 0 : t == STR ? !s.empty() : t == ARR || t == OBJ ? true : false; }
};

static void js_ws(const char *&p) { while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') { p++; } }
static void utf8_put(std::string &o, unsigned c) {
    if (c < 0x80) { o += (char)c; }
    else if (c < 0x800) { o += (char)(0xC0 | (c >> 6)); o += (char)(0x80 | (c & 0x3F)); }
    else if (c < 0x10000) { o += (char)(0xE0 | (c >> 12)); o += (char)(0x80 | ((c >> 6) & 0x3F)); o += (char)(0x80 | (c & 0x3F)); }
    else { o += (char)(0xF0 | (c >> 18)); o += (char)(0x80 | ((c >> 12) & 0x3F)); o += (char)(0x80 | ((c >> 6) & 0x3F)); o += (char)(0x80 | (c & 0x3F)); }
}
static bool js_str(const char *&p, std::string &o) {
    if (*p != '"') { return false; }
    p++;
    while (*p && *p != '"') {
        if (*p == '\\') {
            p++;
            switch (*p) {
            case 'n': o += '\n'; break;
            case 't': o += '\t'; break;
            case 'r': o += '\r'; break;
            case 'b': o += '\b'; break;
            case 'f': o += '\f'; break;
            case 'u': {
                unsigned c = 0;
                for (int i = 1; i <= 4; i++) {
                    char h = p[i];
                    c = c * 16 + (unsigned)(h >= '0' && h <= '9' ? h - '0' : h >= 'a' && h <= 'f' ? h - 'a' + 10 : h >= 'A' && h <= 'F' ? h - 'A' + 10 : 0);
                }
                p += 4;
                if (c >= 0xD800 && c < 0xDC00 && p[1] == '\\' && p[2] == 'u') { // a surrogate pair
                    unsigned lo = 0;
                    for (int i = 3; i <= 6; i++) {
                        char h = p[i];
                        lo = lo * 16 + (unsigned)(h >= '0' && h <= '9' ? h - '0' : h >= 'a' && h <= 'f' ? h - 'a' + 10 : h >= 'A' && h <= 'F' ? h - 'A' + 10 : 0);
                    }
                    p += 6;
                    c = 0x10000 + ((c - 0xD800) << 10) + (lo - 0xDC00);
                }
                utf8_put(o, c);
                break;
            }
            case 0: return false;
            default: o += *p; break;
            }
            p++;
        } else {
            o += *p++;
        }
    }
    if (*p != '"') { return false; }
    p++;
    return true;
}
static bool js_parse(const char *&p, J &v, int depth = 0) {
    js_ws(p);
    if (depth > 64) { return false; }
    if (*p == '{') {
        v = J::obj();
        p++;
        js_ws(p);
        if (*p == '}') { p++; return true; }
        while (1) {
            std::string key;
            J item;
            js_ws(p);
            if (!js_str(p, key)) { return false; }
            js_ws(p);
            if (*p != ':') { return false; }
            p++;
            if (!js_parse(p, item, depth + 1)) { return false; }
            v.k.push_back(key);
            v.a.push_back(item);
            js_ws(p);
            if (*p == ',') { p++; continue; }
            if (*p == '}') { p++; return true; }
            return false;
        }
    }
    if (*p == '[') {
        v = J::arr();
        p++;
        js_ws(p);
        if (*p == ']') { p++; return true; }
        while (1) {
            J item;
            if (!js_parse(p, item, depth + 1)) { return false; }
            v.a.push_back(item);
            js_ws(p);
            if (*p == ',') { p++; continue; }
            if (*p == ']') { p++; return true; }
            return false;
        }
    }
    if (*p == '"') { v.t = J::STR; return js_str(p, v.s); }
    if (strncmp(p, "true", 4) == 0) { v = J::boolean(true); p += 4; return true; }
    if (strncmp(p, "false", 5) == 0) { v = J::boolean(false); p += 5; return true; }
    if (strncmp(p, "null", 4) == 0) { v = J(); p += 4; return true; }
    if (*p == '-' || (*p >= '0' && *p <= '9')) {
        char *end;
        v = J::num(strtod(p, &end));
        p = end;
        return true;
    }
    return false;
}
static bool js_read(const std::string &text, J &v) {
    const char *p = text.c_str();
    return js_parse(p, v);
}
static void js_quote(const std::string &s, std::string &o) {
    o += '"';
    for (unsigned char c : s) {
        if (c == '"' || c == '\\') { o += '\\'; o += (char)c; }
        else if (c == '\n') { o += "\\n"; }
        else if (c == '\r') { o += "\\r"; }
        else if (c == '\t') { o += "\\t"; }
        else if (c < 0x20) { char b[8]; snprintf(b, sizeof(b), "\\u%04x", c); o += b; }
        else { o += (char)c; }
    }
    o += '"';
}
static void js_write(const J &v, std::string &o) {
    switch (v.t) {
    case J::NUL: o += "null"; break;
    case J::BOOL: o += v.b ? "true" : "false"; break;
    case J::NUM: {
        char b[40];
        if (v.n == floor(v.n) && fabs(v.n) < 1e15) { snprintf(b, sizeof(b), "%lld", (long long)v.n); }
        else { snprintf(b, sizeof(b), "%.10g", v.n); }
        o += b;
        break;
    }
    case J::STR: js_quote(v.s, o); break;
    case J::ARR:
        o += '[';
        for (size_t i = 0; i < v.a.size(); i++) { if (i) { o += ','; } js_write(v.a[i], o); }
        o += ']';
        break;
    case J::OBJ:
        o += '{';
        for (size_t i = 0; i < v.a.size(); i++) { if (i) { o += ','; } js_quote(v.k[i], o); o += ':'; js_write(v.a[i], o); }
        o += '}';
        break;
    }
}
static std::string js_text(const J &v) { std::string o; js_write(v, o); return o; }

// =============================================================================================== HTTP
static int sPort = 8765;

#ifdef _WIN32
static std::wstring wide(const std::string &s) {
    int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, NULL, 0);
    std::wstring w(n > 0 ? n - 1 : 0, L'\0');
    if (n > 1) { MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, &w[0], n); }
    return w;
}
// One request to the creator: its status (0: no answer) and its body.
static int http(const char *method, const char *path, const std::string &body, std::string &out, int timeoutMs) {
    HINTERNET ses = NULL, con = NULL, req = NULL;
    DWORD status = 0, len = sizeof(status);
    char origin[96];
    out.clear();
    ses = WinHttpOpen(L"Tenkaichi3Decomp", WINHTTP_ACCESS_TYPE_NO_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (ses == NULL) { return 0; }
    WinHttpSetTimeouts(ses, 2000, 2000, 30000, timeoutMs);
    con = WinHttpConnect(ses, L"127.0.0.1", (INTERNET_PORT)sPort, 0);
    std::wstring wpath = wide(path), wmethod = wide(method);
    if (con != NULL) {
        req = WinHttpOpenRequest(con, wmethod.c_str(), wpath.c_str(), NULL, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, 0);
    }
    snprintf(origin, sizeof(origin), "Content-Type: application/json\r\nOrigin: http://127.0.0.1:%d\r\n", sPort);
    std::wstring head = wide(origin);
    if (req != NULL &&
        WinHttpSendRequest(req, head.c_str(), (DWORD)-1, body.empty() ? WINHTTP_NO_REQUEST_DATA : (LPVOID)body.data(), (DWORD)body.size(), (DWORD)body.size(), 0) &&
        WinHttpReceiveResponse(req, NULL)) {
        WinHttpQueryHeaders(req, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX, &status, &len, WINHTTP_NO_HEADER_INDEX);
        DWORD avail = 0;
        while (WinHttpQueryDataAvailable(req, &avail) && avail > 0) {
            size_t at = out.size();
            DWORD got = 0;
            out.resize(at + avail);
            if (!WinHttpReadData(req, &out[at], avail, &got)) { out.resize(at); break; }
            out.resize(at + got);
            if (got == 0) { break; }
        }
    }
    if (req) { WinHttpCloseHandle(req); }
    if (con) { WinHttpCloseHandle(con); }
    WinHttpCloseHandle(ses);
    return (int)status;
}
#else
static int http(const char *method, const char *path, const std::string &body, std::string &out, int timeoutMs) {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in a;
    struct timeval tv;
    char head[512];
    out.clear();
    if (fd < 0) { return 0; }
    memset(&a, 0, sizeof(a));
    a.sin_family = AF_INET;
    a.sin_port = htons((uint16_t)sPort);
    a.sin_addr.s_addr = htonl(0x7F000001);
    tv.tv_sec = timeoutMs / 1000;
    tv.tv_usec = (timeoutMs % 1000) * 1000;
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    if (connect(fd, (struct sockaddr *)&a, sizeof(a)) != 0) { close(fd); return 0; }
    snprintf(head, sizeof(head), "%s %s HTTP/1.0\r\nHost: 127.0.0.1:%d\r\nOrigin: http://127.0.0.1:%d\r\nContent-Type: application/json\r\nContent-Length: %u\r\n\r\n",
             method, path, sPort, sPort, (unsigned)body.size());
    std::string all = std::string(head) + body, got;
    size_t sent = 0;
    while (sent < all.size()) {
        ssize_t n = send(fd, all.data() + sent, all.size() - sent, 0);
        if (n <= 0) { close(fd); return 0; }
        sent += (size_t)n;
    }
    char buf[16384];
    while (1) {
        ssize_t n = recv(fd, buf, sizeof(buf), 0);
        if (n <= 0) { break; }
        got.append(buf, (size_t)n);
    }
    close(fd);
    size_t end = got.find("\r\n\r\n");
    if (got.compare(0, 5, "HTTP/") != 0 || end == std::string::npos) { return 0; }
    out = got.substr(end + 4);
    return atoi(got.c_str() + got.find(' ') + 1);
}
#endif

// =============================================================================================== the creator's process
static std::string sGame;     // the game's folder (the current directory): creator.py's --game
static std::string sLog;      // its output, when the game started it
static std::string sWhy;      // why it could not be started
#ifdef _WIN32
static HANDLE sJob, sChild;
#else
static pid_t sChild;
#endif

static bool file_is(const std::string &p) {
    struct stat st;
    return stat(p.c_str(), &st) == 0 && (st.st_mode & S_IFMT) == S_IFREG;
}
static bool dir_is(const std::string &p) {
    struct stat st;
    return stat(p.c_str(), &st) == 0 && (st.st_mode & S_IFMT) == S_IFDIR;
}
static std::string parent(const std::string &p) {
    size_t s = p.find_last_of("/\\");
    return s == std::string::npos ? std::string(".") : p.substr(0, s);
}
static std::string trim_slash(std::string p) {
    while (p.size() > 3 && (p.back() == '/' || p.back() == '\\')) { p.pop_back(); }
    return p;
}

// creator.py: the setting creator_path (the script, or a folder of the repository), else beside the game.
static std::string creator_script(void) {
    char set[600];
    std::vector<std::string> c;
    Port_SettingText("creator_path", "", set, sizeof(set));
    const char *env = getenv("BT3_CREATOR");
    if (env != NULL && env[0]) { snprintf(set, sizeof(set), "%s", env); }
    if (set[0]) {
        std::string s = trim_slash(set);
        c.push_back(s);
        c.push_back(s + "/creator.py");
        c.push_back(s + "/port/tools/creator/creator.py");
        c.push_back(s + "/Tenkaichi3Decomp/port/tools/creator/creator.py");
    }
    std::string g = sGame;
    c.push_back(parent(g) + "/Tenkaichi3Decomp/port/tools/creator/creator.py");          // <parent>/Tenkaichi3Decomp
    c.push_back(parent(parent(g)) + "/Tenkaichi3Decomp/port/tools/creator/creator.py");  // the setup's own folders
    c.push_back(g + "/port/tools/creator/creator.py");                                   // run from the repository
    const char *base = SDL_GetBasePath();                                                 // port/build/bt3.exe
    if (base != NULL) {
        std::string b = trim_slash(base);
        c.push_back(parent(b) + "/tools/creator/creator.py");
        c.push_back(parent(parent(b)) + "/Tenkaichi3Decomp/port/tools/creator/creator.py");
    }
    for (const std::string &p : c) {
        if (file_is(p)) { return p; }
    }
    return "";
}

static bool child_alive(void) {
#ifdef _WIN32
    return sChild != NULL && WaitForSingleObject(sChild, 0) == WAIT_TIMEOUT;
#else
    return sChild > 0 && waitpid(sChild, NULL, WNOHANG) == 0;
#endif
}

// Starts creator.py with the first Python of the list from the n-th that can be run: its index, -1 none.
static int spawn(const std::string &script, int which) {
#ifdef _WIN32
    static const char *const kPy[] = {"py -3", "python", "pythonw", "python3"};
    if (which >= 4) { return -1; }
    if (sJob == NULL) { // (the creator ends with the game: the job's last handle closes with the program)
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION li;
        sJob = CreateJobObjectW(NULL, NULL);
        memset(&li, 0, sizeof(li));
        li.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
        if (sJob != NULL) { SetInformationJobObject(sJob, JobObjectExtendedLimitInformation, &li, sizeof(li)); }
    }
    std::string s = script, g = sGame;
    for (char &ch : s) { if (ch == '/') { ch = '\\'; } }
    for (char &ch : g) { if (ch == '/') { ch = '\\'; } }
    char port[16];
    snprintf(port, sizeof(port), "%d", sPort);
    std::string cmd = std::string(kPy[which]) + " \"" + s + "\" --game \"" + g + "\" --port " + port + " --no-browser";
    SECURITY_ATTRIBUTES sa = {sizeof(sa), NULL, TRUE};
    HANDLE log = CreateFileW(wide(sLog).c_str(), GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, &sa, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    STARTUPINFOW si;
    PROCESS_INFORMATION pi;
    memset(&si, 0, sizeof(si));
    memset(&pi, 0, sizeof(pi));
    si.cb = sizeof(si);
    if (log != INVALID_HANDLE_VALUE) {
        si.dwFlags = STARTF_USESTDHANDLES;
        si.hStdOutput = log;
        si.hStdError = log;
        si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
    }
    SetEnvironmentVariableW(L"PYTHONIOENCODING", L"utf-8"); // (its prints of names never fail on the log's code page)
    SetEnvironmentVariableW(L"PYTHONUNBUFFERED", L"1");
    std::wstring wcmd = wide(cmd), wdir = wide(parent(s));
    BOOL ok = CreateProcessW(NULL, &wcmd[0], NULL, NULL, log != INVALID_HANDLE_VALUE, CREATE_NO_WINDOW | CREATE_SUSPENDED, NULL, wdir.c_str(), &si, &pi);
    if (log != INVALID_HANDLE_VALUE) { CloseHandle(log); }
    fprintf(stderr, "bt3: creator: %s: %s\n", ok ? "started" : "could not start", cmd.c_str());
    if (!ok) { return spawn(script, which + 1); }
    if (sJob != NULL) { AssignProcessToJobObject(sJob, pi.hProcess); }
    ResumeThread(pi.hThread);
    CloseHandle(pi.hThread);
    if (sChild != NULL) { CloseHandle(sChild); }
    sChild = pi.hProcess;
    return which;
#else
    static const char *const kPy[] = {"python3", "python"};
    if (which >= 2) { return -1; }
    char port[16];
    snprintf(port, sizeof(port), "%d", sPort);
    const char *argv[] = {kPy[which], script.c_str(), "--game", sGame.c_str(), "--port", port, "--no-browser", NULL};
    posix_spawn_file_actions_t fa;
    posix_spawn_file_actions_init(&fa);
    posix_spawn_file_actions_addopen(&fa, 1, sLog.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
    posix_spawn_file_actions_adddup2(&fa, 1, 2);
    pid_t pid;
    int r = posix_spawnp(&pid, kPy[which], &fa, NULL, (char *const *)argv, environ);
    posix_spawn_file_actions_destroy(&fa);
    if (r != 0) { return spawn(script, which + 1); }
    sChild = pid;
    static bool hooked;
    if (!hooked) { hooked = true; atexit([] { if (sChild > 0) { kill(sChild, SIGTERM); } }); }
    return which;
#endif
}

static std::string log_tail(void) {
    FILE *fp = fopen(sLog.c_str(), "rb");
    std::string t;
    if (fp == NULL) { return t; }
    char buf[4096];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), fp)) > 0) { t.append(buf, n); if (t.size() > 65536) { t.erase(0, t.size() - 8192); } }
    fclose(fp);
    while (!t.empty() && (t.back() == '\n' || t.back() == '\r')) { t.pop_back(); }
    size_t s = t.find_last_of('\n');
    return s == std::string::npos ? t : t.substr(s + 1);
}

// Worker thread: a creator answers, or one is started and waited for. True when one answers.
static bool connect_creator(void) {
    std::string out;
    if (http("GET", "/api/version", "", out, 2500) == 200) { return true; }
    std::string script = creator_script();
    if (script.empty()) {
        sWhy = "The creator (port/tools/creator/creator.py) was not found. Put its path in bt3_settings.txt as creator_path=...";
        return false;
    }
    sLog = sGame + "/custom_fighters/.ingame_creator_log.txt";
    std::string dir = sGame + "/custom_fighters";
#ifdef _WIN32
    CreateDirectoryW(wide(dir).c_str(), NULL);
#else
    mkdir(dir.c_str(), 0755);
#endif
    for (int which = spawn(script, 0); which >= 0; which = spawn(script, which + 1)) {
        for (int t = 0; t < 240; t++) { // (the first start reads the roster from the disc: up to two minutes)
            SDL_Delay(500);
            if (http("GET", "/api/version", "", out, 2500) == 200) { return true; }
            if (!child_alive()) { break; } // (that Python could not run it: the next)
        }
        if (child_alive()) { break; }
    }
    std::string tail = log_tail();
    sWhy = "The creator could not be started (Python 3 is needed: python.org)." + (tail.empty() ? std::string() : "\n" + tail);
    return false;
}

// =============================================================================================== jobs
typedef std::function<void(int status, const J &res)> Done;
struct Job {
    std::string method, path, body;
    int timeout = 60000;
    int kind = 0;          // 1: a model preview (only the newest waiting one is sent)
    int face = -1;         // a portrait: the character id
    Done done;
    std::function<int(void)> work; // instead of a request: run on the worker (the answer is its status)
};
struct Result {
    Job job;
    int status = 0;
    std::string raw;
    uint8_t *pix = NULL;   // a portrait's pixels (RGBA), 128 x 128 or as decoded
    int w = 0, h = 0;
};
static SDL_Mutex *sJobLock;
static SDL_Condition *sJobWake, *sFaceWake;
static std::deque<Job> sJobs, sFaceJobs;
static std::deque<Result> sResults;
static int sBusy;              // requests sent and not answered (not portraits)
static bool sDry;              // a choice is being tried, not made (try_choice): no requests, sounds, lines or screens
static bool sThreads;

static uint8_t *decode_png(const void *data, size_t size, int *w, int *h) {
    SDL_Surface *s = SDL_LoadPNG_IO(SDL_IOFromConstMem(data, size), true), *rgba;
    if (s == NULL) { return NULL; }
    rgba = SDL_ConvertSurface(s, SDL_PIXELFORMAT_RGBA32);
    SDL_DestroySurface(s);
    if (rgba == NULL) { return NULL; }
    uint8_t *pix = (uint8_t *)malloc((size_t)rgba->w * rgba->h * 4);
    for (int y = 0; pix != NULL && y < rgba->h; y++) {
        memcpy(pix + (size_t)y * rgba->w * 4, (const uint8_t *)rgba->pixels + (size_t)y * rgba->pitch, (size_t)rgba->w * 4);
    }
    *w = rgba->w;
    *h = rgba->h;
    SDL_DestroySurface(rgba);
    return pix;
}

static int SDLCALL job_thread(void *) {
    while (1) {
        Job j;
        SDL_LockMutex(sJobLock);
        while (sJobs.empty()) { SDL_WaitCondition(sJobWake, sJobLock); }
        j = sJobs.front();
        sJobs.pop_front();
        SDL_UnlockMutex(sJobLock);
        Result r;
        if (j.work) {
            r.status = j.work();
        } else {
            r.status = http(j.method.c_str(), j.path.c_str(), j.body, r.raw, j.timeout);
        }
        r.job = j;
        SDL_LockMutex(sJobLock);
        sResults.push_back(r);
        SDL_UnlockMutex(sJobLock);
    }
    return 0;
}

// Portraits: the creator keeps them as PNG files in its cache (read from there when they are), else asked for.
static int SDLCALL face_thread(void *) {
    while (1) {
        Job j;
        SDL_LockMutex(sJobLock);
        while (sFaceJobs.empty()) { SDL_WaitCondition(sFaceWake, sJobLock); }
        j = sFaceJobs.front();
        sFaceJobs.pop_front();
        SDL_UnlockMutex(sJobLock);
        Result r;
        char path[700];
        std::string data;
        snprintf(path, sizeof(path), "%s/custom_fighters/.cache/face_%03d.png", sGame.c_str(), j.face);
        FILE *fp = fopen(path, "rb");
        if (fp != NULL) {
            char buf[16384];
            size_t n;
            while ((n = fread(buf, 1, sizeof(buf), fp)) > 0) { data.append(buf, n); }
            fclose(fp);
        }
        if (data.size() < 8) {
            snprintf(path, sizeof(path), "/face/%d.png", j.face);
            if (http("GET", path, "", data, 60000) != 200) { data.clear(); }
        }
        if (!data.empty()) { r.pix = decode_png(data.data(), data.size(), &r.w, &r.h); }
        r.job = j;
        SDL_LockMutex(sJobLock);
        sResults.push_back(r);
        SDL_UnlockMutex(sJobLock);
    }
    return 0;
}

static void jobs_start(void) {
    if (sThreads) { return; }
    sThreads = true;
    sJobLock = SDL_CreateMutex();
    sJobWake = SDL_CreateCondition();
    sFaceWake = SDL_CreateCondition();
    SDL_DetachThread(SDL_CreateThread(job_thread, "creator", NULL));
    SDL_DetachThread(SDL_CreateThread(face_thread, "creator faces", NULL));
}

static void post_job(const Job &j) {
    SDL_LockMutex(sJobLock);
    if (j.face >= 0) {
        sFaceJobs.push_back(j);
        SDL_SignalCondition(sFaceWake);
    } else {
        if (j.kind == 1) { // (a newer look replaces a waiting one)
            for (size_t i = 0; i < sJobs.size(); i++) {
                if (sJobs[i].kind == 1) { sJobs.erase(sJobs.begin() + i); sBusy--; break; }
            }
        }
        sJobs.push_back(j);
        sBusy++;
        SDL_SignalCondition(sJobWake);
    }
    SDL_UnlockMutex(sJobLock);
}

static void api(const char *path, const J *body, Done done, int timeout = 60000, int kind = 0) {
    if (sDry) { return; }
    Job j;
    j.method = body != NULL ? "POST" : "GET";
    j.path = path;
    if (body != NULL) { j.body = js_text(*body); }
    j.done = done;
    j.timeout = timeout;
    j.kind = kind;
    post_job(j);
}

// =============================================================================================== shared with the game's thread
// The game's thread (src/menu/port_creator.c) asks for these each frame; this file sets them on the render thread.
static SDL_Mutex *sShare;
static volatile int sActive;   // the screen is open (PortCreator_Open .. PortCreator_Close)
static volatile int sOpenGen;  // how many times it was opened
static struct {
    int leave;                 // 1 back to the main menu, 2 to the test fight
    int key, chara, costume;   // the model wanted
    char folder[700];          // ... the creator's folder of the warrior's look ("" the disc character's own)
    int reload, reloaded, reloadOk;
    int turn;                  // thousandths of a radian, since the game took it
    int sound[16], sounds;
    int testChara, testCostume;
    float camZoom, camFocus, camPanX, camPanY; // the camera: how close, the point of the model it stays on (0 feet,
                                               // 1 top), moved (mockup pixels); set on the render thread
    int photo;                 // 1: the model alone on a key colour (the "photo of the model": Creator_WantGrab)
    int host;                  // the host beside the warrior (a disc character), -1 none
    int stage;                 // 1: the battle stage around it
} sSh;
#define CAM_X 915.0f     // where the model stands at zoom 1 (mockup pixels: port_creator.c): across, its feet, its height
#define CAM_FEET 635.0f  // (on the stage, as large as the trailer has it: its feet behind the tip box)
#define CAM_H 530.0f
#define HOST_CHARA 58    // the host: Supreme Kai (the disc has no King Kai), on the stage beside the warrior and in the tip box

extern "C" void PortCreator_Open(void) {
    if (sShare == NULL) { sShare = SDL_CreateMutex(); }
    SDL_LockMutex(sShare);
    sSh.leave = 0;
    sSh.key = -1;
    sSh.folder[0] = '\0';
    sSh.reload = sSh.reloaded = 0;
    sSh.turn = 0;
    sSh.sounds = 0;
    sSh.testChara = -1;
    sSh.photo = 0;
    sSh.host = -1;
    sSh.stage = Port_Setting("creator_stage_on", 1) != 0;
    if (sSh.camZoom <= 0.0f) { sSh.camZoom = 1.0f; sSh.camFocus = 0.5f; }
    sOpenGen++;
    sActive = 1;
    SDL_UnlockMutex(sShare);
}

extern "C" void PortCreator_Close(void) {
    SDL_LockMutex(sShare);
    sActive = 0;
    SDL_UnlockMutex(sShare);
    PortCharas_Preview(NULL, -1);
}

extern "C" int PortCreator_Get(int what) {
    int v = -1;
    SDL_LockMutex(sShare);
    switch (what) {
    case 0: v = sSh.leave; break;
    case 1: v = sSh.key; break;
    case 2: v = sSh.chara; break;
    case 3: v = sSh.costume; break;
    case 4: v = sSh.reload; sSh.reload = 0; break;
    case 5: v = sSh.turn; sSh.turn = 0; break;
    case 6:
        if (sSh.sounds > 0) {
            v = sSh.sound[0];
            memmove(sSh.sound, sSh.sound + 1, sizeof(int) * (size_t)(--sSh.sounds));
        }
        break;
    case 7: v = sSh.testChara; break;
    case 8: v = sSh.testCostume; break;
    case 9: case 10: case 11: { // where the model stands (port_creator.c: tenths of the mockup's pixels): across, its
        // feet, its height - its zoom and place: the point `camFocus` of it (0 its feet, 1 its face) stays where it is at
        // zoom 1 (moved by the pan). Measured on the screen: a point t of the model is drawn at feet + 49.5 - height *
        // (0.042 + 0.669 t) (mockup pixels: Mm_ViewDraw's place is where the feet stand on the platform)
        float z = sSh.camZoom > 0.1f ? sSh.camZoom : 1.0f, h = CAM_H * z;
        float feet = CAM_FEET + sSh.camPanY + (h - CAM_H) * (0.042f + 0.669f * sSh.camFocus);
        float r = what == 9 ? CAM_X + sSh.camPanX : what == 10 ? feet : h;
        v = (int)(r * 10.0f);
        break;
    }
    case 12: v = sSh.camZoom < 1.6f; break; // (the platform under it: not in a close shot)
    case 13: v = sSh.photo; break;
    case 14: v = sSh.photo || sSh.camZoom >= 1.6f ? -1 : sSh.host; break; // (not in a close shot: it would be in the way)
    case 15: v = sSh.stage; break;
    }
    SDL_UnlockMutex(sShare);
    return v;
}

extern "C" void PortCreator_Reloaded(int ok) {
    SDL_LockMutex(sShare);
    sSh.reloaded = 1;
    sSh.reloadOk = ok;
    SDL_UnlockMutex(sShare);
}

// (the game's thread, before the viewer reads the model wanted with `key`) the files it is to be read from
extern "C" int PortCreator_ApplyPreview(int key) {
    char folder[700];
    int as = 0;
    folder[0] = '\0';
    SDL_LockMutex(sShare);
    if (key >= 0 && key == sSh.key) {
        snprintf(folder, sizeof(folder), "%s", sSh.folder);
        as = sSh.chara;
    }
    SDL_UnlockMutex(sShare);
    if (folder[0]) {
        if (!PortCharas_Preview(folder, as)) { fprintf(stderr, "bt3: creator: the preview %s could not be read\n", folder); }
    } else {
        PortCharas_Preview(NULL, -1);
    }
    return 1;
}

extern "C" int PortCreator_Active(void) { return sActive; }

static void sound(int id) { // 0 move, 1 choose, 2 back
    if (sDry) { return; }
    SDL_LockMutex(sShare);
    if (sSh.sounds < 16) { sSh.sound[sSh.sounds++] = id; }
    SDL_UnlockMutex(sShare);
}

// =============================================================================================== the screen's state
enum { SC_CONNECT, SC_FAIL, SC_HOME, SC_EDIT };
enum { TAB_WARRIOR, TAB_PARTS, TAB_MOVES, TAB_FORMS, TAB_FINISH, TAB_COUNT };
static const char *const kTabs[TAB_COUNT] = {"WARRIOR", "PARTS", "MOVES", "FORMS", "PLAY"};
static const char *const kSlots[5] = {"blast1_1", "blast1_2", "blast2_1", "blast2_2", "ultimate"};
static const char *const kSlotLabel[5] = {"Blast 1 (1)", "Blast 1 (2)", "Blast 2 (1)", "Blast 2 (2)", "Ultimate"};

static int sScreen = SC_CONNECT, sTab, sSeenGen;
static J sRoster, sFighters, sRosterInfo, sSpec;
static std::string sFile;      // the warrior's file in custom_fighters ("" not saved yet)
static bool sDirty, sHaveBt4, sKeepState, sNoPreview;
static int sCur[8], sTop[8];   // the list's cursor and first row shown: [0] home, 1 + tab
static int sFrames;
static std::string sToast;
static int sToastUntil, sToastErr;
static std::string sTipLine;   // the host's line in the tip box
static int sShownCostume;      // which of the warrior's costumes the model shows (L2 / R2)
static bool sKeyboardLast;     // the last thing pressed was on the keyboard: its keys in the prompts
static int sModelGen = 100000; // keys of the creator's models (a disc character's own: below)
static std::string sPreviewKey, sPreviewSent; // the look the model shows / was asked for
static int sPreviewChanged;    // the frame the look changed (the request waits for a moment of rest)
static bool sPreviewBusy;
static std::string sPendingFolder;
static int sPendingAs = -1;

static void toast(const std::string &t, bool err = false) {
    if (sDry) { return; }
    sToast = t;
    sToastErr = err;
    sToastUntil = sFrames + (err ? 60 * 8 : 60 * 5);
}

// ---- the roster
static const J *entry_of(const J &name) { // by the game's number or by its name
    if (name.isNum()) {
        for (size_t i = 0; i < sRoster.size(); i++) { if ((int)sRoster[i]["id"].number(-1) == (int)name.n) { return &sRoster[i]; } }
        return NULL;
    }
    std::string s = name.text();
    if (SDL_strncasecmp(s.c_str(), "bt4:", 4) == 0) { // ("bt4:34", "BT4: Goku Black": custom_fighter.py chara_id)
        const char *rest = s.c_str() + 4;
        while (*rest == ' ') { rest++; }
        if (*rest >= '0' && *rest <= '9') { return entry_of(J::num(1000 + (double)strtol(rest, NULL, 0))); }
        for (size_t i = 0; i < sRoster.size(); i++) {
            std::string n = sRoster[i]["name"].text();
            if (n.size() > 5 && SDL_strcasecmp(n.c_str() + 5, rest) == 0) { return &sRoster[i]; }
        }
    }
    bool digits = !s.empty() && strspn(s.c_str(), "0123456789") == s.size();
    for (size_t i = 0; i < sRoster.size(); i++) {
        if (digits ? (int)sRoster[i]["id"].number(-1) == atoi(s.c_str()) : SDL_strcasecmp(sRoster[i]["name"].text().c_str(), s.c_str()) == 0) {
            return &sRoster[i];
        }
    }
    return NULL;
}
static const J *chara_of(const J &ref) { return ref.isObj() ? entry_of(ref["chara"]) : ref.nul() ? NULL : entry_of(ref); }
static int chara_id(const J &ref) { const J *c = chara_of(ref); return c != NULL ? (int)(*c)["id"].number(0) : -1; }
static std::string nm(const J &ref, const char *dflt) {
    const J *c = chara_of(ref);
    if (c == NULL) { return dflt; }
    std::string s = (*c)["name"].text();
    int costume = (int)ref["costume"].number(1);
    if (costume > 1) { s += " (costume " + std::to_string(costume) + ")"; }
    return s;
}
static J ref_of(const J &entry, int costume = 0) {
    J r = J::obj();
    r.set("chara", entry["name"]);
    if (costume > 0) { r.set("costume", J::num(costume)); }
    return r;
}

// ---- the warrior
static J look(void) {
    if (sSpec.has("look")) { return sSpec["look"]; }
    if (sSpec["costumes"].size()) { return sSpec["costumes"][0]; }
    if (sSpec.has("base")) { return sSpec["base"]; }
    J r = J::obj();
    r.set("chara", J::num(0));
    return r;
}
static J moveset(void) { return sSpec.has("moveset") ? sSpec["moveset"] : look(); }
static std::string move_name(const J &who, int slot) {
    const J *c = chara_of(who);
    return c != NULL && (*c)["moves"][(size_t)slot].isStr() ? (*c)["moves"][(size_t)slot].s : std::string("?");
}
static int slot_index(const std::string &k) {
    for (int i = 0; i < 5; i++) { if (k == kSlots[i]) { return i; } }
    return 0;
}
struct MoveOf { J who; int slot; std::string name, shown; };
static MoveOf move_of(int key) {
    MoveOf m;
    const J &mv = sSpec["moves"][kSlots[key]];
    m.who = mv.isObj() ? mv : moveset();
    m.slot = mv.isObj() && mv.has("slot") ? slot_index(mv["slot"].text()) : key;
    m.name = move_name(m.who, m.slot);
    m.shown = sSpec["names"][kSlots[key]].isStr() ? sSpec["names"][kSlots[key]].s : m.name;
    return m;
}
static std::string fighter_name(const std::string &file) {
    for (size_t i = 0; i < sFighters.size(); i++) {
        if (sFighters[i]["file"].text() == file) { return sFighters[i]["spec"]["name"].text(file.c_str()); }
    }
    return file;
}
static bool on_roster(const std::string &file) {
    const J &r = sRosterInfo["roster"];
    for (size_t i = 0; i < r.size(); i++) { if (r[i].text() == file) { return true; } }
    return false;
}
static void touch(void) { sDirty = true; }
// the Z WARRIORS shell (its section near the end of the file)
static void set_editing_costume(int k);
static void photo_of_model(void);
static void open_stance_maker(void);
static void clean_empty(const char *key) { // (an empty object or list of the spec goes)
    J *v = sSpec.find(key);
    if (v != NULL && (v->isObj() || v->isArr()) && v->size() == 0) { sSpec.erase(key); }
}

// The package's folder (custom_fighter.py folder_name): the name without the characters a file name cannot have.
static std::string folder_name(const std::string &name) {
    std::string s;
    for (unsigned char c : name) {
        if (c < 0x20 || strchr("<>:\"/\\|?*", c) != NULL) { continue; }
        s += (char)c;
    }
    while (!s.empty() && (s.back() == ' ' || s.back() == '.' || s.back() == '\t')) { s.pop_back(); }
    while (!s.empty() && (s[0] == ' ' || s[0] == '\t')) { s.erase(0, 1); }
    size_t at = 0;
    int count = 0;
    while (at < s.size() && count < 63) { // 63 letters (not bytes)
        unsigned char c = (unsigned char)s[at];
        at += c < 0x80 ? 1 : c < 0xE0 ? 2 : c < 0xF0 ? 3 : 4;
        count++;
    }
    s = s.substr(0, at);
    return s.empty() ? std::string("Custom Fighter") : s;
}

// =============================================================================================== portraits
struct Face { int state; SDL_GPUTexture *tex; }; // state 0 not asked, 1 asked, 2 there, 3 none
static Face sFace[2200];
static int sFacesMade;
static ImTextureID face_tex(int id) {
    if (id < 0 || id >= (int)(sizeof(sFace) / sizeof(sFace[0]))) { return (ImTextureID)0; }
    Face &f = sFace[id];
    if (f.state == 0 && sScreen != SC_CONNECT && sScreen != SC_FAIL) {
        Job j;
        j.face = id;
        f.state = 1;
        post_job(j);
    }
    return f.state == 2 ? (ImTextureID)(intptr_t)f.tex : (ImTextureID)0;
}

// base64 PNG (the colours screen's textures) as a texture; NULL if it is none
static SDL_GPUTexture *png64_tex(const std::string &b64, int *w, int *h) {
    static int8_t dec[256];
    static bool init;
    if (!init) {
        const char *t = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
        memset(dec, -1, sizeof(dec));
        for (int i = 0; t[i]; i++) { dec[(unsigned char)t[i]] = (int8_t)i; }
        init = true;
    }
    std::string out;
    unsigned v = 0;
    int bits = 0;
    for (unsigned char c : b64) {
        if (dec[c] < 0) { continue; }
        v = (v << 6) | (unsigned)dec[c];
        bits += 6;
        if (bits >= 8) { bits -= 8; out += (char)((v >> bits) & 0xFF); }
    }
    uint8_t *pix = out.empty() ? NULL : decode_png(out.data(), out.size(), w, h);
    return pix != NULL ? Ui_Texture(pix, (uint32_t)*w, (uint32_t)*h) : NULL;
}

// =============================================================================================== input
struct In {
    bool up, down, left, right, ok, back, sq, tri, l1, r1, l2, r2, start;
    bool enter, esc, backspace;    // the keyboard's own, for the typing screens
    float turn;                     // the right stick, -1..1
    // the workshop, the camera and the painter
    bool okHeld;                    // cross (Enter / Space) held down
    bool fast;                      // left / right held a while: sliders go faster
    bool view;                      // L3 / V: the camera's next preset (Full, Head)
    bool r3;                        // R3 held: the right stick moves the camera
    bool undo, redo;                // Ctrl+Z / Ctrl+Y
    float zoom;                     // the right stick up / down, + / - keys: > 0 closer
    float lx, ly;                   // the left stick, -1..1 (the painter's crosshair)
    bool dup, ddown, dleft, dright; // the d-pad and the arrow keys alone (a texel a press)
};
static In sIn;
static int sScriptAt;              // BT3_CREATOR_SCRIPT=<frame>:<button>,...: presses for tests
static bool sTyping;               // a screen that takes typed letters is up (the keyboard, the grid's search)

static bool sMouse;                // the mouse is the screen's (not the settings window's)
static bool key(ImGuiKey k, bool repeat = true) { return ImGui::IsKeyPressed(k, repeat); }
static bool click_in(ImVec2 a, ImVec2 b) { return sMouse && ImGui::IsMouseClicked(0) && ImGui::IsMouseHoveringRect(a, b, false); }
static float analog(ImGuiKey k) {
    ImGuiKeyData *d = ImGui::GetKeyData(ImGui::GetCurrentContext(), k);
    return d != NULL ? d->AnalogValue : 0.0f;
}
static float held_for(ImGuiKey k) {
    ImGuiKeyData *d = ImGui::GetKeyData(ImGui::GetCurrentContext(), k);
    return d != NULL && d->Down ? d->DownDuration : 0.0f;
}
static void read_input(bool live) {
    In in;
    memset(&in, 0, sizeof(in));
    if (live) {
        bool pad = false;
        auto P = [&](ImGuiKey k, bool repeat) { bool p = key(k, repeat); pad |= p; return p; };
        in.up = P(ImGuiKey_GamepadDpadUp, true) || P(ImGuiKey_GamepadLStickUp, true);
        in.down = P(ImGuiKey_GamepadDpadDown, true) || P(ImGuiKey_GamepadLStickDown, true);
        in.left = P(ImGuiKey_GamepadDpadLeft, true) || P(ImGuiKey_GamepadLStickLeft, true);
        in.right = P(ImGuiKey_GamepadDpadRight, true) || P(ImGuiKey_GamepadLStickRight, true);
        in.ok = P(ImGuiKey_GamepadFaceDown, false);
        in.back = P(ImGuiKey_GamepadFaceRight, false);
        in.sq = P(ImGuiKey_GamepadFaceLeft, false);
        in.tri = P(ImGuiKey_GamepadFaceUp, false);
        in.l1 = P(ImGuiKey_GamepadL1, true);
        in.r1 = P(ImGuiKey_GamepadR1, true);
        in.l2 = P(ImGuiKey_GamepadL2, false);
        in.r2 = P(ImGuiKey_GamepadR2, false);
        in.start = P(ImGuiKey_GamepadStart, false);
        ImGuiKeyData *rl = ImGui::GetKeyData(ImGui::GetCurrentContext(), ImGuiKey_GamepadRStickLeft);
        ImGuiKeyData *rr = ImGui::GetKeyData(ImGui::GetCurrentContext(), ImGuiKey_GamepadRStickRight);
        in.turn = (rr != NULL ? rr->AnalogValue : 0.0f) - (rl != NULL ? rl->AnalogValue : 0.0f);
        in.zoom = analog(ImGuiKey_GamepadRStickUp) - analog(ImGuiKey_GamepadRStickDown);
        in.lx = analog(ImGuiKey_GamepadLStickRight) - analog(ImGuiKey_GamepadLStickLeft);
        in.ly = analog(ImGuiKey_GamepadLStickDown) - analog(ImGuiKey_GamepadLStickUp);
        in.r3 = ImGui::IsKeyDown(ImGuiKey_GamepadR3);
        in.view = P(ImGuiKey_GamepadL3, false);
        in.okHeld = ImGui::IsKeyDown(ImGuiKey_GamepadFaceDown);
        in.dup = key(ImGuiKey_GamepadDpadUp, true);
        in.ddown = key(ImGuiKey_GamepadDpadDown, true);
        in.dleft = key(ImGuiKey_GamepadDpadLeft, true);
        in.dright = key(ImGuiKey_GamepadDpadRight, true);
        {
            static const ImGuiKey kLr[] = {ImGuiKey_GamepadDpadLeft, ImGuiKey_GamepadDpadRight, ImGuiKey_GamepadLStickLeft,
                                           ImGuiKey_GamepadLStickRight, ImGuiKey_LeftArrow, ImGuiKey_RightArrow};
            float most = 0.0f;
            for (ImGuiKey k : kLr) { most = std::max(most, held_for(k)); }
            in.fast = most > 0.7f;
        }
        bool kb = false;
        auto K = [&](ImGuiKey k, bool repeat) { bool p = key(k, repeat); kb |= p; return p; };
        ImGuiIO &io = ImGui::GetIO();
        in.up |= K(ImGuiKey_UpArrow, true);
        in.down |= K(ImGuiKey_DownArrow, true);
        in.left |= K(ImGuiKey_LeftArrow, true);
        in.right |= K(ImGuiKey_RightArrow, true);
        in.enter = K(ImGuiKey_Enter, false) || K(ImGuiKey_KeypadEnter, false);
        in.esc = K(ImGuiKey_Escape, false);
        in.backspace = K(ImGuiKey_Backspace, true);
        in.sq |= K(ImGuiKey_Delete, false);
        in.tri |= K(ImGuiKey_F2, false);
        in.l1 |= K(ImGuiKey_PageUp, true);
        in.r1 |= K(ImGuiKey_PageDown, true);
        in.l2 |= K(ImGuiKey_Home, false);
        in.r2 |= K(ImGuiKey_End, false);
        if (io.KeyCtrl && K(ImGuiKey_S, false)) { in.start = true; }
        if (io.KeyCtrl && K(ImGuiKey_Z, true)) { in.undo = true; }
        if (io.KeyCtrl && K(ImGuiKey_Y, true)) { in.redo = true; }
        in.dup |= key(ImGuiKey_UpArrow, true);
        in.ddown |= key(ImGuiKey_DownArrow, true);
        in.dleft |= key(ImGuiKey_LeftArrow, true);
        in.dright |= key(ImGuiKey_RightArrow, true);
        if (!sTyping && !io.KeyCtrl) { // (letters are typed on the typing screens)
            in.okHeld |= ImGui::IsKeyDown(ImGuiKey_Space) || ImGui::IsKeyDown(ImGuiKey_Enter);
            in.view |= K(ImGuiKey_V, false);
            if (ImGui::IsKeyDown(ImGuiKey_Equal) || ImGui::IsKeyDown(ImGuiKey_KeypadAdd)) { in.zoom += 0.6f; kb = true; }
            if (ImGui::IsKeyDown(ImGuiKey_Minus) || ImGui::IsKeyDown(ImGuiKey_KeypadSubtract)) { in.zoom -= 0.6f; kb = true; }
        }
        if (!sTyping && !io.KeyCtrl) {
            in.ok |= in.enter || K(ImGuiKey_Space, false);
            in.back |= in.esc || in.backspace;
            in.sq |= K(ImGuiKey_F, false);
            in.tri |= K(ImGuiKey_R, false);
            in.l1 |= K(ImGuiKey_Q, true);
            in.r1 |= K(ImGuiKey_E, true);
            in.l2 |= K(ImGuiKey_Z, false);
            in.r2 |= K(ImGuiKey_C, false);
            if (K(ImGuiKey_A, true)) { in.turn -= 4.0f; }
            if (K(ImGuiKey_D, true)) { in.turn += 4.0f; }
        }
        if (pad) { sKeyboardLast = false; }
        if (kb) { sKeyboardLast = true; }
    }
    const char *script = getenv("BT3_CREATOR_SCRIPT");
    while (script != NULL) { // "<frame>:<button>[,...]" from the screen's opening: up down left right ok back sq tri l1 r1 l2 r2 start
        const char *p = script;
        for (int i = 0; i < sScriptAt && p != NULL; i++) {
            p = strchr(p, ',');
            p = p != NULL ? p + 1 : NULL;
        }
        if (p == NULL || atoi(p) > sFrames) { break; }
        const char *b = strchr(p, ':');
        sScriptAt++;
        if (b == NULL) { continue; }
        b++;
        size_t n = strcspn(b, ",");
        std::string what(b, n);
        fprintf(stderr, "bt3: creator: script %d: %s\n", sFrames, what.c_str());
        if (what == "up") { in.up = true; } else if (what == "down") { in.down = true; } else if (what == "left") { in.left = true; }
        else if (what == "right") { in.right = true; } else if (what == "ok") { in.ok = in.enter = true; } else if (what == "back") { in.back = in.esc = true; }
        else if (what == "sq") { in.sq = true; } else if (what == "tri") { in.tri = true; } else if (what == "l1") { in.l1 = true; }
        else if (what == "r1") { in.r1 = true; } else if (what == "l2") { in.l2 = true; } else if (what == "r2") { in.r2 = true; }
        else if (what == "start") { in.start = true; }
        // (the workshop, the camera, the painter: one frame of each)
        else if (what == "view") { in.view = true; } else if (what == "zin") { in.zoom = 1.0f; } else if (what == "zout") { in.zoom = -1.0f; }
        else if (what == "undo") { in.undo = true; } else if (what == "redo") { in.redo = true; }
        else if (what == "hold") { in.okHeld = true; } else if (what == "press") { in.ok = in.okHeld = true; }
        else if (what == "lsr") { in.lx = 1.0f; } else if (what == "lsl") { in.lx = -1.0f; }
        else if (what == "lsu") { in.ly = -1.0f; } else if (what == "lsd") { in.ly = 1.0f; }
        else if (what == "pan") { in.r3 = true; in.zoom = 1.0f; }
        else if (what == "fright") { in.right = in.fast = true; } else if (what == "fleft") { in.left = in.fast = true; }
        else if (what == "dl") { in.dleft = in.left = true; } else if (what == "dr") { in.dright = in.right = true; }
        else if (what == "du") { in.dup = in.up = true; } else if (what == "dd") { in.ddown = in.down = true; }
        else if (what == "fdr") { in.dright = in.right = in.fast = true; }
        else if (what.compare(0, 5, "type=") == 0) { for (char c : what.substr(5)) { ImGui::GetIO().AddInputCharacter((unsigned)c); } }
    }
    sIn = in;
}

// =============================================================================================== the lists
struct Row {
    std::string label, value, sub, tip;
    bool sec = false, locked = false;
    bool folded = false;                 // a heading whose rows are folded away (fold_rows: cross on it)
    int face = -1;                       // a portrait on the row
    std::function<void()> ok, sq, tri;   // cross, square, triangle
    std::function<void(int)> lr;         // left / right
    const char *sqLabel = NULL, *triLabel = NULL, *lrLabel = NULL;
    float bar = -1.0f, barDef = -1.0f;   // a slider: where it is and its default (0..1), -1 none
    std::function<void(float)> setFrac;  // ... set by the mouse (0..1)
};

// ---- the screens over the list (the grid, the keyboard, lists, the colours): a stack
enum { OV_GRID, OV_LIST, OV_KEYS, OV_COLOURS, OV_PALETTE, OV_TEXT, OV_FX, OV_WORKSHOP, OV_TEXPICK, OV_PAINT, OV_WHEEL };
struct Overlay {
    int kind = OV_LIST;
    int id = 0;                          // (which one: a choice may close it and open another in its place)
    std::string title, text;
    // the grid
    int game = 0;                        // 0 every game, 1 BT3, 2 BT4
    bool bt3only = false, costume = false;
    std::vector<int> moveKinds;          // pick a technique of these slots
    std::string none;                    // the "none" choice (square), "" none
    std::function<void(const J *)> picked;
    std::string pill;                    // what is being chosen, in the gold pill by the title ("" from the title)
    std::string search;
    int stage = 0;                       // 0 the faces, 1 the costumes, 2 the techniques
    int chosen = -1;                     // the roster entry chosen (stages 1, 2)
    int rest = 0;                        // frames the cursor rested
    // a list
    std::vector<Row> rows;
    std::function<void()> back;
    // the keyboard
    size_t max = 63;
    std::function<void(const std::string &)> typed;
    int kr = 0, kc = 0;
    // the colours
    J tex;                                // [{texture, png, colours: [[hex, share]...]}]
    std::vector<SDL_GPUTexture *> pics;
    std::vector<int> picW, picH;
    bool loading = false;
    int texture = 0;
    std::string from;
    std::string fx;                      // an attack's colours (OV_FX, and its palette): "blast1_1".."ultimate", "ki_blasts", "charge"
    int cur = 0, top = 0;
    // the workshop: the piece (kPieces, -1 the body), its layer, the part (-1 the whole layer / the build)
    int piece = -1, layer = 0, node = -1;
    J info;                              // /api/layernodes
    // the colour wheel
    float hue = 0, sat = 0, val = 1;
    unsigned startRgb = 0;
    std::function<void(unsigned)> rgbPicked;
};
static std::vector<Overlay> sOv;
static int sOvIds;
static void ov_push(Overlay &o) { if (!sDry) { o.id = ++sOvIds; sOv.push_back(o); } }

static void painter_close(void);
static void ov_close(void) {
    if (!sOv.empty()) {
        if (sOv.back().kind == OV_PAINT) { painter_close(); } // (its paint into the spec, its picture freed)
        for (SDL_GPUTexture *t : sOv.back().pics) { Ui_TextureFree(t); }
        sOv.pop_back();
    }
}
static void ov_close_all(void) { while (!sOv.empty()) { ov_close(); } }

static std::vector<const J *> grid_items(const Overlay &o) {
    std::vector<const J *> out;
    for (size_t i = 0; i < sRoster.size(); i++) {
        const J &c = sRoster[i];
        if (c["costumes"].size() == 0) { continue; }
        bool bt4 = c["game"].text() == "BT4";
        if ((o.bt3only && bt4) || (o.game == 1 && bt4) || (o.game == 2 && !bt4)) { continue; }
        if (!o.search.empty()) {
            std::string n = c["name"].text(), q = o.search;
            for (char &ch : n) { ch = (char)SDL_tolower((unsigned char)ch); }
            for (char &ch : q) { ch = (char)SDL_tolower((unsigned char)ch); }
            if (n.find(q) == std::string::npos) { continue; }
        }
        out.push_back(&c);
    }
    return out;
}

// The character grid: `picked` gets {"chara": name[, "costume": k][, "slot": key]} or NULL for the none choice.
static void open_grid(const std::string &title, std::function<void(const J *)> picked, bool costume = false, const char *none = NULL,
                      bool bt3only = false, int game = 0, std::vector<int> moves = std::vector<int>()) {
    Overlay o;
    o.kind = OV_GRID;
    o.title = title;
    o.picked = picked;
    o.costume = costume;
    o.none = none != NULL ? none : "";
    o.bt3only = bt3only;
    o.game = game;
    o.moveKinds = moves;
    // the cursor starts on the character the choice is about now, when it is shown
    ov_push(o);
    sound(1);
}
static void open_list(const std::string &title, const std::vector<Row> &rows, const std::string &text = "") {
    Overlay o;
    o.kind = OV_LIST;
    o.title = title;
    o.rows = rows;
    o.text = text;
    while (o.cur < (int)o.rows.size() && (o.rows[o.cur].sec || o.rows[o.cur].locked)) { o.cur++; }
    ov_push(o);
    sound(1);
}
static void open_keys(const std::string &title, const std::string &text, size_t max, std::function<void(const std::string &)> typed) {
    Overlay o;
    o.kind = OV_KEYS;
    o.title = title;
    o.text = text;
    o.max = max;
    o.typed = typed;
    ov_push(o);
    sound(1);
}
static void open_text(const std::string &title, const std::string &text) {
    Overlay o;
    o.kind = OV_TEXT;
    o.title = title;
    o.text = text;
    ov_push(o);
}
static void confirm(const std::string &question, std::function<void()> yes) {
    std::vector<Row> rows(2);
    rows[0].label = "Yes";
    rows[0].ok = [yes] { ov_close(); yes(); };
    rows[1].label = "No";
    rows[1].ok = [] { ov_close(); };
    open_list(question, rows);
    sOv.back().cur = 1;
}

// =============================================================================================== what the screen does
static void refresh_fighters(std::function<void()> then = nullptr) {
    api("/api/fighters", NULL, [then](int st, const J &r) {
        if (st == 200 && r.isArr()) { sFighters = r; }
        if (then) { then(); }
    });
}
static void refresh_roster_info(void) {
    api("/api/rosterinfo", NULL, [](int st, const J &r) { if (st == 200 && r.isObj()) { sRosterInfo = r; } });
}
static std::string error_of(int st, const J &r) {
    if (st == 0) { return "The creator is not answering."; }
    return r["error"].text(("The creator answered " + std::to_string(st)).c_str());
}

static void begin(void) { // the creator answers: the roster, the warriors, what is on the roster
    sTipLine = "Reading the roster from your disc...";
    api("/api/roster", NULL, [](int st, const J &r) {
        if (st != 200 || !r.isArr()) {
            sScreen = SC_FAIL;
            sWhy = error_of(st, r);
            return;
        }
        sRoster = r;
        sHaveBt4 = false;
        for (size_t i = 0; i < r.size(); i++) { sHaveBt4 |= r[i]["game"].text() == "BT4"; }
        refresh_fighters([] {
            if (!sKeepState || sScreen != SC_EDIT) { sScreen = SC_HOME; }
            sTipLine = "";
        });
        refresh_roster_info();
    }, 300000);
}

static void connect_start(void) {
    jobs_start();
    sScreen = SC_CONNECT;
    sTipLine = "Waking up the creator...";
    Job j;
    j.work = [] { return connect_creator() ? 200 : 0; };
    j.done = [](int st, const J &) {
        fprintf(stderr, "bt3: creator: %s\n", st == 200 ? "the creator answers" : sWhy.c_str());
        if (st == 200) {
            begin();
        } else {
            sScreen = SC_FAIL;
        }
    };
    j.timeout = 0;
    post_job(j);
}

static void save(std::function<void()> then = nullptr) {
    if (sSpec["name"].text().empty()) {
        toast("Give your warrior a name first!", true);
        sound(2);
        return;
    }
    J body = J::obj();
    body.set("file", sFile.empty() ? J() : J::str(sFile));
    body.set("spec", sSpec);
    api("/api/save", &body, [then](int st, const J &r) {
        if (st != 200) { toast(error_of(st, r), true); return; }
        sFile = r["file"].text();
        sDirty = false;
        refresh_fighters();
        if (then) { then(); } else { toast("Saved! Your warrior is ready to fight. (custom_fighters/" + sFile + ")"); }
    });
}

static void reload_game(void) { // the characters folder read again, by the game's thread
    SDL_LockMutex(sShare);
    sSh.reload = 1;
    sSh.reloaded = 0;
    SDL_UnlockMutex(sShare);
}

static void act(const char *what) { // add to / take off the roster
    std::string path = std::string("/api/") + what;
    auto run = [path] {
        J body = J::obj();
        body.set("file", J::str(sFile));
        toast(path == "/api/package" ? "Building your warrior for the roster..." : "Working...");
        api(path.c_str(), &body, [path](int st, const J &r) {
            if (st != 200) { toast(error_of(st, r), true); return; }
            std::string t = r["text"].text();
            if (path == "/api/package") {
                size_t cut = t.find("Start Tenkaichi3Decomp-Custom.exe");
                std::string notes = t.find("\nTo make it fit") != std::string::npos ? t.substr(t.find("\nTo make it fit")) : "";
                t = (cut != std::string::npos ? t.substr(0, cut) : t + " ") + "Its cell is at the end of the character select now." + notes;
            } else {
                t = "Taken off the roster.";
            }
            toast(t);
            refresh_roster_info();
            reload_game();
        }, 600000);
    };
    if (sDirty || sFile.empty()) { save(run); } else { run(); }
}

static void new_form(const char *d) {
    if (sSpec["name"].text().empty()) { toast("Name your warrior first.", true); return; }
    std::string dir = d;
    auto make = [dir] {
        std::string parentFile = sFile;
        J parentSpec = sSpec, child = sSpec;
        child.erase("forms");
        child.erase("form_costs");
        child.erase("bt4_slot");
        std::string add = dir == "up" ? " Super" : dir == "left" ? " Form 2" : " Form 3";
        child.set("name", J::str((sSpec["name"].text() + add).substr(0, 63)));
        child.set("title", J::str("a form of " + sSpec["name"].text()));
        J forms = J::obj();
        forms.set("down", J::str(parentFile));
        child.set("forms", forms);
        J body = J::obj();
        body.set("file", J());
        body.set("spec", child);
        api("/api/save", &body, [parentFile, parentSpec, child, dir](int st, const J &r) mutable {
            if (st != 200) { toast(error_of(st, r), true); return; }
            std::string childFile = r["file"].text();
            parentSpec.at("forms").set(dir.c_str(), J::str(childFile));
            if (!parentSpec["form_costs"].has(dir.c_str())) { parentSpec.at("form_costs").set(dir.c_str(), J::num(2)); }
            J b2 = J::obj();
            b2.set("file", J::str(parentFile));
            b2.set("spec", parentSpec);
            api("/api/save", &b2, [child, childFile](int st2, const J &r2) {
                if (st2 != 200) { toast(error_of(st2, r2), true); return; }
                refresh_fighters();
                sSpec = child;
                sFile = childFile;
                sDirty = false;
                sTab = TAB_PARTS;
                toast("New form made: " + child["name"].text() + ". Change its look and moves; it turns back with R3 + Down.");
            });
        });
    };
    if (sDirty || sFile.empty()) { save(make); } else { make(); }
}

static void load_fighter(const J &f) {
    sSpec = f["spec"];
    sFile = f["file"].text();
    sDirty = false;
    sScreen = SC_EDIT;
    sTab = TAB_WARRIOR;
    sShownCostume = 0;
    for (int i = 1; i < 8; i++) { sCur[i] = 1; sTop[i] = 0; } // (on the first row under the heading)
    sound(1);
}

static void new_fighter(void) {
    open_grid("START FROM WHICH BODY?", [](const J *r) {
        if (r == NULL) { return; }
        sSpec = J::obj();
        sSpec.set("name", J::str(""));
        sSpec.set("title", J::str(""));
        if (chara_id(*r) >= 1000) {
            J g = J::obj();
            g.set("chara", J::str("Goku (End)"));
            sSpec.set("base", g);
        } else {
            sSpec.set("base", *r);
        }
        sSpec.set("look", *r);
        J m = J::obj();
        m.set("chara", (*r)["chara"]);
        sSpec.set("moveset", m);
        sFile.clear();
        sDirty = true;
        sScreen = SC_EDIT;
        sTab = TAB_WARRIOR;
        sShownCostume = 0;
        for (int i = 1; i < 8; i++) { sCur[i] = 1; sTop[i] = 0; }
        open_keys("EVERY GREAT WARRIOR NEEDS A GREAT NAME!", "", 63, [](const std::string &t) { sSpec.set("name", J::str(t)); touch(); });
    }, true);
}

static void add_bt4(void) {
    open_grid("WHICH BT4 FIGHTER?", [](const J *r) {
        if (r == NULL) { return; }
        J body = J::obj();
        body.set("chara", (*r)["chara"]);
        std::string who = (*r)["chara"].text();
        toast("Bringing " + (who.size() > 5 ? who.substr(5) : who) + " over... (a few seconds)");
        api("/api/addbt4", &body, [who](int st, const J &res) {
            if (st != 200) { toast(error_of(st, res), true); return; }
            refresh_fighters();
            refresh_roster_info();
            reload_game();
            toast((who.size() > 5 ? who.substr(5) : who) + " is on the roster! Its cell is at the end of the character select.");
        }, 600000);
    }, false, NULL, false, 2);
}

static void test_fight(void) {
    if (!on_roster(sFile) || sDirty) {
        toast(sDirty ? "Save and press Add to roster first: the test fight uses the roster's copy." : "Add your warrior to the roster first.", true);
        sound(2);
        return;
    }
    std::string folder = folder_name(sSpec["name"].text());
    int id = -1;
    for (int i = 0; i < Port_CharaPkgCount(); i++) {
        const PortCharaPkg *p = Port_CharaPkg(i);
        if (p != NULL && p->kind == PORT_PKG_ADDS && strcmp(p->folder, folder.c_str()) == 0) { id = p->id; }
    }
    if (id < 0) {
        toast("The game has not found characters/" + folder + " yet: press Add to roster again.", true);
        sound(2);
        return;
    }
    int costumes = sSpec["costumes"].size() ? (int)sSpec["costumes"].size() : 1;
    SDL_LockMutex(sShare);
    sSh.testChara = id;
    sSh.testCostume = sShownCostume < costumes ? sShownCostume : 0;
    sSh.leave = 2;
    SDL_UnlockMutex(sShare);
    sKeepState = true;
}

static void leave(void) {
    SDL_LockMutex(sShare);
    sSh.leave = 1;
    SDL_UnlockMutex(sShare);
    sKeepState = false;
}

// ---- the colours screen
static void colours_load(Overlay *o);
static void open_colours(void) {
    Overlay o;
    o.kind = OV_COLOURS;
    o.title = "RECOLOUR";
    ov_push(o);
    colours_load(&sOv.back());
    sound(1);
}
static void colours_load(Overlay *o) {
    o->loading = true;
    J body = J::obj();
    body.set("spec", sSpec);
    int depth = (int)sOv.size();
    api("/api/colours", &body, [depth](int st, const J &r) {
        if ((int)sOv.size() < depth || sOv[depth - 1].kind != OV_COLOURS) { return; } // (closed meanwhile)
        Overlay &ov = sOv[depth - 1];
        ov.loading = false;
        if (st != 200) { toast(error_of(st, r), true); return; }
        for (SDL_GPUTexture *t : ov.pics) { Ui_TextureFree(t); }
        ov.pics.clear();
        ov.picW.clear();
        ov.picH.clear();
        ov.tex = r;
        for (size_t i = 0; i < r.size(); i++) {
            int w = 0, h = 0;
            ov.pics.push_back(png64_tex(r[i]["png"].text(), &w, &h));
            ov.picW.push_back(w);
            ov.picH.push_back(h);
        }
        if (ov.cur >= (int)r.size()) { ov.cur = 0; }
    }, 300000);
}
static void hsv(float h, float s, float v, int *r, int *g, int *b) {
    float c = v * s, x = c * (1 - fabsf(fmodf(h / 60.0f, 2.0f) - 1)), m = v - c, rr = 0, gg = 0, bb = 0;
    if (h < 60) { rr = c; gg = x; } else if (h < 120) { rr = x; gg = c; } else if (h < 180) { gg = c; bb = x; }
    else if (h < 240) { gg = x; bb = c; } else if (h < 300) { rr = x; bb = c; } else { rr = c; bb = x; }
    *r = (int)((rr + m) * 255 + 0.5f);
    *g = (int)((gg + m) * 255 + 0.5f);
    *b = (int)((bb + m) * 255 + 0.5f);
}
enum { PAL_COLS = 12, PAL_ROWS = 6 };
static unsigned palette(int i) { // 0xRRGGBB: five rows of twelve hues (light to dark), a row of greys
    int r, g, b, row = i / PAL_COLS, col = i % PAL_COLS;
    if (row == PAL_ROWS - 1) {
        r = g = b = 255 - col * 255 / (PAL_COLS - 1);
    } else {
        static const float S[5] = {0.35f, 0.65f, 0.95f, 0.95f, 0.85f}, V[5] = {1.0f, 1.0f, 1.0f, 0.70f, 0.42f};
        hsv((float)col * 30.0f, S[row], V[row], &r, &g, &b);
    }
    return (unsigned)(r << 16 | g << 8 | b);
}
static unsigned hex_rgb(const std::string &h) { return h.size() >= 7 && h[0] == '#' ? (unsigned)strtoul(h.c_str() + 1, NULL, 16) : 0; }
static ImU32 col32(unsigned rgb, int a = 255) { return IM_COL32((rgb >> 16) & 255, (rgb >> 8) & 255, rgb & 255, a); }
static void rgb_hsv(unsigned rgb, float *h, float *s, float *v) {
    float r = ((rgb >> 16) & 255) / 255.0f, g = ((rgb >> 8) & 255) / 255.0f, b = (rgb & 255) / 255.0f;
    float mx = std::max(r, std::max(g, b)), mn = std::min(r, std::min(g, b)), d = mx - mn;
    *v = mx;
    *s = mx > 0.0f ? d / mx : 0.0f;
    float hh = 0.0f;
    if (d > 0.0f) {
        if (mx == r) { hh = 60.0f * fmodf((g - b) / d, 6.0f); }
        else if (mx == g) { hh = 60.0f * ((b - r) / d + 2.0f); }
        else { hh = 60.0f * ((r - g) / d + 4.0f); }
    }
    *h = hh < 0.0f ? hh + 360.0f : hh;
}
static unsigned hsv_rgb(float h, float s, float v) {
    int r, g, b;
    h = fmodf(h, 360.0f);
    hsv(h < 0.0f ? h + 360.0f : h, s < 0 ? 0 : s > 1 ? 1 : s, v < 0 ? 0 : v > 1 ? 1 : v, &r, &g, &b);
    return (unsigned)(r << 16 | g << 8 | b);
}
static std::string hex_of(unsigned rgb) { char b[16]; snprintf(b, sizeof(b), "#%06x", rgb & 0xFFFFFF); return b; }
// the colours used last (the painter, the colour wheel, the palette), newest first
static std::vector<unsigned> sRecent;
static void add_recent(unsigned rgb) {
    sRecent.erase(std::remove(sRecent.begin(), sRecent.end(), rgb), sRecent.end());
    sRecent.insert(sRecent.begin(), rgb);
    if (sRecent.size() > 12) { sRecent.resize(12); }
}
static void open_wheel(const std::string &title, unsigned start, std::function<void(unsigned)> picked);
static void open_workshop(int piece);
static void open_texpick(int texture = -1);

// ---- an attack's colours (the web page's 🎨: /api/fxcolours, the spec's attack_colours, as index.html writes them)
static const char *fx_label(const std::string &what) {
    for (int i = 0; i < 5; i++) { if (what == kSlots[i]) { return kSlotLabel[i]; } }
    return what == "ki_blasts" ? "Ki blasts" : "Ki charge";
}
static int fx_changes(const char *what) { return (int)sSpec["attack_colours"][what].size(); }
static void fx_load(Overlay *o) {
    o->loading = true;
    J body = J::obj();
    body.set("spec", sSpec);
    body.set("what", J::str(o->fx));
    int id = o->id;
    api("/api/fxcolours", &body, [id](int st, const J &r) {
        Overlay *ov = NULL;
        for (Overlay &x : sOv) { if (x.id == id) { ov = &x; } }
        if (ov == NULL) { return; } // (closed meanwhile)
        ov->loading = false;
        if (st != 200) { toast(error_of(st, r), true); return; }
        for (SDL_GPUTexture *t : ov->pics) { Ui_TextureFree(t); }
        ov->pics.clear();
        ov->picW.clear();
        ov->picH.clear();
        ov->tex = r["colours"];
        int w = 0, h = 0;
        SDL_GPUTexture *t = r["png"].text().empty() ? NULL : png64_tex(r["png"].text(), &w, &h);
        ov->pics.push_back(t);
        ov->picW.push_back(w);
        ov->picH.push_back(h);
        if (ov->cur >= (int)ov->tex.size()) { ov->cur = 0; }
    }, 300000);
}
static void open_fx(const std::string &what, const std::string &name = "") {
    Overlay o;
    o.kind = OV_FX;
    o.fx = what;
    o.title = std::string(fx_label(what)) + (name.empty() ? "" : " - " + name) + ": COLOURS";
    for (char &c : o.title) { c = (char)SDL_toupper((unsigned char)c); }
    ov_push(o);
    fx_load(&sOv.back());
    sound(1);
}
static void fx_original(const std::string &what) {
    J *ac = sSpec.find("attack_colours");
    if (ac != NULL) { ac->erase(what.c_str()); }
    clean_empty("attack_colours");
    touch();
}

static void open_palette(int texture, const std::string &from) {
    Overlay o;
    o.kind = OV_PALETTE;
    o.title = "THE NEW COLOUR";
    o.texture = texture;
    o.from = from;
    ov_push(o);
    sound(1);
}

// ---- the forms: where a direction leads
static void pick_form(const char *d) {
    std::string dir = d;
    std::vector<Row> rows;
    Row s;
    s.sec = true;
    s.label = "MY WARRIORS";
    rows.push_back(s);
    int mine = 0;
    for (size_t i = 0; i < sFighters.size(); i++) {
        const J &f = sFighters[i];
        if (!f["spec"].isObj() || f["file"].text() == sFile) { continue; }
        Row r;
        r.label = f["spec"]["name"].text(f["file"].text().c_str());
        r.value = f["spec"]["title"].text();
        r.face = chara_id(f["spec"]["portrait"].nul() ? f["spec"]["look"] : f["spec"]["portrait"]);
        std::string file = f["file"].text();
        r.ok = [dir, file] { ov_close(); sSpec.at("forms").set(dir.c_str(), J::str(file)); touch(); sound(1); };
        rows.push_back(r);
        mine++;
    }
    if (!mine) {
        Row r;
        r.label = "none yet";
        r.value = "save another warrior first";
        r.locked = true;
        rows.push_back(r);
    }
    s.label = "OTHER";
    rows.push_back(s);
    Row n;
    n.label = "A new form";
    n.value = "a new warrior made from this one";
    n.tip = "Your warrior is saved, copied as its new form, and the copy is opened: change its look and its moves.";
    n.ok = [dir] { ov_close(); new_form(dir.c_str()); };
    if (dir != "down") { rows.push_back(n); }
    Row c;
    c.label = "A roster character";
    c.value = "pick one";
    c.ok = [dir] {
        ov_close();
        open_grid("TRANSFORM INTO WHOM?", [dir](const J *r) {
            if (r == NULL) { return; }
            sSpec.at("forms").set(dir.c_str(), J::str("disc:" + std::to_string(chara_id(*r))));
            touch();
        }, false, NULL, true);
    };
    rows.push_back(c);
    Row x;
    x.label = "Nothing";
    x.value = "no transformation this way";
    x.ok = [dir] {
        ov_close();
        J *f = sSpec.find("forms");
        if (f != NULL) { f->erase(dir.c_str()); }
        clean_empty("forms");
        touch();
    };
    rows.push_back(x);
    open_list("TRANSFORM INTO...", rows);
}

// =============================================================================================== the rows of each screen
static Row sec(const char *t) { Row r; r.sec = true; r.label = t; return r; }

static void rows_home(std::vector<Row> &rows) {
    rows.push_back(sec("NEW"));
    Row n;
    n.label = "New Warrior";
    n.value = "Start from any fighter of the roster";
    n.tip = "Pick a body to start from. Everything else can be changed after!";
    n.face = 3;
    n.ok = new_fighter;
    rows.push_back(n);
    if (sHaveBt4) {
        Row b;
        b.label = "Add a BT4 fighter";
        b.value = "A BT4 character with a cell of its own";
        b.tip = "Brings a whole BT4 character (look, moves, voice, portrait) into this game as a new roster character.";
        b.ok = add_bt4;
        rows.push_back(b);
    }
    if (sFighters.size()) { rows.push_back(sec("MY FIGHTERS")); }
    for (size_t i = 0; i < sFighters.size(); i++) {
        const J &f = sFighters[i];
        Row r;
        if (!f["spec"].isObj()) {
            r.label = f["file"].text();
            r.value = "unreadable: " + f["error"].text();
            r.locked = true;
            rows.push_back(r);
            continue;
        }
        const J &sp = f["spec"];
        std::string file = f["file"].text();
        r.label = sp["name"].text(file.c_str());
        J lk = sp.has("look") ? sp["look"] : sp["costumes"].size() ? sp["costumes"][0] : sp["base"];
        r.value = nm(lk, "");
        r.sub = on_roster(file) ? "ON ROSTER" : sp["title"].text();
        r.face = chara_id(sp.has("portrait") ? sp["portrait"] : sp.has("base") ? sp["base"] : lk);
        r.tip = sp["title"].text().empty() ? "Load this warrior and change it." : sp["name"].text() + " - " + sp["title"].text();
        size_t at = i;
        r.ok = [at] { load_fighter(sFighters[at]); };
        rows.push_back(r);
    }
}

static Row stat_row(const char *label, const char *keyName, int lo, int hi, int step, int start, const char *tip, std::string (*fmt)(int)) {
    Row r;
    r.label = label;
    std::string k = keyName;
    r.value = sSpec.has(keyName) ? fmt((int)sSpec[keyName].number()) : "the base's own";
    r.tip = tip;
    r.lrLabel = "Change";
    r.lr = [k, lo, hi, step, start](int d) {
        int v = sSpec.has(k.c_str()) ? (int)sSpec[k.c_str()].number() + d * step : start;
        v = v < lo ? lo : v > hi ? hi : v;
        sSpec.set(k.c_str(), J::num(v));
        touch();
        sound(0);
    };
    r.sqLabel = "Base's own";
    r.sq = [k] { sSpec.erase(k.c_str()); touch(); sound(2); };
    return r;
}
static std::string fmt_health(int v) { char b[32]; snprintf(b, sizeof(b), "%.1f bars", v / 10000.0); return b; }
static std::string fmt_ki(int v) { return std::to_string(v / 1000) + "%"; }
static std::string fmt_blast(int v) { return std::to_string(v) + " stocks"; }

// ---- the WARRIOR tab (as the trailer's: WARRIOR, LOOK, PORTRAITS, COSTUMES, BATTLE DAMAGE, STATS)
static const char *number_word(int n) {
    static const char *const k[] = {"no", "one", "two", "three", "four", "five", "six", "seven", "eight", "nine", "ten",
                                    "eleven", "twelve", "thirteen", "fourteen", "fifteen", "sixteen"};
    return n >= 0 && n <= 16 ? k[n] : "many";
}
static int costume_count(void) { return sSpec["costumes"].size() ? (int)sSpec["costumes"].size() : 1; }
static J costume_look(int k) { return sSpec["costumes"].size() > (size_t)k ? sSpec["costumes"][(size_t)k] : look(); }
static void set_costume_look(int k, const J &ref) { // the whole look of costume k (0: also the warrior's "look")
    if (sSpec["costumes"].size()) {
        J a = sSpec["costumes"];
        if ((size_t)k < a.size()) { a.a[(size_t)k] = ref; } else { a.push(ref); }
        sSpec.set("costumes", a);
        if (k == 0) { sSpec.set("look", ref); }
    } else {
        sSpec.set("look", ref);
    }
    touch();
}
static void set_costume_count(int n) { // 1..16: costumes added are a copy of the last one; taken off from the end
    int have = costume_count();
    n = n < 1 ? 1 : n > 16 ? 16 : n;
    if (n == have) { return; }
    J a = J::arr();
    for (int i = 0; i < n; i++) { a.push(costume_look(i < have ? i : have - 1)); }
    if (n == 1) { sSpec.erase("costumes"); sSpec.set("look", a[0]); } else { sSpec.set("costumes", a); sSpec.set("look", a[0]); }
    if (sShownCostume >= n) { set_editing_costume(n - 1); }
    touch();
}

static void rows_warrior(std::vector<Row> &rows) {
    rows.push_back(sec("WARRIOR"));
    Row r;
    r.label = "Name";
    r.value = sSpec["name"].text().empty() ? "(press to type a name)" : sSpec["name"].text();
    r.tip = "Every great warrior needs a great name!";
    r.ok = [] { open_keys("YOUR WARRIOR'S NAME", sSpec["name"].text(), 63, [](const std::string &t) { sSpec.set("name", J::str(t)); touch(); }); };
    rows.push_back(r);
    r = Row();
    r.label = "Title";
    r.value = sSpec["title"].text().empty() ? "none" : sSpec["title"].text();
    r.tip = "The line under the name on the roster: \"Super Saiyan\", \"Ultimate Form\"...";
    r.ok = [] { open_keys("THE TITLE UNDER THE NAME", sSpec["title"].text(), 95, [](const std::string &t) { sSpec.set("title", J::str(t)); touch(); }); };
    rows.push_back(r);

    rows.push_back(sec("LOOK"));
    r = Row();
    int k = sShownCostume < costume_count() ? sShownCostume : 0;
    r.label = "Body";
    r.value = nm(costume_look(k), "?");
    r.face = chara_id(costume_look(k));
    r.tip = costume_count() > 1 ? "Whose body costume " + std::to_string(k + 1) + " fights in: hair, face and clothes. Layer pieces on top in PARTS."
                                : std::string("Whose body your warrior fights in: hair, face and clothes. Layer pieces on top in PARTS.");
    r.ok = [k] { open_grid(k ? "COSTUME " + std::to_string(k + 1) + ": WHOSE BODY?" : std::string("BODY: WHOSE?"), [k](const J *p) { if (p) { set_costume_look(k, *p); } }, true); };
    rows.push_back(r);
    r = Row();
    r.label = "Stance";
    r.value = nm(moveset(), "?");
    r.face = chara_id(moveset());
    r.tip = "Whose stance and animations your warrior fights with (the moves you do not change are theirs too).";
    // (the moveset's donor: its stance, its animations; the MOVES tab's Moveset row is the same choice)
    r.ok = [] {
        open_grid("STANCE: WHOSE?", [](const J *p) {
            if (!p) { return; }
            J m = J::obj();
            m.set("chara", (*p)["chara"]);
            sSpec.set("moveset", m);
            touch();
        });
    };
    rows.push_back(r);
    r = Row();
    r.label = "Stance maker";
    {
        const J &pose = sSpec["stance_pose"];
        int n = pose.isObj() || pose.isArr() ? (int)pose.size() : 0;
        if (pose.isObj() && (pose["bones"].isObj() || pose["bones"].isArr())) { n = (int)pose["bones"].size(); } // (as the stance maker keeps it)
        r.value = n ? std::to_string(n) + (n == 1 ? " bone posed" : " bones posed") : "Not posed";
    }
    r.tip = "Grab the dots and pose your warrior right on the model, then fight in that stance.";
    r.ok = [] { open_stance_maker(); };
    rows.push_back(r);
    r = Row();
    r.label = "Voice";
    r.value = nm(sSpec["voice"], "The base's");
    r.face = chara_id(sSpec["voice"]);
    r.tip = "Shouts, callouts, intros and wins, with the mouth moving to match.";
    r.ok = [] {
        open_grid("VOICE: WHOSE?", [](const J *p) { if (p) { sSpec.set("voice", *p); } else { sSpec.erase("voice"); } touch(); }, false, "Default");
    };
    r.sqLabel = "Default";
    r.sq = [] { sSpec.erase("voice"); touch(); sound(2); };
    rows.push_back(r);

    rows.push_back(sec("PORTRAITS"));
    r = Row();
    r.label = "Render";
    r.value = sSpec.has("portrait_image") ? "Photo of the model" : nm(sSpec["portrait"], "The base's");
    r.face = sSpec.has("portrait_image") ? -1 : chara_id(sSpec["portrait"]);
    r.tip = "The big picture of the menus and the character select. Triangle: a photo of your warrior as the model shows it now.";
    r.ok = [] {
        open_grid("RENDER: WHOSE?", [](const J *p) {
            sSpec.erase("portrait_image");
            if (p) { sSpec.set("portrait", *p); } else { sSpec.erase("portrait"); }
            touch();
        }, false, "Default");
    };
    r.sqLabel = "Default";
    r.sq = [] { sSpec.erase("portrait_image"); sSpec.erase("portrait"); touch(); sound(2); };
    r.triLabel = "Photo of the model";
    r.tri = photo_of_model;
    rows.push_back(r);
    r = Row();
    r.label = "Small portrait";
    r.value = sSpec.has("chip") ? nm(sSpec["chip"], "?") : "Automatic";
    r.face = chara_id(sSpec["chip"]);
    r.tip = "The small square of the roster. Automatic: made from the render (or its character's own).";
    r.ok = [] { open_grid("SMALL PORTRAIT: WHOSE?", [](const J *p) { if (p) { sSpec.set("chip", *p); } else { sSpec.erase("chip"); } touch(); }, false, "Automatic"); };
    r.sqLabel = "Automatic";
    r.sq = [] { sSpec.erase("chip"); touch(); sound(2); };
    r.triLabel = "Photo of the model";
    r.tri = [] { sSpec.erase("chip"); photo_of_model(); };
    rows.push_back(r);
    r = Row();
    r.label = "HUD face";
    r.value = sSpec.has("hud") ? nm(sSpec["hud"], "?") : "The body's own";
    r.face = chara_id(sSpec["hud"]);
    r.tip = "The face beside the health bar in battle.";
    r.ok = [] { open_grid("HUD FACE: WHOSE?", [](const J *p) { if (p) { sSpec.set("hud", *p); } else { sSpec.erase("hud"); } touch(); }, false, "Default"); };
    r.sqLabel = "Default";
    r.sq = [] { sSpec.erase("hud"); touch(); sound(2); };
    rows.push_back(r);

    rows.push_back(sec("COSTUMES"));
    int n = costume_count();
    r = Row();
    r.label = "Costumes";
    r.value = n > 1 ? std::string("On: ") + number_word(n) + " outfits" : std::string("Off: one outfit");
    r.tip = "Up to sixteen outfits, each one built on its own. Left / right: how many; cross: on / off.";
    r.lrLabel = "How many";
    r.lr = [](int d) { set_costume_count(costume_count() + d); sound(0); };
    r.ok = [n] {
        if (n > 1) { confirm("KEEP ONLY COSTUME 1?", [] { set_costume_count(1); }); }
        else { set_costume_count(2); toast("Costume 2 is a copy of costume 1: give it a look of its own!"); }
    };
    rows.push_back(r);
    r = Row();
    r.label = "Editing";
    r.value = "Costume " + std::to_string(k + 1) + " of " + std::to_string(n);
    r.tip = "The costume the model shows and your changes go to. Left / right: another one.";
    if (n > 1) {
        r.lrLabel = "Costume";
        r.lr = [n](int d) { set_editing_costume((sShownCostume + d + n) % n); sound(0); };
        r.ok = [n] {
            std::vector<Row> list;
            for (int i = 0; i < n; i++) {
                Row c;
                c.label = "Costume " + std::to_string(i + 1);
                c.value = nm(costume_look(i), "?");
                c.face = chara_id(costume_look(i));
                c.ok = [i] { ov_close(); set_editing_costume(i); };
                list.push_back(c);
            }
            open_list("EDIT WHICH COSTUME?", list);
            if (!sOv.empty()) { sOv.back().cur = sShownCostume < n ? sShownCostume : 0; }
        };
    } else {
        r.locked = true;
    }
    rows.push_back(r);

    rows.push_back(sec("BATTLE DAMAGE"));
    r = Row();
    r.label = "Damaged look";
    const J &dm = sSpec["damaged"];
    r.value = dm.isStr() && dm.s == "none" ? std::string("None: stays clean") : dm.isObj() ? nm(dm, "?") + "'s" : std::string("The fighter's own");
    r.face = dm.isObj() ? chara_id(dm) : -1;
    r.tip = "How your warrior looks torn up late in a fight: its own clothes torn, always clean, or another fighter's.";
    r.ok = [] {
        std::vector<Row> list(3);
        list[0].label = "The fighter's own";
        list[0].value = "its own clothes, torn";
        list[0].ok = [] { ov_close(); sSpec.erase("damaged"); touch(); };
        list[1].label = "None";
        list[1].value = "stays clean";
        list[1].ok = [] { ov_close(); sSpec.set("damaged", J::str("none")); touch(); };
        list[2].label = "Another fighter's";
        list[2].value = "pick one";
        list[2].ok = [] { ov_close(); open_grid("DAMAGED LOOK: WHOSE?", [](const J *p) { if (p) { sSpec.set("damaged", *p); touch(); } }, true); };
        open_list("DAMAGED LOOK", list);
    };
    r.sqLabel = "Default";
    r.sq = [] { sSpec.erase("damaged"); touch(); sound(2); };
    rows.push_back(r);

    rows.push_back(sec("STATS"));
    rows.push_back(stat_row("Health", "health", 10000, 150000, 5000, 50000, "Health: 10000 is one bar. Left / right change it, square: the base's own.", fmt_health));
    rows.push_back(stat_row("Ki at start", "ki", 0, 100000, 5000, 20000, "Ki at the start of a fight. Left / right change it, square: the base's own.", fmt_ki));
    rows.push_back(stat_row("Blast stocks", "blast", 0, 7, 1, 5, "How many blast stocks. Left / right change it, square: the base's own.", fmt_blast));
    r = Row();
    r.label = "Fights like";
    r.value = nm(sSpec["base"], "The body's");
    r.face = chara_id(sSpec["base"]);
    r.tip = "The computer plays your warrior like this character (and the roster's tables are its).";
    r.ok = [] { open_grid("FIGHTING SPIRIT: WHOSE?", [](const J *p) { if (p) { sSpec.set("base", *p); touch(); } }, false, NULL, true); };
    rows.push_back(r);
}

static const char *const kPieces[8][3] = {
    {"head", "Whole head", "Neck, head and face, with its mouth shapes and eyes."},
    {"hair", "Hair", "The hanging hair locks (the scalp is part of the head)."},
    {"torso", "Torso", "Chest, belt and shoulders, and capes or sashes hanging on them."},
    {"arms", "Arms", "Upper arms and forearms."},
    {"hands", "Hands", "Hands and gloves."},
    {"legs", "Legs", "Thighs and shins."},
    {"feet", "Feet", "Feet and boots."},
    {"tail", "Tail", "A tail (only some characters have one)."}};

static std::vector<J> layers(const char *k) {
    std::vector<J> out;
    const J &v = sSpec["parts"][k];
    if (v.isArr()) { for (size_t i = 0; i < v.size(); i++) { out.push_back(v[i]); } }
    else if (v.isObj()) { out.push_back(v); }
    return out;
}
static void set_layers(const char *k, const std::vector<J> &list) {
    J &parts = sSpec.at("parts");
    if (list.empty()) { parts.erase(k); }
    else if (list.size() == 1) { parts.set(k, list[0]); }
    else { J a = J::arr(); for (const J &l : list) { a.push(l); } parts.set(k, a); }
    clean_empty("parts");
    touch();
}

static void rows_parts(std::vector<Row> &rows) {
    rows.push_back(sec("WHOLE LOOK"));
    std::vector<J> cos;
    if (sSpec["costumes"].size()) { for (size_t i = 0; i < sSpec["costumes"].size(); i++) { cos.push_back(sSpec["costumes"][i]); } }
    else { cos.push_back(look()); }
    int shownRows = (int)cos.size() + 1; // the costumes there are and one more (4 at least, up to 16)
    shownRows = shownRows < 4 ? 4 : shownRows > 16 ? 16 : shownRows;
    for (int i = 0; i < shownRows; i++) {
        Row r;
        r.label = "Costume " + std::to_string(i + 1);
        r.value = i < (int)cos.size() ? nm(cos[i], "?") : "none";
        r.sub = i == 0 ? "the look of your warrior" : i == sShownCostume && i < (int)cos.size() ? "shown" : "";
        r.face = i < (int)cos.size() ? chara_id(cos[i]) : -1;
        r.tip = "Body, hair and clothes of a whole character. A warrior can have up to 16 costumes (the WARRIOR tab's Editing row shows them).";
        r.ok = [i] {
            open_grid("COSTUME " + std::to_string(i + 1) + ": WHOSE LOOK?", [i](const J *p) {
                std::vector<J> list;
                if (sSpec["costumes"].size()) { for (size_t k = 0; k < sSpec["costumes"].size(); k++) { list.push_back(sSpec["costumes"][k]); } }
                else { list.push_back(look()); }
                if (p) { if (i < (int)list.size()) { list[i] = *p; } else { list.push_back(*p); } }
                else if (i < (int)list.size()) { list.erase(list.begin() + i); }
                if (list.empty()) { return; }
                J a = J::arr();
                for (const J &l : list) { a.push(l); }
                sSpec.set("costumes", a);
                sSpec.set("look", list[0]);
                if (sShownCostume >= (int)list.size()) { sShownCostume = 0; }
                touch();
            }, true, i ? "No costume" : NULL);
        };
        rows.push_back(r);
    }
    rows.push_back(sec("MIX PIECES (on top of costume 1)"));
    for (int i = 0; i < 8; i++) {
        const char *k = kPieces[i][0];
        std::vector<J> ls = layers(k);
        Row r;
        r.label = kPieces[i][1];
        if (ls.empty()) { r.value = "The body's own"; }
        else {
            r.value = ls[0]["add"].truthy() ? "The body's own + " : "";
            for (size_t l = 0; l < ls.size(); l++) { r.value += (l ? " + " : "") + nm(ls[l], "?"); }
        }
        r.face = ls.empty() ? -1 : chara_id(ls.back());
        r.tip = std::string(kPieces[i][2]) + " Square layers another one on top, triangle takes the top one off.";
        std::string key = k, label = kPieces[i][1];
        for (char &c : label) { c = (char)SDL_toupper((unsigned char)c); }
        r.ok = [key, label] {
            open_grid(label + ": WHOSE?", [key](const J *p) {
                std::vector<J> list;
                if (p) { list.push_back(*p); }
                set_layers(key.c_str(), list);
            }, true, "The body's own");
        };
        r.sqLabel = "+ Layer";
        r.sq = [key, label] {
            open_grid(label + ": LAYER ANOTHER ON TOP", [key](const J *p) {
                if (!p) { return; }
                std::vector<J> list = layers(key.c_str());
                J l = *p;
                l.set("add", J::boolean(true));
                list.push_back(l);
                set_layers(key.c_str(), list);
            }, true);
        };
        if (!ls.empty()) {
            r.triLabel = "Take off";
            r.tri = [key] {
                std::vector<J> list = layers(key.c_str());
                list.pop_back();
                set_layers(key.c_str(), list);
                sound(2);
            };
        }
        rows.push_back(r);
    }
    // the workshop (the web page's 🔧): move, turn and stretch a piece's layers or the body's own parts, and its build
    rows.push_back(sec("WORKSHOP: MOVE, TURN, STRETCH"));
    {
        Row w;
        w.label = "Body";
        int nb = (int)sSpec["body"].size();
        w.value = nb ? "Build and parts changed" : "Build, and its own parts";
        w.sub = "build: torso, arms, legs, head";
        w.tip = "Make your warrior stockier or slimmer, a bigger head... and move, turn or stretch any part of its body.";
        w.ok = [] { open_workshop(-1); };
        rows.push_back(w);
    }
    for (int i = 0; i < 8; i++) {
        std::vector<J> ls = layers(kPieces[i][0]);
        if (ls.empty()) { continue; }
        Row w;
        w.label = std::string(kPieces[i][1]);
        bool changed = false;
        for (const J &l : ls) { changed |= l["adjust"].size() > 0; }
        w.value = (changed ? "Changed: " : "Place it: ") + nm(ls.back(), "?") + (ls.size() > 1 ? " (+" + std::to_string(ls.size() - 1) + ")" : "");
        w.face = chara_id(ls.back());
        w.tip = "Move it, turn it, stretch it! Your warrior changes as you go.";
        w.ok = [i] { open_workshop(i); };
        rows.push_back(w);
    }
    rows.push_back(sec("COLOURS"));
    int nc = (int)sSpec["colours"].size();
    Row c;
    c.label = "Recolour";
    c.value = nc ? std::to_string(nc) + (nc > 1 ? " colour changes" : " colour change") : "Original colours";
    c.tip = "Pick a texture of your warrior, then a colour of it, then its new colour.";
    c.ok = open_colours;
    if (nc) {
        c.sqLabel = "All original";
        c.sq = [] { sSpec.erase("colours"); touch(); sound(2); };
    }
    rows.push_back(c);
    int np = (int)sSpec["paint"].size();
    Row p;
    p.label = "Paint texture...";
    p.value = np ? std::to_string(np) + (np > 1 ? " textures painted" : " texture painted") : "Paint on a texture";
    p.sub = "brush, fill, colour wheel";
    p.tip = "Bring a texture up and paint on it: aim with the crosshair, fill spots, pick colours from it.";
    p.ok = [] { open_texpick(); };
    if (np) {
        p.sqLabel = "Remove all paint";
        p.sq = [] { confirm("REMOVE ALL THE PAINT?", [] { sSpec.erase("paint"); touch(); sound(2); }); };
    }
    rows.push_back(p);
}

static void rows_moves(std::vector<Row> &rows) {
    rows.push_back(sec("FIGHTING"));
    Row r;
    r.label = "Moveset";
    r.value = nm(moveset(), "?");
    r.sub = "everything below starts from it";
    r.face = chara_id(moveset());
    r.tip = "Stance, animations, stats and the techniques you do not change.";
    r.ok = [] {
        open_grid("WHOSE MOVESET?", [](const J *p) {
            if (!p) { return; }
            J m = J::obj();
            m.set("chara", (*p)["chara"]);
            sSpec.set("moveset", m);
            touch();
        });
    };
    rows.push_back(r);
    static const char *const kDonor[2][3] = {{"Melee & rush", "melee", "Combos, rushes and finishers."},
                                             {"Ki blasts", "ki_blasts", "The ki blasts and charged shots."}};
    for (int i = 0; i < 2; i++) {
        Row d;
        d.label = kDonor[i][0];
        d.value = nm(sSpec[kDonor[i][1]], "the moveset's");
        d.face = chara_id(sSpec[kDonor[i][1]]);
        d.tip = kDonor[i][2];
        std::string key = kDonor[i][1], label = kDonor[i][0];
        for (char &c : label) { c = (char)SDL_toupper((unsigned char)c); }
        d.ok = [key, label] {
            open_grid(label + ": FROM WHOM?", [key](const J *p) {
                if (p) { J m = J::obj(); m.set("chara", (*p)["chara"]); sSpec.set(key.c_str(), m); } else { sSpec.erase(key.c_str()); }
                touch();
            }, false, "Moveset's own");
        };
        rows.push_back(d);
    }
    static const char *const kGroups[3] = {"BLAST 1", "BLAST 2", "ULTIMATE"};
    for (int k = 0; k < 5; k++) {
        if (k == 0 || k == 2 || k == 4) { rows.push_back(sec(kGroups[k / 2])); }
        MoveOf m = move_of(k);
        bool renamed = sSpec["names"][kSlots[k]].isStr();
        Row s;
        s.label = kSlotLabel[k];
        s.value = m.shown;
        s.sub = renamed ? "(" + m.name + ", " + nm(m.who, "?") + ")" : "from " + nm(m.who, "?");
        s.face = chara_id(m.who);
        s.tip = "Whose technique should your warrior learn? Triangle renames it.";
        std::vector<int> kinds;
        if (k < 2) { kinds = {0, 1}; } else if (k < 4) { kinds = {2, 3}; } else { kinds = {4}; }
        std::string title = std::string(kSlotLabel[k]) + ": WHOSE TECHNIQUE?";
        for (char &c : title) { c = (char)SDL_toupper((unsigned char)c); }
        s.ok = [k, kinds, title] {
            open_grid(title, [k](const J *p) {
                if (p) {
                    sSpec.at("moves").set(kSlots[k], *p);
                    toast(move_name(*p, slot_index((*p)["slot"].text(kSlots[k]))) + "! Good choice!");
                } else {
                    J *mv = sSpec.find("moves");
                    if (mv) { mv->erase(kSlots[k]); }
                    clean_empty("moves");
                    toast("Back to the moveset's own.");
                }
                touch();
            }, false, "Moveset's own", false, 0, kinds);
        };
        int nfx = fx_changes(kSlots[k]);
        if (nfx) { s.sub = std::to_string(nfx) + (nfx > 1 ? " colours changed - " : " colour changed - ") + s.sub; }
        s.sqLabel = "Colours";
        std::string shown = m.shown;
        s.sq = [k, shown] { open_fx(kSlots[k], shown); };
        s.tip = "Whose technique should your warrior learn? Triangle renames it, square recolours it.";
        s.triLabel = "Rename";
        std::string own = m.name;
        s.tri = [k, own] {
            open_keys("NAME YOUR TECHNIQUE! (empty: " + own + ")", sSpec["names"][kSlots[k]].text(), 40, [k](const std::string &t) {
                if (!t.empty()) { sSpec.at("names").set(kSlots[k], J::str(t)); }
                else { J *n = sSpec.find("names"); if (n) { n->erase(kSlots[k]); } clean_empty("names"); }
                touch();
            });
        };
        rows.push_back(s);
    }
    rows.push_back(sec("ATTACK COLOURS"));
    Row sum;
    sum.label = "Recoloured";
    for (int k = 0; k < 7; k++) {
        const char *what = k < 5 ? kSlots[k] : k == 5 ? "ki_blasts" : "charge";
        if (fx_changes(what)) { sum.value += (sum.value.empty() ? "" : ", ") + (k < 5 ? move_of(k).shown : std::string(fx_label(what))); }
    }
    if (sum.value.empty()) { sum.value = "nothing yet: square on a technique recolours it"; }
    sum.locked = true;
    rows.push_back(sum);
    static const char *const kFx[2][3] = {{"ki_blasts", "Ki blasts", "The ki blasts and their charge glow."},
                                          {"charge", "Ki charge", "The glow and sparks when charging ki (the aura flames are the game's own)."}};
    for (int i = 0; i < 2; i++) {
        Row r;
        r.label = kFx[i][1];
        int nfx = fx_changes(kFx[i][0]);
        r.value = nfx ? std::to_string(nfx) + (nfx > 1 ? " colour changes" : " colour change") : "Original colours";
        r.tip = kFx[i][2];
        std::string what = kFx[i][0];
        r.ok = [what] { open_fx(what); };
        if (nfx) {
            r.sqLabel = "Original colours";
            r.sq = [what] { fx_original(what); sound(2); };
        }
        rows.push_back(r);
    }
}

static void rows_forms(std::vector<Row> &rows) {
    static const char *const kDirs[4][2] = {{"up", "R3 + Up"}, {"left", "R3 + Left"}, {"right", "R3 + Right"}, {"down", "R3 + Down (back)"}};
    rows.push_back(sec("TRANSFORMATIONS"));
    for (int i = 0; i < 4; i++) {
        const char *d = kDirs[i][0];
        std::string t = sSpec["forms"][d].text();
        Row r;
        r.label = kDirs[i][1];
        if (t.empty()) { r.value = "none"; }
        else if (t.compare(0, 5, "disc:") == 0) {
            J id = J::num(atoi(t.c_str() + 5));
            const J *c = entry_of(id);
            r.value = c != NULL ? (*c)["name"].text() : t;
            r.face = atoi(t.c_str() + 5);
        } else {
            r.value = fighter_name(t);
        }
        int cost = (int)sSpec["form_costs"][d].number(0);
        if (!t.empty() && i < 3) {
            r.sub = "costs " + std::to_string(cost) + " blast stocks";
            std::string dir = d;
            r.lrLabel = "Cost";
            r.lr = [dir](int k) {
                int v = (int)sSpec["form_costs"][dir.c_str()].number(0) + k;
                v = v < 0 ? 0 : v > 7 ? 7 : v;
                sSpec.at("form_costs").set(dir.c_str(), J::num(v));
                touch();
                sound(0);
            };
        }
        r.tip = "Transform into another of your warriors, a new form of this one, or a roster character.";
        std::string dir = d;
        r.ok = [dir] { pick_form(dir.c_str()); };
        rows.push_back(r);
    }
}

static void rows_finish(std::vector<Row> &rows) {
    rows.push_back(sec("SAVE"));
    Row r;
    r.label = "Save";
    r.value = sFile.empty() ? "not saved yet" : "custom_fighters/" + sFile;
    r.sub = sDirty ? "NOT SAVED" : "";
    r.tip = "Keep your warrior. (START saves on any tab.)";
    r.ok = [] { save(); };
    rows.push_back(r);
    rows.push_back(sec("ROSTER"));
    bool on = on_roster(sFile);
    r = Row();
    r.label = "Add to roster";
    r.value = on ? "On the roster: update it" : "Give my warrior a cell of its own";
    r.tip = "Your warrior gets its own cell at the end of the character select, with its forms. No restart needed!";
    r.ok = [] { act("package"); };
    rows.push_back(r);
    r = Row();
    r.label = "Test fight";
    r.value = on ? "Fight with it in training now" : "add it to the roster first";
    r.locked = !on;
    r.tip = "A training fight with your warrior (its pause menu brings you back here).";
    r.ok = test_fight;
    rows.push_back(r);
    if (on) {
        r = Row();
        r.label = "Take off roster";
        r.value = "Remove its cell";
        r.ok = [] { act("unpackage"); };
        rows.push_back(r);
    }
    rows.push_back(sec("MORE"));
    r = Row();
    r.label = "Summary";
    r.value = "What your warrior is made of";
    r.ok = [] {
        J body = J::obj();
        body.set("spec", sSpec);
        api("/api/describe", &body, [](int st, const J &res) {
            if (st != 200) { toast(error_of(st, res), true); return; }
            open_text("SUMMARY", res["text"].text());
        });
    };
    rows.push_back(r);
    if (!sFile.empty()) {
        r = Row();
        r.label = "Delete";
        r.value = "Delete this warrior's file";
        r.ok = [] {
            confirm("DELETE " + sSpec["name"].text() + "?", [] {
                J body = J::obj();
                body.set("file", J::str(sFile));
                api("/api/delete", &body, [](int st, const J &res) {
                    if (st != 200) { toast(error_of(st, res), true); return; }
                    refresh_fighters();
                    refresh_roster_info();
                    reload_game();
                    sScreen = SC_HOME;
                    toast("Deleted.");
                });
            });
        };
        rows.push_back(r);
    }
    r = Row();
    r.label = "My Fighters";
    r.value = "Back to the list";
    r.ok = [] {
        if (sDirty) { confirm("LEAVE WITHOUT SAVING?", [] { sScreen = SC_HOME; }); } else { sScreen = SC_HOME; }
    };
    rows.push_back(r);
}

// =============================================================================================== drawing
// The screen's places are those of the Mod Manager's mockups (1376 x 768), spread over the game's picture as its
// drawing spreads them (port_mmkit.c), so the list beside the model stays where the model is; sizes keep their shape.
static float sX0, sY0, sW, sH, sS;
static ImVec2 P(float x, float y) { return ImVec2(sX0 + x * sW / 1376.0f, sY0 + y * sH / 768.0f); }
static float S(float l) { return l * sS; }
static ImFont *sFont, *sBold;
static ImDrawList *sDl;

static const ImU32 kInk = IM_COL32(255, 255, 255, 255), kDim = IM_COL32(201, 184, 240, 255), kDark = IM_COL32(58, 18, 0, 255);
static const ImU32 kGold1 = IM_COL32(255, 211, 77, 255), kGold2 = IM_COL32(243, 156, 18, 255);

static void grad(ImVec2 a, ImVec2 b, ImU32 top, ImU32 bottom, float round) {
    int v0 = sDl->VtxBuffer.Size;
    sDl->AddRectFilled(a, b, IM_COL32(255, 255, 255, (top >> IM_COL32_A_SHIFT) & 0xFF), round);
    ImGui::ShadeVertsLinearColorGradientKeepAlpha(sDl, v0, sDl->VtxBuffer.Size, a, ImVec2(a.x, b.y), top, bottom);
}
static float text_w(ImFont *f, float size, const char *t) { return f->CalcTextSizeA(size, FLT_MAX, 0.0f, t).x; }
static void text(ImFont *f, float size, ImVec2 at, ImU32 c, const std::string &t, float maxW = 0.0f) {
    std::string s = t;
    if (maxW > 0.0f && text_w(f, size, s.c_str()) > maxW) { // (cut, with an ellipsis)
        while (!s.empty() && text_w(f, size, (s + "...").c_str()) > maxW) {
            s.pop_back();
            while (!s.empty() && ((unsigned char)s.back() & 0xC0) == 0x80) { s.pop_back(); }
        }
        s += "...";
    }
    sDl->AddText(f, size, at, c, s.c_str());
}
static void outlined(ImFont *f, float size, ImVec2 at, ImU32 c, ImU32 edge, const std::string &t, float w = 2.0f) {
    for (int dx = -1; dx <= 1; dx++) {
        for (int dy = -1; dy <= 1; dy++) {
            if (dx || dy) { sDl->AddText(f, size, ImVec2(at.x + dx * w, at.y + dy * w), edge, t.c_str()); }
        }
    }
    sDl->AddText(f, size, at, c, t.c_str());
}
static ImFont *bold_font(void) { return sBold != NULL ? sBold : sFont; }
// the list's panel (and every box of the screen): translucent purple, faint stripes, a light rim (creator_look.cpp)
static void panel(ImVec2 a, ImVec2 b) { zlook::panel(sDl, a, b, S(14), sS); }

static void face_at(int id, ImVec2 a, ImVec2 b, float round) {
    ImTextureID t = face_tex(id);
    sDl->AddRectFilled(a, b, IM_COL32(255, 255, 255, 22), round);
    if (t) { sDl->AddImageRounded(ImTextureRef(t), a, b, ImVec2(0, 0), ImVec2(1, 1), IM_COL32_WHITE, round); }
}

// a button of the pad: ✕ ○ □ △ in their colours, the shoulder buttons and START as plates; the keyboard's key
enum { B_CROSS, B_CIRCLE, B_SQUARE, B_TRIANGLE, B_L1R1, B_L2R2, B_START, B_DPAD, B_LR, B_RSTICK, B_R3, B_L3, B_LSTICK };
static float glyph(int b, ImVec2 c, float r) { // -> its width
    static const char *const kKeys[] = {"Enter", "Esc", "F", "R", "Q/E", "Z/C", "Ctrl+S", "Arrows", "Left/Right", "+/- Wheel", "Right drag", "V", "Mouse"};
    if (sKeyboardLast) {
        const char *k = kKeys[b];
        float tw = text_w(sFont, r * 1.1f, k), w = tw + r * 1.2f;
        sDl->AddRectFilled(ImVec2(c.x - r * 0.0f, c.y - r), ImVec2(c.x + w, c.y + r), IM_COL32(42, 26, 85, 255), r * 0.35f);
        sDl->AddRect(ImVec2(c.x, c.y - r), ImVec2(c.x + w, c.y + r), IM_COL32(205, 189, 255, 255), r * 0.35f, 0, S(1.5f));
        sDl->AddText(sFont, r * 1.1f, ImVec2(c.x + r * 0.6f, c.y - r * 0.62f), kInk, k);
        return w;
    }
    if (b >= B_L1R1) {
        const char *k = b == B_L1R1 ? "L1 R1" : b == B_L2R2 ? "L2 R2" : b == B_START ? "START" : b == B_DPAD ? "D-PAD" : b == B_RSTICK ? "R-STICK"
                        : b == B_R3 ? "R3 + R" : b == B_L3 ? "L3" : b == B_LSTICK ? "L-STICK" : "< >";
        float tw = text_w(sFont, r * 1.0f, k), w = tw + r * 1.2f;
        sDl->AddRectFilled(ImVec2(c.x, c.y - r * 0.85f), ImVec2(c.x + w, c.y + r * 0.85f), IM_COL32(42, 26, 85, 255), r * 0.8f);
        sDl->AddRect(ImVec2(c.x, c.y - r * 0.85f), ImVec2(c.x + w, c.y + r * 0.85f), IM_COL32(205, 189, 255, 255), r * 0.8f, 0, S(1.5f));
        sDl->AddText(sFont, r * 1.0f, ImVec2(c.x + r * 0.6f, c.y - r * 0.56f), kInk, k);
        return w;
    }
    ImVec2 m(c.x + r, c.y);
    sDl->AddCircleFilled(m, r, IM_COL32(42, 26, 85, 255));
    sDl->AddCircle(m, r, IM_COL32(205, 189, 255, 255), 0, S(1.5f));
    float k = r * 0.45f, t = S(2.2f);
    if (b == B_CROSS) {
        ImU32 col = IM_COL32(140, 180, 255, 255);
        sDl->AddLine(ImVec2(m.x - k, m.y - k), ImVec2(m.x + k, m.y + k), col, t);
        sDl->AddLine(ImVec2(m.x - k, m.y + k), ImVec2(m.x + k, m.y - k), col, t);
    } else if (b == B_CIRCLE) {
        sDl->AddCircle(m, k * 1.05f, IM_COL32(255, 110, 110, 255), 0, t);
    } else if (b == B_SQUARE) {
        sDl->AddRect(ImVec2(m.x - k, m.y - k), ImVec2(m.x + k, m.y + k), IM_COL32(255, 150, 210, 255), 0, 0, t);
    } else {
        sDl->AddTriangle(ImVec2(m.x, m.y - k * 1.1f), ImVec2(m.x + k * 1.1f, m.y + k * 0.8f), ImVec2(m.x - k * 1.1f, m.y + k * 0.8f), IM_COL32(100, 230, 160, 255), t);
    }
    return r * 2.0f;
}
struct Prompt { int b; std::string label; };
// The buttons of what is up, in the tip box's bottom right corner.
static void prompts(const std::vector<Prompt> &ps) {
    float r = S(10), size = S(15), gap = S(18);
    float total = 0;
    for (const Prompt &p : ps) {
        float gw = p.b >= B_L1R1 || sKeyboardLast ? text_w(sFont, r * 1.1f, "L1 R1") + r * 1.2f : r * 2.0f;
        total += gw + S(6) + text_w(bold_font(), size, p.label.c_str()) + gap;
    }
    float room = P(1284, 0).x - P(560, 0).x;
    if (total > room && total > 0) { float k = room / total; size *= std::max(k, 0.75f); gap *= std::max(k, 0.6f); }
    ImVec2 at = P(1280, 721);
    float x = at.x - total;
    if (x < P(220, 0).x) { x = P(220, 0).x; }
    for (const Prompt &p : ps) {
        float w = glyph(p.b, ImVec2(x, at.y), r);
        x += w + S(6);
        outlined(bold_font(), size, ImVec2(x, at.y - size * 0.55f), IM_COL32(236, 238, 255, 255), IM_COL32(6, 10, 30, 255), p.label, S(1.0f));
        x += text_w(bold_font(), size, p.label.c_str()) + gap;
    }
}

// The tip box: a dark navy box with white corner brackets, the host's round face, its line (a toast in its place).
static void tip_box(const std::string &line) {
    ImVec2 a = P(100, 632), b = P(1297, 744);
    sDl->AddRectFilled(ImVec2(a.x + S(3), a.y + S(5)), ImVec2(b.x + S(3), b.y + S(5)), IM_COL32(0, 0, 10, 90), S(6));
    grad(a, b, IM_COL32(24, 34, 84, 238), IM_COL32(10, 14, 46, 246), S(6));
    sDl->AddRect(a, b, IM_COL32(110, 130, 220, 140), S(6), 0, S(1.5f));
    zlook::brackets(sDl, ImVec2(a.x - S(5), a.y - S(5)), ImVec2(b.x + S(5), b.y + S(5)), S(18), S(2.6f), IM_COL32(255, 255, 255, 240));
    float r = S(34);
    ImVec2 c(a.x + S(16) + r, a.y + S(14) + r);
    zlook::face_ring(sDl, face_tex(HOST_CHARA), c, r, false, ImVec2(0.2f, 0.03f), ImVec2(0.8f, 0.63f), sS);
    bool err = sToastErr && sFrames < sToastUntil && line == sToast;
    float x = c.x + r + S(22), wrap = b.x - x - S(24);
    float size = S(21);
    ImFont *f = bold_font();
    ImVec2 ts = f->CalcTextSizeA(size, FLT_MAX, wrap, line.c_str());
    if (ts.y > size * 2.2f) { size = S(17); ts = f->CalcTextSizeA(size, FLT_MAX, wrap, line.c_str()); }
    if (ts.y > size * 3.2f) { size = S(14); }
    sDl->PushClipRect(a, ImVec2(b.x, b.y - S(26)), true);
    sDl->AddText(f, size, ImVec2(x + S(1), a.y + S(17)), IM_COL32(0, 0, 0, 160), line.c_str(), NULL, wrap);
    sDl->AddText(f, size, ImVec2(x, a.y + S(16)), err ? IM_COL32(255, 150, 150, 255) : IM_COL32(255, 255, 255, 255), line.c_str(), NULL, wrap);
    sDl->PopClipRect();
}

// The gold Greek-key strips at the screen's sides.
static void border_strips(void) {
    zlook::meander_strip(sDl, P(40, 0), ImVec2(P(40, 0).x + S(27), P(0, 768).y));
    zlook::meander_strip(sDl, ImVec2(P(1336, 0).x - S(27), P(0, 0).y), P(1336, 768));
}

// The emblem and the title: the warrior's face in a gold ring, its name in big slanted orange letters.
static void title_and_tabs(const char *heading) {
    ImVec2 e = P(132, 36);
    float er = S(28);
    int face = -1;
    if (sScreen == SC_EDIT) { face = chara_id(sSpec.has("portrait") ? sSpec["portrait"] : look()); }
    if (face >= 0) {
        zlook::face_ring(sDl, face_tex(face), e, er, false, ImVec2(0.2f, 0.03f), ImVec2(0.8f, 0.63f), sS);
    } else {
        sDl->AddCircleFilled(ImVec2(e.x, e.y + S(3)), er + S(3), IM_COL32(107, 58, 0, 255));
        grad(ImVec2(e.x - er, e.y - er), ImVec2(e.x + er, e.y + er), IM_COL32(255, 214, 90, 255), IM_COL32(232, 130, 10, 255), er);
        sDl->AddCircle(e, er, IM_COL32(255, 242, 176, 255), 0, S(3));
        float zs = S(32);
        sDl->AddText(bold_font(), zs, ImVec2(e.x - text_w(bold_font(), zs, "Z") * 0.5f, e.y - zs * 0.55f), IM_COL32(122, 43, 0, 255), "Z");
    }
    float hs = S(52);
    std::string h = heading;
    float room = P(1090, 0).x - P(176, 0).x;
    while (text_w(bold_font(), hs, h.c_str()) > room && hs > S(30)) { hs -= S(2); }
    zlook::title(sDl, bold_font(), hs, ImVec2(P(176, 0).x, P(0, 34).y - hs * 0.6f), h.c_str(), IM_COL32(255, 240, 110, 255),
                IM_COL32(255, 128, 16, 255), IM_COL32(110, 18, 8, 255), S(3.2f), 0.16f);
}

// The tabs: rounded pills, the chosen one gold, the others lavender, the L1 R1 hint after them.
static void draw_tabs(int on, const char *const *names, int count) {
    float x = P(100, 0).x, y0 = P(0, 66).y, h = S(28), size = S(14.5f);
    for (int i = 0; i < count; i++) {
        float w = text_w(bold_font(), size, names[i]) + S(34);
        ImVec2 a(x, y0), b(x + w, y0 + h);
        zlook::pill(sDl, a, b, i == on, sS);
        sDl->AddText(bold_font(), size, ImVec2(x + S(17), y0 + (h - size) * 0.5f), i == on ? zlook::kGoldInk : zlook::kLavInk, names[i]);
        if (click_in(a, b) && count > 1 && sOv.empty()) {
            sTab = i;
            sound(0);
        }
        x += w + S(7);
    }
    if (count > 1) { // (L1 / R1 at its end)
        glyph(B_L1R1, ImVec2(x + S(4), y0 + h * 0.5f), S(10));
    }
}

// A list of rows in a box: the headings gold bars (a + / - when they fold), the cursor's row a bright gold bar, the
// labels left and the values right of them, a row's face at its end. Returns the row under the mouse that was
// clicked, -1 none.
static std::vector<std::pair<int, ImRect>> sBars; // the sliders' tracks drawn by the last draw_rows (for the mouse)
static int draw_rows(ImVec2 a, ImVec2 b, const std::vector<Row> &rows, int cur, int &top, bool active) {
    sBars.clear();
    float rh = S(37), gap = S(2), pad = S(10);
    int fits = (int)((b.y - a.y - pad * 2) / (rh + gap));
    if (fits < 1) { fits = 1; }
    if (cur < top) { top = cur; }
    if (cur >= top + fits) { top = cur - fits + 1; }
    if (top > 0 && top > (int)rows.size() - fits) { top = (int)rows.size() - fits; }
    if (top < 0) { top = 0; }
    if (cur > 0 && cur - 1 < top && rows[cur - 1].sec) { top = cur - 1; } // (the section's heading with its first row)
    int clicked = -1;
    sDl->PushClipRect(a, b, true);
    float y = a.y + pad;
    float labelW = (b.x - a.x) * 0.31f;
    float pulse = 0.5f + 0.5f * sinf(sFrames * 0.09f);
    for (int i = top; i < (int)rows.size() && i < top + fits; i++) {
        const Row &r = rows[i];
        ImVec2 ra(a.x + pad, y), rb(b.x - pad - S(6), y + rh);
        bool sel = i == cur && active;
        if (r.sec) { // a gold bar, as wide as the labels' column (or its words)
            float size = S(14);
            float w = std::max(labelW + S(10), text_w(bold_font(), size, r.label.c_str()) + S(64));
            if (w > rb.x - ra.x) { w = rb.x - ra.x; }
            ImVec2 sa(ra.x + S(2), y + S(8)), sb(ra.x + S(2) + w, y + rh - S(3));
            if (sel) { sDl->AddRectFilled(ImVec2(sa.x - S(3), sa.y - S(3)), ImVec2(sb.x + S(3), sb.y + S(3)), IM_COL32(255, 250, 200, (int)(120 + 100 * pulse)), (sb.y - sa.y) * 0.5f + S(3)); }
            zlook::pill(sDl, sa, sb, true, sS);
            sDl->AddText(bold_font(), size, ImVec2(sa.x + S(16), sa.y + (sb.y - sa.y - size) * 0.5f), zlook::kGoldInk, r.label.c_str());
            if (r.ok) { zlook::fold_sign(sDl, ImVec2(sb.x - S(16), (sa.y + sb.y) * 0.5f), S(5), r.folded, zlook::kGoldInk); }
            if (click_in(sa, sb)) { clicked = i; }
            y += rh + gap;
            continue;
        }
        if (sel) { zlook::cursor_bar(sDl, ra, rb, sS, pulse); }
        float x = ra.x + S(14);
        float size = S(17);
        ImU32 ink = r.locked ? IM_COL32(176, 166, 210, 210) : sel ? kDark : kInk;
        text(sFont, size, ImVec2(x, y + (rh - size) * 0.5f), ink, r.label, ra.x + labelW - x);
        float vx = ra.x + labelW + S(8), vmax = rb.x - vx - S(16);
        if (r.face >= 0) { // (its face at the row's end)
            float fr = (rh - S(12)) * 0.5f;
            ImTextureID t = face_tex(r.face);
            zlook::face_ring(sDl, t, ImVec2(rb.x - S(10) - fr, y + rh * 0.5f), fr, false, ImVec2(0.2f, 0.03f), ImVec2(0.8f, 0.63f), sS * 0.6f);
            vmax -= fr * 2 + S(8);
        }
        if (r.bar >= 0.0f) { // a slider: its track, the default's tick, the knob; the value after it
            float tx0 = vx + S(6), tx1 = rb.x - S(104), my = y + rh * 0.5f;
            sDl->AddRectFilled(ImVec2(tx0, my - S(3)), ImVec2(tx1, my + S(3)), sel ? IM_COL32(107, 58, 0, 120) : IM_COL32(0, 0, 0, 110), S(3));
            float f = r.bar < 0 ? 0 : r.bar > 1 ? 1 : r.bar;
            if (r.barDef >= 0.0f) {
                float dx = tx0 + (tx1 - tx0) * r.barDef;
                sDl->AddRectFilled(ImVec2(dx - S(1), my - S(8)), ImVec2(dx + S(1), my + S(8)), sel ? IM_COL32(107, 58, 0, 200) : IM_COL32(201, 184, 240, 150));
                float fx = tx0 + (tx1 - tx0) * f;
                sDl->AddRectFilled(ImVec2(std::min(dx, fx), my - S(3)), ImVec2(std::max(dx, fx), my + S(3)), sel ? IM_COL32(255, 255, 255, 220) : kGold1, S(3));
            }
            ImVec2 k(tx0 + (tx1 - tx0) * f, my);
            sDl->AddCircleFilled(k, S(9), sel ? IM_COL32(255, 255, 255, 255) : kGold1);
            sDl->AddCircle(k, S(9), sel ? kDark : IM_COL32(107, 58, 0, 255), 0, S(2));
            text(bold_font(), size, ImVec2(tx1 + S(14), y + (rh - size) * 0.5f), sel ? kDark : kInk, r.value, rb.x - tx1 - S(20));
            sBars.push_back({i, ImRect(ImVec2(tx0 - S(8), y), ImVec2(tx1 + S(8), y + rh))});
            if (click_in(ra, rb)) { clicked = i; }
            y += rh + gap;
            continue;
        }
        std::string sub = r.sub;
        float subSize = S(12.5f);
        float subW = sub.empty() ? 0 : std::min(text_w(sFont, subSize, sub.c_str()) + S(10), vmax * 0.45f);
        text(sFont, size, ImVec2(vx, y + (rh - size) * 0.5f), sel ? kDark : r.locked ? ink : IM_COL32(255, 255, 255, 255), r.value, vmax - subW);
        if (!sub.empty()) {
            text(sFont, subSize, ImVec2(vx + vmax - subW + S(10), y + (rh - subSize) * 0.5f), sel ? IM_COL32(107, 58, 0, 255) : kDim, sub, subW - S(10));
        }
        if (r.lr && sel) { // (left / right change it: an arrow before the value)
            ImU32 c = IM_COL32(107, 58, 0, 255);
            float mx = vx - S(10), my = y + rh * 0.5f, k = S(5);
            sDl->AddTriangleFilled(ImVec2(mx - k, my), ImVec2(mx + k * 0.3f, my - k), ImVec2(mx + k * 0.3f, my + k), c);
        }
        if (click_in(ra, rb)) { clicked = i; }
        y += rh + gap;
    }
    sDl->PopClipRect();
    if ((int)rows.size() > fits) { // the scroll bar, and arrows where more rows are
        float h = b.y - a.y - pad * 2, k = h * fits / rows.size(), at = a.y + pad + h * top / rows.size();
        sDl->AddRectFilled(ImVec2(b.x - S(9), a.y + pad), ImVec2(b.x - S(5), b.y - pad), IM_COL32(0, 0, 0, 60), S(2));
        sDl->AddRectFilled(ImVec2(b.x - S(9), at), ImVec2(b.x - S(5), at + k), IM_COL32(255, 214, 90, 255), S(2));
        float mx = (a.x + b.x) * 0.5f, t = S(6);
        if (top > 0) { sDl->AddTriangleFilled(ImVec2(mx, a.y - S(1)), ImVec2(mx + t, a.y + t), ImVec2(mx - t, a.y + t), IM_COL32(255, 220, 100, 255)); }
        if (top + fits < (int)rows.size()) { sDl->AddTriangleFilled(ImVec2(mx, b.y + S(1)), ImVec2(mx - t, b.y - t), ImVec2(mx + t, b.y - t), IM_COL32(255, 220, 100, 255)); }
    }
    return clicked;
}

// The cursor of a list, kept off its headings.
static void move_cursor(const std::vector<Row> &rows, int &cur, int d) {
    int n = (int)rows.size(), c = cur;
    for (int i = 0; i < n; i++) {
        c = (c + d + n) % n;
        if (!rows[c].sec || rows[c].ok) { break; } // (a heading that folds is a stop of its own)
    }
    if (c != cur) { cur = c; sound(0); }
}
static void fix_cursor(const std::vector<Row> &rows, int &cur) {
    if (rows.empty()) { cur = 0; return; }
    if (cur >= (int)rows.size()) { cur = (int)rows.size() - 1; }
    if (cur < 0) { cur = 0; }
    if (rows[cur].sec && !rows[cur].ok) { int c = cur; move_cursor(rows, c, 1); cur = c; }
}
static void list_input(std::vector<Row> &rows, int &cur, int clicked) {
    fix_cursor(rows, cur);
    if (rows.empty()) { return; }
    if (sIn.up) { move_cursor(rows, cur, -1); }
    if (sIn.down) { move_cursor(rows, cur, 1); }
    if (clicked >= 0 && (!rows[clicked].sec || rows[clicked].ok)) {
        cur = clicked;
        sIn.ok = true;
    }
    Row &r = rows[cur];
    if (r.locked) { if (sIn.ok) { sound(2); } return; }
    if (sIn.ok && r.ok) { sound(1); std::function<void()> f = r.ok; f(); return; }
    if ((sIn.left || sIn.right) && r.lr) { r.lr(sIn.right ? 1 : -1); return; }
    if (sIn.sq && r.sq) { std::function<void()> f = r.sq; f(); return; }
    if (sIn.tri && r.tri) { std::function<void()> f = r.tri; f(); return; }
}

// ---- the overlays
static ImVec2 sBoxA, sBoxB; // the left box the overlays are drawn in (the model stays to be seen beside it)
static float sOvRight;      // the right edge of the box on top this frame (the mouse beyond it is on the model)

// A grid's heading as the trailer has it: a choice of a look is "TRY IT ON" (one layered on another "LAYER ON TOP"),
// of a technique "LEARN A TECHNIQUE", and what is being chosen goes in the gold pill ("HAIR: WHOSE?" -> Hair).
static void grid_heading(const Overlay &o, std::string &title, std::string &pill) {
    title = o.title;
    pill = o.pill;
    if (o.kind != OV_GRID) { return; }
    size_t colon = title.find(": ");
    std::string what = colon != std::string::npos ? title.substr(0, colon) : "";
    if (pill.empty() && !what.empty()) {
        pill = what;
        for (size_t i = 1; i < pill.size(); i++) { // (Title case: "COSTUME 2" -> "Costume 2", "BLAST 1 (1)" -> "Blast 1 (1)")
            bool start = pill[i - 1] == ' ' || pill[i - 1] == '(';
            if (!start) { pill[i] = (char)SDL_tolower((unsigned char)pill[i]); }
        }
    }
    if (!o.moveKinds.empty()) { title = "LEARN A TECHNIQUE"; }
    else if (title.find("LAYER") != std::string::npos) { title = "LAYER ON TOP"; }
    else if (o.costume && sScreen == SC_EDIT && what != "DAMAGED LOOK") { title = "TRY IT ON"; }
    else if (!what.empty()) { title = title.substr(colon + 2); }
}

static void ov_title(const Overlay &o) {
    std::string t, pill;
    grid_heading(o, t, pill);
    float pw = 0, ph = S(26), size = S(15);
    if (!pill.empty()) { // the gold pill: what is being chosen
        pw = std::min(text_w(bold_font(), size, pill.c_str()) + S(56), (sBoxB.x - sBoxA.x) * 0.4f);
        ImVec2 a(sBoxB.x - S(16) - pw, sBoxA.y + S(12)), b(sBoxB.x - S(16), sBoxA.y + S(12) + ph);
        zlook::pill(sDl, a, b, true, sS);
        text(bold_font(), size, ImVec2((a.x + b.x) * 0.5f - std::min(text_w(bold_font(), size, pill.c_str()), pw - S(24)) * 0.5f, a.y + (ph - size) * 0.5f),
             zlook::kGoldInk, pill, pw - S(24));
    }
    float ts = S(36), room = sBoxB.x - sBoxA.x - pw - S(48);
    while (text_w(bold_font(), ts, t.c_str()) > room && ts > S(20)) { ts -= S(2); }
    zlook::title(sDl, bold_font(), ts, ImVec2(sBoxA.x + S(16), sBoxA.y + S(26) - ts * 0.62f), t.c_str(), IM_COL32(255, 214, 90, 255),
                IM_COL32(255, 92, 24, 255), IM_COL32(96, 12, 6, 255), S(2.6f), 0.14f);
}

static void grid_frame(Overlay &o, std::vector<Prompt> &ps) {
    std::vector<const J *> items = grid_items(o);
    float top0 = sBoxA.y + S(50);
    // the game filter and the search
    bool games = sHaveBt4 && !o.bt3only && o.stage == 0;
    float x = sBoxA.x + S(18);
    if (games) {
        static const char *const kG[3] = {"ALL", "BT3", "BT4"};
        for (int g = 0; g < 3; g++) {
            float w = text_w(bold_font(), S(13), kG[g]) + S(24), h = S(22);
            ImVec2 a(x, top0), b(x + w, top0 + h);
            zlook::pill(sDl, a, b, g == o.game, sS);
            sDl->AddText(bold_font(), S(13), ImVec2(x + S(12), top0 + (h - S(13)) * 0.5f), g == o.game ? zlook::kGoldInk : zlook::kLavInk, kG[g]);
            if (click_in(a, b)) { o.game = g; o.cur = 0; o.top = 0; }
            x += w + S(6);
        }
        x += S(10);
    }
    if (o.stage == 0) {
        std::string s = o.search.empty() ? "type to search" : "search: " + o.search + ((sFrames / 20) % 2 ? "_" : " ");
        sDl->AddText(sFont, S(14), ImVec2(x, top0 + S(3)), o.search.empty() ? kDim : kInk, s.c_str());
        if (!o.none.empty()) { // (the none choice: a lavender pill at the end of the line, square)
            float w = text_w(bold_font(), S(13), o.none.c_str()) + S(44), h = S(22);
            ImVec2 a(sBoxB.x - S(18) - w, top0), b(sBoxB.x - S(18), top0 + h);
            zlook::pill(sDl, a, b, false, sS);
            glyph(B_SQUARE, ImVec2(a.x + S(6), top0 + h * 0.5f), S(8));
            sDl->AddText(bold_font(), S(13), ImVec2(a.x + S(28), top0 + (h - S(13)) * 0.5f), zlook::kLavInk, o.none.c_str());
            if (click_in(a, b)) { sIn.sq = true; }
        }
    }
    if (o.stage == 0) {
        // the faces
        ImVec2 ga(sBoxA.x + S(14), top0 + S(32)), gb(sBoxB.x - S(14), sBoxB.y - S(70));
        float cw = S(61), ch = S(60);
        int cols = (int)((gb.x - ga.x) / cw);
        if (cols < 1) { cols = 1; }
        cw = (gb.x - ga.x) / cols;
        int rowsFit = (int)((gb.y - ga.y) / ch);
        if (rowsFit < 1) { rowsFit = 1; }
        int n = (int)items.size();
        if (o.cur >= n) { o.cur = n ? n - 1 : 0; }
        int was = o.cur;
        if (sIn.left && o.cur > 0) { o.cur--; }
        if (sIn.right && o.cur < n - 1) { o.cur++; }
        if (sIn.up && o.cur >= cols) { o.cur -= cols; }
        if (sIn.down && o.cur + cols < n) { o.cur += cols; } else if (sIn.down && o.cur / cols < (n - 1) / cols) { o.cur = n - 1; }
        if (o.cur != was) { sound(0); o.rest = 0; } else { o.rest++; }
        int row = o.cur / cols;
        if (row < o.top) { o.top = row; }
        if (row >= o.top + rowsFit) { o.top = row - rowsFit + 1; }
        if (sMouse && ImGui::GetIO().MouseWheel != 0 && ImGui::IsMouseHoveringRect(ga, gb)) {
            o.top -= (int)ImGui::GetIO().MouseWheel;
            int maxTop = (n + cols - 1) / cols - rowsFit;
            o.top = o.top < 0 ? 0 : o.top > maxTop ? (maxTop > 0 ? maxTop : 0) : o.top;
        }
        int clicked = -1;
        sDl->PushClipRect(ga, gb, true);
        for (int i = o.top * cols; i < n && i < (o.top + rowsFit) * cols; i++) {
            int cx = i % cols, cy = i / cols - o.top;
            ImVec2 a(ga.x + cx * cw + S(3), ga.y + cy * ch + S(3)), b(ga.x + (cx + 1) * cw - S(3), ga.y + (cy + 1) * ch - S(3));
            bool sel = i == o.cur;
            float r = std::min(b.x - a.x, b.y - a.y) * 0.5f - S(4);
            zlook::face_ring(sDl, face_tex((int)(*items[i])["id"].number(0)), ImVec2((a.x + b.x) * 0.5f, (a.y + b.y) * 0.5f), sel ? r * 1.08f : r, sel,
                            ImVec2(0.2f, 0.03f), ImVec2(0.8f, 0.63f), sS);
            if (click_in(a, b)) { clicked = i; }
        }
        sDl->PopClipRect();
        if (clicked >= 0) { o.cur = clicked; sIn.ok = true; }
        { // the arrows where more faces are; under the grid, the one the cursor is on
            float mx = (ga.x + gb.x) * 0.5f, t = S(7);
            if (o.top > 0) { sDl->AddTriangleFilled(ImVec2(mx, ga.y - S(4) - t), ImVec2(mx + t, ga.y - S(4)), ImVec2(mx - t, ga.y - S(4)), IM_COL32(255, 220, 100, 255)); }
            if ((o.top + rowsFit) * cols < n) { sDl->AddTriangleFilled(ImVec2(mx, gb.y + S(2) + t), ImVec2(mx - t, gb.y + S(2)), ImVec2(mx + t, gb.y + S(2)), IM_COL32(255, 220, 100, 255)); }
            if (o.cur < n) {
                const J &c = *items[o.cur];
                std::string name = c["name"].text();
                float ns = S(22);
                while (text_w(bold_font(), ns, name.c_str()) > gb.x - ga.x && ns > S(14)) { ns -= S(1); }
                outlined(bold_font(), ns, ImVec2(mx - text_w(bold_font(), ns, name.c_str()) * 0.5f, gb.y + S(14)), IM_COL32(255, 226, 120, 255), IM_COL32(60, 16, 4, 255), name, S(1.6f));
                int nc = (int)c["costumes"].size();
                std::string line = c["game"].text() == "BT4" ? "from BT4" : "";
                if (o.costume && nc > 1) { line += std::string(line.empty() ? "" : " - ") + std::to_string(nc) + " costumes: cross picks one"; }
                else if (o.costume) { line += std::string(line.empty() ? "" : " - ") + "Costume: its own"; }
                if (!line.empty()) { sDl->AddText(sFont, S(14), ImVec2(mx - text_w(sFont, S(14), line.c_str()) * 0.5f, gb.y + S(14) + ns + S(3)), kDim, line.c_str()); }
            }
        }
        // the keyboard's letters search
        ImGuiIO &io = ImGui::GetIO();
        for (int i = 0; i < io.InputQueueCharacters.Size; i++) {
            ImWchar c = io.InputQueueCharacters[i];
            if (c >= 32 && o.search.size() < 30) { utf8_put(o.search, c); o.cur = 0; o.top = 0; }
        }
        if (sIn.backspace && !o.search.empty()) {
            o.search.pop_back();
            while (!o.search.empty() && ((unsigned char)o.search.back() & 0xC0) == 0x80) { o.search.pop_back(); }
            o.cur = 0;
            sIn.back = false;
        } else if (sIn.backspace) {
            sIn.back = true;
        }
        if (sIn.esc) { sIn.back = true; }
        if (sIn.enter) { sIn.ok = true; }
        if (games && (sIn.l1 || sIn.r1)) { o.game = (o.game + (sIn.r1 ? 1 : 2)) % 3; o.cur = 0; o.top = 0; sound(0); }
        ps.push_back({B_DPAD, "Fighter"});
        ps.push_back({B_CROSS, o.costume && sScreen == SC_EDIT ? "Wear it" : "Choose"});
        if (!o.none.empty()) { ps.push_back({B_SQUARE, o.none}); }
        if (games) { ps.push_back({B_L1R1, "BT3 / BT4"}); }
        ps.push_back({B_CIRCLE, "Cancel"});
        if (sIn.back) { ov_close(); sound(2); return; }
        if (sIn.sq && !o.none.empty()) {
            std::function<void(const J *)> f = o.picked;
            ov_close();
            sound(1);
            f(NULL);
            return;
        }
        if (sIn.ok && n > 0) {
            const J &c = *items[o.cur];
            o.chosen = (int)c["id"].number(0);
            if (!o.moveKinds.empty()) { o.stage = 2; o.cur = 0; o.top = 0; sound(1); return; }
            if (o.costume && c["costumes"].size() > 1) { o.stage = 1; o.cur = 0; o.top = 0; sound(1); return; }
            J r = ref_of(c);
            std::function<void(const J *)> f = o.picked;
            ov_close();
            sound(1);
            f(&r);
        }
        return;
    }
    // the costumes or the techniques of the one chosen
    J idj = J::num(o.chosen);
    const J *c = entry_of(idj);
    if (c == NULL) { ov_close(); return; }
    std::vector<Row> rows;
    if (o.stage == 1) {
        rows.push_back(sec(((*c)["name"].text() + ": COSTUME").c_str()));
        for (size_t i = 0; i < (*c)["costumes"].size(); i++) {
            Row r;
            int k = (int)(*c)["costumes"][i].number(1);
            r.label = "Costume " + std::to_string(k);
            r.face = o.chosen;
            r.ok = [k, c, &o] {
                J ref = ref_of(*c, k);
                std::function<void(const J *)> f = o.picked;
                ov_close();
                f(&ref);
            };
            rows.push_back(r);
        }
    } else {
        rows.push_back(sec(((*c)["name"].text() + ": WHICH TECHNIQUE?").c_str()));
        for (int k : o.moveKinds) {
            Row r;
            r.label = kSlotLabel[k];
            r.value = (*c)["moves"][(size_t)k].text("-");
            r.ok = [k, c, &o] {
                J ref = ref_of(*c);
                ref.set("slot", J::str(kSlots[k]));
                std::function<void(const J *)> f = o.picked;
                ov_close();
                f(&ref);
            };
            rows.push_back(r);
        }
    }
    int cur = o.cur + 1;
    int clicked = draw_rows(ImVec2(sBoxA.x + S(10), top0), ImVec2(sBoxB.x - S(10), sBoxB.y - S(10)), rows, cur, o.top, true);
    ps.push_back({B_CROSS, "Choose"});
    ps.push_back({B_CIRCLE, "Back"});
    if (sIn.back) { o.stage = 0; o.cur = 0; sound(2); return; }
    int id = o.id;
    list_input(rows, cur, clicked); // (may close the overlay: nothing of `o` after it unless it is still up)
    if (!sOv.empty() && sOv.back().id == id) { sOv.back().cur = cur - 1; }
}

static void list_frame(Overlay &o, std::vector<Prompt> &ps) {
    float top0 = sBoxA.y + S(54);
    if (!o.text.empty()) {
        sDl->AddText(sFont, S(16), ImVec2(sBoxA.x + S(20), top0), kInk, o.text.c_str(), NULL, sBoxB.x - sBoxA.x - S(40));
        top0 += S(30);
    }
    int clicked = draw_rows(ImVec2(sBoxA.x + S(10), top0), ImVec2(sBoxB.x - S(10), sBoxB.y - S(10)), o.rows, o.cur, o.top, true);
    ps.push_back({B_CROSS, "Choose"});
    ps.push_back({B_CIRCLE, "Back"});
    if (sIn.back) {
        std::function<void()> b = o.back;
        ov_close();
        sound(2);
        if (b) { b(); }
        return;
    }
    std::vector<Row> rows = o.rows; // (a choice may close it)
    int cur = o.cur, id = o.id;
    list_input(rows, cur, clicked);
    if (!sOv.empty() && sOv.back().id == id) { sOv.back().cur = cur; }
}

// the on-screen keyboard: rows of letters, then SPACE / DELETE / OK
static const char *const kKeyRows[6] = {"ABCDEFGHIJKLM", "NOPQRSTUVWXYZ", "abcdefghijklm", "nopqrstuvwxyz", "0123456789.,'", "-!?&()+:/#%*~"};
static void keys_frame(Overlay &o, std::vector<Prompt> &ps) {
    ImGuiIO &io = ImGui::GetIO();
    auto pop_char = [&o] {
        if (o.text.empty()) { return; }
        o.text.pop_back();
        while (!o.text.empty() && ((unsigned char)o.text.back() & 0xC0) == 0x80) { o.text.pop_back(); }
    };
    auto put = [&o](unsigned c) { if (o.text.size() < o.max) { utf8_put(o.text, c); } };
    for (int i = 0; i < io.InputQueueCharacters.Size; i++) {
        if (io.InputQueueCharacters[i] >= 32) { put(io.InputQueueCharacters[i]); }
    }
    if (sIn.backspace) { pop_char(); }
    // the field
    float fy = sBoxA.y + S(60);
    ImVec2 fa(sBoxA.x + S(20), fy), fb(sBoxB.x - S(20), fy + S(46));
    sDl->AddRectFilled(fa, fb, IM_COL32(0, 0, 0, 90), S(8));
    sDl->AddRect(fa, fb, IM_COL32(169, 147, 255, 255), S(8), 0, S(2));
    std::string shown = o.text + ((sFrames / 25) % 2 ? "|" : " ");
    text(sBold != NULL ? sBold : sFont, S(24), ImVec2(fa.x + S(12), fa.y + S(10)), kInk, shown, fb.x - fa.x - S(24));
    char count[32];
    snprintf(count, sizeof(count), "%d / %d", (int)o.text.size(), (int)o.max);
    sDl->AddText(sFont, S(13), ImVec2(fb.x - text_w(sFont, S(13), count), fb.y + S(4)), kDim, count);
    // the keys
    float ky = fb.y + S(26), kw = (sBoxB.x - sBoxA.x - S(40)) / 13.0f, kh = S(44);
    int was = o.kr * 100 + o.kc;
    if (sIn.up) { o.kr = (o.kr + 6) % 7; }
    if (sIn.down) { o.kr = (o.kr + 1) % 7; }
    if (o.kr == 6) {
        if (was / 100 != 6) { o.kc = std::min(2, (was % 100) * 3 / 13); }
        if (sIn.left) { o.kc = (o.kc + 2) % 3; }
        if (sIn.right) { o.kc = (o.kc + 1) % 3; }
    } else {
        if (was / 100 == 6) { o.kc = (was % 100) * 13 / 3 + 2; if (o.kc > 12) { o.kc = 12; } }
        if (sIn.left) { o.kc = (o.kc + 12) % 13; }
        if (sIn.right) { o.kc = (o.kc + 1) % 13; }
    }
    if (o.kr * 100 + o.kc != was) { sound(0); }
    int clickedR = -1, clickedC = -1;
    for (int r = 0; r < 6; r++) {
        for (int c = 0; c < 13; c++) {
            ImVec2 a(sBoxA.x + S(20) + c * kw + S(2), ky + r * (kh + S(4))), b(a.x + kw - S(4), a.y + kh);
            bool sel = o.kr == r && o.kc == c;
            if (sel) { grad(a, b, kGold1, kGold2, S(6)); } else { sDl->AddRectFilled(a, b, IM_COL32(255, 255, 255, 22), S(6)); }
            char ch[2] = {kKeyRows[r][c], 0};
            float size = S(20);
            sDl->AddText(sBold != NULL ? sBold : sFont, size, ImVec2((a.x + b.x - text_w(sBold != NULL ? sBold : sFont, size, ch)) * 0.5f, a.y + (kh - size) * 0.5f), sel ? kDark : kInk, ch);
            if (click_in(a, b)) { clickedR = r; clickedC = c; }
        }
    }
    static const char *const kSpecial[3] = {"SPACE", "DELETE", "OK"};
    for (int c = 0; c < 3; c++) {
        float w = (sBoxB.x - sBoxA.x - S(40)) / 3.0f;
        ImVec2 a(sBoxA.x + S(20) + c * w + S(2), ky + 6 * (kh + S(4))), b(a.x + w - S(4), a.y + kh);
        bool sel = o.kr == 6 && o.kc == c;
        if (sel) { grad(a, b, kGold1, kGold2, S(10)); } else { grad(a, b, IM_COL32(125, 100, 200, 255), IM_COL32(70, 40, 130, 255), S(10)); }
        float size = S(18);
        sDl->AddText(sBold != NULL ? sBold : sFont, size, ImVec2((a.x + b.x - text_w(sBold != NULL ? sBold : sFont, size, kSpecial[c])) * 0.5f, a.y + (kh - size) * 0.5f),
                     sel ? kDark : kInk, kSpecial[c]);
        if (click_in(a, b)) { clickedR = 6; clickedC = c; }
    }
    bool okPad = sIn.ok && !sIn.enter;
    if (clickedR >= 0) { o.kr = clickedR; o.kc = clickedC; okPad = true; }
    bool done = sIn.enter || sIn.start;
    if (okPad) {
        if (o.kr < 6) { put((unsigned char)kKeyRows[o.kr][o.kc]); sound(0); }
        else if (o.kc == 0) { put(' '); sound(0); }
        else if (o.kc == 1) { pop_char(); sound(0); }
        else { done = true; }
    }
    if (sIn.sq) { pop_char(); sound(0); }
    if (sIn.tri) { put(' '); sound(0); }
    ps.push_back({B_CROSS, "Type"});
    ps.push_back({B_SQUARE, "Delete"});
    ps.push_back({B_TRIANGLE, "Space"});
    ps.push_back({B_START, "OK"});
    ps.push_back({B_CIRCLE, "Cancel"});
    if (done) {
        std::string t = o.text;
        while (!t.empty() && t.back() == ' ') { t.pop_back(); }
        while (!t.empty() && t[0] == ' ') { t.erase(0, 1); }
        std::function<void(const std::string &)> f = o.typed;
        ov_close();
        sound(1);
        f(t);
        return;
    }
    if (sIn.esc || (sIn.back && !sIn.backspace)) { ov_close(); sound(2); }
}

static void colours_frame(Overlay &o, std::vector<Prompt> &ps) {
    float top0 = sBoxA.y + S(56);
    int n = (int)o.tex.size();
    if (o.loading && n == 0) {
        sDl->AddText(sFont, S(18), ImVec2(sBoxA.x + S(24), top0 + S(10)), kInk, "Looking at your warrior's textures...");
    }
    float rh = S(76), gap = S(6);
    int fits = (int)((sBoxB.y - top0 - S(12)) / (rh + gap));
    if (fits < 1) { fits = 1; }
    int cols = 8;
    int row = o.cur / 100, col = o.cur % 100;
    int was = o.cur;
    if (sIn.up && row > 0) { row--; }
    if (sIn.down && row < n - 1) { row++; }
    int sw = n ? std::min(cols, (int)o.tex[(size_t)row]["colours"].size()) : 0;
    if (sIn.left && col > 0) { col--; }
    if (sIn.right && col < sw - 1) { col++; }
    if (col >= sw) { col = sw > 0 ? sw - 1 : 0; }
    o.cur = row * 100 + col;
    if (o.cur != was) { sound(0); }
    if (row < o.top) { o.top = row; }
    if (row >= o.top + fits) { o.top = row - fits + 1; }
    int clicked = -1;
    for (int i = o.top; i < n && i < o.top + fits; i++) {
        float y = top0 + (i - o.top) * (rh + gap);
        ImVec2 a(sBoxA.x + S(14), y), b(sBoxB.x - S(14), y + rh);
        sDl->AddRectFilled(a, b, IM_COL32(255, 255, 255, i == row ? 34 : 14), S(10));
        if (i == row) { sDl->AddRect(a, b, IM_COL32(255, 211, 77, 255), S(10), 0, S(2)); }
        if (i < (int)o.pics.size() && o.pics[i] != NULL) {
            float ps2 = rh - S(12);
            sDl->AddImage(ImTextureRef((ImTextureID)(intptr_t)o.pics[i]), ImVec2(a.x + S(6), y + S(6)), ImVec2(a.x + S(6) + ps2, y + S(6) + ps2));
        }
        char label[64];
        snprintf(label, sizeof(label), "Texture %d", (int)o.tex[(size_t)i]["texture"].number(0));
        sDl->AddText(sFont, S(13), ImVec2(a.x + rh + S(4), y + S(4)), kDim, label);
        const J &cs = o.tex[(size_t)i]["colours"];
        for (int c = 0; c < (int)cs.size() && c < cols; c++) {
            float s0 = S(40);
            ImVec2 ca(a.x + rh + S(4) + c * (s0 + S(10)), y + S(24)), cb(ca.x + s0, ca.y + s0);
            unsigned rgb = hex_rgb(cs[(size_t)c][(size_t)0].text());
            sDl->AddRectFilled(ca, cb, col32(rgb), S(6));
            bool sel = i == row && c == col;
            sDl->AddRect(ImVec2(ca.x - S(2), ca.y - S(2)), ImVec2(cb.x + S(2), cb.y + S(2)), sel ? IM_COL32(255, 226, 122, 255) : IM_COL32(255, 255, 255, 60), S(7), 0, sel ? S(3) : S(1));
            if (click_in(ca, cb)) { clicked = i * 100 + c; }
        }
    }
    if (clicked >= 0) { o.cur = clicked; row = clicked / 100; col = clicked % 100; sIn.ok = true; }
    ps.push_back({B_CROSS, "Change this colour"});
    ps.push_back({B_SQUARE, "Texture's own colours"});
    ps.push_back({B_TRIANGLE, "Paint it..."});
    ps.push_back({B_CIRCLE, "Back"});
    if (sIn.back) { ov_close(); sound(2); return; }
    if (n == 0) { return; }
    int texture = (int)o.tex[(size_t)row]["texture"].number(0);
    if (sIn.tri) { // (the texture painter, on this texture: a secondary way to change its colours)
        if (sShownCostume != 0) { sShownCostume = 0; } // (these are costume 1's textures)
        open_texpick(texture);
        return;
    }
    if (sIn.ok && col < (int)o.tex[(size_t)row]["colours"].size()) {
        open_palette(texture, o.tex[(size_t)row]["colours"][(size_t)col][(size_t)0].text());
        return;
    }
    if (sIn.sq) {
        J keep = J::arr();
        const J &all = sSpec["colours"];
        for (size_t i = 0; i < all.size(); i++) { if ((int)all[i]["texture"].number(-1) != texture) { keep.push(all[i]); } }
        if (keep.size()) { sSpec.set("colours", keep); } else { sSpec.erase("colours"); }
        touch();
        sound(2);
        colours_load(&o);
    }
}

static void fx_frame(Overlay &o, std::vector<Prompt> &ps) {
    float y = sBoxA.y + S(56), x0 = sBoxA.x + S(20), x1 = sBoxB.x - S(20);
    int n = (int)o.tex.size();
    if (o.loading && n == 0) {
        sDl->AddText(sFont, S(18), ImVec2(x0, y + S(6)), kInk, "Reading the attack's effects...");
    }
    // its picture: the textures and colours of its effect
    if (!o.pics.empty() && o.pics[0] != NULL && o.picW[0] > 0) {
        float w = x1 - x0, h = w * o.picH[0] / o.picW[0];
        if (h > S(190)) { h = S(190); w = h * o.picW[0] / o.picH[0]; }
        sDl->AddRectFilled(ImVec2(x0, y), ImVec2(x0 + w, y + h), IM_COL32(0, 0, 0, 90), S(6));
        sDl->AddImage(ImTextureRef((ImTextureID)(intptr_t)o.pics[0]), ImVec2(x0, y), ImVec2(x0 + w, y + h));
        y += h + S(14);
    }
    if (!o.loading && n == 0) {
        sDl->AddText(sFont, S(16), ImVec2(x0, y), kDim, "This attack has no colours of its own (its effect is the game's shared one).", NULL, x1 - x0);
        y += S(40);
    }
    // the swatches, in rows
    float sw = S(42), gap = S(12);
    int cols = (int)((x1 - x0 + gap) / (sw + gap));
    if (cols < 1) { cols = 1; }
    int was = o.cur;
    if (n > 0) {
        if (sIn.left && o.cur > 0) { o.cur--; }
        if (sIn.right && o.cur < n - 1) { o.cur++; }
        if (sIn.up && o.cur >= cols) { o.cur -= cols; }
        if (sIn.down && o.cur + cols < n) { o.cur += cols; }
    }
    if (o.cur != was) { sound(0); }
    int clicked = -1;
    for (int i = 0; i < n; i++) {
        ImVec2 a(x0 + (i % cols) * (sw + gap), y + (i / cols) * (sw + gap)), b(a.x + sw, a.y + sw);
        ImVec2 c((a.x + b.x) * 0.5f, (a.y + b.y) * 0.5f);
        sDl->AddCircleFilled(c, sw * 0.5f, col32(hex_rgb(o.tex[(size_t)i]["rgb"].text())));
        bool sel = i == o.cur;
        sDl->AddCircle(c, sw * 0.5f + S(2), sel ? IM_COL32(255, 226, 122, 255) : IM_COL32(255, 255, 255, 200), 0, sel ? S(4) : S(2));
        if (click_in(a, b)) { clicked = i; }
    }
    y += ((n + cols - 1) / cols) * (sw + gap) + S(6);
    if (n > 0 && o.cur < n) {
        std::string about = o.tex[(size_t)o.cur]["rgb"].text() + "  (" + o.tex[(size_t)o.cur]["where"].text() + ")";
        sDl->AddText(sFont, S(14), ImVec2(x0, y), kDim, about.c_str());
        y += S(26);
    }
    // what was changed
    const J &ch = sSpec["attack_colours"][o.fx.c_str()];
    if (ch.size()) {
        std::string line = "Changed: ";
        for (size_t i = 0; i < ch.size(); i++) { line += (i ? ", " : "") + ch[i]["from"].text() + " -> " + ch[i]["to"].text(); }
        sDl->AddText(sFont, S(14), ImVec2(x0, y), IM_COL32(255, 236, 160, 255), line.c_str(), NULL, x1 - x0);
        for (size_t i = 0; i < ch.size() && i < 12; i++) { // (and as little swatches)
            float k = S(16);
            ImVec2 a(x0 + i * (k * 2 + S(16)), y + S(40));
            sDl->AddRectFilled(a, ImVec2(a.x + k, a.y + k), col32(hex_rgb(ch[i]["from"].text())), S(3));
            sDl->AddRectFilled(ImVec2(a.x + k + S(2), a.y), ImVec2(a.x + k * 2 + S(2), a.y + k), col32(hex_rgb(ch[i]["to"].text())), S(3));
        }
    }
    if (clicked >= 0) { o.cur = clicked; sIn.ok = true; }
    if (n > 0) { ps.push_back({B_CROSS, "Change this colour"}); }
    if (ch.size()) { ps.push_back({B_SQUARE, "Original colours"}); }
    ps.push_back({B_CIRCLE, "Back"});
    if (sIn.back) { ov_close(); sound(2); return; }
    if (sIn.sq && ch.size()) {
        fx_original(o.fx);
        sound(2);
        fx_load(&o);
        return;
    }
    if (sIn.ok && n > 0 && o.cur < n) {
        std::string what = o.fx, from = o.tex[(size_t)o.cur]["rgb"].text();
        open_palette(0, from);
        sOv.back().fx = what;
    }
}

static void palette_frame(Overlay &o, std::vector<Prompt> &ps) {
    float top0 = sBoxA.y + S(60);
    unsigned from = hex_rgb(o.from);
    // from -> to
    float s0 = S(44);
    ImVec2 fa(sBoxA.x + S(24), top0);
    sDl->AddRectFilled(fa, ImVec2(fa.x + s0, fa.y + s0), col32(from), S(6));
    sDl->AddText(sFont, S(22), ImVec2(fa.x + s0 + S(14), fa.y + S(10)), kInk, "->");
    unsigned to = palette(o.cur);
    ImVec2 ta(fa.x + s0 + S(50), top0);
    sDl->AddRectFilled(ta, ImVec2(ta.x + s0, ta.y + s0), col32(to), S(6));
    char hex[16];
    snprintf(hex, sizeof(hex), "#%06x", to);
    sDl->AddText(sFont, S(15), ImVec2(ta.x + s0 + S(14), ta.y + S(14)), kDim, hex);
    float gy = top0 + s0 + S(24), cw = (sBoxB.x - sBoxA.x - S(48)) / PAL_COLS, chh = S(46);
    int was = o.cur, r = o.cur / PAL_COLS, c = o.cur % PAL_COLS;
    if (sIn.left) { c = (c + PAL_COLS - 1) % PAL_COLS; }
    if (sIn.right) { c = (c + 1) % PAL_COLS; }
    if (sIn.up) { r = (r + PAL_ROWS - 1) % PAL_ROWS; }
    if (sIn.down) { r = (r + 1) % PAL_ROWS; }
    o.cur = r * PAL_COLS + c;
    if (o.cur != was) { sound(0); }
    int clicked = -1;
    for (int i = 0; i < PAL_COLS * PAL_ROWS; i++) {
        ImVec2 a(sBoxA.x + S(24) + (i % PAL_COLS) * cw, gy + (i / PAL_COLS) * (chh + S(6))), b(a.x + cw - S(6), a.y + chh);
        sDl->AddRectFilled(a, b, col32(palette(i)), S(6));
        if (i == o.cur) { sDl->AddRect(ImVec2(a.x - S(3), a.y - S(3)), ImVec2(b.x + S(3), b.y + S(3)), IM_COL32(255, 226, 122, 255), S(8), 0, S(3)); }
        if (click_in(a, b)) { clicked = i; }
    }
    // the colour wheel, for any colour (its button under the swatches)
    float wy = gy + PAL_ROWS * (chh + S(6)) + S(10);
    ImVec2 wa(sBoxA.x + S(24), wy), wb(sBoxA.x + S(24) + S(230), wy + S(34));
    grad(wa, wb, IM_COL32(198, 181, 245, 255), IM_COL32(125, 100, 200, 255), S(17));
    for (int i = 0; i < 12; i++) { // (a little ring of hues on it)
        float a0 = i * 0.5236f;
        sDl->AddCircleFilled(ImVec2(wa.x + S(20) + cosf(a0) * S(9), wy + S(17) + sinf(a0) * S(9)), S(3.2f), col32(hsv_rgb(i * 30.0f, 1, 1)));
    }
    glyph(B_TRIANGLE, ImVec2(wa.x + S(38), wy + S(17)), S(10));
    sDl->AddText(sBold != NULL ? sBold : sFont, S(15), ImVec2(wa.x + S(66), wy + S(8)), IM_COL32(43, 22, 96, 255), "More: the colour wheel");
    bool wheel = sIn.tri || click_in(wa, wb);
    if (clicked >= 0) { o.cur = clicked; sIn.ok = true; }
    ps.push_back({B_CROSS, "Use this colour"});
    ps.push_back({B_TRIANGLE, "Colour wheel"});
    ps.push_back({B_CIRCLE, "Back"});
    if (sIn.back) { ov_close(); sound(2); return; }
    std::string fromHex = o.from, fx = o.fx;
    int texture = o.texture;
    // {texture, from, to} appended to "colours"; an attack's {from, to, range 1.5} to its "attack_colours", as the web page does
    auto use = [fromHex, fx, texture](unsigned rgb) {
        std::string to = hex_of(rgb);
        add_recent(rgb);
        J ch = J::obj();
        if (fx.empty()) { ch.set("texture", J::num(texture)); }
        ch.set("from", J::str(fromHex));
        ch.set("to", J::str(to));
        if (!fx.empty()) {
            ch.set("range", J::num(1.5));
            J &ac = sSpec.at("attack_colours");
            J list = ac[fx.c_str()].isArr() ? ac[fx.c_str()] : J::arr();
            list.push(ch);
            ac.set(fx.c_str(), list);
        } else {
            J list = sSpec["colours"].isArr() ? sSpec["colours"] : J::arr();
            list.push(ch);
            sSpec.set("colours", list);
        }
        touch();
        sound(1);
        if (!sOv.empty() && sOv.back().kind == OV_FX) { fx_load(&sOv.back()); }
        if (!sOv.empty() && sOv.back().kind == OV_COLOURS) { colours_load(&sOv.back()); }
    };
    if (wheel) {
        int id = o.id;
        open_wheel("THE NEW COLOUR: ANY COLOUR", fromHex.empty() ? to : from, [use, id](unsigned rgb) {
            if (!sOv.empty() && sOv.back().id == id) { ov_close(); } // (the palette under the wheel goes too)
            use(rgb);
        });
        return;
    }
    if (sIn.ok) {
        ov_close();
        use(to);
    }
}

static void text_frame(Overlay &o, std::vector<Prompt> &ps) {
    sDl->AddText(sFont, S(17), ImVec2(sBoxA.x + S(22), sBoxA.y + S(58)), kInk, o.text.c_str(), NULL, sBoxB.x - sBoxA.x - S(44));
    ps.push_back({B_CROSS, "OK"});
    if (sIn.ok || sIn.back || (sMouse && ImGui::IsMouseClicked(0))) { ov_close(); sound(1); }
}

// =============================================================================================== the workshop
// As the web page's (index.html drawWorkshop), writing the same fields: a piece's layer is spec.parts[piece] (or its
// n-th item).adjust {"scale": [x, y, z], "size", "move": [x, y, z], "turn": [x, y, z] degrees, "nodes": {"<node>":
// {"stretch", "size", "move", "turn", "shown": false}}}; the body is spec.body {"build": {"torso", "arms", "legs",
// "head", "hands", "feet": percent}, "nodes": {...}} (custom_fighter.py: mix.py mix(..., opts, adjust)).
struct WsPath { int piece, layer; std::string node; bool build; };
static const J &ws_read(const WsPath &w) {
    static J none;
    const J *o;
    if (w.piece < 0) {
        o = &sSpec["body"];
        if (w.build) { return (*o)["build"]; }
    } else {
        const J &v = sSpec["parts"][kPieces[w.piece][0]];
        o = &(v.isArr() ? v[(size_t)w.layer] : v)["adjust"];
    }
    return w.node.empty() ? *o : (*o)["nodes"][w.node.c_str()];
}
static J &ws_write(const WsPath &w) {
    static J dummy;
    J *o;
    if (w.piece < 0) {
        o = &sSpec.at("body");
        if (w.build) { return o->at("build"); }
    } else {
        J *v = sSpec.at("parts").find(kPieces[w.piece][0]);
        if (v == NULL) { dummy = J::obj(); return dummy; }
        if (v->isArr()) {
            if (v->a.empty()) { dummy = J::obj(); return dummy; }
            v = &v->a[(size_t)std::min(w.layer, (int)v->a.size() - 1)];
        }
        o = &v->at("adjust");
    }
    return w.node.empty() ? *o : o->at("nodes").at(w.node.c_str());
}
static bool prune_empty(J &o) { // empty objects inside go; true when o is empty itself
    for (size_t i = o.k.size(); i-- > 0;) {
        if (o.a[i].isObj() && prune_empty(o.a[i])) { o.k.erase(o.k.begin() + i); o.a.erase(o.a.begin() + i); }
    }
    return o.isObj() && o.size() == 0;
}
static void ws_prune(void) {
    J *b = sSpec.find("body");
    if (b != NULL && b->isObj() && prune_empty(*b)) { sSpec.erase("body"); }
    J *parts = sSpec.find("parts");
    for (size_t i = 0; parts != NULL && parts->isObj() && i < parts->a.size(); i++) {
        J &v = parts->a[i];
        std::vector<J *> ls;
        if (v.isArr()) { for (J &l : v.a) { ls.push_back(&l); } } else { ls.push_back(&v); }
        for (J *l : ls) {
            J *adj = l->find("adjust");
            if (adj != NULL && adj->isObj() && prune_empty(*adj)) { l->erase("adjust"); }
        }
    }
}
static double ws_get(const WsPath &w, const char *k, int axis, double def) {
    const J &v = ws_read(w)[k];
    if (axis < 0) { return v.isNum() ? v.n : def; }
    return v.isArr() && v[(size_t)axis].isNum() ? v[(size_t)axis].n : def;
}
static void ws_set(const WsPath &w, const char *k, int axis, double val, double def) {
    J &o = ws_write(w);
    if (axis < 0) {
        if (fabs(val - def) < 1e-6) { o.erase(k); } else { o.set(k, J::num(val)); }
    } else {
        J *a = o.find(k);
        if (a == NULL || !a->isArr() || a->size() < 3) {
            J arr = J::arr();
            for (int i = 0; i < 3; i++) { arr.push(J::num(a != NULL && a->isArr() && (*a)[(size_t)i].isNum() ? (*a)[(size_t)i].n : def)); }
            a = &o.set(k, arr);
        }
        a->a[(size_t)axis] = J::num(val);
        bool all = true;
        for (int i = 0; i < 3; i++) { all &= fabs((*a)[(size_t)i].number(def) - def) < 1e-6; }
        if (all) { o.erase(k); }
    }
    ws_prune();
    touch();
}
enum { WF_PCT, WF_PCT100, WF_DEG, WF_NUM };
static std::string ws_fmt(double v, int f) {
    char b[32];
    if (f == WF_PCT) { snprintf(b, sizeof(b), "%d%%", (int)lround(v * 100)); }
    else if (f == WF_PCT100) { snprintf(b, sizeof(b), "%d%%", (int)lround(v)); }
    else if (f == WF_DEG) { snprintf(b, sizeof(b), "%d\xc2\xb0", (int)lround(v)); }
    else { snprintf(b, sizeof(b), "%+.2f", v); }
    return b;
}
static Row ws_slider(const WsPath &w, const char *label, const char *k, int axis, double lo, double hi, double step, double def, int fmt, const char *tip) {
    Row r;
    double v = ws_get(w, k, axis, def);
    std::string key = k;
    r.label = label;
    r.value = ws_fmt(v, fmt);
    r.bar = (float)((v - lo) / (hi - lo));
    r.barDef = (float)((def - lo) / (hi - lo));
    r.tip = tip;
    r.lrLabel = "Adjust (hold: faster)";
    r.lr = [w, key, axis, lo, hi, step, def](int d) {
        double x = ws_get(w, key.c_str(), axis, def) + d * step * (sIn.fast ? 8 : 1);
        x = x < lo ? lo : x > hi ? hi : x;
        x = round(x / step) * step;
        ws_set(w, key.c_str(), axis, x, def);
        if (!sIn.fast || sFrames % 4 == 0) { sound(0); }
    };
    r.setFrac = [w, key, axis, lo, hi, step, def](float f) {
        double x = lo + (hi - lo) * (f < 0 ? 0 : f > 1 ? 1 : f);
        x = round(x / step) * step;
        if (fabs(x - ws_get(w, key.c_str(), axis, def)) > 1e-9) { ws_set(w, key.c_str(), axis, x, def); }
    };
    r.triLabel = "Reset";
    r.tri = [w, key, axis, def] { ws_set(w, key.c_str(), axis, def, def); sound(2); };
    return r;
}
static void ws_load(Overlay *o) {
    o->loading = true;
    J body = J::obj();
    J spec = sSpec;
    if (spec["name"].text().empty()) { spec.set("name", J::str("preview")); }
    body.set("spec", spec);
    int id = o->id;
    api("/api/layernodes", &body, [id](int st, const J &r) {
        Overlay *ov = NULL;
        for (Overlay &x : sOv) { if (x.id == id) { ov = &x; } }
        if (ov == NULL) { return; }
        ov->loading = false;
        if (st != 200) { toast(error_of(st, r), true); return; }
        ov->info = r;
    }, 300000);
}
static void open_workshop(int piece) {
    Overlay o;
    o.kind = OV_WORKSHOP;
    o.piece = piece;
    std::string what = piece < 0 ? "BODY" : kPieces[piece][1];
    for (char &c : what) { c = (char)SDL_toupper((unsigned char)c); }
    o.title = "WORKSHOP: " + what;
    ov_push(o);
    ws_load(&sOv.back());
    sound(1);
}
static void ws_rows(Overlay &o, std::vector<Row> &rows) {
    static J none;
    std::vector<const J *> ls;
    if (o.piece >= 0) {
        const J &all = o.info["layers"];
        for (size_t i = 0; i < all.size(); i++) { if (all[i]["piece"].text() == kPieces[o.piece][0]) { ls.push_back(&all[i]); } }
        int have = (int)layers(kPieces[o.piece][0]).size();
        if ((int)ls.size() > have) { ls.resize((size_t)have); }
        if (ls.empty()) { return; }
        o.layer = std::min(std::max(o.layer, 0), (int)ls.size() - 1);
    }
    const J &nodes = o.piece < 0 ? o.info["body"] : (*ls[(size_t)o.layer])["nodes"];
    if (o.node >= (int)nodes.size()) { o.node = -1; }
    WsPath w = {o.piece, o.layer, o.node >= 0 ? std::to_string((int)nodes[(size_t)o.node]["node"].number(0)) : std::string(), false};
    int id = o.id, nn = (int)nodes.size(), nl = (int)ls.size();
    auto self = [id]() -> Overlay * { for (Overlay &x : sOv) { if (x.id == id) { return &x; } } return NULL; };
    rows.push_back(sec("WHICH PART?"));
    if (nl > 1) {
        Row r;
        r.label = "Layer";
        r.value = (*ls[(size_t)o.layer])["label"].text();
        r.sub = std::to_string(o.layer + 1) + " / " + std::to_string(nl);
        r.tip = "This piece has layers on top of each other: which one to change.";
        r.lrLabel = "Layer";
        r.lr = [self, nl](int d) { Overlay *x = self(); if (x) { x->layer = (x->layer + d + nl) % nl; x->node = -1; sound(0); } };
        r.ok = [self, nl] { Overlay *x = self(); if (x) { x->layer = (x->layer + 1) % nl; x->node = -1; } };
        rows.push_back(r);
    }
    {
        Row r;
        r.label = "Part";
        r.value = o.node < 0 ? std::string(o.piece < 0 ? "Build (the whole body)" : "The whole layer") : nodes[(size_t)o.node]["name"].text("?");
        r.sub = o.node < 0 ? (nn ? std::to_string(nn) + " parts: left / right" : "") : std::to_string(o.node + 1) + " / " + std::to_string(nn);
        if (o.piece >= 0 && o.node < 0 && nl) { r.sub = (*ls[(size_t)o.layer])["label"].text(); }
        r.tip = "Left / right: the part to change - the whole piece, or one of its parts (a lock of hair, a sleeve...).";
        r.lrLabel = "Part";
        r.lr = [self, nn](int d) { Overlay *x = self(); if (x) { x->node = ((x->node + 1 + d + nn + 1) % (nn + 1)) - 1; sound(0); } };
        r.ok = [self, nn] { Overlay *x = self(); if (x) { x->node = ((x->node + 2) % (nn + 1)) - 1; } };
        rows.push_back(r);
    }
    if (o.piece < 0 && o.node < 0) {
        static const char *const kBuild[6][2] = {{"torso", "Torso build"}, {"arms", "Arm build"}, {"legs", "Leg build"},
                                                 {"head", "Head size"}, {"hands", "Hand size"}, {"feet", "Foot size"}};
        rows.push_back(sec("BUILD"));
        WsPath b = w;
        b.build = true;
        for (int i = 0; i < 6; i++) {
            rows.push_back(ws_slider(b, kBuild[i][1], kBuild[i][0], -1, 50, 160, 1, 100, WF_PCT100,
                                     "Thicker or thinner around the bones (the joints stay where they are). Triangle: back to 100%."));
        }
    } else {
        if (o.node >= 0) {
            Row r;
            bool shown = ws_read(w)["shown"].nul() || ws_read(w)["shown"].truthy();
            r.label = "Shown";
            r.value = shown ? "On" : "Off (hidden)";
            r.tip = "Hide this part of the model (a lock of hair, a cape...).";
            auto flip = [w] {
                bool on = ws_read(w)["shown"].nul() || ws_read(w)["shown"].truthy();
                J &t = ws_write(w);
                if (on) { t.set("shown", J::boolean(false)); } else { t.erase("shown"); }
                ws_prune();
                touch();
                sound(0);
            };
            r.ok = flip;
            r.lr = [flip](int) { flip(); };
            r.lrLabel = "On / off";
            rows.push_back(r);
        }
        const char *sk = o.node < 0 ? "scale" : "stretch";
        static const char *const kAx[3] = {"X", "Y", "Z"};
        rows.push_back(sec("SHAPE"));
        for (int i = 0; i < 3; i++) {
            std::string l = std::string("Stretch ") + kAx[i];
            rows.push_back(ws_slider(w, "", sk, i, 0.25, 3, 0.01, 1, WF_PCT, "Stretch it along one axis: X across, Y up, Z front to back."));
            rows.back().label = l;
        }
        rows.push_back(ws_slider(w, "Size", "size", -1, 0.25, 3, 0.01, 1, WF_PCT, "Bigger or smaller, all ways at once."));
        rows.push_back(sec("PLACE"));
        for (int i = 0; i < 3; i++) {
            rows.push_back(ws_slider(w, "", "move", i, -8, 8, 0.05, 0, WF_NUM, "Move it: X across, Y up, Z towards you (a fighter is about 20 high)."));
            rows.back().label = std::string("Move ") + kAx[i];
        }
        for (int i = 0; i < 3; i++) {
            rows.push_back(ws_slider(w, "", "turn", i, -180, 180, 1, 0, WF_DEG, "Turn it about an axis: X tips it forward, Y turns it round, Z tilts it."));
            rows.back().label = std::string("Turn ") + kAx[i];
        }
    }
    rows.push_back(sec("UNDO"));
    Row z;
    z.label = "Reset";
    z.value = "Put this part back as it was";
    z.tip = "Everything of this part back as it was.";
    z.ok = [w] {
        if (!w.node.empty()) {
            WsPath up = w;
            up.node.clear();
            J *n = ws_write(up).find("nodes");
            if (n != NULL) { n->erase(w.node.c_str()); }
        } else if (w.piece < 0) {
            sSpec.at("body").erase("build");
        } else {
            J &t = ws_write(w);
            for (const char *k : {"scale", "size", "move", "turn"}) { t.erase(k); }
        }
        ws_prune();
        touch();
        sound(2);
    };
    rows.push_back(z);
}
static int sBarDrag = -1; // the slider the mouse holds (its row)
static void workshop_frame(Overlay &o, std::vector<Prompt> &ps) {
    float top0 = sBoxA.y + S(54);
    ps.push_back({B_CIRCLE, "Done"});
    if (o.info.nul()) {
        sDl->AddText(sFont, S(18), ImVec2(sBoxA.x + S(24), top0 + S(10)), kInk, o.loading ? "Reading the parts..." : "The parts could not be read.");
        if (sIn.back) { ov_close(); sound(2); }
        return;
    }
    std::vector<Row> rows;
    ws_rows(o, rows);
    if (rows.empty()) {
        sDl->AddText(sFont, S(18), ImVec2(sBoxA.x + S(24), top0 + S(10)), kInk, "This piece is the body's own: pick whose it is first.", NULL, sBoxB.x - sBoxA.x - S(48));
        if (sIn.back) { ov_close(); sound(2); }
        return;
    }
    fix_cursor(rows, o.cur);
    int clicked = draw_rows(ImVec2(sBoxA.x + S(10), top0), ImVec2(sBoxB.x - S(10), sBoxB.y - S(10)), rows, o.cur, o.top, true);
    const Row &r = rows[(size_t)o.cur];
    std::vector<Prompt> mine;
    if (r.bar >= 0.0f) { mine.push_back({B_LR, "Adjust (hold: fast)"}); mine.push_back({B_TRIANGLE, "Reset"}); }
    else if (r.lr) { mine.push_back({B_LR, r.lrLabel != NULL ? r.lrLabel : "Change"}); }
    if (r.ok) { mine.push_back({B_CROSS, "Choose"}); }
    mine.push_back({B_SQUARE, "Reset part"});
    for (const Prompt &p : ps) { mine.push_back(p); }
    ps = mine;
    if (sIn.back) { sBarDrag = -1; ov_close(); sound(2); return; }
    // the mouse on a slider: it follows while held
    if (sMouse && ImGui::IsMouseClicked(0)) {
        for (const auto &b : sBars) { if (ImGui::IsMouseHoveringRect(b.second.Min, b.second.Max, false)) { sBarDrag = b.first; o.cur = b.first; clicked = -1; } }
    }
    if (sBarDrag >= 0 && (!ImGui::IsMouseDown(0) || sBarDrag >= (int)rows.size())) { sBarDrag = -1; }
    if (sBarDrag >= 0) {
        for (const auto &b : sBars) {
            if (b.first == sBarDrag && rows[(size_t)sBarDrag].setFrac) {
                float f = (ImGui::GetIO().MousePos.x - b.second.Min.x - S(8)) / (b.second.Max.x - b.second.Min.x - S(16));
                rows[(size_t)sBarDrag].setFrac(f);
            }
        }
        return;
    }
    if (sIn.sq) { // (the whole part back)
        rows.back().ok();
        return;
    }
    int cur = o.cur, id = o.id;
    list_input(rows, cur, clicked);
    for (Overlay &x : sOv) { if (x.id == id) { x.cur = cur; } }
}

// =============================================================================================== the texture painter
// A secondary way to change the colours (PARTS: "Paint texture..."): a texture of the warrior's built model is brought
// up large, a crosshair aimed on it with the left stick (or the mouse) paints with a brush, fills a spot, takes a
// colour from it or rubs the paint out; the colours come from the colour wheel, the recent ones or the texture's own.
// The paint is kept in the spec as "paint": {"<costume>:<texture>": {w, h, sig, png}} - an RGBA PNG of the texture's
// size, alpha 255 where painted - which custom_fighter.py paints into the texture's palette when it builds (after the
// colour changes; also on the face's eye frames / mouth textures and the damaged costume where they are the same
// picture), so the model beside it shows it once it is built again.
enum { T_BRUSH, T_FILL, T_PICK, T_ERASE, T_COUNT };
static const char *const kTools[T_COUNT] = {"BRUSH", "FILL", "PICKER", "ERASER"};
struct Painter {
    std::string key, sig;
    int index = 0, w = 0, h = 0, bits = 4, variants = 0, costume = 1;
    std::vector<uint8_t> base, ov;             // RGBA: the texture as built (without the paint), the paint (alpha 255)
    std::vector<std::vector<uint8_t>> undo, redo;
    std::vector<unsigned> pal;                 // the texture's own colours
    SDL_GPUTexture *gpu = NULL;
    bool dirty = true;
    float cx = 0, cy = 0, zoom = 1, ox = 0, oy = 0; // the crosshair (texels), the zoom (1: it fits), the texel at the view's corner
    int tool = T_BRUSH, lastTool = T_BRUSH, size = 3, tol = 24;
    unsigned colour = 0xff3030;
    bool stroke = false, mouseDown = false, mouseAim = false;
    float sx = 0, sy = 0;                      // where the stroke was the frame before
    int edits = 0;                             // paint changed since it was written into the spec
};
static Painter sPt;
static std::vector<SDL_GPUTexture *> sFreeLater; // textures drawn this frame: freed the next one

static std::string b64enc(const uint8_t *d, size_t n) {
    static const char *t = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string o;
    o.reserve((n + 2) / 3 * 4);
    for (size_t i = 0; i < n; i += 3) {
        unsigned v = (unsigned)d[i] << 16 | (i + 1 < n ? (unsigned)d[i + 1] << 8 : 0) | (i + 2 < n ? d[i + 2] : 0);
        o += t[(v >> 18) & 63];
        o += t[(v >> 12) & 63];
        o += i + 1 < n ? t[(v >> 6) & 63] : '=';
        o += i + 2 < n ? t[v & 63] : '=';
    }
    return o;
}
static std::string b64dec(const std::string &b64) {
    std::string out;
    unsigned v = 0;
    int bits = 0;
    for (unsigned char c : b64) {
        int d = c >= 'A' && c <= 'Z' ? c - 'A' : c >= 'a' && c <= 'z' ? c - 'a' + 26 : c >= '0' && c <= '9' ? c - '0' + 52 : c == '+' ? 62 : c == '/' ? 63 : -1;
        if (d < 0) { continue; }
        v = (v << 6) | (unsigned)d;
        bits += 6;
        if (bits >= 8) { bits -= 8; out += (char)((v >> bits) & 0xFF); }
    }
    return out;
}
static std::string png_b64(const uint8_t *rgba, int w, int h) { // (SDL's PNG writer)
    std::string out;
    SDL_Surface *s = SDL_CreateSurfaceFrom(w, h, SDL_PIXELFORMAT_RGBA32, (void *)rgba, w * 4);
    SDL_IOStream *io = SDL_IOFromDynamicMem();
    if (s != NULL && io != NULL && SDL_SavePNG_IO(s, io, false)) {
        Sint64 n = SDL_TellIO(io);
        void *p = SDL_GetPointerProperty(SDL_GetIOProperties(io), SDL_PROP_IOSTREAM_DYNAMIC_MEMORY_POINTER, NULL);
        if (p != NULL && n > 0) { out = b64enc((const uint8_t *)p, (size_t)n); }
    }
    if (io != NULL) { SDL_CloseIO(io); }
    if (s != NULL) { SDL_DestroySurface(s); }
    return out;
}
static bool rgba_of_png64(const std::string &b64, std::vector<uint8_t> &out, int *w, int *h) {
    std::string d = b64dec(b64);
    uint8_t *pix = d.empty() ? NULL : decode_png(d.data(), d.size(), w, h);
    if (pix == NULL) { return false; }
    out.assign(pix, pix + (size_t)*w * *h * 4);
    free(pix);
    return true;
}
static void pt_release(void) {
    if (sPt.gpu != NULL) { sFreeLater.push_back(sPt.gpu); sPt.gpu = NULL; }
    sPt.dirty = true;
}
// The paint into the spec (and so into the next build of the model).
static void pt_commit(void) {
    Painter &p = sPt;
    p.edits = 0;
    bool any = false;
    for (size_t i = 3; i < p.ov.size(); i += 4) { if (p.ov[i] >= 128) { any = true; break; } }
    if (!any) {
        J *pa = sSpec.find("paint");
        if (pa != NULL) { pa->erase(p.key.c_str()); }
        clean_empty("paint");
        touch();
        return;
    }
    J e = J::obj();
    e.set("w", J::num(p.w));
    e.set("h", J::num(p.h));
    e.set("sig", J::str(p.sig));
    e.set("png", J::str(png_b64(p.ov.data(), p.w, p.h)));
    sSpec.at("paint").set(p.key.c_str(), e);
    touch();
}
static void pt_snapshot(void) {
    sPt.undo.push_back(sPt.ov);
    if (sPt.undo.size() > 40) { sPt.undo.erase(sPt.undo.begin()); }
    sPt.redo.clear();
}
static void pt_composite(std::vector<uint8_t> &out) { // the texture with its paint (a painted pixel keeps its alpha)
    const Painter &p = sPt;
    out = p.base;
    for (size_t i = 0; i + 3 < out.size() && i + 3 < p.ov.size(); i += 4) {
        if (p.ov[i + 3] >= 128) { out[i] = p.ov[i]; out[i + 1] = p.ov[i + 1]; out[i + 2] = p.ov[i + 2]; }
    }
}
static unsigned pt_pixel(int x, int y, int *alpha) {
    const Painter &p = sPt;
    size_t i = ((size_t)y * p.w + x) * 4;
    const uint8_t *s = p.ov[i + 3] >= 128 ? &p.ov[i] : &p.base[i];
    if (alpha) { *alpha = p.base[i + 3]; }
    return (unsigned)(s[0] << 16 | s[1] << 8 | s[2]);
}
static void pt_dab(float fx, float fy) { // the brush (or the eraser) at a point
    Painter &p = sPt;
    float r = p.size * 0.5f;
    int x0 = (int)floorf(fx - r), x1 = (int)floorf(fx + r), y0 = (int)floorf(fy - r), y1 = (int)floorf(fy + r);
    for (int y = y0; y <= y1; y++) {
        for (int x = x0; x <= x1; x++) {
            if (x < 0 || y < 0 || x >= p.w || y >= p.h) { continue; }
            float dx = x + 0.5f - fx, dy = y + 0.5f - fy;
            if (p.size > 1 && dx * dx + dy * dy > r * r + 0.25f) { continue; }
            if (p.size <= 1 && (x != (int)floorf(fx) || y != (int)floorf(fy))) { continue; }
            uint8_t *o = &p.ov[((size_t)y * p.w + x) * 4];
            if (p.tool == T_ERASE) { o[0] = o[1] = o[2] = o[3] = 0; }
            else { o[0] = (uint8_t)(p.colour >> 16); o[1] = (uint8_t)(p.colour >> 8); o[2] = (uint8_t)p.colour; o[3] = 255; }
        }
    }
    p.dirty = true;
    p.edits++;
}
static void pt_line(float x0, float y0, float x1, float y1) {
    float d = std::max(fabsf(x1 - x0), fabsf(y1 - y0));
    int n = (int)(d * 2.0f) + 1;
    for (int i = 1; i <= n; i++) { pt_dab(x0 + (x1 - x0) * i / n, y0 + (y1 - y0) * i / n); }
}
static void pt_fill(int sx, int sy) { // the spot the crosshair is on: its pixels of about the same colour, joined
    Painter &p = sPt;
    if (sx < 0 || sy < 0 || sx >= p.w || sy >= p.h) { return; }
    int a0;
    unsigned c0 = pt_pixel(sx, sy, &a0);
    std::vector<uint8_t> seen((size_t)p.w * p.h, 0);
    std::vector<int> stack(1, sy * p.w + sx);
    seen[(size_t)(sy * p.w + sx)] = 1;
    int n = 0;
    while (!stack.empty()) {
        int at = stack.back();
        stack.pop_back();
        int x = at % p.w, y = at / p.w;
        uint8_t *o = &p.ov[(size_t)at * 4];
        o[0] = (uint8_t)(p.colour >> 16); o[1] = (uint8_t)(p.colour >> 8); o[2] = (uint8_t)p.colour; o[3] = 255;
        n++;
        static const int kD[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
        for (const auto &d : kD) {
            int nx = x + d[0], ny = y + d[1];
            if (nx < 0 || ny < 0 || nx >= p.w || ny >= p.h || seen[(size_t)(ny * p.w + nx)]) { continue; }
            int a;
            unsigned c = pt_pixel(nx, ny, &a);
            int dr = abs((int)((c >> 16) & 255) - (int)((c0 >> 16) & 255)), dg = abs((int)((c >> 8) & 255) - (int)((c0 >> 8) & 255)), db = abs((int)(c & 255) - (int)(c0 & 255));
            if (std::max(dr, std::max(dg, db)) <= p.tol && (a >= 128) == (a0 >= 128)) {
                seen[(size_t)(ny * p.w + nx)] = 1;
                stack.push_back(ny * p.w + nx);
            }
        }
    }
    p.dirty = true;
    p.edits += n;
}
static void painter_close(void) {
    if (sPt.edits) { pt_commit(); }
    pt_release();
    sPt.stroke = false;
}
static void open_painter(const J &t, int costume) {
    Painter &p = sPt;
    painter_close();
    int w = 0, h = 0;
    std::vector<uint8_t> base;
    if (!rgba_of_png64(t["png"].text(), base, &w, &h) || w != (int)t["w"].number(0) || h != (int)t["h"].number(0)) {
        toast("That texture could not be read.", true);
        return;
    }
    bool same = p.key == t["key"].text() && p.sig == t["sig"].text() && p.w == w && p.h == h;
    p.key = t["key"].text();
    p.sig = t["sig"].text();
    p.index = (int)t["index"].number(0);
    p.w = w;
    p.h = h;
    p.bits = (int)t["bits"].number(4);
    p.variants = (int)t["variants"].number(0);
    p.costume = costume;
    p.base = base;
    p.ov.assign((size_t)w * h * 4, 0);
    const J &have = sSpec["paint"][p.key.c_str()];
    if (have.isObj()) {
        std::vector<uint8_t> ov;
        int ow = 0, oh = 0;
        if (rgba_of_png64(have["png"].text(), ov, &ow, &oh) && ow == w && oh == h) { p.ov = ov; }
    }
    if (!same) {
        p.undo.clear();
        p.redo.clear();
        p.cx = w * 0.5f;
        p.cy = h * 0.5f;
        p.zoom = 1.0f;
    }
    p.pal.clear();
    for (size_t i = 0; i < t["palette"].size(); i++) { p.pal.push_back(hex_rgb(t["palette"][i].text())); }
    p.edits = 0;
    p.dirty = true;
    p.stroke = false;
    Overlay o;
    o.kind = OV_PAINT;
    o.title = "PAINT: TEXTURE " + std::to_string(p.index) + " (COSTUME " + std::to_string(costume) + ")";
    ov_push(o);
    sound(1);
}

// ---- which texture: the textures the shown costume draws with (/api/textures), most used first
static void texpick_load(Overlay *o) {
    o->loading = true;
    J body = J::obj();
    J spec = sSpec;
    if (spec["name"].text().empty()) { spec.set("name", J::str("preview")); }
    body.set("spec", spec);
    body.set("costume", J::num(sShownCostume));
    int id = o->id;
    api("/api/textures", &body, [id](int st, const J &r) {
        Overlay *ov = NULL;
        for (Overlay &x : sOv) { if (x.id == id) { ov = &x; } }
        if (ov == NULL) { return; }
        ov->loading = false;
        if (st != 200) { toast(st == 404 ? "This creator is older than the game: it has no texture painter." : error_of(st, r), true); return; }
        for (SDL_GPUTexture *t : ov->pics) { Ui_TextureFree(t); }
        ov->pics.clear();
        ov->picW.clear();
        ov->picH.clear();
        ov->tex = r["textures"];
        ov->layer = (int)r["costume"].number(1);
        for (size_t i = 0; i < ov->tex.size(); i++) {
            int w = 0, h = 0;
            ov->pics.push_back(png64_tex(ov->tex[i]["png"].text(), &w, &h));
            ov->picW.push_back(w);
            ov->picH.push_back(h);
            if (ov->texture >= 0 && (int)ov->tex[i]["index"].number(-1) == ov->texture) { ov->cur = (int)i; }
        }
        if (ov->texture >= 0 && ov->cur < (int)ov->tex.size()) { // (asked for one texture: straight to it)
            J t = ov->tex[(size_t)ov->cur];
            int costume = ov->layer;
            ov->texture = -1;
            open_painter(t, costume);
        }
    }, 300000);
}
static void open_texpick(int texture) {
    Overlay o;
    o.kind = OV_TEXPICK;
    o.title = "PAINT: WHICH TEXTURE?";
    o.texture = texture;
    ov_push(o);
    texpick_load(&sOv.back());
    sound(1);
}
static void texpick_frame(Overlay &o, std::vector<Prompt> &ps) {
    float top0 = sBoxA.y + S(56);
    int n = (int)o.tex.size();
    if (o.loading && n == 0) {
        sDl->AddText(sFont, S(18), ImVec2(sBoxA.x + S(24), top0 + S(10)), kInk, "Bringing up your warrior's textures...");
    }
    float rh = S(76), gap = S(6);
    int fits = (int)((sBoxB.y - top0 - S(12)) / (rh + gap));
    if (fits < 1) { fits = 1; }
    int was = o.cur;
    if (sIn.up && o.cur > 0) { o.cur--; }
    if (sIn.down && o.cur < n - 1) { o.cur++; }
    if (o.cur != was) { sound(0); }
    if (o.cur < o.top) { o.top = o.cur; }
    if (o.cur >= o.top + fits) { o.top = o.cur - fits + 1; }
    if (sMouse && ImGui::GetIO().MouseWheel != 0 && ImGui::IsMouseHoveringRect(sBoxA, sBoxB)) {
        o.top = std::max(0, std::min(n - fits, o.top - (int)ImGui::GetIO().MouseWheel));
    }
    int clicked = -1;
    for (int i = o.top; i < n && i < o.top + fits; i++) {
        const J &t = o.tex[(size_t)i];
        float y = top0 + (i - o.top) * (rh + gap);
        ImVec2 a(sBoxA.x + S(14), y), b(sBoxB.x - S(14), y + rh);
        bool sel = i == o.cur;
        if (sel) { grad(a, b, kGold1, kGold2, S(10)); } else { sDl->AddRectFilled(a, b, IM_COL32(255, 255, 255, 16), S(10)); }
        float ps2 = rh - S(12);
        ImVec2 pa(a.x + S(6), y + S(6)), pb(a.x + S(6) + ps2, y + S(6) + ps2);
        for (int cy = 0; cy < 4; cy++) { // (a checkerboard behind it: its clear parts)
            for (int cx = 0; cx < 4; cx++) {
                sDl->AddRectFilled(ImVec2(pa.x + cx * ps2 / 4, pa.y + cy * ps2 / 4), ImVec2(pa.x + (cx + 1) * ps2 / 4, pa.y + (cy + 1) * ps2 / 4),
                                   (cx + cy) & 1 ? IM_COL32(90, 80, 120, 255) : IM_COL32(140, 130, 170, 255));
            }
        }
        if (i < (int)o.pics.size() && o.pics[(size_t)i] != NULL) {
            float tw = (float)o.picW[(size_t)i], th = (float)o.picH[(size_t)i], k = std::min(ps2 / tw, ps2 / th);
            ImVec2 c((pa.x + pb.x) * 0.5f, (pa.y + pb.y) * 0.5f);
            sDl->AddImage(ImTextureRef((ImTextureID)(intptr_t)o.pics[(size_t)i]), ImVec2(c.x - tw * k * 0.5f, c.y - th * k * 0.5f), ImVec2(c.x + tw * k * 0.5f, c.y + th * k * 0.5f));
        }
        ImU32 ink = sel ? kDark : kInk;
        char line[128];
        snprintf(line, sizeof(line), "Texture %d", (int)t["index"].number(0));
        sDl->AddText(sBold != NULL ? sBold : sFont, S(18), ImVec2(pb.x + S(14), y + S(8)), ink, line);
        snprintf(line, sizeof(line), "%d x %d, %d colours%s", (int)t["w"].number(0), (int)t["h"].number(0), (int)t["bits"].number(4) == 4 ? 16 : 256,
                 t["variants"].number(0) > 0 ? " - with its eye frames / mouth shapes" : "");
        sDl->AddText(sFont, S(14), ImVec2(pb.x + S(14), y + S(32)), sel ? IM_COL32(107, 58, 0, 255) : kDim, line);
        const J &pal = t["palette"];
        for (size_t c = 0; c < pal.size() && c < 16; c++) {
            ImVec2 ca(pb.x + S(14) + c * S(17), y + S(52));
            sDl->AddRectFilled(ca, ImVec2(ca.x + S(14), ca.y + S(14)), col32(hex_rgb(pal[c].text())), S(3));
        }
        if (sSpec["paint"].has(t["key"].text().c_str())) {
            const char *pt = "PAINTED";
            float w = text_w(sFont, S(13), pt) + S(16);
            ImVec2 ca(b.x - w - S(10), y + S(8));
            sDl->AddRectFilled(ca, ImVec2(ca.x + w, ca.y + S(22)), sel ? IM_COL32(74, 26, 0, 255) : IM_COL32(245, 197, 66, 255), S(11));
            sDl->AddText(sFont, S(13), ImVec2(ca.x + S(8), ca.y + S(4)), sel ? kGold1 : kDark, pt);
        }
        if (click_in(a, b)) { clicked = i; }
    }
    if (clicked >= 0) { o.cur = clicked; sIn.ok = true; }
    bool painted = n > 0 && sSpec["paint"].has(o.tex[(size_t)o.cur]["key"].text().c_str());
    ps.push_back({B_CROSS, "Paint on it"});
    if (painted) { ps.push_back({B_SQUARE, "Remove its paint"}); }
    ps.push_back({B_CIRCLE, "Back"});
    if (sIn.back) { ov_close(); sound(2); return; }
    if (n == 0) { return; }
    if (sIn.sq && painted) {
        J *pa = sSpec.find("paint");
        if (pa != NULL) { pa->erase(o.tex[(size_t)o.cur]["key"].text().c_str()); }
        clean_empty("paint");
        touch();
        sound(2);
        return;
    }
    if (sIn.ok) { J t = o.tex[(size_t)o.cur]; open_painter(t, o.layer); }
}

// ---- the painter itself
static void painter_frame(Overlay &o, std::vector<Prompt> &ps) {
    Painter &p = sPt;
    ImGuiIO &io = ImGui::GetIO();
    if (p.w <= 0 || p.base.empty()) { ov_close(); return; }
    // the view: a square on the left of the box; the tools beside it
    ImVec2 va(sBoxA.x + S(16), sBoxA.y + S(54));
    float side = std::min(sBoxB.y - S(14) - va.y, sBoxB.x - sBoxA.x - S(320));
    ImVec2 vb(va.x + side, va.y + side);
    float fit = side / (float)std::max(p.w, p.h);
    bool over = sMouse && ImGui::IsMouseHoveringRect(va, vb, false);
    // zoom: the right stick up / down, + / -, the wheel over it (about the crosshair, or the mouse)
    float dz = sIn.zoom != 0.0f && !sIn.r3 ? powf(1.03f, sIn.zoom) : 1.0f;
    if (over && io.MouseWheel != 0.0f) { dz *= powf(1.25f, io.MouseWheel); }
    if (dz != 1.0f) {
        float sc0 = fit * p.zoom;
        float ax = over ? p.ox + (io.MousePos.x - va.x) / sc0 : p.cx, ay = over ? p.oy + (io.MousePos.y - va.y) / sc0 : p.cy;
        float sxp = (ax - p.ox) * sc0, syp = (ay - p.oy) * sc0;
        p.zoom = std::max(1.0f, std::min(16.0f, p.zoom * dz));
        float sc1 = fit * p.zoom;
        p.ox = ax - sxp / sc1;
        p.oy = ay - syp / sc1;
    }
    float sc = fit * p.zoom, visW = side / sc, visH = side / sc;
    // the crosshair: the left stick (faster the further), the d-pad / arrows a texel a press, the mouse
    bool moved = false;
    float speed = std::max(p.w, p.h) / 128.0f * 1.6f / sqrtf(p.zoom);
    if (fabsf(sIn.lx) > 0.05f || fabsf(sIn.ly) > 0.05f) {
        p.cx += sIn.lx * fabsf(sIn.lx) * speed * 2.0f;
        p.cy += sIn.ly * fabsf(sIn.ly) * speed * 2.0f;
        moved = true;
        p.mouseAim = false;
    }
    if (sIn.dleft || sIn.dright || sIn.dup || sIn.ddown) {
        p.cx = floorf(p.cx) + 0.5f + (sIn.dright ? 1 : 0) - (sIn.dleft ? 1 : 0);
        p.cy = floorf(p.cy) + 0.5f + (sIn.ddown ? 1 : 0) - (sIn.dup ? 1 : 0);
        moved = true;
        p.mouseAim = false;
    }
    if (over && (io.MouseDelta.x != 0 || io.MouseDelta.y != 0 || ImGui::IsMouseClicked(0))) {
        p.cx = p.ox + (io.MousePos.x - va.x) / sc;
        p.cy = p.oy + (io.MousePos.y - va.y) / sc;
        p.mouseAim = true;
    }
    p.cx = std::max(0.0f, std::min((float)p.w - 0.01f, p.cx));
    p.cy = std::max(0.0f, std::min((float)p.h - 0.01f, p.cy));
    // the view follows the crosshair (not the mouse's); the right mouse button drags it
    if (moved) {
        float m = visW * 0.12f;
        if (p.cx < p.ox + m) { p.ox = p.cx - m; }
        if (p.cx > p.ox + visW - m) { p.ox = p.cx - visW + m; }
        if (p.cy < p.oy + m) { p.oy = p.cy - m; }
        if (p.cy > p.oy + visH - m) { p.oy = p.cy - visH + m; }
    }
    if (over && ImGui::IsMouseDragging(1)) { p.ox -= io.MouseDelta.x / sc; p.oy -= io.MouseDelta.y / sc; }
    if (sIn.r3) { p.ox += sIn.turn * visW * 0.02f; p.oy -= sIn.zoom * visH * 0.02f; }
    p.ox = visW >= p.w ? (p.w - visW) * 0.5f : std::max(0.0f, std::min(p.w - visW, p.ox));
    p.oy = visH >= p.h ? (p.h - visH) * 0.5f : std::max(0.0f, std::min(p.h - visH, p.oy));

    // ---- the tools (the buttons, and the mouse on the side panel)
    float x0 = vb.x + S(18), x1 = sBoxB.x - S(16), y = va.y;
    ImFont *bold = sBold != NULL ? sBold : sFont;
    int clickTool = -1, clickColour = -1;
    unsigned clickRgb = 0;
    bool clickWheel = false, clickUndo = false, clickRedo = false, clickMinus = false, clickPlus = false;
    sDl->AddText(sFont, S(13), ImVec2(x0, y), kDim, "TOOL");
    y += S(18);
    float cw = (x1 - x0 - S(8)) * 0.5f;
    for (int t = 0; t < T_COUNT; t++) {
        ImVec2 a(x0 + (t % 2) * (cw + S(8)), y + (t / 2) * S(36)), b(a.x + cw, a.y + S(30));
        if (t == p.tool) { grad(a, b, kGold1, kGold2, S(15)); } else { grad(a, b, IM_COL32(198, 181, 245, 255), IM_COL32(125, 100, 200, 255), S(15)); }
        sDl->AddText(bold, S(15), ImVec2(a.x + (cw - text_w(bold, S(15), kTools[t])) * 0.5f, a.y + S(7)), t == p.tool ? IM_COL32(74, 26, 0, 255) : IM_COL32(43, 22, 96, 255), kTools[t]);
        if (click_in(a, b)) { clickTool = t; }
    }
    y += S(78);
    {
        char line[64];
        if (p.tool == T_FILL) { snprintf(line, sizeof(line), "Fill tolerance: %d", p.tol); }
        else if (p.tool == T_PICK) { snprintf(line, sizeof(line), "Takes the colour it is on"); }
        else { snprintf(line, sizeof(line), "%s size: %d px", p.tool == T_ERASE ? "Eraser" : "Brush", p.size); }
        float gx = x0 + glyph(B_L2R2, ImVec2(x0, y + S(10)), S(10)) + S(8);
        sDl->AddText(sFont, S(15), ImVec2(gx, y + S(1)), kInk, line);
        if (p.tool != T_PICK) {
            ImVec2 ma(x1 - S(54), y - S(2)), mb(x1 - S(30), y + S(22)), pa(x1 - S(24), y - S(2)), pb(x1, y + S(22));
            sDl->AddRectFilled(ma, mb, IM_COL32(42, 26, 85, 255), S(5));
            sDl->AddRectFilled(pa, pb, IM_COL32(42, 26, 85, 255), S(5));
            sDl->AddText(bold, S(16), ImVec2(ma.x + S(8), ma.y + S(2)), kInk, "-");
            sDl->AddText(bold, S(16), ImVec2(pa.x + S(7), pa.y + S(2)), kInk, "+");
            clickMinus = click_in(ma, mb);
            clickPlus = click_in(pa, pb);
        }
    }
    y += S(32);
    sDl->AddText(sFont, S(13), ImVec2(x0, y), kDim, "COLOUR");
    y += S(18);
    {
        ImVec2 a(x0, y), b(x0 + S(56), y + S(56));
        sDl->AddRectFilled(a, b, col32(p.colour), S(8));
        sDl->AddRect(a, b, IM_COL32(255, 255, 255, 220), S(8), 0, S(2));
        sDl->AddText(bold, S(17), ImVec2(b.x + S(12), y + S(2)), kInk, hex_of(p.colour).c_str());
        ImVec2 wa(b.x + S(12), y + S(26)), wb(x1, y + S(54));
        grad(wa, wb, IM_COL32(198, 181, 245, 255), IM_COL32(125, 100, 200, 255), S(14));
        float gx = wa.x + S(6) + glyph(B_TRIANGLE, ImVec2(wa.x + S(6), (wa.y + wb.y) * 0.5f), S(9)) + S(6);
        sDl->AddText(sFont, S(14), ImVec2(gx, wa.y + S(6)), IM_COL32(43, 22, 96, 255), "Colour wheel");
        clickWheel = click_in(a, b) || click_in(wa, wb);
    }
    y += S(66);
    auto swatches = [&](const char *label, const std::vector<unsigned> &cols) {
        if (cols.empty()) { return; }
        sDl->AddText(sFont, S(13), ImVec2(x0, y), kDim, label);
        y += S(17);
        float s0 = S(22), g = S(5);
        int per = std::max(1, (int)((x1 - x0 + g) / (s0 + g)));
        for (size_t i = 0; i < cols.size() && i < 32; i++) {
            ImVec2 a(x0 + (i % per) * (s0 + g), y + (i / per) * (s0 + g)), b(a.x + s0, a.y + s0);
            sDl->AddRectFilled(a, b, col32(cols[i]), S(4));
            sDl->AddRect(a, b, cols[i] == p.colour ? IM_COL32(255, 226, 122, 255) : IM_COL32(255, 255, 255, 60), S(4), 0, cols[i] == p.colour ? S(2.5f) : S(1));
            if (click_in(a, b)) { clickColour = 1; clickRgb = cols[i]; }
        }
        y += ((std::min(cols.size(), (size_t)32) + per - 1) / per) * (s0 + g) + S(6);
    };
    swatches("RECENT", sRecent);
    swatches("ITS OWN COLOURS", p.pal);
    {
        char line[160];
        snprintf(line, sizeof(line), "%d x %d, %d colours: new colours share them (close ones are merged).%s", p.w, p.h, p.bits == 4 ? 16 : 256,
                 p.variants > 0 ? " Its eye frames and mouth shapes get the paint too." : "");
        sDl->AddText(sFont, S(13), ImVec2(x0, y), kDim, line, NULL, x1 - x0);
        y += S(56);
        ImVec2 ua(x0, y), ub(x0 + cw, y + S(26)), ra(x0 + cw + S(8), y), rb(x1, y + S(26));
        grad(ua, ub, IM_COL32(198, 181, 245, p.undo.empty() ? 110 : 255), IM_COL32(125, 100, 200, p.undo.empty() ? 110 : 255), S(13));
        grad(ra, rb, IM_COL32(198, 181, 245, p.redo.empty() ? 110 : 255), IM_COL32(125, 100, 200, p.redo.empty() ? 110 : 255), S(13));
        snprintf(line, sizeof(line), "L1 Undo (%d)", (int)p.undo.size());
        if (sKeyboardLast) { snprintf(line, sizeof(line), "Ctrl+Z Undo (%d)", (int)p.undo.size()); }
        sDl->AddText(sFont, S(13), ImVec2(ua.x + S(10), ua.y + S(5)), IM_COL32(43, 22, 96, 255), line);
        snprintf(line, sizeof(line), sKeyboardLast ? "Ctrl+Y Redo (%d)" : "R1 Redo (%d)", (int)p.redo.size());
        sDl->AddText(sFont, S(13), ImVec2(ra.x + S(10), ra.y + S(5)), IM_COL32(43, 22, 96, 255), line);
        clickUndo = click_in(ua, ub);
        clickRedo = click_in(ra, rb);
        y += S(34);
        snprintf(line, sizeof(line), "Zoom %.1fx   crosshair %d, %d", p.zoom, (int)p.cx, (int)p.cy);
        sDl->AddText(sFont, S(13), ImVec2(x0, y), kDim, line);
    }

    // ---- what the buttons do
    ps.push_back({B_CROSS, p.tool == T_FILL ? "Fill" : p.tool == T_PICK ? "Take colour" : p.tool == T_ERASE ? "Rub out" : "Paint (hold)"});
    ps.push_back({B_SQUARE, "Tool"});
    ps.push_back({B_TRIANGLE, "Colour wheel"});
    ps.push_back({B_L1R1, "Undo / redo"});
    ps.push_back({B_L2R2, p.tool == T_FILL ? "Tolerance" : "Size"});
    ps.push_back({B_RSTICK, "Zoom"});
    ps.push_back({B_CIRCLE, "Done"});
    if (sIn.back) { painter_close(); ov_close(); sound(2); return; }
    if (clickTool >= 0 || sIn.sq) {
        int t = clickTool >= 0 ? clickTool : (p.tool + 1) % T_COUNT;
        if (t == T_PICK && p.tool != T_PICK) { p.lastTool = p.tool; }
        p.tool = t;
        sound(0);
    }
    if (sIn.l2 || sIn.r2 || clickMinus || clickPlus) {
        int d = sIn.r2 || clickPlus ? 1 : -1;
        if (p.tool == T_FILL) { p.tol = std::max(0, std::min(128, p.tol + d * 4)); }
        else { p.size = std::max(1, std::min(32, p.size + d * (p.size >= 8 ? 2 : 1))); }
        sound(0);
    }
    bool undo = sIn.l1 || sIn.undo || clickUndo, redo = sIn.r1 || sIn.redo || clickRedo;
    if (undo && !p.undo.empty() && !p.stroke) {
        p.redo.push_back(p.ov);
        p.ov = p.undo.back();
        p.undo.pop_back();
        p.dirty = true;
        pt_commit();
        sound(2);
    } else if (redo && !p.redo.empty() && !p.stroke) {
        p.undo.push_back(p.ov);
        p.ov = p.redo.back();
        p.redo.pop_back();
        p.dirty = true;
        pt_commit();
        sound(0);
    }
    if (clickColour >= 0) { p.colour = clickRgb; sound(0); }
    if (sIn.tri || clickWheel) {
        open_wheel("THE BRUSH'S COLOUR", p.colour, [](unsigned rgb) { sPt.colour = rgb; add_recent(rgb); if (sPt.tool == T_PICK || sPt.tool == T_ERASE) { sPt.tool = T_BRUSH; } });
        return;
    }
    // the tool on the texture: cross (held) or the left mouse button pressed on it
    if (sMouse && ImGui::IsMouseClicked(0) && over) { p.mouseDown = true; }
    if (!ImGui::IsMouseDown(0)) { p.mouseDown = false; }
    bool press = sIn.ok || (p.mouseDown && ImGui::IsMouseClicked(0));
    bool held = sIn.okHeld || p.mouseDown;
    int tx = (int)floorf(p.cx), ty = (int)floorf(p.cy);
    if (p.tool == T_BRUSH || p.tool == T_ERASE) {
        if (held && !p.stroke) {
            pt_snapshot();
            p.stroke = true;
            pt_dab(p.cx, p.cy);
            p.sx = p.cx;
            p.sy = p.cy;
        } else if (held) {
            if (p.cx != p.sx || p.cy != p.sy) { pt_line(p.sx, p.sy, p.cx, p.cy); }
            p.sx = p.cx;
            p.sy = p.cy;
        } else if (p.stroke) {
            p.stroke = false;
            pt_commit();
            add_recent(p.colour);
        }
    } else if (press && p.tool == T_FILL) {
        pt_snapshot();
        pt_fill(tx, ty);
        pt_commit();
        add_recent(p.colour);
        sound(1);
    } else if (press && p.tool == T_PICK) {
        int a;
        p.colour = pt_pixel(tx, ty, &a);
        add_recent(p.colour);
        p.tool = p.lastTool == T_PICK ? T_BRUSH : p.lastTool;
        sound(1);
    }

    // ---- the texture, as painted, on a checkerboard; the crosshair and the brush's outline over it
    if (p.dirty || p.gpu == NULL) {
        std::vector<uint8_t> pic;
        pt_composite(pic);
        uint8_t *pix = (uint8_t *)malloc(pic.size());
        if (pix != NULL) {
            memcpy(pix, pic.data(), pic.size());
            pt_release();
            p.gpu = Ui_Texture(pix, (uint32_t)p.w, (uint32_t)p.h);
        }
        p.dirty = false;
    }
    sDl->AddRectFilled(ImVec2(va.x - S(3), va.y - S(3)), ImVec2(vb.x + S(3), vb.y + S(3)), IM_COL32(20, 10, 40, 255), S(6));
    sDl->PushClipRect(va, vb, true);
    float ck = std::max(S(12), sc * 4.0f);
    for (float cy = va.y; cy < vb.y; cy += ck) {
        for (float cx = va.x; cx < vb.x; cx += ck) {
            int k = (int)((cx - va.x) / ck) + (int)((cy - va.y) / ck);
            sDl->AddRectFilled(ImVec2(cx, cy), ImVec2(cx + ck, cy + ck), k & 1 ? IM_COL32(70, 60, 100, 255) : IM_COL32(110, 100, 140, 255));
        }
    }
    ImVec2 i0(va.x - p.ox * sc, va.y - p.oy * sc), i1(i0.x + p.w * sc, i0.y + p.h * sc);
    if (p.gpu != NULL) { sDl->AddImage(ImTextureRef((ImTextureID)(intptr_t)p.gpu), i0, i1); }
    if (p.zoom >= 6.0f) { // (the texel grid, close up)
        for (int gx = (int)p.ox; gx <= (int)(p.ox + visW) + 1; gx++) { float sx2 = i0.x + gx * sc; sDl->AddLine(ImVec2(sx2, va.y), ImVec2(sx2, vb.y), IM_COL32(0, 0, 0, 40)); }
        for (int gy = (int)p.oy; gy <= (int)(p.oy + visH) + 1; gy++) { float sy2 = i0.y + gy * sc; sDl->AddLine(ImVec2(va.x, sy2), ImVec2(vb.x, sy2), IM_COL32(0, 0, 0, 40)); }
    }
    ImVec2 c(i0.x + p.cx * sc, i0.y + p.cy * sc);
    if (p.tool == T_BRUSH || p.tool == T_ERASE) {
        float r = std::max(p.size * 0.5f * sc, S(3));
        if (p.size <= 1) {
            ImVec2 a(i0.x + tx * sc, i0.y + ty * sc);
            sDl->AddRect(a, ImVec2(a.x + sc, a.y + sc), IM_COL32(0, 0, 0, 255), 0, 0, S(3));
            sDl->AddRect(a, ImVec2(a.x + sc, a.y + sc), IM_COL32(255, 255, 255, 255), 0, 0, S(1.2f));
        } else {
            sDl->AddCircle(c, r, IM_COL32(0, 0, 0, 255), 0, S(3));
            sDl->AddCircle(c, r, p.tool == T_ERASE ? IM_COL32(255, 255, 255, 255) : col32(p.colour), 0, S(1.5f));
        }
    }
    float arm = S(16), gapc = S(5);
    for (int pass = 0; pass < 2; pass++) { // (the crosshair: dark under light)
        ImU32 col = pass == 0 ? IM_COL32(0, 0, 0, 230) : IM_COL32(255, 255, 255, 255);
        float th = pass == 0 ? S(4) : S(2);
        sDl->AddLine(ImVec2(c.x - arm - gapc, c.y), ImVec2(c.x - gapc, c.y), col, th);
        sDl->AddLine(ImVec2(c.x + gapc, c.y), ImVec2(c.x + arm + gapc, c.y), col, th);
        sDl->AddLine(ImVec2(c.x, c.y - arm - gapc), ImVec2(c.x, c.y - gapc), col, th);
        sDl->AddLine(ImVec2(c.x, c.y + gapc), ImVec2(c.x, c.y + arm + gapc), col, th);
    }
    sDl->PopClipRect();
    sDl->AddRect(ImVec2(va.x - S(3), va.y - S(3)), ImVec2(vb.x + S(3), vb.y + S(3)), IM_COL32(245, 197, 66, 255), S(6), 0, S(2));
    // what is under the crosshair
    int ua;
    unsigned under = pt_pixel(tx, ty, &ua);
    ImVec2 ua0(vb.x - S(118), vb.y - S(30));
    sDl->AddRectFilled(ua0, ImVec2(vb.x - S(4), vb.y - S(4)), IM_COL32(20, 10, 40, 200), S(6));
    sDl->AddRectFilled(ImVec2(ua0.x + S(5), ua0.y + S(5)), ImVec2(ua0.x + S(21), ua0.y + S(21)), col32(under), S(3));
    sDl->AddText(sFont, S(13), ImVec2(ua0.x + S(27), ua0.y + S(5)), ua >= 128 ? kInk : kDim, ua >= 128 ? hex_of(under).c_str() : "(clear)");
}

// =============================================================================================== the colour wheel
// A hue ring around a saturation / brightness square, the colour before and after, its code (typed: square), the recent
// colours. Left stick: saturation and brightness; d-pad / arrows: the hue (left / right) and the brightness (up /
// down); L1 / R1 saturation; L2 / R2 the recent colours; the mouse drags on the ring and the square.
static int sWheelDrag; // 1 the ring, 2 the square (the mouse holds it)
static void open_wheel(const std::string &title, unsigned start, std::function<void(unsigned)> picked) {
    Overlay o;
    o.kind = OV_WHEEL;
    o.title = title;
    o.startRgb = start;
    rgb_hsv(start, &o.hue, &o.sat, &o.val);
    o.rgbPicked = picked;
    o.cur = -1;
    ov_push(o);
    sWheelDrag = 0;
    sound(1);
}
static void wheel_frame(Overlay &o, std::vector<Prompt> &ps) {
    ImGuiIO &io = ImGui::GetIO();
    float top0 = sBoxA.y + S(58);
    float R = std::min((sBoxB.y - top0 - S(24)) * 0.5f, S(170)), ring = S(26);
    ImVec2 c(sBoxA.x + S(28) + R, top0 + R + S(4));
    float inner = R - ring, half = inner * 0.70f;
    ImVec2 sa(c.x - half, c.y - half), sb(c.x + half, c.y + half);
    // the input
    float step = sIn.fast ? 8.0f : 2.0f;
    if (sIn.dleft) { o.hue -= step; }
    if (sIn.dright) { o.hue += step; }
    if (sIn.dup) { o.val += 0.02f; }
    if (sIn.ddown) { o.val -= 0.02f; }
    if (sIn.l1) { o.sat -= 0.04f; }
    if (sIn.r1) { o.sat += 0.04f; }
    if (fabsf(sIn.lx) > 0.05f) { o.sat += sIn.lx * 0.015f; }
    if (fabsf(sIn.ly) > 0.05f) { o.val -= sIn.ly * 0.015f; }
    if (sIn.dleft || sIn.dright || sIn.dup || sIn.ddown || sIn.l1 || sIn.r1) { o.cur = -1; }
    if (sMouse && ImGui::IsMouseClicked(0)) {
        float dx = io.MousePos.x - c.x, dy = io.MousePos.y - c.y, d = sqrtf(dx * dx + dy * dy);
        sWheelDrag = d >= inner - S(2) && d <= R + S(6) ? 1 : ImGui::IsMouseHoveringRect(sa, sb, false) ? 2 : 0;
    }
    if (!ImGui::IsMouseDown(0)) { sWheelDrag = 0; }
    if (sWheelDrag == 1) { o.hue = atan2f(io.MousePos.y - c.y, io.MousePos.x - c.x) * 57.29578f; o.cur = -1; }
    if (sWheelDrag == 2) {
        o.sat = (io.MousePos.x - sa.x) / (sb.x - sa.x);
        o.val = 1.0f - (io.MousePos.y - sa.y) / (sb.y - sa.y);
        o.cur = -1;
    }
    o.hue = fmodf(o.hue + 360.0f, 360.0f);
    o.sat = o.sat < 0 ? 0 : o.sat > 1 ? 1 : o.sat;
    o.val = o.val < 0 ? 0 : o.val > 1 ? 1 : o.val;
    if ((sIn.l2 || sIn.r2) && !sRecent.empty()) {
        int n = (int)sRecent.size();
        o.cur = o.cur < 0 ? (sIn.r2 ? 0 : n - 1) : (o.cur + (sIn.r2 ? 1 : n - 1)) % n;
        rgb_hsv(sRecent[(size_t)o.cur], &o.hue, &o.sat, &o.val);
        sound(0);
    }
    unsigned now = hsv_rgb(o.hue, o.sat, o.val);
    // the ring
    const int N = 120;
    for (int i = 0; i < N; i++) {
        float a0 = i * 6.2831853f / N, a1 = (i + 1.15f) * 6.2831853f / N;
        ImVec2 p0(c.x + cosf(a0) * inner, c.y + sinf(a0) * inner), p1(c.x + cosf(a0) * R, c.y + sinf(a0) * R);
        ImVec2 p2(c.x + cosf(a1) * R, c.y + sinf(a1) * R), p3(c.x + cosf(a1) * inner, c.y + sinf(a1) * inner);
        sDl->AddQuadFilled(p0, p1, p2, p3, col32(hsv_rgb(i * 360.0f / N, 1, 1)));
    }
    sDl->AddCircle(c, R, IM_COL32(255, 255, 255, 120), 0, S(1.5f));
    sDl->AddCircle(c, inner, IM_COL32(255, 255, 255, 120), 0, S(1.5f));
    {
        float a = o.hue / 57.29578f, rm = (inner + R) * 0.5f;
        ImVec2 m(c.x + cosf(a) * rm, c.y + sinf(a) * rm);
        sDl->AddCircleFilled(m, ring * 0.42f, col32(hsv_rgb(o.hue, 1, 1)));
        sDl->AddCircle(m, ring * 0.42f, IM_COL32(0, 0, 0, 255), 0, S(3));
        sDl->AddCircle(m, ring * 0.42f, IM_COL32(255, 255, 255, 255), 0, S(1.5f));
    }
    // the square: white to the hue across, to black down
    ImU32 hc = col32(hsv_rgb(o.hue, 1, 1));
    sDl->AddRectFilledMultiColor(sa, sb, IM_COL32_WHITE, hc, hc, IM_COL32_WHITE);
    sDl->AddRectFilledMultiColor(sa, sb, IM_COL32(0, 0, 0, 0), IM_COL32(0, 0, 0, 0), IM_COL32_BLACK, IM_COL32_BLACK);
    sDl->AddRect(sa, sb, IM_COL32(255, 255, 255, 160), 0, 0, S(1.5f));
    ImVec2 k(sa.x + o.sat * (sb.x - sa.x), sa.y + (1.0f - o.val) * (sb.y - sa.y));
    sDl->AddCircle(k, S(8), IM_COL32(0, 0, 0, 255), 0, S(3.5f));
    sDl->AddCircle(k, S(8), IM_COL32(255, 255, 255, 255), 0, S(1.8f));
    // beside it: before and after, the code, the recent colours
    float x0 = c.x + R + S(30), x1 = sBoxB.x - S(20), y = top0;
    ImFont *bold = sBold != NULL ? sBold : sFont;
    sDl->AddText(sFont, S(13), ImVec2(x0, y), kDim, "WAS");
    sDl->AddText(sFont, S(13), ImVec2(x0 + S(70), y), kDim, "NEW");
    y += S(17);
    sDl->AddRectFilled(ImVec2(x0, y), ImVec2(x0 + S(60), y + S(60)), col32(o.startRgb), S(8));
    sDl->AddRectFilled(ImVec2(x0 + S(70), y), ImVec2(x0 + S(130), y + S(60)), col32(now), S(8));
    sDl->AddRect(ImVec2(x0 + S(70), y), ImVec2(x0 + S(130), y + S(60)), IM_COL32(255, 226, 122, 255), S(8), 0, S(2.5f));
    y += S(72);
    ImVec2 ha(x0, y), hb(x1, y + S(34));
    sDl->AddRectFilled(ha, hb, IM_COL32(0, 0, 0, 90), S(8));
    sDl->AddRect(ha, hb, IM_COL32(169, 147, 255, 255), S(8), 0, S(1.5f));
    sDl->AddText(bold, S(19), ImVec2(ha.x + S(10), ha.y + S(6)), kInk, hex_of(now).c_str());
    glyph(B_SQUARE, ImVec2(hb.x - S(30), (ha.y + hb.y) * 0.5f), S(10));
    bool typeHex = click_in(ha, hb);
    y += S(44);
    char line[96];
    snprintf(line, sizeof(line), "Hue %d  Sat %d%%  Bright %d%%", (int)lround(o.hue), (int)lround(o.sat * 100), (int)lround(o.val * 100));
    sDl->AddText(sFont, S(14), ImVec2(x0, y), kDim, line);
    y += S(20);
    snprintf(line, sizeof(line), "R %d  G %d  B %d", (int)(now >> 16) & 255, (int)(now >> 8) & 255, (int)now & 255);
    sDl->AddText(sFont, S(14), ImVec2(x0, y), kDim, line);
    y += S(28);
    bool pickedRecent = false;
    if (!sRecent.empty()) {
        sDl->AddText(sFont, S(13), ImVec2(x0, y), kDim, sKeyboardLast ? "RECENT (Z / C)" : "RECENT (L2 / R2)");
        y += S(17);
        float s0 = S(24), g = S(6);
        int per = std::max(1, (int)((x1 - x0 + g) / (s0 + g)));
        for (size_t i = 0; i < sRecent.size(); i++) {
            ImVec2 a(x0 + (i % per) * (s0 + g), y + (i / per) * (s0 + g)), b(a.x + s0, a.y + s0);
            sDl->AddRectFilled(a, b, col32(sRecent[i]), S(4));
            bool sel = (int)i == o.cur;
            sDl->AddRect(a, b, sel ? IM_COL32(255, 226, 122, 255) : IM_COL32(255, 255, 255, 60), S(4), 0, sel ? S(2.5f) : S(1));
            if (click_in(a, b)) { o.cur = (int)i; rgb_hsv(sRecent[i], &o.hue, &o.sat, &o.val); pickedRecent = true; }
        }
    }
    (void)pickedRecent;
    ps.push_back({B_CROSS, "Use this colour"});
    ps.push_back({B_DPAD, "Hue / brightness"});
    ps.push_back({B_LSTICK, "Saturation"});
    ps.push_back({B_SQUARE, "Type a code"});
    ps.push_back({B_CIRCLE, "Back"});
    if (sIn.back) { ov_close(); sound(2); return; }
    if (sIn.sq || typeHex) {
        int id = o.id;
        open_keys("THE COLOUR'S CODE (#RRGGBB)", hex_of(now), 7, [id](const std::string &t) {
            std::string s = t;
            if (!s.empty() && s[0] == '#') { s.erase(0, 1); }
            if (s.size() == 3) { s = std::string() + s[0] + s[0] + s[1] + s[1] + s[2] + s[2]; }
            if (s.size() != 6 || strspn(s.c_str(), "0123456789abcdefABCDEF") != 6) { toast("A colour's code is # and six of 0-9, A-F: #ff8800", true); return; }
            for (Overlay &x : sOv) { if (x.id == id) { rgb_hsv((unsigned)strtoul(s.c_str(), NULL, 16), &x.hue, &x.sat, &x.val); x.cur = -1; } }
        });
        return;
    }
    if (sIn.ok) {
        std::function<void(unsigned)> f = o.rgbPicked;
        ov_close();
        sound(1);
        if (f) { f(now); }
    }
}

// =============================================================================================== the camera
// Zoomed in and out (the right stick up / down, + / -, the wheel over the model), moved (R3 + the right stick, the right
// mouse button dragging it), and two places to go back to (L3 / V, or the chips): the whole warrior and its head.
static int sCamPreset; // 0 full, 1 head (what L3 goes to next is the other)
static void cam_set(float zoom, float focus, float px, float py) {
    SDL_LockMutex(sShare);
    sSh.camZoom = zoom < 0.6f ? 0.6f : zoom > 4.5f ? 4.5f : zoom;
    sSh.camFocus = focus;
    sSh.camPanX = px < -700.0f ? -700.0f : px > 500.0f ? 500.0f : px;
    sSh.camPanY = py < -900.0f ? -900.0f : py > 900.0f ? 900.0f : py;
    SDL_UnlockMutex(sShare);
}
static void cam_preset(int which) {
    sCamPreset = which;
    if (which == 1) { cam_set(3.0f, 1.0f, 0.0f, 240.0f - (CAM_FEET + 49.5f - CAM_H * 0.711f)); } // (the face about the middle of the picture: seen)
    else { cam_set(1.0f, 0.5f, 0.0f, 0.0f); }
}
static void cam_frame(float boxRight, bool padZoom) {
    ImGuiIO &io = ImGui::GetIO();
    SDL_LockMutex(sShare);
    float z = sSh.camZoom, f = sSh.camFocus, px = sSh.camPanX, py = sSh.camPanY;
    SDL_UnlockMutex(sShare);
    float k = sW / 1376.0f; // (screen pixels a mockup pixel)
    bool overModel = sMouse && io.MousePos.x > boxRight && io.MousePos.y > P(0, 60).y && io.MousePos.y < P(0, 660).y;
    float nz = z, npx = px, npy = py;
    if (!sTyping) {
        if (sIn.r3) { npx += sIn.turn * 9.0f; npy -= sIn.zoom * 9.0f; }
        else if (padZoom && sIn.zoom != 0.0f) { nz *= powf(1.025f, sIn.zoom); }
        if (sIn.view) { cam_preset(sCamPreset == 1 ? 0 : 1); sound(0); return; }
    }
    if (overModel && io.MouseWheel != 0.0f) { nz *= powf(1.15f, io.MouseWheel); }
    if (overModel && ImGui::IsMouseDragging(1) && k > 0) { npx += io.MouseDelta.x / k; npy += io.MouseDelta.y / k; }
    if (nz != z || npx != px || npy != py) { cam_set(nz, f, npx, npy); }
    // the hints in a row over the tip box, under the model (as the trailer has them): the camera's buttons, the zoom
    // when it is not the whole warrior; "Full / Head" goes to the other preset when clicked
    char zl[32];
    snprintf(zl, sizeof(zl), "Zoom %.1fx", z);
    struct { int b; const char *t; } lines[4] = {{B_RSTICK, "Turn / look"}, {B_R3, "Move"}, {B_L3, sCamPreset == 1 ? "Full" : "Head"}, {-1, zl}};
    int count = fabsf(z - 1.0f) < 0.01f && px == 0 && py == 0 ? 3 : 4;
    float r = S(8), size = S(13), gap = S(14), total = 0;
    for (int i = 0; i < count; i++) { total += (lines[i].b >= 0 ? text_w(sFont, r, "R-STICK") + r * 1.4f + S(5) : 0) + text_w(bold_font(), size, lines[i].t) + gap; }
    float x = P(CAM_X, 0).x - total * 0.5f, y = P(0, 612).y;
    if (x + total > P(1300, 0).x) { x = P(1300, 0).x - total; }
    for (int i = 0; i < count; i++) {
        float x0 = x;
        if (lines[i].b >= 0) { x += glyph(lines[i].b, ImVec2(x, y), r) + S(5); }
        outlined(bold_font(), size, ImVec2(x, y - size * 0.55f), kInk, IM_COL32(16, 8, 40, 255), lines[i].t, S(1.1f));
        x += text_w(bold_font(), size, lines[i].t);
        if (lines[i].b == B_L3 && click_in(ImVec2(x0, y - r), ImVec2(x, y + r))) { cam_preset(sCamPreset == 1 ? 0 : 1); sound(0); }
        x += gap;
    }
}

// =============================================================================================== the model wanted
static void want_model(int key, int chara, int costume, const std::string &folder) {
    SDL_LockMutex(sShare);
    if (sSh.key != key) {
        sSh.key = key;
        sSh.chara = chara;
        sSh.costume = costume;
        snprintf(sSh.folder, sizeof(sSh.folder), "%s", folder.c_str());
    }
    SDL_UnlockMutex(sShare);
}
static int sBuiltKey = -1, sBuiltAs = 0;
static std::string sBuiltFolder;
static void want_disc(const J &ref) { // a disc character's own model (key 1 + id * 16 + costume)
    int id = chara_id(ref), costume = (int)ref["costume"].number(1) - 1;
    if (id < 0 || id >= 161) { return; }
    costume = costume < 0 || costume > 15 ? 0 : costume;
    want_model(1 + id * 16 + costume, id, costume, "");
}
// =============================================================================================== the Z WARRIORS shell
// What the trailer's screen has over the lists: the costume being edited, the whole warrior's undo, a choice of the
// grid tried on before it is made, the photo of the model, the sections of a list that fold.

// ---- the costume being edited (the model shows it; the PARTS tab's per-costume editing hooks in here)
static void set_editing_costume(int k) {
    int n = costume_count();
    sShownCostume = k < 0 ? 0 : k >= n ? n - 1 : k;
}

// ---- the Stance Maker (its own screen opens from the WARRIOR tab's row)
static void open_stance_maker(void) {
    toast("The Stance Maker is not in this build yet.", true);
    sound(2);
}

// ---- undo / redo of the whole warrior: every change, whichever screen made it, is a step (changes coming within
// half a second of each other are one: a slider held, a name typed). L2 / R2 (Z / C) where nothing else has them,
// Ctrl+Z / Ctrl+Y; not in the painter, which has its own. Saving keeps them; another warrior starts afresh.
static std::vector<std::string> sUndo, sRedo;
static std::string sUndoNow, sUndoFile;
static int sUndoAt = -1000;
static bool sUndoOn;
static int undo_count(void) { return (int)sUndo.size(); }
static void undo_limit(std::vector<std::string> &v) {
    size_t total = 0;
    for (const std::string &s : v) { total += s.size(); }
    while (v.size() > 1 && (v.size() > 300 || total > (size_t)96 << 20)) { total -= v.front().size(); v.erase(v.begin()); }
}
static void undo_track(void) {
    if (sScreen != SC_EDIT) { sUndoOn = false; sUndo.clear(); sRedo.clear(); return; }
    std::string now = js_text(sSpec);
    if (!sUndoOn || (sFile != sUndoFile && !sUndoFile.empty())) { // (another warrior, or a form of it opened)
        sUndoOn = true;
        sUndo.clear();
        sRedo.clear();
        sUndoNow = now;
        sUndoFile = sFile;
        return;
    }
    sUndoFile = sFile;
    if (now == sUndoNow) { return; }
    if (sFrames - sUndoAt > 30 || sUndo.empty()) { sUndo.push_back(sUndoNow); undo_limit(sUndo); }
    sUndoNow = now;
    sUndoAt = sFrames;
    sRedo.clear();
}
static void undo_step(bool redo) {
    std::vector<std::string> &from = redo ? sRedo : sUndo, &to = redo ? sUndo : sRedo;
    if (from.empty()) { toast(redo ? "Nothing to redo." : "Nothing to undo."); sound(2); return; }
    J v;
    if (!js_read(from.back(), v)) { from.pop_back(); return; }
    to.push_back(sUndoNow);
    sUndoNow = from.back();
    from.pop_back();
    sSpec = v;
    sDirty = true;
    sUndoAt = -1000;
    if (sShownCostume >= costume_count()) { sShownCostume = 0; }
    sound(redo ? 1 : 2);
    toast(redo ? "Redone! Back the way it was." : "Undone! Change your mind as often as you like.");
}
static void undo_input(void) {
    if (sScreen != SC_EDIT) { return; }
    int top = sOv.empty() ? -1 : sOv.back().kind;
    if (top == OV_PAINT || top == OV_WHEEL || top == OV_KEYS || top == OV_GRID || top == OV_TEXT) { return; } // (their own buttons)
    bool pad = top < 0 || top == OV_WORKSHOP || top == OV_COLOURS || top == OV_FX || top == OV_TEXPICK;
    if (sIn.undo || (pad && sIn.l2)) { undo_step(false); sIn.l2 = sIn.undo = false; }
    else if (sIn.redo || (pad && sIn.r2)) { undo_step(true); sIn.r2 = sIn.redo = false; }
}

// ---- trying a choice on: the grid's choice under the cursor made as a dry run (sDry: no requests, sounds, lines or
// screens), the warrior's spec afterwards kept and the screen's own put back; when it changed the warrior's look, the
// model shows the warrior wearing it ("TRYING IT ON...") until the choice is made (the same look: shown at once) or
// cancelled.
struct Trial {
    std::string at;      // the cursor's place it is for ("" none)
    bool look = false;   // it changes the look
    J spec;
    int costume = 0;     // the costume it changes (the one shown)
    int face = -1;
    std::string name;
};
static Trial sTry;
static std::string look_text(const J &sp) {
    static const char *const kKeys[] = {"look", "costumes", "parts", "colours", "body", "paint", "costume_edits", "shading"};
    J k = J::arr();
    for (const char *key : kKeys) { k.push(sp[key]); }
    return js_text(k);
}
static void try_choice(const Overlay &o, const J &ref, const std::string &at, const std::string &name, int face) {
    if (sTry.at == at) { return; }
    sTry = Trial();
    sTry.at = at;
    sTry.name = name;
    sTry.face = face;
    if (!o.picked || sScreen != SC_EDIT || !o.moveKinds.empty()) { return; }
    J keep = sSpec;
    bool dirty = sDirty;
    std::string file = sFile;
    int tab = sTab, shown = sShownCostume, screen = sScreen;
    std::function<void(const J *)> f = o.picked;
    sDry = true;
    f(&ref);
    sDry = false;
    J trial = sSpec;
    sSpec = keep;
    sDirty = dirty;
    sFile = file;
    sTab = tab;
    sShownCostume = shown;
    sScreen = screen;
    if (look_text(trial) == look_text(keep)) { return; }
    sTry.look = true;
    sTry.spec = trial;
    sTry.costume = shown;
    size_t n = std::max(trial["costumes"].size(), keep["costumes"].size());
    for (size_t i = 0; i < n; i++) { // (a costume of its own changed: that one is shown)
        if (js_text(trial["costumes"][i]) != js_text(keep["costumes"][i])) { sTry.costume = (int)i; break; }
    }
    int count = trial["costumes"].size() ? (int)trial["costumes"].size() : 1;
    if (sTry.costume >= count) { sTry.costume = count - 1; }
}
// the grid on top, its cursor rested: what it would try on (stage 0 a fighter in its first costume, 1 the costume)
static const Trial *trial_now(void) {
    if (sOv.empty() || sOv.back().kind != OV_GRID || sScreen != SC_EDIT) { sTry = Trial(); return NULL; }
    Overlay &o = sOv.back();
    if (o.stage == 0) {
        if (o.rest < 12) { return NULL; }
        std::vector<const J *> items = grid_items(o);
        if (o.cur >= (int)items.size()) { return NULL; }
        const J &c = *items[(size_t)o.cur];
        try_choice(o, ref_of(c), "g" + std::to_string((int)c["id"].number(-1)), c["name"].text(), (int)c["id"].number(-1));
    } else if (o.stage == 1) {
        J idj = J::num(o.chosen);
        const J *c = entry_of(idj);
        if (c == NULL || o.cur < 0 || o.cur >= (int)(*c)["costumes"].size()) { return NULL; }
        int k = (int)(*c)["costumes"][(size_t)o.cur].number(1);
        try_choice(o, ref_of(*c, k), "c" + std::to_string(o.chosen) + ":" + std::to_string(k), (*c)["name"].text() + " (costume " + std::to_string(k) + ")", o.chosen);
    } else {
        return NULL;
    }
    return sTry.look ? &sTry : NULL;
}

// ---- the photo of the model: one frame of the game with the model alone on a key colour (port_creator.c, PC_PHOTO),
// read from the window's picture (gs_gpu.c: Creator_WantGrab / Creator_Grabbed), the key taken out, made the warrior's
// portrait by the creator (/api/portraitshot: the render, the small portrait and the team pictures of it).
static int sPhotoWait; // frames the picture has been waited for, 0 none
static void photo_end(void) {
    sPhotoWait = 0;
    SDL_LockMutex(sShare);
    sSh.photo = 0;
    SDL_UnlockMutex(sShare);
}
static void photo_of_model(void) {
    if (sPhotoWait) { return; }
    if (sSpec["name"].text().empty()) { toast("Give your warrior a name first!", true); sound(2); return; }
    if (sBuiltKey < 0 || sPreviewBusy || sPreviewKey != sPreviewSent) { toast("Wait until your warrior's model is built, then smile!", true); sound(2); return; }
    sPhotoWait = 1;
    SDL_LockMutex(sShare);
    sSh.photo = 1;
    SDL_UnlockMutex(sShare);
    sound(1);
}
extern "C" int Creator_WantGrab(void) { return sActive && sPhotoWait > 0; }
extern "C" void Creator_Grabbed(const unsigned char *px, int w, int h, int bgr) {
    if (!sActive || sPhotoWait <= 0 || w < 16 || h < 16) { return; }
    auto rgb = [&](int x, int y, int *r, int *g, int *b) {
        const unsigned char *q = px + ((size_t)y * (size_t)w + (size_t)x) * 4;
        *r = q[bgr ? 2 : 0];
        *g = q[1];
        *b = q[bgr ? 0 : 2];
    };
    auto keyed = [&](int x, int y) { int r, g, b; rgb(x, y, &r, &g, &b); return r > 200 && b > 200 && g < 70; };
    if (!keyed(3, 3) || !keyed(w - 4, 3) || !keyed(3, h - 4) || !keyed(w - 4, h - 4)) { // (not that frame yet)
        if (++sPhotoWait > 300) { photo_end(); toast("The photo did not come out: try again.", true); }
        return;
    }
    std::vector<uint8_t> a((size_t)w * h);
    int x0 = w, y0 = h, x1 = -1, y1 = -1;
    std::vector<int> todo;
    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            bool k = keyed(x, y);
            a[(size_t)y * w + x] = k ? 0 : 255;
            if (k) { todo.push_back(y * w + x); }
        }
    }
    // the model's shadow on the key is a darker magenta: what joins the key through magenta-ish pixels goes too (the
    // model's own purples are inside its outline, not joined to the key)
    auto dim_key = [&](int i) {
        int r, g, b;
        rgb(i % w, i / w, &r, &g, &b);
        return std::min(r, b) - g > 28 && abs(r - b) < 60 && g < 110;
    };
    while (!todo.empty()) {
        int i = todo.back();
        todo.pop_back();
        int x = i % w, y = i / w;
        int nb[4] = {x > 0 ? i - 1 : -1, x < w - 1 ? i + 1 : -1, y > 0 ? i - w : -1, y < h - 1 ? i + w : -1};
        for (int j : nb) {
            if (j >= 0 && a[(size_t)j] && dim_key(j)) { a[(size_t)j] = 0; todo.push_back(j); }
        }
    }
    std::vector<uint8_t> out((size_t)w * h * 4);
    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            int r, g, b;
            rgb(x, y, &r, &g, &b);
            size_t i = (size_t)y * w + x;
            int al = a[i];
            if (al && ((x > 0 && !a[i - 1]) || (x < w - 1 && !a[i + 1]) || (y > 0 && !a[i - w]) || (y < h - 1 && !a[i + w]))) {
                // an edge: how much of the key is mixed in, taken out
                float m = (float)(std::min(r, b) - g) / 255.0f;
                if (m > 0.05f) {
                    m = m > 0.95f ? 0.95f : m;
                    al = (int)(255.0f * (1.0f - m));
                    r = (int)((r - 255.0f * m) / (1.0f - m));
                    b = (int)((b - 255.0f * m) / (1.0f - m));
                    r = r < 0 ? 0 : r > 255 ? 255 : r;
                    b = b < 0 ? 0 : b > 255 ? 255 : b;
                }
            }
            uint8_t *o = &out[i * 4];
            o[0] = (uint8_t)r;
            o[1] = (uint8_t)g;
            o[2] = (uint8_t)b;
            o[3] = (uint8_t)al;
            if (al > 0) { x0 = std::min(x0, x); y0 = std::min(y0, y); x1 = std::max(x1, x); y1 = std::max(y1, y); }
        }
    }
    photo_end();
    if (x1 < x0 || y1 < y0) { toast("Nobody in the picture: wait for the model, then try again.", true); return; }
    int m = 8; // (a little room around it)
    x0 = std::max(0, x0 - m); y0 = std::max(0, y0 - m); x1 = std::min(w - 1, x1 + m); y1 = std::min(h - 1, y1 + m);
    int cw = x1 - x0 + 1, ch = y1 - y0 + 1;
    std::vector<uint8_t> crop((size_t)cw * ch * 4);
    for (int y = 0; y < ch; y++) { memcpy(&crop[(size_t)y * cw * 4], &out[((size_t)(y + y0) * w + x0) * 4], (size_t)cw * 4); }
    std::string png = png_b64(crop.data(), cw, ch);
    if (png.empty()) { toast("The photo could not be made.", true); return; }
    fprintf(stderr, "bt3: creator: photo of the model, %d x %d\n", cw, ch);
    auto shot = [png] {
        J body = J::obj();
        body.set("file", J::str(sFile));
        body.set("png", J::str(png));
        toast("Developing the photo...");
        api("/api/portraitshot", &body, [](int st, const J &r) {
            if (st != 200) { toast(error_of(st, r), true); return; }
            bool dirty = sDirty;
            sSpec.set("portrait_image", r["file"]);
            sDirty = dirty; // (its file has it already)
            toast("Click! Your warrior's portraits are a photo of the model now.");
        });
    };
    if (sDirty || sFile.empty()) { save(shot); } else { shot(); }
}

// ---- the sections of a list fold: cross on a heading folds its rows away (a "+" on it) and opens them again
static std::vector<std::string> sFolded; // "<list>:<heading>"
static void fold_rows(std::vector<Row> &rows, int list) {
    std::vector<Row> out;
    bool skip = false;
    for (Row &r : rows) {
        if (r.sec) {
            std::string key = std::to_string(list) + ":" + r.label;
            bool folded = std::find(sFolded.begin(), sFolded.end(), key) != sFolded.end();
            r.folded = folded;
            r.tip = folded ? "This part of the list is folded away: cross opens it." : "Cross folds this part of the list away.";
            r.ok = [key] {
                auto it = std::find(sFolded.begin(), sFolded.end(), key);
                if (it != sFolded.end()) { sFolded.erase(it); } else { sFolded.push_back(key); }
                sound(0);
            };
            skip = folded;
            out.push_back(r);
        } else if (!skip) {
            out.push_back(r);
        }
    }
    rows.swap(out);
}

// ---- the top right: NOT SAVED, the undo count, the costume being edited; over the model, a choice tried on
static void status_frame(const Trial *tr) {
    ImFont *bf = bold_font();
    if (sScreen == SC_EDIT || sScreen == SC_HOME) {
        char chip[64];
        if (sScreen == SC_HOME) { snprintf(chip, sizeof(chip), "%d SAVED", (int)sFighters.size()); }
        else { snprintf(chip, sizeof(chip), "%s", sDirty ? "NOT SAVED" : on_roster(sFile) ? "SAVED - ON THE ROSTER" : "SAVED"); }
        float size = S(14), w = text_w(bf, size, chip) + S(40), h = S(28);
        ImVec2 b = P(1296, 52), a(b.x - w, b.y), bb(b.x, b.y + h);
        zlook::pill(sDl, a, bb, true, sS);
        sDl->AddText(bf, size, ImVec2(a.x + S(20), a.y + (h - size) * 0.5f), zlook::kGoldInk, chip);
    }
    if (sScreen != SC_EDIT) { return; }
    float y = P(0, 86).y, rx = P(1296, 0).x, size = S(13.5f);
    auto line = [&](const std::string &t) {
        outlined(bf, size, ImVec2(rx - text_w(bf, size, t.c_str()), y), IM_COL32(255, 214, 80, 255), IM_COL32(40, 12, 0, 255), t, S(1.2f));
        y += size + S(8);
    };
    int n = undo_count();
    if (n > 0 || !sRedo.empty()) { line(std::to_string(n) + " undo" + (sRedo.empty() ? "" : " - " + std::to_string(sRedo.size()) + " redo")); }
    int count = costume_count();
    line("EDITING COSTUME " + std::to_string((sShownCostume < count ? sShownCostume : 0) + 1) + (count > 1 ? " OF " + std::to_string(count) : ""));
    // over the model: building it, or trying a choice on
    bool building = sPreviewBusy || (sPreviewKey != sPreviewSent && !sNoPreview);
    if (tr != NULL || building) {
        const char *b = tr != NULL ? "TRYING IT ON..." : "BUILDING YOUR WARRIOR...";
        float bs = S(17);
        ImVec2 at = P(CAM_X, 132);
        outlined(bf, bs, ImVec2(at.x - text_w(bf, bs, b) * 0.5f, at.y), IM_COL32(255, 226, 96, 170 + (int)(85 * fabsf(sinf(sFrames * 0.08f)))), IM_COL32(40, 10, 0, 255), b, S(1.4f));
    }
    if (tr != NULL) { // the card of what is tried on, under the model
        std::string pill, t;
        if (!sOv.empty()) { grid_heading(sOv.back(), t, pill); }
        float fs = S(15), w = std::max(text_w(bf, fs, tr->name.c_str()), text_w(sFont, S(13), pill.c_str())) + S(76), h = S(48);
        ImVec2 a(P(CAM_X, 0).x - w * 0.5f, P(0, 556).y), b(a.x + w, a.y + h);
        sDl->AddRectFilled(a, b, IM_COL32(30, 22, 60, 225), S(8));
        sDl->AddRect(a, b, IM_COL32(150, 130, 220, 200), S(8), 0, S(1.5f));
        zlook::face_ring(sDl, face_tex(tr->face), ImVec2(a.x + S(26), (a.y + b.y) * 0.5f), S(16), false, ImVec2(0.2f, 0.03f), ImVec2(0.8f, 0.63f), sS * 0.7f);
        sDl->AddText(bf, fs, ImVec2(a.x + S(52), a.y + S(7)), IM_COL32(255, 236, 150, 255), tr->name.c_str());
        sDl->AddText(sFont, S(13), ImVec2(a.x + S(52), a.y + S(27)), kDim, pill.c_str());
    }
}

static void model_frame(void) {
    // the grid's cursor, once it rested on a character of the disc: the warrior wearing it when the choice is a look
    // (trial_now: "TRY IT ON"), else that character itself
    const Trial *tr = trial_now();
    if (tr == NULL && sScreen == SC_EDIT && !sOv.empty() && sOv.back().kind == OV_GRID && sOv.back().stage == 0 && sOv.back().rest < 12) { return; }
    if (tr == NULL && !sOv.empty() && sOv.back().kind == OV_GRID && sOv.back().stage == 0 && sOv.back().rest >= 12) {
        std::vector<const J *> items = grid_items(sOv.back());
        if (sOv.back().cur < (int)items.size()) {
            const J &c = *items[(size_t)sOv.back().cur];
            int id = (int)c["id"].number(-1);
            if (id >= 0 && id < 161) { want_model(1 + id * 16, id, 0, ""); return; }
        }
    }
    if (sScreen == SC_HOME) {
        J ref;
        std::vector<Row> rows;
        rows_home(rows);
        int cur = sCur[0];
        fix_cursor(rows, cur);
        size_t fi = 0;
        int k = 0;
        for (int i = 0; i < (int)rows.size(); i++) { // the cursor's fighter (rows after "MY FIGHTERS")
            if (rows[i].sec && rows[i].label == "MY FIGHTERS") { k = i; }
        }
        if (k > 0 && cur > k && (fi = (size_t)(cur - k - 1)) < sFighters.size()) {
            const J &sp = sFighters[fi]["spec"];
            ref = sp.has("look") ? sp["look"] : sp["costumes"].size() ? sp["costumes"][0] : sp["base"];
        } else {
            ref = J::obj();
            ref.set("chara", J::num(3));
        }
        if (chara_id(ref) >= 0 && chara_id(ref) < 161) { want_disc(ref); } else { J b = sFighters[fi]["spec"]["base"]; if (chara_id(b) >= 0) { want_disc(b); } }
        return;
    }
    if (sScreen != SC_EDIT) {
        J g = J::obj();
        g.set("chara", J::num(3));
        want_disc(g);
        return;
    }
    // the warrior as it is (or with the choice tried on): built by the creator once its look has rested a moment
    const J &sp = tr != NULL ? tr->spec : sSpec;
    int costume = tr != NULL ? tr->costume : sShownCostume;
    J key = J::arr();
    key.push(sp.has("look") ? sp["look"] : sp["costumes"].size() ? sp["costumes"][0] : sp.has("base") ? sp["base"] : look());
    static const char *const kLook[] = {"costumes", "parts", "colours", "body", "paint", "moveset", "moves", "melee", "ki_blasts", "voice", "names",
                                        "costume_edits", "shading", "stance", "stance_pose"};
    for (const char *k : kLook) { key.push(sp[k]); }
    key.push(J::num(costume));
    std::string text = js_text(key);
    if (text != sPreviewKey) {
        sPreviewKey = text;
        sPreviewChanged = sFrames;
    }
    if (sNoPreview) { // (an older creator, without /api/preview: the look's own character)
        J lk = look();
        if (sp["costumes"].size() > (size_t)costume) { lk = sp["costumes"][(size_t)costume]; }
        if (chara_id(lk) >= 0 && chara_id(lk) < 161) { want_disc(lk); } else { want_disc(sp["base"]); }
        return;
    }
    if (sPreviewKey != sPreviewSent && !sPreviewBusy && sFrames - sPreviewChanged >= 20) {
        sPreviewSent = sPreviewKey;
        sPreviewBusy = true;
        J body = J::obj();
        J spec = sp;
        if (spec["name"].text().empty()) { spec.set("name", J::str("preview")); }
        body.set("spec", spec);
        body.set("costume", J::num(costume));
        api("/api/preview", &body, [](int st, const J &r) {
            sPreviewBusy = false;
            if (st == 404) { sNoPreview = true; toast("This creator is older than the game: its look is shown as the body's character.", true); return; }
            if (st != 200) { toast(error_of(st, r), true); return; }
            std::string folder = r["folder"].text();
            for (char &c : folder) { if (c == '\\') { c = '/'; } }
            sBuiltKey = ++sModelGen;
            sBuiltAs = (int)r["as"].number(0);
            sBuiltFolder = folder;
            const J &notes = r["notes"];
            if (notes.size()) { toast("To make it fit, the builder: " + notes[0].text()); }
        }, 300000, 1);
    }
    if (sBuiltKey >= 0) { want_model(sBuiltKey, sBuiltAs, 0, sBuiltFolder); }
    else { J lk = look(); if (chara_id(lk) >= 0 && chara_id(lk) < 161) { want_disc(lk); } }
}

// =============================================================================================== a frame
static void results(void) {
    std::deque<Result> got;
    SDL_LockMutex(sJobLock);
    got.swap(sResults);
    SDL_UnlockMutex(sJobLock);
    for (Result &r : got) {
        if (r.job.face >= 0) {
            Face &f = sFace[r.job.face];
            if (r.pix != NULL) {
                f.tex = Ui_Texture(r.pix, (uint32_t)r.w, (uint32_t)r.h);
                f.state = f.tex != NULL ? 2 : 3;
                sFacesMade++;
            } else {
                f.state = 3;
            }
            continue;
        }
        SDL_LockMutex(sJobLock);
        sBusy--;
        SDL_UnlockMutex(sJobLock);
        J res;
        if (!r.raw.empty()) { js_read(r.raw, res); }
        if (r.job.done) { r.job.done(r.status, res); }
    }
}

static void on_open(void) {
    sSeenGen = sOpenGen;
    sFrames = 0;
    sScriptAt = 0;
    sBuiltKey = -1;
    sPreviewSent.clear();
    sPreviewKey.clear();
    ov_close_all();
    {
        char *cwd = SDL_GetCurrentDirectory();
        sGame = trim_slash(cwd != NULL ? cwd : ".");
        SDL_free(cwd);
    }
    sPort = Port_Setting("creator_port", 8765);
    if (getenv("BT3_CREATOR_PORT") != NULL) { sPort = atoi(getenv("BT3_CREATOR_PORT")); }
    fprintf(stderr, "bt3: creator: the Custom Character screen (creator at 127.0.0.1:%d, game folder %s)\n", sPort, sGame.c_str());
    if (!sKeepState) {
        sScreen = SC_CONNECT;
        sSpec = J();
        sFile.clear();
        sDirty = false;
        sCur[0] = 1;
        sTop[0] = 0;
    }
    if (sRoster.size() == 0 || !sKeepState) { connect_start(); }
    else { refresh_fighters(); refresh_roster_info(); }
    sKeepState = false;
}

// Called by ui.cpp every frame while the screen is open (input: the screen has the keyboard and the controller).
// Returns false when it is not open.
bool Creator_Frame(bool input) {
    if (!sActive) { sTyping = false; return false; }
    if (sSeenGen != sOpenGen) { on_open(); }
    sFrames++;
    for (SDL_GPUTexture *t : sFreeLater) { Ui_TextureFree(t); } // (drawn by the last frame: done with now)
    sFreeLater.clear();
    results();
    {
        SDL_LockMutex(sShare);
        int reloaded = sSh.reloaded;
        sSh.reloaded = 0;
        SDL_UnlockMutex(sShare);
        if (reloaded) { fprintf(stderr, "bt3: creator: the characters folder was read again\n"); }
    }
    sTyping = !sOv.empty() && (sOv.back().kind == OV_KEYS || (sOv.back().kind == OV_GRID && sOv.back().stage == 0));
    read_input(input);
    sMouse = input && !ImGui::GetIO().WantCaptureMouse;
    undo_input(); // (L2 / R2, Ctrl+Z / Ctrl+Y: before anything else has the buttons)

    ImGuiIO &io = ImGui::GetIO();
    sX0 = (float)gUiPresentX;
    sY0 = (float)gUiPresentY;
    sW = (float)gUiPresentW;
    sH = (float)gUiPresentH;
    if (sW <= 0 || sH <= 0) { sX0 = sY0 = 0; sW = io.DisplaySize.x; sH = io.DisplaySize.y; }
    sS = std::min(sW / 1376.0f, sH / 768.0f);
    sDl = ImGui::GetBackgroundDrawList();
    sFont = ImGui::GetFont();
    sBold = Ui_FontBold();
    sBoxA = P(100, 112); // (the list: as the trailer has it, under the tabs, left of the warrior)
    sBoxB = P(660, 566);
    sOvRight = sBoxB.x;
    border_strips();

    std::vector<Prompt> ps;
    std::string tipLine = sTipLine;
    const char *heading = "CUSTOM CHARACTER";
    std::string headingText;

    // ---- the screen under the overlays
    size_t ovBefore = sOv.size(); // (one opened by this frame's press gets the buttons from the next frame on)
    std::vector<Row> rows;
    int listKey = sScreen == SC_EDIT ? 1 + sTab : 0;
    if (sScreen == SC_HOME) { rows_home(rows); }
    else if (sScreen == SC_EDIT) {
        switch (sTab) {
        case TAB_WARRIOR: rows_warrior(rows); break;
        case TAB_PARTS: rows_parts(rows); break;
        case TAB_MOVES: rows_moves(rows); break;
        case TAB_FORMS: rows_forms(rows); break;
        default: rows_finish(rows); break;
        }
        fold_rows(rows, listKey);
        headingText = sSpec["name"].text().empty() ? std::string("New Warrior") : sSpec["name"].text();
        heading = headingText.c_str();
    }
    title_and_tabs(heading);
    if (sScreen == SC_HOME) { static const char *const kHome[1] = {"MY FIGHTERS"}; draw_tabs(0, kHome, 1); }
    else if (sScreen == SC_EDIT) { draw_tabs(sTab, kTabs, TAB_COUNT); }

    // the list
    if (sScreen == SC_HOME || sScreen == SC_EDIT) {
        panel(sBoxA, sBoxB);
        int &cur = sCur[listKey];
        fix_cursor(rows, cur);
        int clicked = draw_rows(sBoxA, sBoxB, rows, cur, sTop[listKey], sOv.empty());
        if (!rows.empty() && tipLine.empty() && cur < (int)rows.size()) { tipLine = rows[cur].tip; }
        if (sOv.empty()) {
            const Row *r = cur < (int)rows.size() ? &rows[cur] : NULL;
            if (r == NULL || (r->ok && !r->locked)) { ps.push_back({B_CROSS, sScreen == SC_HOME ? "Choose" : r != NULL && r->sec ? (r->folded ? "Open" : "Fold") : "Change"}); }
            if (r != NULL && r->lr) { ps.push_back({B_LR, r->lrLabel != NULL ? r->lrLabel : "Change"}); }
            if (r != NULL && r->sq) { ps.push_back({B_SQUARE, r->sqLabel != NULL ? r->sqLabel : ""}); }
            if (r != NULL && r->tri) { ps.push_back({B_TRIANGLE, r->triLabel != NULL ? r->triLabel : ""}); }
            if (sScreen == SC_EDIT) { ps.push_back({B_L1R1, "Tab"}); ps.push_back({B_START, "Save"}); }
            if (sScreen == SC_EDIT && (undo_count() || !sRedo.empty())) { ps.push_back({B_L2R2, "Undo / redo"}); }
            ps.push_back({B_CIRCLE, sScreen == SC_HOME ? "Main menu" : "Back"});
            // the screen's own buttons
            if (sScreen == SC_EDIT && (sIn.l1 || sIn.r1)) {
                sTab = (sTab + (sIn.r1 ? 1 : TAB_COUNT - 1)) % TAB_COUNT;
                sound(0);
            } else if (sScreen == SC_EDIT && sIn.start) {
                save();
            } else if (sIn.back) {
                sound(2);
                if (sScreen == SC_HOME) { leave(); }
                else if (sDirty) { confirm("LEAVE WITHOUT SAVING?", [] { sScreen = SC_HOME; }); }
                else { sScreen = SC_HOME; }
            } else {
                list_input(rows, cur, clicked);
            }
        }
    } else if (sScreen == SC_CONNECT) {
        panel(sBoxA, sBoxB);
        const char *line = "Waking up the creator...";
        float size = S(24);
        ImFont *big = sBold != NULL ? sBold : sFont;
        sDl->AddText(big, size, ImVec2(sBoxA.x + S(30), sBoxA.y + S(40)), kInk, line);
        for (int i = 0; i < 7; i++) { // (the dragon balls, going round)
            float t = sFrames * 0.05f + i * 0.9f;
            ImVec2 c((sBoxA.x + sBoxB.x) * 0.5f + cosf(t) * S(70), (sBoxA.y + sBoxB.y) * 0.5f + sinf(t) * S(70));
            sDl->AddCircleFilled(c, S(12), IM_COL32(255, 160 + i * 10, 30, 255));
            sDl->AddCircleFilled(ImVec2(c.x - S(3), c.y - S(3)), S(4), IM_COL32(255, 240, 200, 255));
        }
        sDl->AddText(sFont, S(16), ImVec2(sBoxA.x + S(30), sBoxB.y - S(80)), kDim,
                     "The first time it reads the whole roster from your game's data: this can take a minute.", NULL, sBoxB.x - sBoxA.x - S(60));
        ps.push_back({B_CIRCLE, "Main menu"});
        if (sIn.back) { leave(); }
    } else { // SC_FAIL
        panel(sBoxA, sBoxB);
        ImFont *big = sBold != NULL ? sBold : sFont;
        sDl->AddText(big, S(24), ImVec2(sBoxA.x + S(30), sBoxA.y + S(30)), IM_COL32(255, 140, 140, 255), "THE CREATOR IS NOT ANSWERING");
        sDl->AddText(sFont, S(17), ImVec2(sBoxA.x + S(30), sBoxA.y + S(80)), kInk, sWhy.c_str(), NULL, sBoxB.x - sBoxA.x - S(60));
        tipLine = "Is Python 3 installed? The creator is port/tools/creator/creator.py of the Tenkaichi3Decomp folder.";
        ps.push_back({B_CROSS, "Try again"});
        ps.push_back({B_CIRCLE, "Main menu"});
        if (sIn.ok) { connect_start(); }
        else if (sIn.back) { leave(); }
    }

    // ---- the overlays (the top one gets the buttons)
    if (sOv.size() > ovBefore) {
        float turn = sIn.turn;
        memset(&sIn, 0, sizeof(sIn));
        sIn.turn = turn;
    }
    if (!sOv.empty()) {
        std::vector<Prompt> ops;
        Overlay &o = sOv.back();
        int kind = o.kind;
        // the box (and the list under it dimmed)
        ImVec2 a = sBoxA, b = sBoxB;
        if (kind == OV_TEXT || (kind == OV_LIST && o.rows.size() <= 2)) { // (a question, a text: in the middle, the rest dimmed)
            a = P(300, 220);
            b = P(1076, 560);
            sDl->AddRectFilled(P(0, 0), P(1376, 768), IM_COL32(10, 5, 30, 130));
        }
        else { b.y = P(0, 604).y; } // (over the tabs, down to the tip box: as the trailer's pop-ups)
        if (kind == OV_PAINT) { b = P(880, 604); } // (the texture large, its tools beside it: the model still to be seen)
        ImVec2 keepA = sBoxA, keepB = sBoxB;
        sBoxA = ImVec2(a.x, P(0, 40).y);
        sBoxB = b;
        sOvRight = b.x;
        if (kind == OV_TEXT || (kind == OV_LIST && o.rows.size() <= 2)) { sBoxA = a; }
        zlook::panel(sDl, sBoxA, sBoxB, S(14), sS);
        grad(sBoxA, sBoxB, IM_COL32(44, 26, 96, 215), IM_COL32(30, 16, 72, 225), S(14)); // (darker than the list it is over)
        sDl->AddRect(sBoxA, sBoxB, IM_COL32(236, 226, 255, 200), S(14), 0, S(2.5f));
        ov_title(o);
        if (kind == OV_GRID) {
            std::string t, pill;
            grid_heading(o, t, pill);
            tipLine = t == "TRY IT ON" ? "Pick the " + (pill.empty() ? std::string("look") : pill) + " your warrior fights in! Your warrior tries it on."
                    : t == "LAYER ON TOP" ? "Whose " + (pill.empty() ? std::string("piece") : pill) + "? It goes on top of what your warrior has."
                    : t == "LEARN A TECHNIQUE" ? std::string("Whose technique should your warrior learn?")
                    : o.stage == 0 ? std::string("Pick a character! The model shows whom the cursor is on.") : std::string("And which one?");
            if (o.stage == 1) { tipLine = "And which costume?"; }
        }
        else if (kind == OV_KEYS) { tipLine = "Type with the keys (or your keyboard). START or Enter when it is right."; }
        else if (kind == OV_COLOURS) { tipLine = "Pick a colour of one of your warrior's textures, then its new colour."; }
        else if (kind == OV_PALETTE) { tipLine = "Pick the new colour."; }
        else if (kind == OV_FX) { tipLine = "Pick a colour of the attack, then its new colour. The picture shows its textures and colours."; }
        else if (kind == OV_WORKSHOP) {
            tipLine = "Move it, turn it, stretch it! Your warrior is built again as you go.";
            std::vector<Row> wr;
            if (!o.info.nul()) { ws_rows(o, wr); }
            if (o.cur < (int)wr.size() && !wr[(size_t)o.cur].tip.empty()) { tipLine = wr[(size_t)o.cur].tip; }
        }
        else if (kind == OV_TEXPICK) { tipLine = "Which texture? The most used first. Its paint shows on the model once it is built again."; }
        else if (kind == OV_PAINT) { tipLine = sKeyboardLast ? "Aim with the arrows or the mouse, paint with Enter / the left button. F: tool, R: colour wheel, Z / C: size."
                                                             : "Aim with the left stick (the d-pad: a texel at a time), cross paints. Square: tool, triangle: colour wheel."; }
        else if (kind == OV_WHEEL) { tipLine = "Turn the ring for the hue, then pick how strong and how bright in the square."; }
        else if (kind == OV_LIST && o.cur < (int)o.rows.size() && !o.rows[o.cur].tip.empty()) { tipLine = o.rows[o.cur].tip; }
        switch (kind) {
        case OV_GRID: grid_frame(o, ops); break;
        case OV_LIST: list_frame(o, ops); break;
        case OV_KEYS: keys_frame(o, ops); break;
        case OV_COLOURS: colours_frame(o, ops); break;
        case OV_PALETTE: palette_frame(o, ops); break;
        case OV_FX: fx_frame(o, ops); break;
        case OV_WORKSHOP: workshop_frame(o, ops); break;
        case OV_TEXPICK: texpick_frame(o, ops); break;
        case OV_PAINT: painter_frame(o, ops); break;
        case OV_WHEEL: wheel_frame(o, ops); break;
        default: text_frame(o, ops); break;
        }
        sBoxA = keepA;
        sBoxB = keepB;
        ps = ops;
    }
    if (input) {
        io.InputQueueCharacters.resize(0); // (the letters were the screen's)
    }

    // ---- the model: turned with the right stick, A / D, or dragged with the mouse; the camera (cam_frame)
    {
        int top = sOv.empty() ? -1 : sOv.back().kind;
        bool beside = top < 0 || top == OV_WORKSHOP || top == OV_PAINT || top == OV_TEXPICK || top == OV_COLOURS || top == OV_WHEEL || top == OV_PALETTE;
        float turn = sIn.r3 ? 0.0f : sIn.turn * 0.05f;
        if (sMouse && ImGui::IsMouseDragging(0) && beside && io.MouseClickedPos[0].x > std::max(sOvRight, P(740, 0).x)) { turn += io.MouseDelta.x * 0.012f; }
        if (turn != 0.0f) {
            SDL_LockMutex(sShare);
            sSh.turn += (int)(turn * 1000.0f);
            SDL_UnlockMutex(sShare);
        }
        if ((sScreen == SC_EDIT || sScreen == SC_HOME) && beside) { cam_frame(std::max(sOvRight, P(740, 0).x), top != OV_PAINT); }
    }
    model_frame();
    status_frame(trial_now());
    SDL_LockMutex(sShare);
    sSh.host = sScreen == SC_EDIT || sScreen == SC_HOME ? HOST_CHARA : -1;
    SDL_UnlockMutex(sShare);
    undo_track();

    // the tip box (the host's line: a line that came, else what the cursor is on), the prompts in its corner
    if (tipLine.empty()) { tipLine = sScreen == SC_HOME ? "Make your own warrior! Mix the looks, moves and voices of the whole roster." : "Change anything: the model shows your warrior as it is."; }
    if (sBusy > 0 && sScreen != SC_CONNECT) { tipLine = "Working on it..." + std::string(sBusy > 1 ? " (" + std::to_string(sBusy) + ")" : ""); if (sPreviewBusy && sBusy == 1) { tipLine = "Building your warrior's model..."; } }
    if (sPhotoWait) { tipLine = "Say cheese!"; }
    if (!sToast.empty() && sFrames < sToastUntil) { tipLine = sToast; }
    tip_box(tipLine);
    prompts(ps);
    return true;
}

// The screen takes typed letters now (ui.cpp turns the system's text input on for it).
bool Creator_Typing(void) { return sActive && sTyping; }
