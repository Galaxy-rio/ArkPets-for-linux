// Copyright (c) 2026. Native ArkPets Hyprland prototype.
// This application's original code is GPL-3.0-or-later. Spine has its own license.
#include <gtk/gtk.h>
#include <gtk4-layer-shell.h>
#include <epoxy/gl.h>
#include <glib-unix.h>
#include <spine/spine.h>
#include <algorithm>
#include <cmath>
#include <csignal>
#include <clocale>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>
#include <optional>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <fcntl.h>
#include <sstream>
#include <regex>
#include <memory>
#include "activity.h"
#include "viewport.h"
#include "gravity.h"

namespace fs = std::filesystem;
// The runtime's default file reader; avoid importing extension.h's global math macros.
extern "C" char* _spReadFile(const char* path, int* length);
struct Texture { GdkPixbuf* pixels = nullptr; GLuint id = 0; };
static std::string textureError;

extern "C" void _spAtlasPage_createTexture(spAtlasPage* page, const char* path) {
    GError* error = nullptr;
    auto* original = gdk_pixbuf_new_from_file(path, &error);
    if (!original) {
        textureError = error ? error->message : "图片读取失败";
        g_clear_error(&error);
        return;
    }
    auto* texture = new Texture;
    texture->pixels = gdk_pixbuf_add_alpha(original, FALSE, 0, 0, 0);
    g_object_unref(original);
    page->width = gdk_pixbuf_get_width(texture->pixels);
    page->height = gdk_pixbuf_get_height(texture->pixels);
    page->rendererObject = texture;
}
extern "C" void _spAtlasPage_disposeTexture(spAtlasPage* page) {
    auto* texture = static_cast<Texture*>(page->rendererObject);
    if (texture) { g_object_unref(texture->pixels); delete texture; }
}
extern "C" char* _spUtil_readFile(const char* path, int* length) { return _spReadFile(path, length); }

struct Options {
    fs::path model, capture;
    std::string animation = "Relax", monitor, layer = "top", ns = "arkpets-hyprland";
    int fps = 30, right = 64, bottom = 40;
    double scale = 1, duration = 0;
    bool list = false, flip = false, straightAlpha = false, clickThrough = false, autoActivity = false;
    bool gravity = false, windowEdges = false;
    std::optional<std::uint32_t> activitySeed, interactionSeed;
};

static void prepareGraphicsEnvironment() {
    // GtkGLArea below uses desktop GL / GLSL 330. GDK's shared context must use
    // the same API: NVIDIA cannot share it with GTK's default GLES context.
    std::string debug = g_getenv("GDK_DEBUG") ? g_getenv("GDK_DEBUG") : "";
    if (debug.find("gl-prefer-gl") == std::string::npos) {
        if (!debug.empty()) debug += ',';
        debug += "gl-prefer-gl";
        g_setenv("GDK_DEBUG", debug.c_str(), TRUE);
    }
    g_setenv("GDK_BACKEND", "wayland", TRUE);
    // A desktop session can request a locale that has not been generated. Keep
    // valid locales intact; the fallback affects only this renderer process.
    if (!std::setlocale(LC_ALL, "")) {
        const char* fallback = std::setlocale(LC_ALL, "C.UTF-8") ? "C.UTF-8" : "C";
        std::setlocale(LC_ALL, fallback);
        g_setenv("LC_ALL", fallback, TRUE);
    }
}

static int integer(const std::string& text, int low, int high) {
    size_t used = 0;
    int value = std::stoi(text, &used);
    if (used != text.size() || value < low || value > high)
        throw std::runtime_error("数值超出范围: " + text);
    return value;
}

static Options arguments(int argc, char** argv) {
    Options o;
    for (int i = 1; i < argc; ++i) {
        std::string key = argv[i];
        auto value = [&]() -> std::string {
            if (++i == argc) throw std::runtime_error("参数缺少值: " + key);
            return argv[i];
        };
        if (key == "--model") o.model = value();
        else if (key == "--animation") o.animation = value();
        else if (key == "--monitor") o.monitor = value();
        else if (key == "--layer") o.layer = value();
        else if (key == "--namespace") o.ns = value();
        else if (key == "--scale") {
            std::string v = value(); size_t used = 0;
            o.scale = std::stod(v, &used);
            if (used != v.size() || !std::isfinite(o.scale) || o.scale < .0625 || o.scale > 6.25)
                throw std::runtime_error("scale 必须是 0.0625 到 6.25 之间的倍率");
        }
        else if (key == "--fps") o.fps = integer(value(), 1, 120);
        else if (key == "--right") o.right = integer(value(), -20000, 20000);
        else if (key == "--bottom") o.bottom = integer(value(), -20000, 20000);
        else if (key == "--duration") {
            std::string v = value(); size_t used = 0;
            o.duration = std::stod(v, &used);
            if (used != v.size() || !std::isfinite(o.duration) || o.duration <= 0 || o.duration > 86400)
                throw std::runtime_error("duration 必须是 0 到 86400 之间的秒数");
        }
        else if (key == "--capture-dir") o.capture = value();
        else if (key == "--list-animations") o.list = true;
        else if (key == "--flip") o.flip = true;
        else if (key == "--straight-alpha") o.straightAlpha = true;
        else if (key == "--click-through") o.clickThrough = true;
        else if (key == "--auto-activity") o.autoActivity = true;
        else if (key == "--gravity") o.gravity = true;
        else if (key == "--window-edges") { o.windowEdges = true; o.gravity = true; }
        else if (key == "--activity-seed") o.activitySeed = integer(value(), 0, std::numeric_limits<int>::max());
        else if (key == "--interaction-seed") o.interactionSeed = integer(value(), 0, std::numeric_limits<int>::max());
        else if (key == "--help") {
            std::cout << "ArkPets Linux 渲染器（Spine 3.8）\n"
                "  --model DIR          必填：含同名 .skel / .atlas / PNG 的模型目录\n"
                "  --animation NAME     循环动作，默认 Relax\n"
                "  --list-animations    列出模型的动作后退出，不需要图形会话\n"
                "  --monitor NAME       显示器接口名，例如 DP-4\n"
                "  --layer top|bottom|overlay  默认 top；bottom 在普通窗口后面\n"
                "  --scale 1            原始模型缩放倍率，1 单位对应 1 逻辑像素，支持 HiDPI\n"
                "  --right 64 --bottom 40     距离屏幕右/下边缘的逻辑像素\n"
                "  --fps 30 --flip       帧率 / 水平翻转\n"
                "  --straight-alpha     仅用于不是 PMA 的旧纹理；Ark-Models 默认 PMA\n"
                "  --click-through      鼠标穿透；默认左键点击互动、按住拖动\n"
                "  --auto-activity      自动走动，随机切换待机、坐下和睡觉\n"
                "  --gravity            启用重力，松开角色后落到支撑面\n"
                "  --window-edges       接收管理进程提供的窗口边缘（标准输入），同时开启重力\n"
                "  --activity-seed N    固定随机种子，用于复现活动测试\n"
                "  --interaction-seed N 固定点击互动随机种子，用于复现测试\n"
                "  --duration SECONDS    自动退出，适合验证\n"
                "  --capture-dir DIR     保存相隔半秒的两帧透明 PNG，适合验证\n"
                "窗口不接受键盘焦点；左键点击播放 Interact，有 Special 时 20% 概率播放 Special；\n"
                "按住拖动；透明区域穿透；Ctrl+C 退出。\n";
            std::exit(0);
        } else throw std::runtime_error("未知参数: " + key);
    }
    if (o.layer != "top" && o.layer != "bottom" && o.layer != "overlay")
        throw std::runtime_error("layer 必须是 top、bottom 或 overlay");
    if (o.model.empty()) throw std::runtime_error("请从图形界面下载并选择角色，或使用 --model 指定目录");
    return o;
}

