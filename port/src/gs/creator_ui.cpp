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
} sSh;

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
    case 9: v = 10300; break;   // where the model stands (port_creator.c: tenths of the mockup's pixels): across,
    case 10: v = 6050; break;   // its feet,
    case 11: v = 3900; break;   // its height
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
    SDL_LockMutex(sShare);
    if (sSh.sounds < 16) { sSh.sound[sSh.sounds++] = id; }
    SDL_UnlockMutex(sShare);
}

// =============================================================================================== the screen's state
enum { SC_CONNECT, SC_FAIL, SC_HOME, SC_EDIT };
enum { TAB_WARRIOR, TAB_PARTS, TAB_MOVES, TAB_FORMS, TAB_FINISH, TAB_COUNT };
static const char *const kTabs[TAB_COUNT] = {"WARRIOR", "PARTS", "MOVES", "FORMS", "SAVE & PLAY"};
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
};
static In sIn;
static int sScriptAt;              // BT3_CREATOR_SCRIPT=<frame>:<button>,...: presses for tests
static bool sTyping;               // a screen that takes typed letters is up (the keyboard, the grid's search)

static bool sMouse;                // the mouse is the screen's (not the settings window's)
static bool key(ImGuiKey k, bool repeat = true) { return ImGui::IsKeyPressed(k, repeat); }
static bool click_in(ImVec2 a, ImVec2 b) { return sMouse && click_in(a, b); }
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
        if (!sTyping) { // (letters are typed on the typing screens)
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
        else if (what.compare(0, 5, "type=") == 0) { for (char c : what.substr(5)) { ImGui::GetIO().AddInputCharacter((unsigned)c); } }
    }
    sIn = in;
}

// =============================================================================================== the lists
struct Row {
    std::string label, value, sub, tip;
    bool sec = false, locked = false;
    int face = -1;                       // a portrait on the row
    std::function<void()> ok, sq, tri;   // cross, square, triangle
    std::function<void(int)> lr;         // left / right
    const char *sqLabel = NULL, *triLabel = NULL, *lrLabel = NULL;
};

// ---- the screens over the list (the grid, the keyboard, lists, the colours): a stack
enum { OV_GRID, OV_LIST, OV_KEYS, OV_COLOURS, OV_PALETTE, OV_TEXT, OV_FX };
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
};
static std::vector<Overlay> sOv;
static int sOvIds;
static void ov_push(Overlay &o) { o.id = ++sOvIds; sOv.push_back(o); }

