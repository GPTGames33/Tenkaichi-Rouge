// The Custom Character screen's STANCE MAKER: "grab the dots and pose him, right on the model, then fight in that stance".
//
// This file is a part of creator_ui.cpp (it is #included there, after the screen's own helpers, with
// CREATOR_STANCE_IN_UI defined; compiled on its own it is empty), so it draws and reads the pads as the rest of the
// screen does. What it makes is the warrior's "stance_pose": {"head": [x, y, z] degrees, ...} (port/tools/custom_fighter.py
// STANCE_BONES), a rotation per bone in its parent's space, put on the motions in fights (src/battle/bobj_a.c,
// BtlObj_PortStance). And the WARRIOR tab's two rows: "Stance" (whose fighting stance the warrior has: the stance
// motions of another character, "stance") and "Stance maker" (N bones posed).
//
// While it is open the game's viewer (src/menu/port_creator.c) shows the warrior in its fighting stance (motion 0 of the
// warrior's animations, read from the creator's preview folder) with the pose being made on it, live
// (PortCreator_StanceLive), and hands back where every joint of the model was drawn (PortCreator_StancePut: the
// nodes' matrices in the model's space and the model-to-screen matrix). A dot is drawn on each bone's far end (the
// head's top, the elbow for the arm...): dragging it turns the bone about its joint so that the dot follows the
// cursor (in the plane facing the camera through the dot: no IK), freely or kept to one axis of the parent (X, Y, Z).
// The controller: the d-pad picks the bone, the left stick moves its dot.
#ifdef CREATOR_STANCE_IN_UI

struct StanceBone { const char *key; int node; const char *label; int handle; }; // handle: the node at its far end, -1 none
static const StanceBone kStanceBones[] = {
    {"head", 0x2F, "Head", 0x32}, {"neck", 0x2E, "Neck", 0x2F}, {"chest", 0x11, "Chest", 0x2E}, {"waist", 0x10, "Waist", 0x11},
    {"r_shoulder", 0x12, "Right shoulder", 0x13}, {"r_arm", 0x13, "Right arm", 0x14}, {"r_forearm", 0x14, "Right forearm", 0x15},
    {"r_hand", 0x15, "Right hand", 0x1F}, {"l_shoulder", 0x20, "Left shoulder", 0x21}, {"l_arm", 0x21, "Left arm", 0x22},
    {"l_forearm", 0x22, "Left forearm", 0x23}, {"l_hand", 0x23, "Left hand", 0x2D}, {"r_thigh", 0x09, "Right thigh", 0x0A},
    {"r_shin", 0x0A, "Right shin", 0x0B}, {"r_foot", 0x0B, "Right foot", -1}, {"l_thigh", 0x0D, "Left thigh", 0x0E},
    {"l_shin", 0x0E, "Left shin", 0x0F}, {"l_foot", 0x0F, "Left foot", -1}};
enum { ST_BONES = (int)(sizeof(kStanceBones) / sizeof(kStanceBones[0])), ST_NODES = 71, ST_GOT = 18 + ST_NODES * 32 };

// ---- shared with the game's thread (under sShare)
static volatile int sStanceOpen;    // the Stance Maker is open (PortCreator_Get(13))
static float sStLive[ST_NODES * 4]; // the pose being made: a quaternion per node
static int sStTpose;                // the bind pose under it
static std::vector<uint8_t> sStMotion; // motion 0 of the warrior's animations, packed (the fighting stance)
static int sStMotionGen;
static float sStGot[ST_GOT];        // where the model's nodes were (BtlObj_PortStanceCapture)
static int sStGotSeq;

extern "C" int PortCreator_StanceLive(void *obj, float *quats, int *tpose) {
    (void)obj;
    if (!sStanceOpen || !sActive || sShare == NULL) { return 0; }
    SDL_LockMutex(sShare);
    memcpy(quats, sStLive, sizeof(sStLive));
    *tpose = sStTpose;
    SDL_UnlockMutex(sShare);
    return 1;
}
// -> the motion's number (0 none); copied into dst when the game's (`have`) is another
extern "C" int PortCreator_StanceMotion(void *dst, int max, int *size, int have) {
    *size = 0;
    if (!sStanceOpen || !sActive || sShare == NULL) { return 0; }
    SDL_LockMutex(sShare);
    int gen = sStMotion.empty() || (int)sStMotion.size() > max ? 0 : sStMotionGen;
    if (gen > 0 && gen != have) {
        memcpy(dst, sStMotion.data(), sStMotion.size());
        *size = (int)sStMotion.size();
    }
    SDL_UnlockMutex(sShare);
    return gen;
}
extern "C" void PortCreator_StancePut(float *data, int count) {
    if (sShare == NULL) { return; }
    SDL_LockMutex(sShare);
    memcpy(sStGot, data, sizeof(float) * (size_t)std::min(count, (int)ST_GOT));
    sStGotSeq++;
    SDL_UnlockMutex(sShare);
}