// Reject incompatible headers before entering the upstream 3.8 binary reader.
static void checkVersion(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    auto readString = [&]() {
        unsigned length = 0;
        for (unsigned shift = 0; ; shift += 7) {
            int byte = in.get();
            if (byte < 0 || shift > 28) throw std::runtime_error("损坏的 Spine 文件头");
            length |= unsigned(byte & 127) << shift;
            if (!(byte & 128)) break;
        }
        if (length > 1024) throw std::runtime_error("不支持此 Spine 文件头");
        std::string text(length ? length - 1 : 0, '\0');
        in.read(text.data(), static_cast<std::streamsize>(text.size()));
        if (!in) throw std::runtime_error("Spine 文件被截断");
        return text;
    };
    readString();
    auto version = readString();
    if (version.rfind("3.8.", 0) != 0)
        throw std::runtime_error("仅支持 Spine 3.8，当前模型: " + version);
}

struct Geometry {
    std::vector<float> vertices;
    float* uv = nullptr;
    unsigned short* indices = nullptr;
    int count = 0;
    spColor* color = nullptr;
    Texture* texture = nullptr;
};

static Geometry geometry(spSlot* slot) {
    static unsigned short quad[] = {0, 1, 2, 2, 3, 0};
    Geometry g;
    auto* a = slot->attachment;
    if (!a || !slot->bone->active || slot->color.a <= 0) return g;
    spAtlasRegion* region = nullptr;
    if (a->type == SP_ATTACHMENT_REGION) {
        auto* r = reinterpret_cast<spRegionAttachment*>(a);
        g.vertices.resize(8);
        spRegionAttachment_computeWorldVertices(r, slot->bone, g.vertices.data(), 0, 2);
        g.uv = r->uvs; g.indices = quad; g.count = 6; g.color = &r->color;
        region = static_cast<spAtlasRegion*>(r->rendererObject);
    } else if (a->type == SP_ATTACHMENT_MESH) {
        auto* m = reinterpret_cast<spMeshAttachment*>(a);
        g.vertices.resize(m->super.worldVerticesLength);
        spVertexAttachment_computeWorldVertices(&m->super, slot, 0,
            m->super.worldVerticesLength, g.vertices.data(), 0, 2);
        g.uv = m->uvs; g.indices = m->triangles; g.count = m->trianglesCount; g.color = &m->color;
        region = static_cast<spAtlasRegion*>(m->rendererObject);
    }
    if (region) g.texture = static_cast<Texture*>(region->page->rendererObject);
    return g;
}

