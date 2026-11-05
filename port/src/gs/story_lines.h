// The Story Creator's catalog of Dragon History's voiced lines (included by gs/story_ui.cpp, inside its namespace).
//
// Each of the disc's 49 story battles has a script (file 0x1FF + n: the battle's GSC script, src/battle/btl_script.c)
// that says which story it is (command 2), who fights (command 12: side << 15 | member, character) and who says each
// line (command 15: -v line who); the story's text file (0x231 + story) has the lines' English text, and each line's
// voice is a file of its own (0xD48 + story * 300 + line; 0x47E0 for the other language). Read once, the first time a
// character's voice list is opened: for each character, the lines it says in Dragon History, with their words.

struct StoryLine { int story, line, chara; std::string text; };
std::vector<StoryLine> sStoryLines;
bool sStoryLinesRead;

bool disc_file(int id, std::string &out) {
    char path[600];
    if (id <= 0) { return false; }
    bool ok = id < kPt2 ? Port_FilePath(1, id - 1, NULL, path, sizeof(path)) != 0 : Port_FilePath(2, id - kPt2, NULL, path, sizeof(path)) != 0;
    return ok && read_file(path, out);
}
uint32_t rd32(const std::string &b, size_t o) {
    uint32_t v = 0;
    if (o + 4 <= b.size()) { memcpy(&v, b.data() + o, 4); }
    return v;
}
// the lines of a text file of the stories: a count, its offsets, UTF-16 texts
std::vector<std::string> story_texts(const std::string &b) {
    std::vector<std::string> out;
    uint32_t n = rd32(b, 0);
    if (n > 2000) { return out; }
    for (uint32_t i = 0; i < n; i++) {
        uint32_t o = rd32(b, 4 + i * 4);
        std::string t;
        for (size_t p = o; p + 1 < b.size(); p += 2) {
            unsigned c = (unsigned char)b[p] | ((unsigned char)b[p + 1] << 8);
            if (c == 0) { break; }
            if (c == 0xFEFF) { continue; }
            if (c == '\n') { if (!t.empty() && t.back() != ' ') { t += ' '; } continue; }
            utf8_put(t, c);
        }
        while (!t.empty() && t.back() == ' ') { t.pop_back(); }
        out.push_back(t);
    }
    return out;
}
void story_lines_read(void) {
    if (sStoryLinesRead) { return; }
    sStoryLinesRead = true;
    for (int n = 0; n < 60; n++) {
        std::string b;
        if (!disc_file(0x1FF + n, b) || b.compare(0, 4, "GSCF") != 0) { continue; }
        // the chunks: GSCF { GSHD, GSCD { GSAC.. }, GSDT }
        size_t head = rd32(b, 4);
        size_t code = head + 16 + rd32(b, head + 4) + rd32(b, head + 8);
        size_t data = code + 16 + rd32(b, code + 4) + rd32(b, code + 8);
        if (data + 16 > b.size() || b.compare(code, 4, "GSCD") != 0 || b.compare(data, 4, "GSDT") != 0) { continue; }
        size_t values = data + rd32(b, data + 4);
        auto value = [&](uint32_t w) { return (int32_t)rd32(b, values + (w >> 8) * 4); };
        int story = -1;
        std::vector<std::pair<int, int>> members; // who, chara
        std::vector<std::pair<int, int>> speakers; // line, who
        for (size_t ac = code + rd32(b, code + 4); ac + 16 <= b.size() && b.compare(ac, 4, "GSAC") == 0; ac += 16 + rd32(b, ac + 4) + rd32(b, ac + 8)) {
            size_t w0 = ac + rd32(b, ac + 4), w1 = w0 + rd32(b, ac + 8);
            for (size_t p = w0; p + 4 <= w1; p += 4) {
                uint32_t w = rd32(b, p);
                if ((w & 0xF) != 1) { continue; }
                int id = (int)(w >> 16);
                std::vector<uint32_t> ops; // the words of the command, its options' too
                for (size_t q = p + 4; q + 4 <= w1 && (rd32(b, q) & 8); q += 4) { ops.push_back(rd32(b, q)); }
                if (id == 2 && !ops.empty() && (ops[0] & 0xF) == 10) { story = value(ops[0]); }
                if (id == 12 && ops.size() >= 2) { members.push_back({value(ops[0]), value(ops[1])}); }
                if (id == 15) {
                    for (size_t k = 0; k + 2 < ops.size(); k++) {
                        if ((ops[k] & 0xF) == 8 && ((ops[k] >> 8) & 0xFF) == 'v') { speakers.push_back({value(ops[k + 1]), value(ops[k + 2])}); k += 2; }
                    }
                }
            }
        }
        if (story < 0) { continue; }
        std::string tb;
        if (!disc_file(0x231 + story, tb)) { continue; }
        std::vector<std::string> texts = story_texts(tb);
        for (const auto &sp : speakers) {
            int chara = -1;
            for (const auto &m : members) { if (m.first == sp.second) { chara = m.second; } }
            if (chara < 0 || sp.first < 0 || sp.first >= (int)texts.size() || texts[(size_t)sp.first].empty()) { continue; }
            char path[600];
            struct stat st;
            if (!Port_FilePath(2, story * 300 + sp.first, NULL, path, sizeof(path)) || stat(path, &st) != 0 || st.st_size <= 1024) { continue; }
            sStoryLines.push_back({story, sp.first, chara, texts[(size_t)sp.first]});
        }
    }
    fprintf(stderr, "bt3: story: %d voiced lines of Dragon History found\n", (int)sStoryLines.size());
}
// (read on a thread of its own when the screens open: a few seconds of files; the lists use it once it is there)
SDL_AtomicInt sStoryLinesDone;
int SDLCALL story_lines_thread(void *) {
    story_lines_read();
    SDL_SetAtomicInt(&sStoryLinesDone, 1);
    return 0;
}
void story_lines_start(void) {
    static bool started;
    if (!started) {
        started = true;
        SDL_Thread *t = SDL_CreateThread(story_lines_thread, "story lines", NULL);
        if (t != NULL) { SDL_DetachThread(t); } else { story_lines_thread(NULL); }
    }
}
bool story_lines_ready(void) { return SDL_GetAtomicInt(&sStoryLinesDone) != 0; }