static void ov_close(void) {
    if (!sOv.empty()) {
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
        if (then) { then(); } else { toast("Saved! custom_fighters/" + sFile); }
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
    for (int i = 1; i < 8; i++) { sCur[i] = sTop[i] = 0; }
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
        for (int i = 1; i < 8; i++) { sCur[i] = sTop[i] = 0; }
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
    r = Row();
    r.label = "Base";
    r.value = nm(sSpec["base"], "the look's");
    r.sub = "AI and roster tables";
    r.face = chara_id(sSpec["base"]);
    r.tip = "The computer plays your warrior like this character.";
    r.ok = [] { open_grid("WHOSE FIGHTING SPIRIT?", [](const J *p) { if (p) { sSpec.set("base", *p); touch(); } }, false, NULL, true); };
    rows.push_back(r);
    rows.push_back(sec("STATS"));
    rows.push_back(stat_row("Health", "health", 10000, 150000, 5000, 50000, "Health: 10000 is one bar. Left / right change it, square: the base's own.", fmt_health));
    rows.push_back(stat_row("Ki at start", "ki", 0, 100000, 5000, 20000, "Ki at the start of a fight. Left / right change it, square: the base's own.", fmt_ki));
    rows.push_back(stat_row("Blast stocks", "blast", 0, 7, 1, 5, "How many blast stocks. Left / right change it, square: the base's own.", fmt_blast));
    rows.push_back(sec("VOICE & FACE"));
    r = Row();
    r.label = "Voice";
    r.value = nm(sSpec["voice"], "the base's");
    r.face = chara_id(sSpec["voice"]);
    r.tip = "Shouts, callouts, intros and wins, with the mouth moving to match.";
    r.ok = [] {
        open_grid("WHOSE VOICE?", [](const J *p) { if (p) { sSpec.set("voice", *p); } else { sSpec.erase("voice"); } touch(); }, false, "Base's own");
    };
    rows.push_back(r);
    r = Row();
    r.label = "Portrait";
    r.value = sSpec.has("portrait_image") ? "Your own picture" : nm(sSpec["portrait"], "the base's");
    r.face = chara_id(sSpec["portrait"]);
    r.tip = "The big picture of the menus, the roster chip and the team picture in fights.";
    r.ok = [] {
        open_grid("WHOSE PORTRAIT?", [](const J *p) {
            sSpec.erase("portrait_image");
            if (p) { sSpec.set("portrait", *p); } else { sSpec.erase("portrait"); }
            touch();
        }, false, "Base's own");
    };
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
    for (int i = 0; i < 4; i++) {
        Row r;
        r.label = "Costume " + std::to_string(i + 1);
        r.value = i < (int)cos.size() ? nm(cos[i], "?") : "none";
        r.sub = i == 0 ? "the look of your warrior" : i == sShownCostume && i < (int)cos.size() ? "shown" : "";
        r.face = i < (int)cos.size() ? chara_id(cos[i]) : -1;
        r.tip = "Body, hair and clothes of a whole character. A warrior can have up to 4 costumes (L2 / R2 show them).";
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
static void panel(ImVec2 a, ImVec2 b) {
    sDl->AddRectFilled(ImVec2(a.x, a.y + S(5)), ImVec2(b.x, b.y + S(5)), IM_COL32(0, 0, 0, 70), S(12));
    sDl->AddRectFilled(a, b, IM_COL32(46, 26, 92, 228), S(12));
    sDl->AddRect(a, b, IM_COL32(185, 165, 255, 255), S(12), 0, S(3));
}
static void face_at(int id, ImVec2 a, ImVec2 b, float round) {
    ImTextureID t = face_tex(id);
    sDl->AddRectFilled(a, b, IM_COL32(255, 255, 255, 22), round);
    if (t) { sDl->AddImageRounded(ImTextureRef(t), a, b, ImVec2(0, 0), ImVec2(1, 1), IM_COL32_WHITE, round); }
}

// a button of the pad: ✕ ○ □ △ in their colours, the shoulder buttons and START as plates; the keyboard's key
enum { B_CROSS, B_CIRCLE, B_SQUARE, B_TRIANGLE, B_L1R1, B_L2R2, B_START, B_DPAD, B_LR };
static float glyph(int b, ImVec2 c, float r) { // -> its width
    static const char *const kKeys[] = {"Enter", "Esc", "F", "R", "Q/E", "Z/C", "Ctrl+S", "Arrows", "Left/Right"};
    if (sKeyboardLast) {
        const char *k = kKeys[b];
        float tw = text_w(sFont, r * 1.1f, k), w = tw + r * 1.2f;
        sDl->AddRectFilled(ImVec2(c.x - r * 0.0f, c.y - r), ImVec2(c.x + w, c.y + r), IM_COL32(42, 26, 85, 255), r * 0.35f);
        sDl->AddRect(ImVec2(c.x, c.y - r), ImVec2(c.x + w, c.y + r), IM_COL32(205, 189, 255, 255), r * 0.35f, 0, S(1.5f));
        sDl->AddText(sFont, r * 1.1f, ImVec2(c.x + r * 0.6f, c.y - r * 0.62f), kInk, k);
        return w;
    }
    if (b >= B_L1R1) {
        const char *k = b == B_L1R1 ? "L1 R1" : b == B_L2R2 ? "L2 R2" : b == B_START ? "START" : b == B_DPAD ? "D-PAD" : "< >";
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
static void prompts(const std::vector<Prompt> &ps) {
    float r = S(12), size = S(17), gap = S(22);
    float total = 0;
    for (const Prompt &p : ps) {
        float gw = sKeyboardLast ? text_w(sFont, r * 1.1f, p.b == B_CROSS ? "Enter" : "Esc") : 0; // (an estimate for the width)
        (void)gw;
        total += S(70) + text_w(sFont, size, p.label.c_str()) + gap;
    }
    ImVec2 at = P(1336, 742);
    float x = at.x - total;
    if (x < P(40, 0).x) { x = P(40, 0).x; }
    for (const Prompt &p : ps) {
        float w = glyph(p.b, ImVec2(x, at.y), r);
        x += w + S(6);
        outlined(sFont, size, ImVec2(x, at.y - size * 0.55f), kInk, IM_COL32(20, 8, 40, 255), p.label, S(1.2f));
        x += text_w(sFont, size, p.label.c_str()) + gap;
    }
}

static void tip_box(const std::string &line) {
    ImVec2 a = P(40, 664), b = P(1336, 720);
    sDl->AddRectFilled(a, b, IM_COL32(30, 18, 70, 235), S(8));
    sDl->AddRect(a, b, IM_COL32(199, 180, 255, 255), S(8), 0, S(2));
    float r = (b.y - a.y) * 0.5f - S(4);
    ImVec2 c(a.x + S(10) + r, (a.y + b.y) * 0.5f);
    sDl->AddCircleFilled(c, r + S(3), IM_COL32(255, 255, 255, 255));
    ImTextureID t = face_tex(3); // (the host: Goku)
    if (t) { sDl->AddImageRounded(ImTextureRef(t), ImVec2(c.x - r, c.y - r), ImVec2(c.x + r, c.y + r), ImVec2(0.18f, 0.02f), ImVec2(0.82f, 0.66f), IM_COL32_WHITE, r); }
    else { sDl->AddCircleFilled(c, r, IM_COL32(44, 123, 214, 255)); }
    float size = S(19);
    std::string l = line;
    sDl->PushClipRect(a, b, true);
    sDl->AddText(sFont, size, ImVec2(c.x + r + S(16), c.y - size * 0.55f - (text_w(sFont, size, l.c_str()) > (b.x - a.x) - r * 2 - S(40) ? size * 0.5f : 0)),
                 kInk, l.c_str(), NULL, (b.x - c.x) - r - S(30));
    sDl->PopClipRect();
}

static void title_and_tabs(const char *heading) {
    ImVec2 e = P(76, 56);
    float er = S(30);
    sDl->AddCircleFilled(ImVec2(e.x, e.y + S(3)), er, IM_COL32(107, 58, 0, 255));
    sDl->AddCircleFilled(e, er, kGold2);
    sDl->AddCircleFilled(ImVec2(e.x - er * 0.25f, e.y - er * 0.3f), er * 0.55f, IM_COL32(255, 230, 128, 160));
    sDl->AddCircle(e, er, IM_COL32(255, 242, 176, 255), 0, S(3));
    ImFont *big = sBold != NULL ? sBold : sFont;
    float zs = S(34);
    sDl->AddText(big, zs, ImVec2(e.x - text_w(big, zs, "Z") * 0.5f, e.y - zs * 0.55f), IM_COL32(122, 43, 0, 255), "Z");
    float hs = S(46);
    outlined(big, hs, P(120, 30), IM_COL32(255, 214, 70, 255), IM_COL32(90, 29, 0, 255), heading, S(2.5f));
}

static void draw_tabs(int on, const char *const *names, int count) {
    float x = P(40, 0).x, y0 = P(0, 98).y, h = S(32), size = S(16);
    for (int i = 0; i < count; i++) {
        float w = text_w(sFont, size, names[i]) + S(36);
        ImVec2 a(x, y0), b(x + w, y0 + h);
        if (i == on) { grad(a, b, kGold1, kGold2, h * 0.5f); }
        else { grad(a, b, IM_COL32(198, 181, 245, 255), IM_COL32(125, 100, 200, 255), h * 0.5f); }
        sDl->AddRect(a, b, i == on ? IM_COL32(255, 241, 176, 255) : IM_COL32(231, 220, 255, 255), h * 0.5f, 0, S(2));
        sDl->AddText(sBold != NULL ? sBold : sFont, size, ImVec2(x + S(18), y0 + (h - size) * 0.5f), i == on ? IM_COL32(74, 26, 0, 255) : IM_COL32(43, 22, 96, 255), names[i]);
        if (click_in(a, b) && count > 1 && sOv.empty()) {
            sTab = i;
            sound(0);
        }
        x += w + S(8);
    }
    if (count > 1) { // (L1 / R1 at its ends)
        glyph(B_L1R1, ImVec2(x + S(4), y0 + h * 0.5f), S(11));
    }
}

// A list of rows in a box: the cursor's row gold. Returns the row under the mouse that was clicked, -1 none.
static int draw_rows(ImVec2 a, ImVec2 b, const std::vector<Row> &rows, int cur, int &top, bool active) {
    float rh = S(40), gap = S(4), pad = S(8);
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
    float labelW = (b.x - a.x) * 0.32f;
    for (int i = top; i < (int)rows.size() && i < top + fits; i++) {
        const Row &r = rows[i];
        ImVec2 ra(a.x + pad, y), rb(b.x - pad, y + rh);
        if (r.sec) {
            float size = S(15), w = text_w(sBold != NULL ? sBold : sFont, size, r.label.c_str()) + S(32);
            ImVec2 sa(ra.x + S(4), y + S(10)), sb(ra.x + S(4) + w, y + rh - S(4));
            grad(sa, sb, kGold1, kGold2, (sb.y - sa.y) * 0.5f);
            sDl->AddText(sBold != NULL ? sBold : sFont, size, ImVec2(sa.x + S(16), sa.y + (sb.y - sa.y - size) * 0.5f), IM_COL32(74, 26, 0, 255), r.label.c_str());
            y += rh + gap;
            continue;
        }
        bool sel = i == cur && active;
        if (sel) {
            grad(ra, rb, kGold1, kGold2, rh * 0.5f);
        } else {
            sDl->AddRectFilled(ra, rb, IM_COL32(255, 255, 255, r.locked ? 8 : 16), rh * 0.5f);
        }
        float x = ra.x + S(14);
        float size = S(18);
        ImU32 ink = r.locked ? IM_COL32(170, 160, 200, 200) : sel ? kDark : kInk;
        if (r.face >= 0) {
            float fs = rh - S(8);
            face_at(r.face, ImVec2(x - S(6), y + S(4)), ImVec2(x - S(6) + fs, y + S(4) + fs), fs * 0.5f);
            x += fs + S(2);
        }
        text(sFont, size, ImVec2(x, y + (rh - size) * 0.5f), ink, r.label, ra.x + labelW - x);
        float vx = ra.x + labelW + S(8), vmax = rb.x - vx - S(16);
        std::string sub = r.sub;
        float subSize = S(13);
        float subW = sub.empty() ? 0 : std::min(text_w(sFont, subSize, sub.c_str()) + S(10), vmax * 0.45f);
        text(sBold != NULL ? sBold : sFont, size, ImVec2(vx, y + (rh - size) * 0.5f), sel ? kDark : r.locked ? ink : IM_COL32(255, 255, 255, 255), r.value, vmax - subW);
        if (!sub.empty()) {
            text(sFont, subSize, ImVec2(rb.x - S(16) - subW + S(10), y + (rh - subSize) * 0.5f), sel ? IM_COL32(107, 58, 0, 255) : kDim, sub, subW - S(10));
        }
        if (r.lr && sel) { // (left / right change it: arrows at its ends)
            ImU32 c = IM_COL32(107, 58, 0, 255);
            float mx = vx - S(10), my = y + rh * 0.5f, k = S(5);
            sDl->AddTriangleFilled(ImVec2(mx - k, my), ImVec2(mx + k * 0.3f, my - k), ImVec2(mx + k * 0.3f, my + k), c);
        }
        if (click_in(ra, rb)) { clicked = i; }
        y += rh + gap;
    }
    sDl->PopClipRect();
    if ((int)rows.size() > fits) { // the scroll bar
        float h = b.y - a.y - pad * 2, k = h * fits / rows.size(), at = a.y + pad + h * top / rows.size();
        sDl->AddRectFilled(ImVec2(b.x - S(7), at), ImVec2(b.x - S(3), at + k), IM_COL32(245, 197, 66, 255), S(2));
    }
    return clicked;
}

// The cursor of a list, kept off its headings.
static void move_cursor(const std::vector<Row> &rows, int &cur, int d) {
    int n = (int)rows.size(), c = cur;
    for (int i = 0; i < n; i++) {
        c = (c + d + n) % n;
        if (!rows[c].sec) { break; }
    }
    if (c != cur) { cur = c; sound(0); }
}
static void fix_cursor(const std::vector<Row> &rows, int &cur) {
    if (rows.empty()) { cur = 0; return; }
    if (cur >= (int)rows.size()) { cur = (int)rows.size() - 1; }
    if (cur < 0) { cur = 0; }
    if (rows[cur].sec) { int c = cur; move_cursor(rows, c, 1); cur = c; }
}
static void list_input(std::vector<Row> &rows, int &cur, int clicked) {
    fix_cursor(rows, cur);
    if (rows.empty()) { return; }
    if (sIn.up) { move_cursor(rows, cur, -1); }
    if (sIn.down) { move_cursor(rows, cur, 1); }
    if (clicked >= 0 && !rows[clicked].sec) {
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

static void ov_title(const Overlay &o) {
    ImFont *big = sBold != NULL ? sBold : sFont;
    outlined(big, S(28), ImVec2(sBoxA.x + S(18), sBoxA.y + S(12)), IM_COL32(255, 210, 63, 255), IM_COL32(90, 29, 0, 255), o.title, S(1.5f));
}

static void grid_frame(Overlay &o, std::vector<Prompt> &ps) {
    std::vector<const J *> items = grid_items(o);
    float top0 = sBoxA.y + S(56);
    // the game filter and the search
    bool games = sHaveBt4 && !o.bt3only && o.stage == 0;
    float x = sBoxA.x + S(18);
    if (games) {
        static const char *const kG[3] = {"ALL", "BT3", "BT4"};
        for (int g = 0; g < 3; g++) {
            float w = text_w(sFont, S(14), kG[g]) + S(24), h = S(24);
            ImVec2 a(x, top0), b(x + w, top0 + h);
            if (g == o.game) { grad(a, b, kGold1, kGold2, h * 0.5f); } else { grad(a, b, IM_COL32(198, 181, 245, 255), IM_COL32(125, 100, 200, 255), h * 0.5f); }
            sDl->AddText(sFont, S(14), ImVec2(x + S(12), top0 + (h - S(14)) * 0.5f), g == o.game ? IM_COL32(74, 26, 0, 255) : IM_COL32(43, 22, 96, 255), kG[g]);
            if (click_in(a, b)) { o.game = g; o.cur = 0; o.top = 0; }
            x += w + S(6);
        }
        x += S(10);
    }
    if (o.stage == 0) {
        std::string s = o.search.empty() ? "type to search" : "search: " + o.search + ((sFrames / 20) % 2 ? "_" : " ");
        sDl->AddText(sFont, S(15), ImVec2(x, top0 + S(3)), o.search.empty() ? kDim : kInk, s.c_str());
    }
    if (o.stage == 0) {
        // the faces
        ImVec2 ga(sBoxA.x + S(14), top0 + S(34)), gb(sBoxB.x - S(14), sBoxB.y - S(12));
        float cw = S(104), ch = S(122);
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
            if (sel) { grad(a, b, IM_COL32(255, 207, 77, 255), IM_COL32(232, 138, 0, 255), S(8)); }
            else { grad(a, b, IM_COL32(90, 63, 168, 255), IM_COL32(44, 27, 98, 255), S(8)); }
            sDl->AddRect(a, b, sel ? IM_COL32(255, 226, 122, 255) : IM_COL32(143, 122, 224, 255), S(8), 0, S(2));
            float fs = (b.x - a.x) - S(14);
            if (fs > ch - S(42)) { fs = ch - S(42); }
            ImVec2 fa((a.x + b.x - fs) * 0.5f, a.y + S(5));
            face_at((int)(*items[i])["id"].number(0), fa, ImVec2(fa.x + fs, fa.y + fs), S(6));
            std::string name = (*items[i])["name"].text();
            float size = S(12.5f);
            sDl->PushClipRect(a, b, true);
            sDl->AddText(sFont, size, ImVec2(a.x + S(5), fa.y + fs + S(2)), sel ? kDark : kInk, name.c_str(), NULL, b.x - a.x - S(10));
            sDl->PopClipRect();
            if (click_in(a, b)) { clicked = i; }
        }
        sDl->PopClipRect();
        if (clicked >= 0) { o.cur = clicked; sIn.ok = true; }
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
        ps.push_back({B_CROSS, "Choose"});
        ps.push_back({B_CIRCLE, "Back"});
        if (!o.none.empty()) { ps.push_back({B_SQUARE, o.none}); }
        if (games) { ps.push_back({B_L1R1, "BT3 / BT4"}); }
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
    ps.push_back({B_CIRCLE, "Back"});
    if (sIn.back) { ov_close(); sound(2); return; }
    if (n == 0) { return; }
    int texture = (int)o.tex[(size_t)row]["texture"].number(0);
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
    if (clicked >= 0) { o.cur = clicked; sIn.ok = true; }
    ps.push_back({B_CROSS, "Use this colour"});
    ps.push_back({B_CIRCLE, "Back"});
    if (sIn.back) { ov_close(); sound(2); return; }
    if (sIn.ok && !o.fx.empty()) { // an attack's: {from, to, range 1.5} appended, as the web page does
        J ch = J::obj();
        ch.set("from", J::str(o.from));
        ch.set("to", J::str(hex));
        ch.set("range", J::num(1.5));
        J &ac = sSpec.at("attack_colours");
        J list = ac[o.fx.c_str()].isArr() ? ac[o.fx.c_str()] : J::arr();
        list.push(ch);
        ac.set(o.fx.c_str(), list);
        touch();
        ov_close();
        sound(1);
        if (!sOv.empty() && sOv.back().kind == OV_FX) { fx_load(&sOv.back()); }
        return;
    }
    if (sIn.ok) {
        J ch = J::obj();
        ch.set("texture", J::num(o.texture));
        ch.set("from", J::str(o.from));
        ch.set("to", J::str(hex));
        J list = sSpec["colours"].isArr() ? sSpec["colours"] : J::arr();
        list.push(ch);
        sSpec.set("colours", list);
        touch();
        ov_close();
        sound(1);
        if (!sOv.empty() && sOv.back().kind == OV_COLOURS) { colours_load(&sOv.back()); }
    }
}

static void text_frame(Overlay &o, std::vector<Prompt> &ps) {
    sDl->AddText(sFont, S(17), ImVec2(sBoxA.x + S(22), sBoxA.y + S(58)), kInk, o.text.c_str(), NULL, sBoxB.x - sBoxA.x - S(44));
    ps.push_back({B_CROSS, "OK"});
    if (sIn.ok || sIn.back || (sMouse && ImGui::IsMouseClicked(0))) { ov_close(); sound(1); }
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
static void want_disc(const J &ref) { // a disc character's own model (key 1 + id * 8 + costume)
    int id = chara_id(ref), costume = (int)ref["costume"].number(1) - 1;
    if (id < 0 || id >= 161) { return; }
    costume = costume < 0 || costume > 3 ? 0 : costume;
    want_model(1 + id * 8 + costume, id, costume, "");
}
static void model_frame(void) {
    // the grid's cursor, once it rested on a character of the disc: that one
    if (!sOv.empty() && sOv.back().kind == OV_GRID && sOv.back().stage == 0 && sOv.back().rest >= 12) {
        std::vector<const J *> items = grid_items(sOv.back());
        if (sOv.back().cur < (int)items.size()) {
            const J &c = *items[(size_t)sOv.back().cur];
            int id = (int)c["id"].number(-1);
            if (id >= 0 && id < 161) { want_model(1 + id * 8, id, 0, ""); return; }
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
    // the warrior as it is: built by the creator once its look has rested a moment
    J key = J::arr();
    key.push(look());
    static const char *const kLook[] = {"costumes", "parts", "colours", "body", "moveset", "moves", "melee", "ki_blasts", "voice", "names"};
    for (const char *k : kLook) { key.push(sSpec[k]); }
    key.push(J::num(sShownCostume));
    std::string text = js_text(key);
    if (text != sPreviewKey) {
        sPreviewKey = text;
        sPreviewChanged = sFrames;
    }
    if (sNoPreview) { // (an older creator, without /api/preview: the look's own character)
        J lk = look();
        if (sSpec["costumes"].size() > (size_t)sShownCostume) { lk = sSpec["costumes"][(size_t)sShownCostume]; }
        if (chara_id(lk) >= 0 && chara_id(lk) < 161) { want_disc(lk); } else { want_disc(sSpec["base"]); }
        return;
    }
    if (sPreviewKey != sPreviewSent && !sPreviewBusy && sFrames - sPreviewChanged >= 20) {
        sPreviewSent = sPreviewKey;
        sPreviewBusy = true;
        J body = J::obj();
        J spec = sSpec;
        if (spec["name"].text().empty()) { spec.set("name", J::str("preview")); }
        body.set("spec", spec);
        body.set("costume", J::num(sShownCostume));
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
    sBoxA = P(40, 138);
    sBoxB = P(720, 650);

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
        headingText = sSpec["name"].text().empty() ? std::string("NEW WARRIOR") : sSpec["name"].text();
        for (char &c : headingText) { c = (char)SDL_toupper((unsigned char)c); }
        if (headingText.size() > 22) { headingText = headingText.substr(0, 22); }
        heading = headingText.c_str();
    }
    title_and_tabs(heading);
    if (sScreen == SC_HOME) { static const char *const kHome[1] = {"MY FIGHTERS"}; draw_tabs(0, kHome, 1); }
    else if (sScreen == SC_EDIT) { draw_tabs(sTab, kTabs, TAB_COUNT); }

    // the saved chip, the name over the model
    if (sScreen == SC_EDIT || sScreen == SC_HOME) {
        char chip[64];
        if (sScreen == SC_HOME) { snprintf(chip, sizeof(chip), "%d SAVED", (int)sFighters.size()); }
        else { snprintf(chip, sizeof(chip), "%s", sDirty ? "NOT SAVED" : on_roster(sFile) ? "SAVED - ON THE ROSTER" : "SAVED"); }
        float size = S(15), w = text_w(sBold != NULL ? sBold : sFont, size, chip) + S(36);
        ImVec2 b = P(1336, 30), a(b.x - w, b.y), bb(b.x, b.y + S(30));
        grad(a, bb, kGold1, kGold2, S(15));
        sDl->AddRect(a, bb, IM_COL32(255, 243, 192, 255), S(15), 0, S(2));
        sDl->AddText(sBold != NULL ? sBold : sFont, size, ImVec2(a.x + S(18), a.y + (S(30) - size) * 0.5f), IM_COL32(74, 26, 0, 255), chip);
    }
    if (sScreen == SC_EDIT) {
        ImFont *big = sBold != NULL ? sBold : sFont;
        std::string n = sSpec["name"].text().empty() ? std::string("New Warrior") : sSpec["name"].text();
        float size = S(36);
        ImVec2 c = P(1030, 86);
        outlined(big, size, ImVec2(c.x - text_w(big, size, n.c_str()) * 0.5f, c.y), kInk, IM_COL32(58, 22, 96, 255), n, S(2));
        std::string t = sSpec["title"].text();
        if (!t.empty()) { outlined(sFont, S(18), ImVec2(c.x - text_w(sFont, S(18), t.c_str()) * 0.5f, c.y + size + S(2)), IM_COL32(255, 236, 160, 255), IM_COL32(40, 14, 70, 255), t, S(1.2f)); }
        if (sPreviewBusy || (sPreviewKey != sPreviewSent && !sNoPreview)) {
            const char *b = "BUILDING YOUR WARRIOR...";
            float bs = S(16);
            ImVec2 at = P(1030, 170);
            outlined(sFont, bs, ImVec2(at.x - text_w(sFont, bs, b) * 0.5f, at.y), IM_COL32(255, 220, 90, 160 + (int)(95 * fabsf(sinf(sFrames * 0.08f)))), IM_COL32(30, 10, 50, 255), b, S(1.2f));
        }
        int costumes = sSpec["costumes"].size() ? (int)sSpec["costumes"].size() : 1;
        if (costumes > 1) {
            char line[48];
            snprintf(line, sizeof(line), "COSTUME %d / %d", sShownCostume + 1, costumes);
            ImVec2 at = P(1030, 628);
            float x = at.x - S(80);
            x += glyph(B_L2R2, ImVec2(x, at.y + S(9)), S(10)) + S(8);
            outlined(sFont, S(15), ImVec2(x, at.y), kInk, IM_COL32(30, 10, 50, 255), line, S(1));
        }
    }

    // the list
    if (sScreen == SC_HOME || sScreen == SC_EDIT) {
        panel(sBoxA, sBoxB);
        int &cur = sCur[listKey];
        fix_cursor(rows, cur);
        int clicked = draw_rows(sBoxA, sBoxB, rows, cur, sTop[listKey], sOv.empty());
        if (!rows.empty() && tipLine.empty() && cur < (int)rows.size()) { tipLine = rows[cur].tip; }
        if (sOv.empty()) {
            const Row *r = cur < (int)rows.size() ? &rows[cur] : NULL;
            ps.push_back({B_CROSS, sScreen == SC_HOME ? "Choose" : "Change"});
            if (r != NULL && r->lr) { ps.push_back({B_LR, r->lrLabel != NULL ? r->lrLabel : "Change"}); }
            if (r != NULL && r->sq) { ps.push_back({B_SQUARE, r->sqLabel != NULL ? r->sqLabel : ""}); }
            if (r != NULL && r->tri) { ps.push_back({B_TRIANGLE, r->triLabel != NULL ? r->triLabel : ""}); }
            ps.push_back({B_CIRCLE, sScreen == SC_HOME ? "Main menu" : "My Fighters"});
            if (sScreen == SC_EDIT) { ps.push_back({B_L1R1, "Tab"}); ps.push_back({B_START, "Save"}); }
            // the screen's own buttons
            if (sScreen == SC_EDIT && (sIn.l1 || sIn.r1)) {
                sTab = (sTab + (sIn.r1 ? 1 : TAB_COUNT - 1)) % TAB_COUNT;
                sound(0);
            } else if (sScreen == SC_EDIT && sIn.start) {
                save();
            } else if (sScreen == SC_EDIT && (sIn.l2 || sIn.r2)) {
                int costumes = sSpec["costumes"].size() ? (int)sSpec["costumes"].size() : 1;
                sShownCostume = (sShownCostume + (sIn.r2 ? 1 : costumes - 1)) % costumes;
                sound(0);
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
        ImVec2 keepA = sBoxA, keepB = sBoxB;
        sBoxA = ImVec2(a.x, P(0, 98).y);
        sBoxB = b;
        if (kind == OV_TEXT || (kind == OV_LIST && o.rows.size() <= 2)) { sBoxA = a; }
        sDl->AddRectFilled(ImVec2(sBoxA.x, sBoxA.y + S(5)), ImVec2(sBoxB.x, sBoxB.y + S(5)), IM_COL32(0, 0, 0, 90), S(12));
        sDl->AddRectFilled(sBoxA, sBoxB, IM_COL32(40, 22, 82, 255), S(12));
        sDl->AddRect(sBoxA, sBoxB, IM_COL32(245, 197, 66, 255), S(12), 0, S(3));
        ov_title(o);
        if (kind == OV_GRID) { tipLine = o.stage == 0 ? "Pick a character! The model shows whom the cursor is on." : "And which one?"; }
        else if (kind == OV_KEYS) { tipLine = "Type with the keys (or your keyboard). START or Enter when it is right."; }
        else if (kind == OV_COLOURS) { tipLine = "Pick a colour of one of your warrior's textures, then its new colour."; }
        else if (kind == OV_PALETTE) { tipLine = "Pick the new colour."; }
        else if (kind == OV_FX) { tipLine = "Pick a colour of the attack, then its new colour. The picture shows its textures and colours."; }
        else if (kind == OV_LIST && o.cur < (int)o.rows.size() && !o.rows[o.cur].tip.empty()) { tipLine = o.rows[o.cur].tip; }
        switch (kind) {
        case OV_GRID: grid_frame(o, ops); break;
        case OV_LIST: list_frame(o, ops); break;
        case OV_KEYS: keys_frame(o, ops); break;
        case OV_COLOURS: colours_frame(o, ops); break;
        case OV_PALETTE: palette_frame(o, ops); break;
        case OV_FX: fx_frame(o, ops); break;
        default: text_frame(o, ops); break;
        }
        sBoxA = keepA;
        sBoxB = keepB;
        ps = ops;
    }
    if (input) {
        io.InputQueueCharacters.resize(0); // (the letters were the screen's)
    }

    // ---- the model: turned with the right stick, A / D, or dragged with the mouse
    {
        float turn = sIn.turn * 0.05f;
        if (sMouse && ImGui::IsMouseDragging(0) && sOv.empty() && io.MouseClickedPos[0].x > P(740, 0).x) { turn += io.MouseDelta.x * 0.012f; }
        if (turn != 0.0f) {
            SDL_LockMutex(sShare);
            sSh.turn += (int)(turn * 1000.0f);
            SDL_UnlockMutex(sShare);
        }
    }
    model_frame();

    // the tip box, the prompts, a line that came
    if (tipLine.empty()) { tipLine = sScreen == SC_HOME ? "Make your own warrior! Mix the looks, moves and voices of the whole roster." : "Change anything: the model shows your warrior as it is."; }
    if (sBusy > 0 && sScreen != SC_CONNECT) { tipLine = "Working on it..." + std::string(sBusy > 1 ? " (" + std::to_string(sBusy) + ")" : ""); if (sPreviewBusy && sBusy == 1) { tipLine = "Building your warrior's model..."; } }
    tip_box(tipLine);
    prompts(ps);
    if (!sToast.empty() && sFrames < sToastUntil) {
        float size = S(17), wrap = P(1100, 0).x - P(300, 0).x;
        ImVec2 ts = sFont->CalcTextSizeA(size, FLT_MAX, wrap, sToast.c_str());
        ImVec2 c = P(688, 24);
        ImVec2 a(c.x - ts.x * 0.5f - S(18), c.y), b(c.x + ts.x * 0.5f + S(18), c.y + ts.y + S(20));
        sDl->AddRectFilled(a, b, IM_COL32(28, 16, 64, 240), S(10));
        sDl->AddRect(a, b, sToastErr ? IM_COL32(255, 107, 107, 255) : IM_COL32(245, 197, 66, 255), S(10), 0, S(2));
        sDl->AddText(sFont, size, ImVec2(a.x + S(18), a.y + S(10)), kInk, sToast.c_str(), NULL, wrap);
    }
    return true;
}

// The screen takes typed letters now (ui.cpp turns the system's text input on for it).
bool Creator_Typing(void) { return sActive && sTyping; }