class Model {
public:
    spAtlas* atlas = nullptr;
    spSkeletonData* data = nullptr;
    spSkeleton* skeleton = nullptr;
    spAnimationStateData* stateData = nullptr;
    spAnimationState* state = nullptr;
    spSkeletonClipping* clip = nullptr;
    ModelBounds bounds, movementBounds;
    std::string currentAnimation, interactionAnimation, specialAnimation;
    bool currentLoop = true;
    std::vector<std::string> animationNames() const {
        std::vector<std::string> names;
        for (int i = 0; i < data->animationsCount; ++i) names.emplace_back(data->animations[i]->name);
        return names;
    }
    ~Model() {
        if (clip) spSkeletonClipping_dispose(clip);
        if (state) spAnimationState_dispose(state);
        if (stateData) spAnimationStateData_dispose(stateData);
        if (skeleton) spSkeleton_dispose(skeleton);
        if (data) spSkeletonData_dispose(data);
        if (atlas) spAtlas_dispose(atlas);
    }
    void load(const Options& o) {
        if (!fs::is_directory(o.model)) throw std::runtime_error("模型目录不存在: " + o.model.string());
        std::vector<fs::path> files;
        for (const auto& e : fs::directory_iterator(o.model))
            if (e.is_regular_file() && e.path().extension() == ".skel") files.push_back(e.path());
        if (files.size() != 1) throw std::runtime_error("模型目录中必须恰有一个 .skel 文件");
        checkVersion(files[0]);
        auto atlasPath = files[0]; atlasPath.replace_extension(".atlas");
        if (!fs::exists(atlasPath)) throw std::runtime_error("缺少同名 .atlas 文件");
        textureError.clear();
        atlas = spAtlas_createFromFile(atlasPath.c_str(), nullptr);
        if (!atlas || !atlas->pages || !textureError.empty())
            throw std::runtime_error("图集加载失败: " + textureError);
        auto* reader = spSkeletonBinary_create(atlas);
        data = spSkeletonBinary_readSkeletonDataFile(reader, files[0].c_str());
        std::string error = reader->error ? reader->error : "未知错误";
        spSkeletonBinary_dispose(reader);
        if (!data) throw std::runtime_error("骨骼加载失败: " + error);
        if (!data->animationsCount) throw std::runtime_error("模型没有动画");
        if (o.list) return;
        auto* animation = spSkeletonData_findAnimation(data, o.animation.c_str());
        if (!animation) throw std::runtime_error("没有动作 " + o.animation + "，用 --list-animations 查看");
        skeleton = spSkeleton_create(data);
        stateData = spAnimationStateData_create(data);
        state = spAnimationState_create(stateData);
        clip = spSkeletonClipping_create();
        const auto names = animationNames();
        const auto plan = activityPlan(names, o.animation);
        const auto defaultAnimation = findAnimation(names, {"default"});
        ModelBounds idleBounds;
        interactionAnimation = findAnimation(names, {"interact"});
        specialAnimation = findAnimation(names, {"special"});
        std::vector<std::string> boundsAnimations{o.animation};
        auto includeAnimation = [&](const std::string& name) {
            if (!name.empty() && std::find(boundsAnimations.begin(), boundsAnimations.end(), name) == boundsAnimations.end())
                boundsAnimations.push_back(name);
        };
        // Use the same surface in manual and automatic modes. Interaction can
        // start at any time, so its bounds must also be reserved before mapping.
        for (const auto& action : plan) includeAnimation(action.animation);
        includeAnimation(interactionAnimation);
        includeAnimation(specialAnimation);
        includeAnimation(defaultAnimation);
        for (const auto& name : boundsAnimations) {
            auto* sampled = spSkeletonData_findAnimation(data, name.c_str());
            spAnimationState_clearTracks(state);
            spSkeleton_setToSetupPose(skeleton);
            spAnimationState_setAnimation(state, 0, sampled, false);
            ModelBounds sampledBounds;
            int samples = std::clamp(static_cast<int>(std::ceil(sampled->duration * 60)), 2, 600);
            for (int n = 0; n <= samples; ++n) {
                if (n) spAnimationState_update(state, sampled->duration / samples);
                spAnimationState_apply(state, skeleton);
                spSkeleton_updateWorldTransform(skeleton);
                for (int s = 0; s < skeleton->slotsCount; ++s) {
                    auto g = geometry(skeleton->drawOrder[s]);
                    if (!g.color || g.color->a <= 0) continue;
                    for (size_t v = 0; v + 1 < g.vertices.size(); v += 2) {
                        sampledBounds.include(g.vertices[v], g.vertices[v+1]);
                    }
                }
            }
            bounds.include(sampledBounds);
            if (name == defaultAnimation) movementBounds = sampledBounds;
            if (name == plan.front().animation) idleBounds = sampledBounds;
        }
        if (!bounds.valid())
            throw std::runtime_error("模型没有可见的绘制内容");
        // Some exports omit Default or leave it empty. Prefer the idle pose
        // before falling back to the only drawable bounds available.
        if (!movementBounds.valid()) movementBounds = idleBounds.valid() ? idleBounds : bounds;
        spAnimationState_clearTracks(state);
        stateData->defaultMix = 0.18f;
        bounds.includeMirror();
        skeleton->scaleX = o.flip ? -1 : 1;
        play(o.animation);
    }
    void play(const std::string& name, bool loop = true, bool restart = false) {
        if (!restart && currentAnimation == name && currentLoop == loop) return;
        currentAnimation = name;
        currentLoop = loop;
        spSkeleton_setToSetupPose(skeleton);
        spAnimationState_setAnimationByName(state, 0, name.c_str(), loop);
        advance(0);
    }
    bool finished() const {
        auto* entry = spAnimationState_getCurrent(state, 0);
        return entry && !entry->loop && entry->trackTime >= entry->animationEnd - entry->animationStart;
    }
    void advance(float dt) {
        spSkeleton_update(skeleton, dt);
        spAnimationState_update(state, dt);
        spAnimationState_apply(state, skeleton);
        spSkeleton_updateWorldTransform(skeleton);
    }
};

static GLuint shader(GLenum type, const char* code) {
    GLuint s = glCreateShader(type);
    glShaderSource(s, 1, &code, nullptr); glCompileShader(s);
    GLint ok = 0; glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[2048] = {}; glGetShaderInfoLog(s, sizeof(log), nullptr, log);
        glDeleteShader(s); throw std::runtime_error(log);
    }
    return s;
}

class Pet {
public:
    Options o;
    Model model;
    GtkWindow* window = nullptr;
    GtkGLArea* area = nullptr;
    GMainLoop* loop = nullptr;
    GLuint program = 0, vao = 0, vbo = 0;
    GLint light = -1, dark = -1;
    gint64 previous = 0, started = 0;
    unsigned captures = 0;
    bool failed = false, ready = false;
    bool dragging = false;
    bool interacting = false;
    std::string activeInteraction;
    std::mt19937 interactionRandom;
    gint64 lastInputUpdate = 0;
    int screenWidth = 0, screenHeight = 0, dragRight = 0, dragBottom = 0;
    double startPointerX = 0, startPointerY = 0;
    cairo_region_t* inputRegion = nullptr;
    std::unique_ptr<ActivityDirector> activity;
    std::unique_ptr<ModelViewport> viewport;
    GravityBody body;
    std::vector<StandingEdge> standingEdges;
    std::string edgeInput;
    unsigned long long edgesGeneration = 0;
    gint64 lastEdgesAt = 0, lastPhysicsPublish = 0;
    bool dropWindowSupport = false;
    guint edgeWatch = 0;

    ~Pet() { if (inputRegion) cairo_region_destroy(inputRegion); }