// ---- small maths: quaternions (x, y, z, w; Hamilton), 3-vectors
struct V3 { float x, y, z; };
static V3 v3(float x, float y, float z) { V3 v = {x, y, z}; return v; }
static V3 vsub(V3 a, V3 b) { return v3(a.x - b.x, a.y - b.y, a.z - b.z); }
static V3 vadd(V3 a, V3 b) { return v3(a.x + b.x, a.y + b.y, a.z + b.z); }
static V3 vmul(V3 a, float k) { return v3(a.x * k, a.y * k, a.z * k); }
static float vdot(V3 a, V3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
static V3 vcross(V3 a, V3 b) { return v3(a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x); }
static float vlen(V3 a) { return sqrtf(vdot(a, a)); }
static V3 vnorm(V3 a) { float l = vlen(a); return l > 1e-9f ? vmul(a, 1.0f / l) : v3(0, 0, 0); }
struct Q4 { float x, y, z, w; };
static Q4 qid(void) { Q4 q = {0, 0, 0, 1}; return q; }
static Q4 qmul(Q4 a, Q4 b) {
    Q4 o;
    o.w = a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z;
    o.x = a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y;
    o.y = a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x;
    o.z = a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w;
    return o;
}
static Q4 qaxis(V3 axis, float rad) {
    V3 a = vnorm(axis);
    float s = sinf(rad * 0.5f);
    Q4 q = {a.x * s, a.y * s, a.z * s, cosf(rad * 0.5f)};
    return q;
}
static Q4 qfix(Q4 q) { // unit, w >= 0 (the game slerps from the identity by |w|)
    float n = sqrtf(q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w);
    if (n < 1e-9f) { return qid(); }
    float k = (q.w < 0 ? -1.0f : 1.0f) / n;
    Q4 o = {q.x * k, q.y * k, q.z * k, q.w * k};
    return o;
}
static const float kDeg = 3.14159265f / 180.0f;
// degrees about the parent's x, then y, then z (q = qz * qy * qx: custom_fighter.py euler_quat)
static Q4 q_euler(float rx, float ry, float rz) {
    return qfix(qmul(qaxis(v3(0, 0, 1), rz * kDeg), qmul(qaxis(v3(0, 1, 0), ry * kDeg), qaxis(v3(1, 0, 0), rx * kDeg))));
}
static void euler_q(Q4 q, float *rx, float *ry, float *rz) {
    float r00 = 1 - 2 * (q.y * q.y + q.z * q.z), r10 = 2 * (q.x * q.y + q.w * q.z);
    float r20 = 2 * (q.x * q.z - q.w * q.y), r21 = 2 * (q.y * q.z + q.w * q.x), r22 = 1 - 2 * (q.x * q.x + q.y * q.y);
    float s = -r20 < -1 ? -1 : -r20 > 1 ? 1 : -r20;
    *ry = asinf(s) / kDeg;
    if (fabsf(s) < 0.9999f) {
        *rx = atan2f(r21, r22) / kDeg;
        *rz = atan2f(r10, r00) / kDeg;
    } else { // (gimbal lock: all of it on x)
        float r01 = 2 * (q.x * q.y - q.w * q.z), r11 = 1 - 2 * (q.x * q.x + q.z * q.z);
        *rx = atan2f(-r01 * s, r11) / kDeg;
        *rz = 0;
    }
}

// ---- the Stance Maker's state (the render thread's)
static Q4 sStPose[ST_BONES];   // the pose being made
static int sStSel;             // the bone picked
static int sStAxis;            // 0 free, 1..3 kept to the parent's X, Y, Z
static bool sStAdvanced, sStTposeOn;
static int sStRow;             // Advanced: the slider picked (0..2)
static int sStDrag = -1;       // the bone the mouse holds, -1 none
static Q4 sStDragQ0;           // ... its pose when it was grabbed
static V3 sStDragU0, sStDragJ0, sStDragH0, sStDragP[3]; // ... its joint, its dot, the parent's axes then (model space)
static ImVec2 sStDragM0;       // ... where the mouse was then
static bool sStMouseBusy;      // the mouse is the Stance Maker's this frame (not the model's turn)
static float sStGotLocal[ST_GOT];
static int sStGotSeen = -1;
static std::string sStMotionFolder;
static std::vector<std::vector<Q4>> sStUndo; // poses before each change (Ctrl+Z)
static std::string sStPoseKey; // the pose when it was opened (the model's key keeps it while it is open)

// The pose of the spec: the shown costume's own (costume_edits, PARTS & per-costume) when it has one, else the warrior's.
static J *stance_owner(bool make) {
    J *edits = sSpec.find("costume_edits");
    if (edits != NULL && edits->isObj()) {
        J *c = edits->find(std::to_string(sShownCostume + 1).c_str());
        if (c != NULL && c->isObj() && c->find("stance_pose") != NULL) { return c; }
    }
    (void)make;
    return &sSpec;
}
static const J &stance_pose_json(void) { static J none; J *o = stance_owner(false); const J *p = o->find("stance_pose"); return p != NULL ? *p : none; }
static int stance_posed(void) { return (int)stance_pose_json().size(); }
static int stance_bone_of(const std::string &key) {
    std::string k = key;
    for (char &c : k) { c = (char)SDL_tolower((unsigned char)c); }
    for (int i = 0; i < ST_BONES; i++) {
        std::string l = kStanceBones[i].label;
        for (char &c : l) { c = (char)SDL_tolower((unsigned char)c); }
        if (k == kStanceBones[i].key || k == l) { return i; }
    }
    return -1;
}
static void stance_read(void) { // the spec's pose -> sStPose
    for (int i = 0; i < ST_BONES; i++) { sStPose[i] = qid(); }
    const J &p = stance_pose_json();
    for (size_t i = 0; p.isObj() && i < p.k.size(); i++) {
        int b = stance_bone_of(p.k[i]);
        const J &v = p.a[i];
        if (b >= 0 && v.size() == 3) { sStPose[b] = q_euler((float)v[0].number(), (float)v[1].number(), (float)v[2].number()); }
    }
}
static void stance_share(void) { // sStPose -> the game's live pose
    SDL_LockMutex(sShare);
    for (int n = 0; n < ST_NODES; n++) { sStLive[n * 4 + 0] = sStLive[n * 4 + 1] = sStLive[n * 4 + 2] = 0; sStLive[n * 4 + 3] = 1; }
    for (int i = 0; i < ST_BONES; i++) {
        Q4 q = qfix(sStPose[i]);
        float *d = sStLive + kStanceBones[i].node * 4;
        d[0] = q.x; d[1] = q.y; d[2] = q.z; d[3] = q.w;
    }
    sStTpose = sStTposeOn ? 1 : 0;
    SDL_UnlockMutex(sShare);
}
static double r01(double v) { return round(v * 10.0) / 10.0; }
static void stance_write(void) { // sStPose -> the spec (a bone that is not turned is left out; none: no "stance_pose")
    J pose = J::obj();
    for (int i = 0; i < ST_BONES; i++) {
        float x, y, z;
        euler_q(qfix(sStPose[i]), &x, &y, &z);
        if (fabsf(x) < 0.05f && fabsf(y) < 0.05f && fabsf(z) < 0.05f) { continue; }
        J v = J::arr();
        v.push(J::num(r01(x)));
        v.push(J::num(r01(y)));
        v.push(J::num(r01(z)));
        pose.set(kStanceBones[i].key, v);
    }
    J *o = stance_owner(true);
    if (pose.size() == 0) { o->erase("stance_pose"); } else { o->set("stance_pose", pose); }
    touch();
    stance_share();
}
static void stance_remember(void) { // (before a change: for Ctrl+Z)
    sStUndo.push_back(std::vector<Q4>(sStPose, sStPose + ST_BONES));
    if (sStUndo.size() > 64) { sStUndo.erase(sStUndo.begin()); }
}

// The fighting stance for the viewer: motion 0 of the preview folder's animations (entry 1 of preview_anm.pak).
static void stance_motion_load(const std::string &folder) {
    if (folder == sStMotionFolder) { return; }
    sStMotionFolder = folder;
    std::vector<uint8_t> got;
    FILE *f = folder.empty() ? NULL : fopen((folder + "/preview_anm.pak").c_str(), "rb");
    if (f != NULL) {
        uint32_t head[3];
        if (fread(head, 4, 3, f) == 3 && head[0] > 1 && head[2] > head[1] && head[2] - head[1] <= 0x20000) {
            got.resize(head[2] - head[1]);
            if (fseek(f, (long)head[1], SEEK_SET) != 0 || fread(got.data(), 1, got.size(), f) != got.size()) { got.clear(); }
        }
        fclose(f);
    }
    SDL_LockMutex(sShare);
    sStMotion = got;
    sStMotionGen++;
    SDL_UnlockMutex(sShare);
}

// ---- where the model is: from the game's last frame
static bool st_node(int node, const float **m) { // its matrix and its parent's (model space), false: the model lacks it
    const float *p = sStGotLocal + 18 + node * 32;
    *m = p;
    return p[15] != 0.0f;
}
static V3 st_pos(const float *m) { return v3(m[12], m[13], m[14]); }
static V3 st_row(const float *m, int r) { return vnorm(v3(m[r * 4], m[r * 4 + 1], m[r * 4 + 2])); }
// a point of the model's space -> the window (false behind the camera)
static bool st_project(V3 p, ImVec2 *out, float *depth = NULL) {
    const float *M = sStGotLocal;
    float X = p.x * M[0] + p.y * M[4] + p.z * M[8] + M[12];
    float Y = p.x * M[1] + p.y * M[5] + p.z * M[9] + M[13];
    float W = p.x * M[3] + p.y * M[7] + p.z * M[11] + M[15];
    if (W <= 1e-6f) { return false; }
    float px = X / W - sStGotLocal[16] + 256.0f, py = Y / W - sStGotLocal[17] + 224.0f; // (the 512 x 448 picture)
    *out = ImVec2(sX0 + px * sW / 512.0f, sY0 + py * sH / 448.0f);
    if (depth != NULL) { *depth = W; }
    return true;
}
// the point of the plane through `on` facing the camera that is under the window's point `at`
static bool st_unproject(ImVec2 at, V3 on, V3 *out) {
    const float *M = sStGotLocal;
    auto ray = [&](ImVec2 w, V3 *a, float *aw, V3 *b, float *bw) {
        float X = (w.x - sX0) / sW * 512.0f + sStGotLocal[16] - 256.0f, Y = (w.y - sY0) / sH * 448.0f + sStGotLocal[17] - 224.0f;
        *a = v3(M[0] - X * M[3], M[4] - X * M[7], M[8] - X * M[11]);
        *aw = M[12] - X * M[15];
        *b = v3(M[1] - Y * M[3], M[5] - Y * M[7], M[9] - Y * M[11]);
        *bw = M[13] - Y * M[15];
    };
    V3 a, b, a2, b2;
    float aw, bw, aw2, bw2;
    ImVec2 onW;
    if (!st_project(on, &onW)) { return false; }
    ray(onW, &a2, &aw2, &b2, &bw2);
    V3 n = vnorm(vcross(a2, b2)); // (the camera's ray through the point)
    ray(at, &a, &aw, &b, &bw);
    // a . p = -aw, b . p = -bw, n . p = n . on
    float det = vdot(a, vcross(b, n));
    if (fabsf(det) < 1e-12f) { return false; }
    float r0 = -aw, r1 = -bw, r2 = vdot(n, on);
    V3 c0 = vcross(b, n), c1 = vcross(n, a), c2 = vcross(a, b);
    *out = vmul(vadd(vadd(vmul(c0, r0), vmul(c1, r1)), vmul(c2, r2)), 1.0f / det);
    return true;
}
// a bone's joint and dot (model space); false: not on this model
static bool st_bone(int i, V3 *joint, V3 *dot, V3 parentAxes[3]) {
    const float *m, *h;
    if (!st_node(kStanceBones[i].node, &m)) { return false; }
    *joint = st_pos(m);
    for (int k = 0; k < 3; k++) { parentAxes[k] = st_row(m + 16, k); }
    if (kStanceBones[i].handle >= 0 && st_node(kStanceBones[i].handle, &h) && vlen(vsub(st_pos(h), *joint)) > 1e-4f) {
        *dot = st_pos(h);
        return true;
    }
    // a foot (or a hand without its tip): a point ahead of the joint, in the bone's own axes, as long as a third of its
    // parent bone (the model's bind pose has every bone's axes those of the model: its front is -z)
    float len = vlen(vsub(*joint, st_pos(m + 16))) * 0.33f;
    if (len < 1e-3f) { len = 1.0f; }
    V3 down = st_row(m, 1), front = st_row(m, 2);
    *dot = vadd(*joint, vadd(vmul(down, -len * 0.45f), vmul(front, -len)));
    return true;
}
// Kept to an axis (model space a): the turn about it that a move of `delta` (window pixels) of the dot asks for - the
// move along the way the dot goes on the screen when it turns (so it turns whichever way it is seen from); a dot that
// goes nowhere on the screen when it turns (the axis pointing at the camera through it, or the dot on the axis):
// across the screen turns it.
static float st_locked_angle(V3 J, V3 u, V3 a, ImVec2 delta) {
    const float eps = 0.05f;
    V3 k = vnorm(a);
    V3 r = vadd(vadd(vmul(u, cosf(eps)), vmul(vcross(k, u), sinf(eps))), vmul(k, vdot(k, u) * (1.0f - cosf(eps))));
    ImVec2 p0, p1;
    if (st_project(vadd(J, u), &p0) && st_project(vadd(J, r), &p1)) {
        float tx = (p1.x - p0.x) / eps, ty = (p1.y - p0.y) / eps, t2 = tx * tx + ty * ty; // (pixels a radian)
        if (t2 > S(40) * S(40)) { return (delta.x * tx + delta.y * ty) / t2; }
    }
    return delta.x / S(120);
}
// the pose of bone i turned by `rad` about the parent's axis (parent space), from q0
static Q4 st_turn(Q4 q0, V3 axisParent, float rad) { return qfix(qmul(qaxis(axisParent, rad), q0)); }
// the turn (parent space) that takes the dot from u to v (model space) about the joint, or about the kept axis
static bool st_turn_to(V3 u, V3 v, const V3 P[3], int axis, V3 *axisParent, float *rad) {
    if (axis == 0) {
        V3 c = vcross(u, v);
        float s = vlen(c), d = vdot(u, v);
        if (s < 1e-7f) { return false; }
        V3 am = vmul(c, 1.0f / s);
        *axisParent = v3(vdot(am, P[0]), vdot(am, P[1]), vdot(am, P[2]));
        *rad = atan2f(s, d);
        return true;
    }
    V3 a = P[axis - 1];
    V3 u2 = vsub(u, vmul(a, vdot(u, a))), v2 = vsub(v, vmul(a, vdot(v, a)));
    if (vlen(u2) < vlen(u) * 0.2f || vlen(v2) < 1e-6f) { return false; } // (the dot is on the axis: see the caller)
    *axisParent = v3(axis == 1 ? 1.0f : 0.0f, axis == 2 ? 1.0f : 0.0f, axis == 3 ? 1.0f : 0.0f);
    *rad = atan2f(vdot(vcross(u2, v2), a), vdot(u2, v2));
    return true;
}

// ---- opening and closing
static void open_stance_maker(void) {
    Overlay o;
    o.kind = OV_STANCE;
    o.title = "STANCE MAKER";
    stance_read();
    sStUndo.clear();
    sStSel = 0;
    sStAxis = 0;
    sStDrag = -1;
    sStTposeOn = false;
    sStPoseKey = js_text(stance_pose_json());
    stance_share();
    sStGotSeen = -1;
    memset(sStGotLocal, 0, sizeof(sStGotLocal));
    sStMotionFolder = "\x01"; // (read again)
    stance_motion_load(sBuiltFolder);
    sStanceOpen = 1;
    ov_push(o);
    sound(1);
}
static void stance_close(void) {
    sStanceOpen = 0;
    sStDrag = -1;
    sStMouseBusy = false;
    SDL_LockMutex(sShare);
    sStMotion.clear();
    sStMotionGen++;
    SDL_UnlockMutex(sShare);
    sStMotionFolder.clear();
}
static bool stance_mouse_busy(void) { return sStMouseBusy; }
// the pose's part of the model's key: kept as it was while the Stance Maker is open (the viewer shows it live)
static J stance_model_key(void) { if (sStanceOpen) { J k; js_read(sStPoseKey, k); return k; } return stance_pose_json(); }

// ---- the WARRIOR tab's rows
static void stance_rows(std::vector<Row> &rows) {
    rows.push_back(sec("STANCE"));
    Row r;
    r.label = "Stance";
    r.value = nm(sSpec["stance"], "the moveset's");
    r.face = chara_id(sSpec["stance"]);
    r.tip = "Whose fighting stance: how your warrior stands, walks, dashes and flies. The attacks stay the moveset's.";
    r.ok = [] {
        open_grid("WHOSE STANCE?", [](const J *p) { if (p) { sSpec.set("stance", *p); } else { sSpec.erase("stance"); } touch(); }, false, "Moveset's own");
    };
    rows.push_back(r);
    r = Row();
    r.label = "Stance maker";
    int n = stance_posed();
    r.value = n == 0 ? "not posed" : n == 1 ? "1 bone posed" : std::to_string(n) + " bones posed";
    r.tip = "Grab the dots and pose your warrior right on the model - head, arms, legs - then fight in that stance!";
    r.ok = [] { open_stance_maker(); };
    if (n > 0) {
        r.sqLabel = "Clear the pose";
        r.sq = [] { confirm("CLEAR THE WHOLE POSE?", [] { stance_owner(true)->erase("stance_pose"); touch(); sound(2); }); };
    }
    rows.push_back(r);
}

// ---- a frame of it (the box on the left; the dots on the model)
static void stance_chip(ImVec2 a, ImVec2 b, const char *t, bool on, std::function<void()> click) {
    float h = b.y - a.y;
    if (on) { grad(a, b, kGold1, kGold2, h * 0.5f); } else { grad(a, b, IM_COL32(198, 181, 245, 235), IM_COL32(125, 100, 200, 235), h * 0.5f); }
    sDl->AddRect(a, b, on ? IM_COL32(255, 241, 176, 255) : IM_COL32(231, 220, 255, 255), h * 0.5f, 0, S(1.5f));
    ImFont *f = sBold != NULL ? sBold : sFont;
    float size = S(13);
    sDl->AddText(f, size, ImVec2((a.x + b.x - text_w(f, size, t)) * 0.5f, a.y + (h - size) * 0.5f), on ? IM_COL32(74, 26, 0, 255) : IM_COL32(43, 22, 96, 255), t);
    if (click_in(a, b)) { sStMouseBusy = true; click(); }
}
static void stance_set_euler(int b, int axis, float deg) {
    float e[3];
    euler_q(qfix(sStPose[b]), &e[0], &e[1], &e[2]);
    e[axis] = deg;
    sStPose[b] = q_euler(e[0], e[1], e[2]);
}
static void stance_reset_bone(int b) { stance_remember(); sStPose[b] = qid(); stance_write(); sound(2); }
static void stance_reset_all(void) {
    confirm("RESET THE WHOLE POSE?", [] { stance_remember(); for (int i = 0; i < ST_BONES; i++) { sStPose[i] = qid(); } stance_write(); sound(2); });
}

static void stance_frame(Overlay &o, std::vector<Prompt> &ps) {
    (void)o;
    ImGuiIO &io = ImGui::GetIO();
    sStMouseBusy = false;
    stance_motion_load(sBuiltFolder); // (the warrior's look built again: its animations too)
    {
        SDL_LockMutex(sShare);
        if (sStGotSeq != sStGotSeen) { memcpy(sStGotLocal, sStGot, sizeof(sStGotLocal)); sStGotSeen = sStGotSeq; }
        SDL_UnlockMutex(sShare);
    }
    bool have = sStGotSeen >= 0 && sStGotLocal[15] != 0.0f;

    // ---- the box
    float x0 = sBoxA.x + S(22), x1 = sBoxB.x - S(22), y = sBoxA.y + S(56);
    ImFont *big = sBold != NULL ? sBold : sFont;
    stance_chip(ImVec2(x0, y), ImVec2(x0 + S(190), y + S(28)), "Advanced (numbers)", sStAdvanced, [] { sStAdvanced = !sStAdvanced; sound(0); });
    stance_chip(ImVec2(x0 + S(200), y), ImVec2(x0 + S(290), y + S(28)), "T-pose", sStTposeOn, [] { sStTposeOn = !sStTposeOn; stance_share(); sound(0); });
    y += S(40);
    sDl->AddText(sFont, S(16), ImVec2(x0, y), kInk, "Drag the dots on your warrior.");
    y += S(22);
    sDl->AddText(sFont, S(16), ImVec2(x0, y), kDim, "Turn the camera (right stick) to pose from the side.");
    y += S(36);
    const StanceBone &sb = kStanceBones[sStSel];
    outlined(big, S(24), ImVec2(x0, y), IM_COL32(255, 214, 70, 255), IM_COL32(90, 29, 0, 255), sb.label, S(1.5f));
    {
        char n[48];
        snprintf(n, sizeof(n), "%d / %d", sStSel + 1, (int)ST_BONES);
        sDl->AddText(sFont, S(14), ImVec2(x1 - text_w(sFont, S(14), n), y + S(6)), kDim, n);
    }
    y += S(34);
    static const char *const kAxis[4] = {"Free (press X, Y or Z to keep to one axis)", "Kept to X", "Kept to Y", "Kept to Z"};
    sDl->AddText(sFont, S(16), ImVec2(x0, y), sStAxis ? IM_COL32(120, 255, 140, 255) : kInk, kAxis[sStAxis]);
    y += S(28);
    static const char *const kAxisChip[4] = {"FREE", "X", "Y", "Z"};
    for (int i = 0; i < 4; i++) {
        float cx = x0 + i * S(66);
        stance_chip(ImVec2(cx, y), ImVec2(cx + S(58), y + S(26)), kAxisChip[i], sStAxis == i, [i] { sStAxis = i; sound(0); });
    }
    y += S(40);
    float e[3];
    euler_q(qfix(sStPose[sStSel]), &e[0], &e[1], &e[2]);
    if (sStAdvanced) { // the three angles of the bone as sliders (left / right, or the mouse)
        std::vector<Row> rows;
        static const char *const kNames[3] = {"X (forward / back)", "Y (turn)", "Z (side to side)"};
        for (int k = 0; k < 3; k++) {
            Row r;
            char v[32];
            snprintf(v, sizeof(v), "%+.0f\xc2\xb0", e[k]);
            r.label = kNames[k];
            r.value = v;
            r.bar = (e[k] + 180.0f) / 360.0f;
            r.barDef = 0.5f;
            r.lr = [k](int d) {
                float a[3];
                euler_q(qfix(sStPose[sStSel]), &a[0], &a[1], &a[2]);
                float nv = a[k] + d * (sIn.fast ? 10.0f : 2.0f);
                nv = nv < -180 ? -180 : nv > 180 ? 180 : nv;
                if (!sIn.fast || sFrames % 4 == 0) { sound(0); }
                stance_set_euler(sStSel, k, nv);
                stance_write();
            };
            r.setFrac = [k](float f) {
                f = f < 0 ? 0 : f > 1 ? 1 : f;
                stance_set_euler(sStSel, k, roundf(-180.0f + 360.0f * f));
                stance_write();
            };
            rows.push_back(r);
        }
        int top = 0;
        draw_rows(ImVec2(sBoxA.x + S(10), y), ImVec2(sBoxB.x - S(10), y + S(3 * 44 + 16)), rows, sStRow, top, true);
        static int barDrag = -1;
        if (sMouse && ImGui::IsMouseClicked(0)) {
            for (const auto &b : sBars) {
                if (ImGui::IsMouseHoveringRect(b.second.Min, b.second.Max, false)) { barDrag = b.first; sStRow = b.first; stance_remember(); }
            }
        }
        if (barDrag >= 0 && !ImGui::IsMouseDown(0)) { barDrag = -1; }
        if (barDrag >= 0) {
            sStMouseBusy = true;
            for (const auto &b : sBars) {
                if (b.first == barDrag) { rows[(size_t)barDrag].setFrac((io.MousePos.x - b.second.Min.x - S(8)) / (b.second.Max.x - b.second.Min.x - S(16))); }
            }
        }
        if (sIn.dup) { sStRow = (sStRow + 2) % 3; sound(0); }
        if (sIn.ddown) { sStRow = (sStRow + 1) % 3; sound(0); }
        if ((sIn.dleft || sIn.dright) && barDrag < 0) {
            if (!sIn.fast) { stance_remember(); }
            rows[(size_t)sStRow].lr(sIn.dright ? 1 : -1);
        }
        y += S(3 * 44 + 24);
    } else {
        char line[96];
        snprintf(line, sizeof(line), "X %+.0f\xc2\xb0    Y %+.0f\xc2\xb0    Z %+.0f\xc2\xb0", e[0], e[1], e[2]);
        sDl->AddText(sFont, S(16), ImVec2(x0, y), kDim, line);
        y += S(34);
    }
    stance_chip(ImVec2(x0, y), ImVec2(x0 + S(150), y + S(28)), "Reset this bone", false, [] { stance_reset_bone(sStSel); });
    stance_chip(ImVec2(x0 + S(160), y), ImVec2(x0 + S(290), y + S(28)), "Reset all", false, [] { stance_reset_all(); });
    y += S(44);
    {
        int n = 0;
        for (int i = 0; i < ST_BONES; i++) { if (qfix(sStPose[i]).w < 0.99999f) { n++; } }
        char line[64];
        snprintf(line, sizeof(line), n == 1 ? "1 bone posed" : "%d bones posed", n);
        sDl->AddText(sFont, S(15), ImVec2(x0, y), kDim, line);
        std::string st = "Stance: " + nm(sSpec["stance"], "the moveset's");
        sDl->AddText(sFont, S(15), ImVec2(x0, y + S(22)), kDim, st.c_str());
    }
    if (!have) {
        const char *w = sBuiltFolder.empty() ? "Your warrior is being built..." : "Waiting for the model...";
        sDl->AddText(sFont, S(15), ImVec2(x0, sBoxB.y - S(34)), IM_COL32(255, 220, 90, 255), w);
    }

    // ---- the dots on the model
    struct Dot { int i; ImVec2 at, joint; float depth; bool ok; V3 J, H, P[3]; };
    Dot dots[ST_BONES];
    for (int i = 0; i < ST_BONES; i++) {
        Dot &d = dots[i];
        d.i = i;
        d.ok = have && st_bone(i, &d.J, &d.H, d.P) && st_project(d.H, &d.at, &d.depth) && st_project(d.J, &d.joint);
    }
    bool onModel = sMouse && io.MousePos.x > sOvRight && io.MousePos.y > P(0, 60).y && io.MousePos.y < P(0, 660).y;
    int hover = -1;
    float best = S(16);
    for (int i = 0; i < ST_BONES && onModel && sStDrag < 0; i++) {
        if (!dots[i].ok) { continue; }
        float dx = dots[i].at.x - io.MousePos.x, dy = dots[i].at.y - io.MousePos.y, dd = sqrtf(dx * dx + dy * dy);
        if (dd < best) { best = dd; hover = i; }
    }
    // grabbed with the mouse: from the pose it had, the turn that takes its dot to the cursor
    if (hover >= 0 && ImGui::IsMouseClicked(0)) {
        const Dot &d = dots[hover];
        sStSel = hover;
        sStDrag = hover;
        sStDragQ0 = sStPose[hover];
        sStDragJ0 = d.J;
        sStDragH0 = d.H;
        sStDragU0 = vsub(d.H, d.J);
        for (int k = 0; k < 3; k++) { sStDragP[k] = d.P[k]; }
        sStDragM0 = io.MousePos;
        stance_remember();
        sound(0);
    }
    if (sStDrag >= 0 && (!ImGui::IsMouseDown(0) || !sMouse)) { sStDrag = -1; }
    static const V3 kE[4] = {{0, 0, 0}, {1, 0, 0}, {0, 1, 0}, {0, 0, 1}}; // (the parent's axes in its own space)
    if (sStDrag >= 0) {
        sStMouseBusy = true;
        V3 t, ax;
        float rad;
        if (sStAxis != 0) {
            ImVec2 moved(io.MousePos.x - sStDragM0.x, io.MousePos.y - sStDragM0.y);
            sStPose[sStDrag] = st_turn(sStDragQ0, kE[sStAxis], st_locked_angle(sStDragJ0, sStDragU0, sStDragP[sStAxis - 1], moved));
            stance_write();
        } else if (st_unproject(io.MousePos, sStDragH0, &t) && st_turn_to(sStDragU0, vsub(t, sStDragJ0), sStDragP, 0, &ax, &rad)) {
            sStPose[sStDrag] = st_turn(sStDragQ0, ax, rad);
            stance_write();
        }
    }
    // the controller: the left stick moves the dot of the bone picked (a little each frame, toward where it points)
    if (sStDrag < 0 && !sStAdvanced && dots[sStSel].ok && (fabsf(sIn.lx) > 0.2f || fabsf(sIn.ly) > 0.2f)) {
        const Dot &d = dots[sStSel];
        static int sStickFrom = -100;
        if (sFrames - sStickFrom > 1) { stance_remember(); } // (one undo step a push of the stick)
        sStickFrom = sFrames;
        V3 t, ax;
        float rad;
        ImVec2 step(sIn.lx * S(5), sIn.ly * S(5));
        if (sStAxis != 0) {
            sStPose[sStSel] = st_turn(sStPose[sStSel], kE[sStAxis], st_locked_angle(d.J, vsub(d.H, d.J), d.P[sStAxis - 1], step));
            stance_write();
        } else if (st_unproject(ImVec2(d.at.x + step.x, d.at.y + step.y), d.H, &t) && st_turn_to(vsub(d.H, d.J), vsub(t, d.J), d.P, 0, &ax, &rad)) {
            sStPose[sStSel] = st_turn(sStPose[sStSel], ax, rad);
            stance_write();
        }
    }
    // drawn far ones first: the bones as faint sticks, the dots, the picked one white with its green guide
    if (have) {
        Dot *order[ST_BONES];
        for (int i = 0; i < ST_BONES; i++) { order[i] = &dots[i]; }
        std::sort(order, order + ST_BONES, [](const Dot *a, const Dot *b) { return a->depth > b->depth; });
        const Dot &s = dots[sStSel];
        if (s.ok) { // the guide: the bone, and the axis it is kept to (or a ring about the joint)
            ImU32 g = IM_COL32(90, 255, 120, 230);
            sDl->AddLine(s.joint, s.at, g, S(3));
            float len = vlen(vsub(s.H, s.J));
            if (sStAxis != 0) {
                V3 a = s.P[sStAxis - 1];
                ImVec2 p0, p1;
                if (st_project(vadd(s.J, vmul(a, -len * 1.3f)), &p0) && st_project(vadd(s.J, vmul(a, len * 1.3f)), &p1)) {
                    for (int k = 0; k < 12; k += 2) { // (dashed)
                        ImVec2 a0(p0.x + (p1.x - p0.x) * k / 12.0f, p0.y + (p1.y - p0.y) * k / 12.0f), a1(p0.x + (p1.x - p0.x) * (k + 1) / 12.0f, p0.y + (p1.y - p0.y) * (k + 1) / 12.0f);
                        sDl->AddLine(a0, a1, g, S(3));
                    }
                }
                V3 e1 = vnorm(vcross(a, fabsf(a.x) < 0.9f ? v3(1, 0, 0) : v3(0, 1, 0))), e2 = vcross(a, e1);
                ImVec2 prev;
                bool havePrev = false;
                for (int k = 0; k <= 32; k++) { // the ring it turns along
                    float t = k * 6.2831853f / 32.0f;
                    ImVec2 q;
                    bool ok = st_project(vadd(s.J, vadd(vmul(e1, cosf(t) * len * 0.6f), vmul(e2, sinf(t) * len * 0.6f))), &q);
                    if (ok && havePrev) { sDl->AddLine(prev, q, IM_COL32(90, 255, 120, 150), S(2)); }
                    prev = q;
                    havePrev = ok;
                }
            } else {
                sDl->AddCircle(s.joint, S(9), g, 0, S(2));
            }
        }
        for (int i = 0; i < ST_BONES; i++) {
            const Dot &d = *order[i];
            if (!d.ok) { continue; }
            bool sel = d.i == sStSel, hot = d.i == hover || d.i == sStDrag;
            float r = sel ? S(9) : S(7);
            sDl->AddCircleFilled(ImVec2(d.at.x, d.at.y + S(1.5f)), r + S(2), IM_COL32(40, 20, 0, 160));
            sDl->AddCircleFilled(d.at, r + S(1.5f), IM_COL32(60, 30, 0, 255));
            sDl->AddCircleFilled(d.at, r, sel ? IM_COL32(255, 255, 255, 255) : hot ? IM_COL32(255, 240, 150, 255) : IM_COL32(255, 200, 40, 255));
            if (qfix(sStPose[d.i]).w < 0.99999f && !sel) { sDl->AddCircleFilled(d.at, r * 0.4f, IM_COL32(200, 90, 0, 255)); } // (posed)
        }
        int named = hover >= 0 ? hover : sStSel;
        if (dots[named].ok) {
            const char *l = kStanceBones[named].label;
            ImVec2 at(dots[named].at.x + S(14), dots[named].at.y - S(22));
            outlined(sFont, S(15), at, kInk, IM_COL32(30, 10, 50, 255), l, S(1));
        }
    }

    // ---- the buttons
    ps.push_back({B_LSTICK, "Move the dot"});
    ps.push_back({B_DPAD, sStAdvanced ? "Angle" : "Bone"});
    ps.push_back({B_SQUARE, "Axis"});
    ps.push_back({B_TRIANGLE, "Reset"});
    ps.push_back({B_L2R2, "T-pose / Numbers"});
    ps.push_back({B_RSTICK, "Turn / zoom"});
    ps.push_back({B_CIRCLE, "Done"});
    if (sIn.back) { ov_close(); sound(2); return; } // (every change is in the spec already)
    if (!sTyping && !io.KeyCtrl) { // the keyboard's own: X, Y, Z keep to an axis (again: free), T the T-pose, N the numbers
        static const ImGuiKey kK[3] = {ImGuiKey_X, ImGuiKey_Y, ImGuiKey_Z};
        for (int k = 0; k < 3; k++) {
            if (key(kK[k], false)) { sStAxis = sStAxis == k + 1 ? 0 : k + 1; sound(0); sIn.l2 = false; }
        }
        if (key(ImGuiKey_T, false)) { sIn.l2 = true; }
        if (key(ImGuiKey_N, false)) { sIn.r2 = true; }
    }
    if (sIn.sq) { sStAxis = (sStAxis + 1) % 4; sound(0); }
    if (sIn.tri) { stance_reset_bone(sStSel); }
    if (sIn.l2) { sStTposeOn = !sStTposeOn; stance_share(); sound(0); }
    if (sIn.r2) { sStAdvanced = !sStAdvanced; sound(0); }
    if (sIn.start) { stance_reset_all(); }
    if (sIn.undo && !sStUndo.empty()) {
        for (int i = 0; i < ST_BONES; i++) { sStPose[i] = sStUndo.back()[(size_t)i]; }
        sStUndo.pop_back();
        stance_write();
        sound(2);
    }
    int step = (sIn.l1 ? -1 : 0) + (sIn.r1 ? 1 : 0);
    if (!sStAdvanced) { step += (sIn.dup ? -1 : 0) + (sIn.ddown ? 1 : 0); }
    if (step != 0) {
        for (int n = 0; n < ST_BONES; n++) { // (the next bone this model has)
            sStSel = (sStSel + step + ST_BONES) % ST_BONES;
            if (!have || dots[sStSel].ok) { break; }
        }
        sound(0);
    }
}

#endif // CREATOR_STANCE_IN_UI