    void publishPosition() {
        std::cout << "ARKPETS_POSITION " << o.right << ' ' << o.bottom << '\n' << std::flush;
    }
    MovementLimits movementLimits() const {
        return viewport->movementLimits(model.movementBounds, screenWidth, screenHeight, model.skeleton->scaleX < 0);
    }
    double bodyCenterAtZero() const {
        double center=(model.movementBounds.minX+model.movementBounds.maxX)/2.0;
        if (model.skeleton->scaleX<0) center=-center;
        return screenWidth-viewport->width+(center-viewport->minX)*viewport->scale;
    }
    double bodyFeetAtZero() const {
        return screenHeight-(model.movementBounds.minY-viewport->minY)*viewport->scale;
    }
    MovementLimits walkingLimits() const {
        auto limits=movementLimits();
        if (o.gravity && body.grounded()) {
            const double origin=bodyCenterAtZero();
            limits.minRight=std::max(limits.minRight,static_cast<int>(std::ceil(origin-body.walkRight())));
            limits.maxRight=std::min(limits.maxRight,static_cast<int>(std::floor(origin-body.walkLeft())));
            if (limits.minRight>limits.maxRight) limits.minRight=limits.maxRight=static_cast<int>(std::lround(origin-body.x));
        }
        return limits;
    }
    void resetGravity() {
        body.release(); body.x=bodyCenterAtZero()-o.right; body.y=bodyFeetAtZero()-o.bottom;
    }
    void readEdges() {
        char buffer[8192]; ssize_t count;
        while ((count=read(STDIN_FILENO,buffer,sizeof(buffer)))>0) edgeInput.append(buffer,static_cast<size_t>(count));
        if (edgeInput.size()>1024*1024) { edgeInput.clear(); standingEdges.clear(); dropWindowSupport=true; return; }
        for (size_t end;(end=edgeInput.find('\n'))!=std::string::npos;) {
            unsigned long long generation=edgesGeneration;
            if (readStandingEdges(edgeInput.substr(0,end),generation,standingEdges)) {
                dropWindowSupport|=generation!=edgesGeneration;
                edgesGeneration=generation; lastEdgesAt=g_get_monotonic_time();
            }
            edgeInput.erase(0,end+1);
        }
    }
    void updateGravity(double elapsed) {
        if (!o.gravity || dragging) return;
        const auto now=g_get_monotonic_time();
        if (o.windowEdges && now-lastEdgesAt>500000) {
            if (!standingEdges.empty()) dropWindowSupport=true;
            standingEdges.clear();
        }
        const auto limits=movementLimits();
        const bool wasGrounded=body.grounded();
        body.step(elapsed,standingEdges,screenWidth,bodyFeetAtZero()-limits.minBottom,dropWindowSupport);
        dropWindowSupport=false;
        const int right=std::clamp(static_cast<int>(std::lround(bodyCenterAtZero()-body.x)),limits.minRight,limits.maxRight);
        const int bottom=std::clamp(static_cast<int>(std::lround(bodyFeetAtZero()-body.y)),limits.minBottom,limits.maxBottom);
        const bool moved=right!=o.right || bottom!=o.bottom;
        o.right=right; o.bottom=bottom;
        if (activity) activity->setRight(bodyCenterAtZero()-body.x);
        if (moved) {
            gtk_layer_set_margin(window,GTK_LAYER_SHELL_EDGE_RIGHT,o.right);
            gtk_layer_set_margin(window,GTK_LAYER_SHELL_EDGE_BOTTOM,o.bottom);
        }
        if ((!wasGrounded && body.grounded()) || (moved && now-lastPhysicsPublish>500000)) {
            publishPosition(); lastPhysicsPublish=now;
        }
    }
    void applyActivity(bool announce) {
        if (!activity) return;
        // Ark-Models' default chibi orientation faces right; --flip can adapt
        // models whose artwork uses the opposite orientation.
        model.skeleton->scaleX = (activity->facingRight() ? 1 : -1) * (o.flip ? -1 : 1);
        if (!interacting) model.play(activity->animation());
        if (announce)
            std::cout << "ARKPETS_ACTIVITY " << activity->animation() << '\t'
                      << o.right << '\t' << (activity->paused() ? "paused" : activity->walking() ? "walking" : "resting")
                      << '\n' << std::flush;
    }
    void updateActivity(double elapsed) {
        if (!activity || dragging || interacting || (o.gravity && !body.grounded())) return;
        bool wasWalking = activity->walking();
        auto limits = walkingLimits();
        bool changed = activity->advance(elapsed, limits.maxRight, std::clamp(72.0*o.scale, 24.0, 100.0), limits.minRight);
        const int previousRight=o.right;
        o.right=static_cast<int>(std::lround(activity->right()));
        const double center=bodyCenterAtZero()-activity->right();
        const bool flipped=model.skeleton->scaleX<0;
        applyActivity(false);
        if (o.gravity && flipped!=(model.skeleton->scaleX<0))
            activity->setRight(bodyCenterAtZero()-center);
        // Turning mirrors an asymmetric Default pose. Recheck its new edges
        // without resetting the activity timer or its walking direction.
        limits = walkingLimits();
        activity->advance(0, limits.maxRight, 0, limits.minRight);
        int right = static_cast<int>(std::lround(activity->right()));
        o.right=right;
        if (right != previousRight) {
            gtk_layer_set_margin(window, GTK_LAYER_SHELL_EDGE_RIGHT, o.right);
        }
        if (o.gravity) body.x=bodyCenterAtZero()-activity->right();
        if (changed) applyActivity(true);
        if (changed && wasWalking) publishPosition();
    }

    void startInteraction() {
        if (o.clickThrough) return;
        const auto& animation = chooseInteraction(model.interactionAnimation, model.specialAnimation, interactionRandom);
        if (animation.empty()) return;
        activeInteraction = animation;
        interacting = true;
        if (activity) activity->pause();
        model.play(activeInteraction, false, true);
        std::cout << "ARKPETS_INTERACTION " << activeInteraction << "\tstarted\n" << std::flush;
        gtk_gl_area_queue_render(area);
    }
    void finishInteraction() {
        if (!interacting || !model.finished()) return;
        interacting = false;
        if (activity) {
            if (!dragging) activity->resumeAt(o.right);
            applyActivity(true);
        } else model.play(o.animation);
        std::cout << "ARKPETS_INTERACTION " << activeInteraction << "\tfinished\t"
                  << model.currentAnimation << '\n' << std::flush;
    }

    // Wayland deliberately has no client-global cursor position. Hyprland's IPC
    // supplies stable logical coordinates even while the layer itself is moving.
    std::optional<std::pair<int, int>> pointerPosition() {
        const char* runtime = g_getenv("XDG_RUNTIME_DIR");
        const char* signature = g_getenv("HYPRLAND_INSTANCE_SIGNATURE");
        if (!runtime || !signature) return std::nullopt;
        std::string path = std::string(runtime) + "/hypr/" + signature + "/.socket.sock";
        sockaddr_un address{}; address.sun_family = AF_UNIX;
        if (path.size() >= sizeof(address.sun_path)) return std::nullopt;
        std::copy(path.begin(), path.end(), address.sun_path);
        int fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
        if (fd < 0) return std::nullopt;
        timeval timeout{0, 100000};
        setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
        setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
        if (connect(fd, reinterpret_cast<sockaddr*>(&address), sizeof(address)) < 0) {
            close(fd); return std::nullopt;
        }
        constexpr char request[] = "j/cursorpos";
        if (send(fd, request, sizeof(request)-1, MSG_NOSIGNAL) < 0) { close(fd); return std::nullopt; }
        char buffer[512]{}; ssize_t n = recv(fd, buffer, sizeof(buffer)-1, 0); close(fd);
        if (n <= 0) return std::nullopt;
        static const std::regex expression(R"re("x"\s*:\s*(-?\d+)\s*,\s*"y"\s*:\s*(-?\d+))re");
        std::cmatch match;
        if (!std::regex_search(buffer, match, expression)) return std::nullopt;
        return std::pair{std::stoi(match[1]), std::stoi(match[2])};
    }
    void setInput(cairo_region_t* region) {
        if (!inputRegion || !cairo_region_equal(inputRegion, region)) {
            gdk_surface_set_input_region(gtk_native_get_surface(GTK_NATIVE(window)), region);
            if (inputRegion) cairo_region_destroy(inputRegion);
            inputRegion = cairo_region_copy(region);
        }
        cairo_region_destroy(region);
    }
    void updateInput() {
        if (o.clickThrough || dragging) return;
        auto now = g_get_monotonic_time();
        if (now - lastInputUpdate < 150000) return;
        lastInputUpdate = now;
        GLint viewport[4]; glGetIntegerv(GL_VIEWPORT, viewport);
        int w = viewport[2], h = viewport[3];
        if (w <= 0 || h <= 0) return;
        const int logicalWidth = gtk_widget_get_width(GTK_WIDGET(area));
        const int logicalHeight = gtk_widget_get_height(GTK_WIDGET(area));
        std::vector<unsigned char> pixels(static_cast<size_t>(w)*h*4);
        glPixelStorei(GL_PACK_ALIGNMENT, 1);
        glReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
        auto* region = cairo_region_create();
        // A small logical-pixel grid avoids thousands of tiny Wayland rectangles.
        // Only visible parts of the character receive pointer events.
        constexpr int step = 2;
        for (int y = 0; y < logicalHeight; y += step) {
            int run = -1;
            for (int x = 0; x <= logicalWidth + step - 1; x += step) {
                bool opaque = false;
                if (x < logicalWidth) {
                    int px = std::min(w-1, (x+1)*w/logicalWidth);
                    int py = std::max(0, h-1-std::min(h-1, (y+1)*h/logicalHeight));
                    opaque = pixels[(static_cast<size_t>(py)*w+px)*4+3] > 20;
                }
                if (opaque && run < 0) run = x;
                if (!opaque && run >= 0) {
                    cairo_rectangle_int_t rect{run, y, std::min(x, logicalWidth)-run, std::min(step, logicalHeight-y)};
                    cairo_region_union_rectangle(region, &rect); run = -1;
                }
            }
        }
        setInput(region);
    }
    void beginDrag(GtkGesture* gesture) {
        // GtkGestureDrag begins on button press; clicks and drags both interact.
        startInteraction();
        auto cursor = pointerPosition();
        if (o.clickThrough || !cursor) {
            gtk_gesture_set_state(gesture, GTK_EVENT_SEQUENCE_DENIED); return;
        }
        dragging = true; dragRight = o.right; dragBottom = o.bottom;
        if (o.gravity) resetGravity();
        if (activity) {activity->pause(); applyActivity(true);}
        startPointerX = cursor->first; startPointerY = cursor->second;
        cairo_rectangle_int_t rect{0, 0, viewport->width, viewport->height};
        setInput(cairo_region_create_rectangle(&rect));
        gtk_widget_set_cursor_from_name(GTK_WIDGET(area), "grabbing");
        gtk_gesture_set_state(gesture, GTK_EVENT_SEQUENCE_CLAIMED);
    }
    void moveDrag() {
        if (!dragging) return;
        auto cursor = pointerPosition();
        if (!cursor) return;
        const auto limits = movementLimits();
        o.right = std::clamp(dragRight - int(cursor->first-startPointerX), limits.minRight, limits.maxRight);
        o.bottom = std::clamp(dragBottom - int(cursor->second-startPointerY), limits.minBottom, limits.maxBottom);
        if (o.gravity) resetGravity();
        gtk_layer_set_margin(window, GTK_LAYER_SHELL_EDGE_RIGHT, o.right);
        gtk_layer_set_margin(window, GTK_LAYER_SHELL_EDGE_BOTTOM, o.bottom);
        gtk_gl_area_queue_render(area);
    }
    void endDrag() {
        if (!dragging) return;
        moveDrag(); dragging = false; lastInputUpdate = 0;
        gtk_widget_set_cursor_from_name(GTK_WIDGET(area), "grab");
        if (activity && !interacting) {activity->resumeAt(o.right); applyActivity(true);}
        publishPosition();
        gtk_gl_area_queue_render(area);
    }

    void fail(const std::string& error) {
        std::cerr << "错误: " << error << '\n'; failed = true;
        g_main_loop_quit(loop);
    }
    void initializeGL() {
        const char* vs = R"GLSL(#version 330 core
layout(location=0) in vec2 position;
layout(location=1) in vec2 uv;
out vec2 texCoord;
void main() { texCoord = uv; gl_Position = vec4(position, 0.0, 1.0); }
)GLSL";
        const char* fragment = R"GLSL(#version 330 core
uniform sampler2D image;
uniform vec4 light;
uniform vec3 dark;
uniform bool straightAlpha;
in vec2 texCoord;
out vec4 color;
void main() {
    vec4 t = texture(image, texCoord);
    if (straightAlpha) t.rgb *= t.a;
    color = vec4((t.rgb * light.rgb + (vec3(t.a)-t.rgb)*dark)*light.a, t.a*light.a);
}
)GLSL";
        GLuint v = shader(GL_VERTEX_SHADER, vs), f = shader(GL_FRAGMENT_SHADER, fragment);
        program = glCreateProgram(); glAttachShader(program, v); glAttachShader(program, f);
        glLinkProgram(program); glDeleteShader(v); glDeleteShader(f);
        GLint ok = 0; glGetProgramiv(program, GL_LINK_STATUS, &ok);
        if (!ok) throw std::runtime_error("OpenGL 着色器链接失败");
        glUseProgram(program); glUniform1i(glGetUniformLocation(program, "image"), 0);
        glUniform1i(glGetUniformLocation(program, "straightAlpha"), o.straightAlpha);
        light = glGetUniformLocation(program, "light"); dark = glGetUniformLocation(program, "dark");
        glGenVertexArrays(1, &vao); glBindVertexArray(vao);
        glGenBuffers(1, &vbo); glBindBuffer(GL_ARRAY_BUFFER, vbo);
        glEnableVertexAttribArray(0); glEnableVertexAttribArray(1);
        glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4*sizeof(float), nullptr);
        glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4*sizeof(float), reinterpret_cast<void*>(2*sizeof(float)));
        for (auto* p = model.atlas->pages; p; p = p->next) {
            auto* t = static_cast<Texture*>(p->rendererObject);
            glGenTextures(1, &t->id); glBindTexture(GL_TEXTURE_2D, t->id);
            glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
            glPixelStorei(GL_UNPACK_ROW_LENGTH, gdk_pixbuf_get_rowstride(t->pixels)/4);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, p->width, p->height, 0, GL_RGBA,
                GL_UNSIGNED_BYTE, gdk_pixbuf_get_pixels(t->pixels));
            glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        }
        glBindVertexArray(0);
        std::cout << "OpenGL: " << glGetString(GL_RENDERER) << " / " << glGetString(GL_VERSION) << '\n' << std::flush;
    }
    void disposeGL() {
        if (!program) return;
        for (auto* p = model.atlas->pages; p; p = p->next) {
            auto* t = static_cast<Texture*>(p->rendererObject);
            if (t && t->id) { glDeleteTextures(1, &t->id); t->id = 0; }
        }
        glDeleteBuffers(1, &vbo); glDeleteVertexArrays(1, &vao); glDeleteProgram(program); program = 0;
    }
    void capture() {
        GLint vp[4]; glGetIntegerv(GL_VIEWPORT, vp);
        int w = vp[2], h = vp[3];
        std::vector<unsigned char> pixels(static_cast<size_t>(w)*h*4), out(pixels.size());
        glPixelStorei(GL_PACK_ALIGNMENT, 1);
        glReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
        // GTK expects premultiplied framebuffer pixels; PNG stores straight alpha.
        for (int y = 0; y < h; ++y) for (int x = 0; x < w; ++x) {
            size_t src = (static_cast<size_t>(h-1-y)*w+x)*4, dst = (static_cast<size_t>(y)*w+x)*4;
            int a = pixels[src+3]; out[dst+3] = a;
            for (int c = 0; c < 3; ++c)
                out[dst+c] = a ? std::min(255, (int(pixels[src+c])*255+a/2)/a) : 0;
        }
        fs::create_directories(o.capture);
        auto path = o.capture / ("frame-" + std::to_string(captures) + ".png");
        auto* pb = gdk_pixbuf_new_from_data(out.data(), GDK_COLORSPACE_RGB, TRUE, 8, w, h, w*4, nullptr, nullptr);
        GError* error = nullptr;
        bool ok = gdk_pixbuf_save(pb, path.c_str(), "png", &error, nullptr);
        g_object_unref(pb);
        if (!ok) { std::string msg = error->message; g_error_free(error); throw std::runtime_error(msg); }
        ++captures;
    }
    void render() {
        if (!program) return;
        auto now = g_get_monotonic_time();
        if (!started) { started = previous = now; }
        // Preserve animation/activity speed even at low user-selected FPS.
        double elapsed = std::min(1.0, (now-previous)/1000000.0); previous = now;
        updateGravity(elapsed);
        updateActivity(elapsed);
        model.advance(static_cast<float>(elapsed));
        finishInteraction();
        glDisable(GL_DEPTH_TEST); glDisable(GL_CULL_FACE); glDisable(GL_SCISSOR_TEST);
        glClearColor(0, 0, 0, 0); glClear(GL_COLOR_BUFFER_BIT);
        glEnable(GL_BLEND); glBlendEquation(GL_FUNC_ADD);
        glUseProgram(program); glBindVertexArray(vao); glBindBuffer(GL_ARRAY_BUFFER, vbo);
        glActiveTexture(GL_TEXTURE0);
        const int logicalWidth = gtk_widget_get_width(GTK_WIDGET(area));
        const int logicalHeight = gtk_widget_get_height(GTK_WIDGET(area));
        auto* skeleton = model.skeleton;
        auto* clip = model.clip;
        for (int s = 0; s < skeleton->slotsCount; ++s) {
            auto* slot = skeleton->drawOrder[s];
            if (slot->attachment && slot->attachment->type == SP_ATTACHMENT_CLIPPING && slot->bone->active) {
                spSkeletonClipping_clipStart(clip, slot, reinterpret_cast<spClippingAttachment*>(slot->attachment));
                continue;
            }
            auto g = geometry(slot);
            if (g.count && g.texture && g.color && g.color->a > 0) {
                float* xy = g.vertices.data(); float* uv = g.uv;
                auto* indices = g.indices; int count = g.count;
                if (spSkeletonClipping_isClipping(clip)) {
                    spSkeletonClipping_clipTriangles(clip, xy, static_cast<int>(g.vertices.size()), indices, count, uv, 2);
                    xy = clip->clippedVertices->items; uv = clip->clippedUVs->items;
                    indices = clip->clippedTriangles->items; count = clip->clippedTriangles->size;
                }
                std::vector<float> vertices; vertices.reserve(count*4);
                for (int i = 0; i < count; ++i) {
                    unsigned p = indices[i]*2;
                    vertices.insert(vertices.end(), {viewport->projectX(xy[p], logicalWidth),
                        viewport->projectY(xy[p+1], logicalHeight), uv[p], uv[p+1]});
                }
                auto& a = *g.color; auto& b = slot->color; auto& c = skeleton->color;
                glUniform4f(light, a.r*b.r*c.r, a.g*b.g*c.g, a.b*b.b*c.b, a.a*b.a*c.a);
                auto* d = slot->darkColor;
                glUniform3f(dark, d ? d->r : 0, d ? d->g : 0, d ? d->b : 0);
                GLenum src = GL_ONE, dst = GL_ONE_MINUS_SRC_ALPHA;
                switch (slot->data->blendMode) {
                    case SP_BLEND_MODE_ADDITIVE: dst = GL_ONE; break;
                    case SP_BLEND_MODE_MULTIPLY: src = GL_DST_COLOR; break;
                    case SP_BLEND_MODE_SCREEN: src = GL_ONE; dst = GL_ONE_MINUS_SRC_COLOR; break;
                    default: break;
                }
                // Alpha accumulation must be source-over, not SRC_ALPHA squared.
                glBlendFuncSeparate(src, dst, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
                glBindTexture(GL_TEXTURE_2D, g.texture->id);
                glBufferData(GL_ARRAY_BUFFER, vertices.size()*sizeof(float), vertices.data(), GL_STREAM_DRAW);
                glDrawArrays(GL_TRIANGLES, 0, count);
            }
            spSkeletonClipping_clipEnd(clip, slot);
        }
        spSkeletonClipping_clipEnd2(clip);
        glBindVertexArray(0); glUseProgram(0); glDisable(GL_BLEND);
        updateInput();
        if (!o.capture.empty() && captures < 2 && (now-started)/1000000.0 >= captures*0.5) capture();
        if (!ready) {
            ready = true;
            std::cout << "ARKPETS_READY\n" << std::flush;
        }
    }
    int run() {
        // A separate generator keeps click frequency independent of walking,
        // frame rate and the other models' interaction histories.
        interactionRandom.seed(o.interactionSeed ? *o.interactionSeed : std::random_device{}());
        if (!g_getenv("WAYLAND_DISPLAY")) throw std::runtime_error("请在 Hyprland 的 Wayland 会话中运行");
        if (!gtk_init_check() || !gtk_layer_is_supported())
            throw std::runtime_error("无法连接支持 layer-shell 的 Wayland 合成器");
        auto* display = gdk_display_get_default();
        auto* monitors = gdk_display_get_monitors(display);
        GdkMonitor* monitor = nullptr;
        if (!o.monitor.empty()) {
            for (guint i = 0; i < g_list_model_get_n_items(monitors); ++i) {
                auto* m = GDK_MONITOR(g_list_model_get_item(monitors, i));
                const char* connector = gdk_monitor_get_connector(m);
                if (connector && o.monitor == connector) { monitor = m; break; }
                g_object_unref(m);
            }
            if (!monitor) throw std::runtime_error("找不到显示器: " + o.monitor);
        }
        if (!monitor && g_list_model_get_n_items(monitors))
            monitor = GDK_MONITOR(g_list_model_get_item(monitors, 0));
        if (monitor) {
            GdkRectangle rect; gdk_monitor_get_geometry(monitor, &rect);
            screenWidth = rect.width; screenHeight = rect.height;
            if (const char* connector = gdk_monitor_get_connector(monitor)) o.monitor = connector;
        }
        viewport = std::make_unique<ModelViewport>(model.bounds, o.scale);
        std::cout << "ARKPETS_LAYOUT " << viewport->scale << '\t' << viewport->width
                  << '\t' << viewport->height << '\n' << std::flush;
        if (o.autoActivity) {
            activity = std::make_unique<ActivityDirector>(activityPlan(model.animationNames(), o.animation), o.right,
                o.activitySeed ? *o.activitySeed : std::random_device{}());
            applyActivity(true);
        }
        const auto limits = movementLimits();
        o.right = std::clamp(o.right, limits.minRight, limits.maxRight);
        o.bottom = std::clamp(o.bottom, limits.minBottom, limits.maxBottom);
        body.halfWidth=model.movementBounds.width()*viewport->scale/2;
        body.height=model.movementBounds.height()*viewport->scale;
        resetGravity();
        if (activity) activity->advance(0, limits.maxRight, 0, limits.minRight);
        loop = g_main_loop_new(nullptr, FALSE);
        window = GTK_WINDOW(gtk_window_new());
        gtk_window_set_title(window, "ArkPets Linux");
        gtk_window_set_decorated(window, FALSE);
        gtk_window_set_resizable(window, FALSE);
        gtk_window_set_default_size(window, viewport->width, viewport->height);
        gtk_widget_add_css_class(GTK_WIDGET(window), "arkpets");
        auto* css = gtk_css_provider_new();
        gtk_css_provider_load_from_string(css,
            "window.arkpets, window.arkpets > * { background: transparent; background-image: none;"
            " border: none; box-shadow: none; border-radius: 0; padding: 0; margin: 0; }");
        gtk_style_context_add_provider_for_display(display, GTK_STYLE_PROVIDER(css), GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
        g_object_unref(css);
        gtk_layer_init_for_window(window);
        gtk_layer_set_namespace(window, o.ns.c_str());
        gtk_layer_set_layer(window, o.layer == "bottom" ? GTK_LAYER_SHELL_LAYER_BOTTOM :
            o.layer == "overlay" ? GTK_LAYER_SHELL_LAYER_OVERLAY : GTK_LAYER_SHELL_LAYER_TOP);
        gtk_layer_set_keyboard_mode(window, GTK_LAYER_SHELL_KEYBOARD_MODE_NONE);
        gtk_layer_set_exclusive_zone(window, -1); // No reservation; use the full output geometry.
        gtk_layer_set_anchor(window, GTK_LAYER_SHELL_EDGE_RIGHT, TRUE);
        gtk_layer_set_anchor(window, GTK_LAYER_SHELL_EDGE_BOTTOM, TRUE);
        gtk_layer_set_margin(window, GTK_LAYER_SHELL_EDGE_RIGHT, o.right);
        gtk_layer_set_margin(window, GTK_LAYER_SHELL_EDGE_BOTTOM, o.bottom);
        if (monitor) { gtk_layer_set_monitor(window, monitor); g_object_unref(monitor); }
        area = GTK_GL_AREA(gtk_gl_area_new());
        gtk_gl_area_set_required_version(area, 3, 3);
        gtk_gl_area_set_allowed_apis(area, GDK_GL_API_GL);
        gtk_gl_area_set_auto_render(area, FALSE);
        gtk_widget_set_size_request(GTK_WIDGET(area), viewport->width, viewport->height);
        gtk_widget_set_can_target(GTK_WIDGET(area), !o.clickThrough);
        if (!o.clickThrough) {
            auto* drag = gtk_gesture_drag_new();
            gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(drag), GDK_BUTTON_PRIMARY);
            g_signal_connect(drag, "drag-begin", G_CALLBACK(+[](GtkGestureDrag* gesture, double, double, gpointer self) {
                static_cast<Pet*>(self)->beginDrag(GTK_GESTURE(gesture));
            }), this);
            g_signal_connect(drag, "drag-update", G_CALLBACK(+[](GtkGestureDrag*, double, double, gpointer self) {
                static_cast<Pet*>(self)->moveDrag();
            }), this);
            g_signal_connect(drag, "drag-end", G_CALLBACK(+[](GtkGestureDrag*, double, double, gpointer self) {
                static_cast<Pet*>(self)->endDrag();
            }), this);
            gtk_widget_add_controller(GTK_WIDGET(area), GTK_EVENT_CONTROLLER(drag));
            gtk_widget_set_cursor_from_name(GTK_WIDGET(area), "grab");
        }
        gtk_window_set_child(window, GTK_WIDGET(area));
        g_signal_connect(window, "map", G_CALLBACK(+[](GtkWidget* w, gpointer) {
            auto* surface = gtk_native_get_surface(GTK_NATIVE(w));
            auto* empty = cairo_region_create();
            gdk_surface_set_input_region(surface, empty);
            cairo_region_destroy(empty);
        }), nullptr);
        g_signal_connect(area, "realize", G_CALLBACK(+[](GtkGLArea* a, gpointer self) {
            auto& p = *static_cast<Pet*>(self); gtk_gl_area_make_current(a);
            if (auto* e = gtk_gl_area_get_error(a)) { p.fail(e->message); return; }
            try { p.initializeGL(); } catch (const std::exception& e) { p.fail(e.what()); }
        }), this);
        g_signal_connect(area, "render", G_CALLBACK(+[](GtkGLArea*, GdkGLContext*, gpointer self) -> gboolean {
            auto& p = *static_cast<Pet*>(self);
            try { p.render(); } catch (const std::exception& e) { p.fail(e.what()); }
            return TRUE;
        }), this);
        g_signal_connect(area, "unrealize", G_CALLBACK(+[](GtkGLArea* a, gpointer self) {
            gtk_gl_area_make_current(a);
            if (!gtk_gl_area_get_error(a)) static_cast<Pet*>(self)->disposeGL();
        }), this);
        g_signal_connect(window, "close-request", G_CALLBACK(+[](GtkWindow*, gpointer self) -> gboolean {
            g_main_loop_quit(static_cast<Pet*>(self)->loop); return TRUE;
        }), this);
        auto quit = +[](gpointer self) -> gboolean { g_main_loop_quit(static_cast<Pet*>(self)->loop); return G_SOURCE_CONTINUE; };
        guint sigint = g_unix_signal_add(SIGINT, quit, this), sigterm = g_unix_signal_add(SIGTERM, quit, this);
        guint timer = g_timeout_add(std::max(1, 1000/o.fps), +[](gpointer self) -> gboolean {
            auto& p = *static_cast<Pet*>(self); gtk_gl_area_queue_render(p.area); return G_SOURCE_CONTINUE;
        }, this);
        guint expiry = o.duration ? g_timeout_add(static_cast<guint>(o.duration*1000), quit, this) : 0;
        int inputFlags=-1;
        if (o.windowEdges) {
            inputFlags=fcntl(STDIN_FILENO,F_GETFL,0);
            if (inputFlags>=0 && fcntl(STDIN_FILENO,F_SETFL,inputFlags|O_NONBLOCK)==0)
                edgeWatch=g_unix_fd_add(STDIN_FILENO,GIOCondition(G_IO_IN|G_IO_HUP|G_IO_ERR),
                    +[](gint,GIOCondition condition,gpointer self)->gboolean {
                        auto& p=*static_cast<Pet*>(self); p.readEdges();
                        if (condition&(G_IO_HUP|G_IO_ERR)) {
                            p.standingEdges.clear(); p.dropWindowSupport=true; p.edgeWatch=0; return G_SOURCE_REMOVE;
                        }
                        return G_SOURCE_CONTINUE;
                    },this);
        }
        std::cout << "模型: " << o.model << "，动作: " << o.animation << "，" << o.fps << " FPS\n"
                  << "Wayland layer-shell: " << o.ns << (o.clickThrough ? "；鼠标穿透" : "；可拖动")
                  << "；不占用键盘焦点\n" << std::flush;
        gtk_window_present(window);
        if (!failed) g_main_loop_run(loop);
        publishPosition();
        g_source_remove(timer); g_source_remove(sigint); g_source_remove(sigterm);
        if (expiry) g_source_remove(expiry);
        if (edgeWatch) g_source_remove(edgeWatch);
        if (inputFlags>=0) fcntl(STDIN_FILENO,F_SETFL,inputFlags);
        gtk_window_destroy(window);
        g_main_loop_unref(loop);
        return failed ? 1 : 0;
    }
};

int main(int argc, char** argv) {
    try {
        Pet pet; pet.o = arguments(argc, argv);
        if (!pet.o.list) prepareGraphicsEnvironment();
        pet.model.load(pet.o);
        if (pet.o.list) {
            for (int i = 0; i < pet.model.data->animationsCount; ++i) {
                auto* a = pet.model.data->animations[i];
                std::cout << a->name << '\t' << a->duration << "s\n";
            }
            return 0;
        }
        return pet.run();
    } catch (const std::exception& e) { std::cerr << "错误: " << e.what() << '\n'; return 1; }
}
