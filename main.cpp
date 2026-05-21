#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>
#include <sndfile.h>
#include <ebur128.h>
#include <vector>
#include <iostream>
#include <sstream>
#include <iomanip>
#include <cmath>
#include <windows.h>
#include <deque>
#include <string>
#include "Button.h"
#include "drawText.h"
#include "Slider.h"

std::deque<std::string> logLines;
const int MAX_LOG_LINES = 20;
bool displayDebugWindow = false;

int windowWidth = 1024;
int windowHeight = 600;

float viewStart = 0.0f;  // seconds
float viewWidth = 10.0f; // visible duration in seconds

// =========================
// AUDIO DATA
// =========================
std::string filepath;
std::vector<float> samples;
int channels;
int sampleRate = 0;
SF_INFO info;
float volume = 1.0;
float gain = 0.21f;
bool hasAudio = false;
float totalTime = 0;

// Playback
float playPos = 0.0f;
int playIndex = 0;
bool playing = false;
int sampleWindowOffsetX = 50;
int sampleWindowWidth = 950;
int sampleWindowHeight = 200;

// LUFS data
std::vector<float> lufs;
std::vector<float> lufsMomentary;
std::vector<float> times;

// use SAME mapping as LUFS graph
float minLUFS = -60.0f;
float maxLUFS = 0.0f;
int lufsToPxScale = 4;
int lufsDelta = 60;
// Lufs: -60 to 0
// Delta Lufs: 60
// Scale: 4
// Graph height: Delta Lufs / Scale = 60 * 4 = 240px
int lufsGraphStartY = 500;

// FPS
float fps = 0.0f;
Uint32 lastFPSTime = 0;
int frameCount = 0;
const float targetFrameTime = 1.0f / 60.0f; // ~0.01667 sec

void logMessage(const std::string &msg)
{
    logLines.push_back(msg);

    if (logLines.size() > MAX_LOG_LINES)
        logLines.pop_front();
}

// =========================
// LOAD AUDIO
// =========================
bool loadFile(const char *path)
{
    if (!path)
    {
        sampleRate = 44100;
        channels = 2;
        hasAudio = false;
        return false;
    }

    SNDFILE *file = sf_open(path, SFM_READ, &info);

    if (!file)
    {
        sampleRate = 44100;
        channels = 2;
        hasAudio = false;

        return false;
    }

    sampleRate = info.samplerate;

    logMessage("SAMPLE RATE: " + std::to_string(sampleRate));

    std::vector<float> tmp(info.frames * info.channels);
    sf_readf_float(file, tmp.data(), info.frames);
    sf_close(file);

    samples.resize(info.frames);
    samples = tmp; // keep interleaved multichannel data
    channels = info.channels;
    hasAudio = true;

    return true;
}

// =========================
// AUDIO CALLBACK (SDL)
// =========================

void audioCallback(void *userdata, Uint8 *stream, int len)
{
    float *out = (float *)stream;
    int count = len / sizeof(float);

    if (!hasAudio || samples.empty())
    {
        for (int i = 0; i < count; i++)
            out[i] = 0;
        return;
    }

    for (int i = 0; i < count; i++)
    {
        if (playing && playIndex < samples.size())
            out[i] = samples[playIndex++] * gain;
        else
            out[i] = 0;
    }
}

void initAudio()
{
    // Audio setup
    SDL_AudioSpec spec{};
    spec.freq = sampleRate;
    spec.format = AUDIO_F32SYS;
    spec.channels = channels;
    spec.samples = 1024;
    spec.callback = audioCallback;

    SDL_OpenAudio(&spec, NULL);
    SDL_PauseAudio(0);
}

// =========================
// LUFS ANALYSIS
// =========================
void computeLUFS()
{
    int window = sampleRate * 3;
    int hop = sampleRate / 2;

    ebur128_state *st = ebur128_init(
        channels,
        sampleRate,
        EBUR128_MODE_S | EBUR128_MODE_I | EBUR128_MODE_M);

    int totalFrames = info.frames;
    int position = 0;

    while (position < totalFrames)
    {
        int framesToProcess = std::min(hop, totalFrames - position);

        // feed chunk
        ebur128_add_frames_float(
            st,
            samples.data() + position * channels,
            framesToProcess);

        position += framesToProcess;

        // only start collecting after enough data
        if (position >= window)
        {
            double val;
            ebur128_loudness_shortterm(st, &val);

            lufs.push_back(val);
            times.push_back((float)position / sampleRate);

            double momentaryVal;
            ebur128_loudness_momentary(st, &momentaryVal);
            lufsMomentary.push_back(momentaryVal);
        }
    }

    totalTime = times.empty() ? 1.0f : times.back();

    ebur128_destroy(&st);
}

void loadNewFile()
{
    playing = false;
    playPos = 0.0f;
    playIndex = 0;
    char newFilePath[MAX_PATH] = "";

    OPENFILENAMEA ofn;
    ZeroMemory(&ofn, sizeof(ofn));

    ofn.lStructSize = sizeof(ofn);
    ofn.lpstrFilter = "Audio Files\0*.wav;*.mp3\0All Files\0*.*\0";
    ofn.lpstrFile = newFilePath;
    ofn.nMaxFile = MAX_PATH;
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;

    if (GetOpenFileNameA(&ofn))
    {
        lufs.clear();
        times.clear();
        samples.clear();

        if (!loadFile(newFilePath))
        {
            logMessage(std::string("Failed to load file ") + newFilePath);
            return;
        }

        filepath = newFilePath;

        initAudio();
        computeLUFS();

        viewStart = 0.0f;
        viewWidth = totalTime;

        logMessage(std::string("Loaded: ") + filepath);
    }
}

void reloadFile()
{
    playing = false;
    playPos = 0.0f;
    playIndex = 0;

    lufs.clear();
    times.clear();
    samples.clear();

    if (!loadFile(filepath.c_str()))
    {
        logMessage(std::string("Failed to load file") + filepath);
        return;
    }

    initAudio();
    computeLUFS();

    viewStart = 0.0f;
    viewWidth = totalTime;

    logMessage(std::string("Loaded: ") + filepath);
}

float dbToGain(float db)
{
    return powf(10.0f, db / 20.0f);
}

float getCurrentLUFS(float playPos)
{
    if (lufs.empty())
        return minLUFS;

    for (int i = 1; i < times.size(); i++)
    {
        if (times[i] >= playPos)
            return lufs[i];
    }

    return lufs.back();
}

float getCurrentLUFSMomentary(float playPos)
{
    if (lufsMomentary.empty())
        return minLUFS;

    for (int i = 1; i < times.size(); i++)
    {
        if (times[i] >= playPos)
            return lufsMomentary[i];
    }

    return lufsMomentary.back();
}

float computeRMS(int frameIndex, int windowFrames)
{
    if (samples.empty())
        return 0.0f;

    int start = frameIndex * channels;
    int end = start + windowFrames * channels;

    if (end > samples.size())
        end = samples.size();

    float sum = 0.0f;
    int count = 0;

    for (int i = start; i < end; i += channels)
    {
        float v = 0.0f;

        // combine channels (energy-based)
        for (int c = 0; c < channels; c++)
        {
            float s = samples[i + c];
            v += s * s;
        }

        v /= channels;

        sum += v;
        count++;
    }

    if (count == 0)
        return 0.0f;

    float rms = sqrtf(sum / count);

    return rms;
}

float rmsToDb(float rms)
{
    if (rms <= 0.000001f)
        return minLUFS;

    return 20.0f * log10f(rms);
}

float getCurrentRMS(float playPos)
{
    int frameIndex = (int)(playPos * sampleRate);
    int window = sampleRate * 0.05f; // 50 ms window (fast response)
    float rms = computeRMS(frameIndex, window);

    return rmsToDb(rms);
}

float getTimeAtCursor(int mouseX)
{
    // normalize inside visible window
    float t = (float)(mouseX - sampleWindowOffsetX) / sampleWindowWidth;

    // allow outside bounds safely
    if (t < 0.0f)
        t = 0.0f;
    if (t > 1.0f)
        t = 1.0f;

    // convert to world time using zoom/pan
    return viewStart + t * viewWidth;
}

std::string formatTime(float seconds)
{
    int total = (int)seconds;
    int minutes = total / 60;
    int secs = total % 60;

    std::stringstream ss;
    if (secs < 10)
        ss << minutes << ":0" << secs;
    else
        ss << minutes << ":" << secs;

    return ss.str();
}

std::string formatTimeMs(float seconds)
{
    int totalSeconds = (int)seconds;
    int minutes = totalSeconds / 60;
    int secs = totalSeconds % 60;

    int millis = (int)((seconds - totalSeconds) * 1000);

    std::stringstream ss;

    // mm:ss.mmm
    ss << minutes << ":";

    if (secs < 10)
        ss << "0";
    ss << secs << ".";

    if (millis < 100)
        ss << "0";
    if (millis < 10)
        ss << "0";
    ss << millis;

    return ss.str();
}

float rmsPeakDisplay = minLUFS;

void renderRmsBar(SDL_Renderer *renderer, float rmsDB)
{
    if (!playing)
        rmsDB = minLUFS;
    if (rmsDB > maxLUFS)
        rmsDB = maxLUFS;
    if (rmsDB < minLUFS)
        rmsDB = minLUFS;

    int barX = 5;
    int barWidth = 8;
    int baseY = 500;

    int totalHeight = lufsDelta * lufsToPxScale;

    // =========================
    // PEAK HOLD STATE
    // =========================
    static float rmsPeakDisplay = minLUFS;
    static int holdCounter = 0;

    const int holdFrames = 5;
    const float releaseSpeed = 0.05f;

    // update peak
    if (rmsDB > rmsPeakDisplay)
    {
        rmsPeakDisplay = rmsDB;
        holdCounter = holdFrames;
    }
    else
    {
        if (holdCounter > 0)
        {
            holdCounter--;
        }
        else
        {
            // smooth decay
            rmsPeakDisplay += (rmsDB - rmsPeakDisplay) * releaseSpeed;
        }
    }

    // =========================
    // BAR RENDER
    // =========================
    int currentHeight = (rmsDB + lufsDelta) * lufsToPxScale;

    for (int i = 0; i < totalHeight; i++)
    {
        float db = minLUFS + (float)i / totalHeight * (maxLUFS - minLUFS);
        float t = (db - minLUFS) / (maxLUFS - minLUFS);

        Uint8 r, g, b;

        if (t < 0.5f)
        {
            float k = t / 0.5f;
            r = (Uint8)(k * 255);
            g = 255;
            b = 0;
        }
        else
        {
            float k = (t - 0.5f) / 0.5f;
            r = 255;
            g = (Uint8)((1.0f - k) * 255);
            b = 0;
        }

        int y = baseY - i;

        if (i <= currentHeight)
            SDL_SetRenderDrawColor(renderer, r, g, b, 255);
        else
            SDL_SetRenderDrawColor(renderer, 40, 40, 40, 255);

        SDL_RenderDrawLine(renderer, barX, y, barX + barWidth, y);
    }

    // =========================
    // PEAK LINE DRAW
    // =========================
    int peakY = baseY - (rmsPeakDisplay + lufsDelta) * lufsToPxScale;

    SDL_SetRenderDrawColor(renderer, 255, 255, 255, 255);
    SDL_RenderDrawLine(renderer, barX - 1, peakY, barX + barWidth + 1, peakY);
}

int timeToX(float t)
{
    return (t - viewStart) / viewWidth * sampleWindowWidth + sampleWindowOffsetX;
}

void renderTimeline(SDL_Renderer *renderer, TTF_Font *font)
{
    int timelineY = 210;

    // baseline
    SDL_SetRenderDrawColor(renderer, 180, 180, 180, 255);
    SDL_RenderDrawLine(renderer,
                       sampleWindowOffsetX,
                       timelineY,
                       sampleWindowOffsetX + sampleWindowWidth,
                       timelineY);

    // adaptive step (important for zoom)
    float pixelsPerSecond = sampleWindowWidth / viewWidth;
    // target spacing ~ 80–120 pixels between labels
    float targetPx = 100.0f;
    // desired seconds per tick
    float rawStep = targetPx / pixelsPerSecond;
    // choose nice rounded steps
    float steps[] = {
        1, 2, 5,
        10, 15, 30,
        60, 120, 300,
        600, 1200, 1800};

    float step = steps[sizeof(steps) / sizeof(float) - 1];

    for (float s : steps)
    {
        if (s >= rawStep)
        {
            step = s;
            break;
        }
    }

    int startSec = (int)(viewStart / step) * step;
    for (float sec = startSec; sec <= viewStart + viewWidth; sec += step)
    {
        if (sec < 0)
            continue;
        if (sec > totalTime)
            break;

        int x = timeToX((float)sec);

        if (x < sampleWindowOffsetX || x > sampleWindowOffsetX + sampleWindowWidth)
            continue;

        // major tick (every N secs)
        SDL_SetRenderDrawColor(renderer, 200, 200, 200, 255);
        SDL_RenderDrawLine(renderer, x, timelineY, x, timelineY + 10);

        std::string label;
        if (step >= 60)
        {
            // minutes only
            int minutes = (int)(sec / 60);
            label = std::to_string(minutes) + "m";
        }
        else
        {
            label = formatTime(sec);
        }

        drawText(renderer, font, label, x - 15, timelineY + 12);
    }

    // minor ticks (optional, denser)
    float minorStep = step / 5.0f;
    if (minorStep < 0.2f)
        minorStep = step / 2.0f;

    for (float t = viewStart; t <= viewStart + viewWidth; t += minorStep)
    {
        int x = timeToX(t);

        if (x < sampleWindowOffsetX || x > sampleWindowOffsetX + sampleWindowWidth)
            continue;

        SDL_SetRenderDrawColor(renderer, 120, 120, 120, 120);
        SDL_RenderDrawLine(renderer, x, timelineY, x, timelineY + 5);
    }
}

// =========================
// MAIN
// =========================
int main(int argc, char **argv)
{
    SetDllDirectory("dll");

    if (argc > 1)
    {
        filepath = argv[1];
    }
    else
    {
        std::cout << "Usage: app.exe <audio_file>\n";
        filepath = "";
    }

    if (filepath != "")
    {
        if (!loadFile(filepath.c_str()))
        {
            std::cout << "Failed to load audio: " << filepath << "\n";
            // return 0;
        }

        std::cout << "Loaded: " << filepath << "\n";

        initAudio();
        computeLUFS();

        viewStart = 0.0f;
        viewWidth = totalTime;
    }

    TTF_Init();
    TTF_Font *font_14 = TTF_OpenFont("C:/Windows/Fonts/consola.ttf", 14);
    TTF_Font *font_12 = TTF_OpenFont("C:/Windows/Fonts/consola.ttf", 12);
    TTF_Font *font_10 = TTF_OpenFont("C:/Windows/Fonts/consola.ttf", 10);

    Button loadBtn(900, 560, 90, 24, 14, "LOAD FILE");
    loadBtn.onClick = loadNewFile;

    Button reloadBtn(900, 530, 80, 24, 14, "RELOAD");
    reloadBtn.onClick = reloadFile;

    Slider gainSlider(200, 580, 130, 4, gain);
    gainSlider.onChange = [](float v)
    {
        gain = v;
    };

    SDL_Init(SDL_INIT_AUDIO | SDL_INIT_VIDEO);

    // Window
    SDL_Window *window = SDL_CreateWindow(
        "LUFS Meter",
        SDL_WINDOWPOS_CENTERED,
        SDL_WINDOWPOS_CENTERED,
        windowWidth, windowHeight,
        SDL_WINDOW_SHOWN);

    SDL_Renderer *renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED);
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);

    bool running = true;
    Uint32 lastTime = SDL_GetTicks();

    while (running)
    {
        Uint32 frameStart = SDL_GetTicks();
        Uint32 now = frameStart;
        float dt = (now - lastTime) / 1000.0f;
        lastTime = now;
        frameCount++;

        SDL_Event e;
        while (SDL_PollEvent(&e))
        {
            if (e.type == SDL_QUIT)
                running = false;

            if (e.type == SDL_MOUSEBUTTONDOWN)
            {
                if (e.button.button == SDL_BUTTON_LEFT && e.button.y < 500)
                {
                    float t = (float)(e.button.x - sampleWindowOffsetX) / sampleWindowWidth;

                    if (t < 0.0f)
                        t = 0.0f;
                    if (t > 1.0f)
                        t = 1.0f;

                    // ✅ correct mapping using view
                    playPos = viewStart + t * viewWidth;
                    playIndex = playPos * sampleRate * channels;
                }
            }

            if (e.type == SDL_KEYDOWN)
            {
                if (e.key.keysym.sym == SDLK_SPACE)
                    playing = !playing;
                if (e.key.keysym.sym == SDLK_BACKQUOTE)
                    displayDebugWindow = !displayDebugWindow;
            }

            // ZOOM
            if (e.type == SDL_MOUSEWHEEL)
            {
                const Uint8 *keystate = SDL_GetKeyboardState(NULL);

                int mouseX, mouseY;
                SDL_GetMouseState(&mouseX, &mouseY);

                float mouseT = (float)(mouseX - sampleWindowOffsetX) / sampleWindowWidth;
                float worldTime = viewStart + mouseT * viewWidth;

                // zoom
                if (keystate[SDL_SCANCODE_LCTRL] || keystate[SDL_SCANCODE_RCTRL])
                {
                    float zoomFactor = (e.wheel.y > 0) ? 0.8f : 1.25f;

                    float newWidth = viewWidth * zoomFactor;

                    // clamp zoom limits
                    float minWidth = 1.0f;      // zoom in limit
                    float maxWidth = totalTime; // zoom out limit

                    if (newWidth < minWidth)
                        newWidth = minWidth;
                    if (newWidth > maxWidth)
                        newWidth = maxWidth;

                    // keep mouse position fixed in world space
                    viewStart = worldTime - mouseT * newWidth;
                    viewWidth = newWidth;
                }

                // PAN
                if (keystate[SDL_SCANCODE_LSHIFT] || keystate[SDL_SCANCODE_RSHIFT])
                {
                    float panSpeed = viewWidth * 0.1f;

                    viewStart -= e.wheel.y * panSpeed;
                }

                if (viewStart < 0.0f)
                    viewStart = 0.0f;
                if (viewStart + viewWidth > totalTime)
                    viewStart = totalTime;
                if (viewStart < 0.0f)
                    viewStart = 0.0f;
            }

            loadBtn.handleEvent(e);
            reloadBtn.handleEvent(e);
            gainSlider.handleEvent(e);
        }

        if (now - lastFPSTime >= 1000) // every 1 second
        {
            fps = frameCount / ((now - lastFPSTime) / 1000.0f);
            frameCount = 0;
            lastFPSTime = now;
        }

        if (playing)
            playPos += dt;

        // ===== RENDER
        SDL_SetRenderDrawColor(renderer, 20, 20, 20, 255);
        SDL_RenderClear(renderer);

        // --- WAVEFORM ---

        SDL_SetRenderDrawColor(renderer, 0, 150, 255, 255);

        int N = samples.size();

        // SAMPLE
        float scale = sampleWindowHeight / 2.2f;
        int centerY = sampleWindowHeight / 2;
        int startFrame = viewStart * sampleRate;
        int endFrame = (viewStart + viewWidth) * sampleRate;
        int visibleFrames = endFrame - startFrame;
        int samplesPerPixel = (visibleFrames * channels) / sampleWindowWidth;

        SDL_SetRenderDrawColor(renderer, 0, 150, 0, 100);
        for (float v = -1.0f; v <= 1.0f; v += 0.25f)
        {
            int y = centerY + (int)(v * scale);
            int vInt = v * 100;
            bool major = (vInt % 50 == 0);

            if (major)
                SDL_SetRenderDrawColor(renderer, 140, 140, 140, 100);
            else
                SDL_SetRenderDrawColor(renderer, 80, 80, 80, 100);

            SDL_RenderDrawLine(renderer, sampleWindowOffsetX, y, sampleWindowOffsetX + sampleWindowWidth, y);

            if (major)
            {
                std::stringstream ss;
                ss << std::fixed << std::setprecision(1) << v;
                drawText(
                    renderer,
                    font_12,
                    ss.str(),
                    20, y - 5);
            }
        }

        if (hasAudio)
        {
            SDL_SetRenderDrawColor(renderer, 0, 150, 255, 255);
            for (int x = 0; x < sampleWindowWidth; x++)
            {
                int start = startFrame * channels + x * samplesPerPixel;
                int end = start + samplesPerPixel;

                if (end > samples.size())
                    end = samples.size();

                float minVal = 1.0f;
                float maxVal = -1.0f;

                for (int i = start; i < end; i++)
                {
                    float v = samples[i];
                    if (v < minVal)
                        minVal = v;
                    if (v > maxVal)
                        maxVal = v;
                }

                int y1 = centerY + (int)(minVal * scale);
                int y2 = centerY + (int)(maxVal * scale);

                SDL_RenderDrawLine(renderer, x + sampleWindowOffsetX, y1, x + sampleWindowOffsetX, y2);
            }
        }

        // --- LUFS ---

        // LUFS GRID
        for (int l = minLUFS; l <= maxLUFS; l += 5)
        {
            int y = lufsGraphStartY - (l + lufsDelta) * lufsToPxScale;

            // Major lines every 10 LUFS
            if (l % 10 == 0)
                SDL_SetRenderDrawColor(renderer, 140, 140, 140, 100);
            else
                SDL_SetRenderDrawColor(renderer, 80, 80, 80, 100);

            SDL_RenderDrawLine(renderer, sampleWindowOffsetX, y, sampleWindowWidth + sampleWindowOffsetX, y);

            if (l % 10 == 0)
                drawText(renderer, font_12, std::to_string(l), 20, y - 6);
        }

        // LUFS LINE
        if (hasAudio)
        {
            for (int i = 1; i < lufs.size(); i++)
            {
                float t1 = times[i - 1];
                float t2 = times[i];

                // ✅ skip completely outside segments
                if (t2 < viewStart)
                    continue;
                if (t1 > viewStart + viewWidth)
                    break;

                float val1 = lufs[i - 1];
                float val2 = lufs[i];

                if (val1 > maxLUFS)
                    val1 = maxLUFS;
                if (val1 < minLUFS)
                    val1 = minLUFS;
                if (val2 > maxLUFS)
                    val2 = maxLUFS;
                if (val2 < minLUFS)
                    val2 = minLUFS;

                int x1 = (t1 - viewStart) / viewWidth * sampleWindowWidth + sampleWindowOffsetX;
                int y1 = lufsGraphStartY - (val1 + lufsDelta) * lufsToPxScale;

                int x2 = (t2 - viewStart) / viewWidth * sampleWindowWidth + sampleWindowOffsetX;
                int y2 = lufsGraphStartY - (val2 + lufsDelta) * lufsToPxScale;

                // ✅ optional: clamp to screen (extra safety)
                if (x1 < sampleWindowOffsetX)
                    x1 = sampleWindowOffsetX;
                if (x1 > sampleWindowOffsetX + sampleWindowWidth)
                    x1 = sampleWindowOffsetX + sampleWindowWidth;

                if (x2 < sampleWindowOffsetX)
                    x2 = sampleWindowOffsetX;
                if (x2 > sampleWindowOffsetX + sampleWindowWidth)
                    x2 = sampleWindowOffsetX + sampleWindowWidth;

                SDL_SetRenderDrawColor(renderer, 255, 80, 80, 255);
                SDL_RenderDrawLine(renderer, x1, y1, x2, y2);

                SDL_SetRenderDrawColor(renderer, 255, 80, 80, 180);
                SDL_RenderDrawLine(renderer, x1 + 1, y1, x2, y2 + 1);
                SDL_RenderDrawLine(renderer, x1, y1 + 1, x2, y2 + 1);
            }
        }

        // INSTANT LUFS text
        float currentLufs = getCurrentLUFS(playPos);

        drawText(
            renderer,
            font_14,
            "LUFS: " + std::to_string((int)roundf(currentLufs)),
            20, 535);

        // RMS BAR
        float rms = getCurrentRMS(playPos);
        renderRmsBar(renderer, rms);

        drawText(
            renderer,
            font_14,
            "RMS: " + std::to_string((int)roundf(rms)),
            20, 555);

        drawText(
            renderer,
            font_12,
            "RMS    LUFS",
            5, 510);

        if (playing)
        {
            logMessage("LUFS: " + std::to_string(currentLufs) + " RMS: " + std::to_string(rms));
        }

        // --- PLAYHEAD ---
        int px = (playPos - viewStart) / viewWidth * sampleWindowWidth + sampleWindowOffsetX;
        SDL_SetRenderDrawColor(renderer, 255, 255, 0, 180);
        SDL_RenderDrawLine(renderer, px, 0, px, 210);
        SDL_RenderDrawLine(renderer, px, 240, px, 500);

        // TIMELINE
        renderTimeline(renderer, font_12);

        // TIME
        float timeAtCursor = getTimeAtCursor(px);
        drawText(
            renderer,
            font_14,
            "Time: " + formatTimeMs(timeAtCursor),
            20, 575);

        // GAIN
        drawText(
            renderer,
            font_14,
            "Output gain: " + std::to_string(int(gain * 100)) + "%",
            200, 555);

        // LOG
        if (displayDebugWindow)
        {
            SDL_SetRenderDrawColor(renderer, 0, 0, 0, 120);
            SDL_Rect bg = {5, 5, 400, 300};
            SDL_RenderFillRect(renderer, &bg);

            int yOffset = 10;
            for (const auto &line : logLines)
            {
                drawText(renderer, font_12, line, 10, yOffset);
                yOffset += 14;
            }

            std::stringstream ss;
            ss << "FPS: " << (int)fps;
            drawText(renderer, font_12, ss.str(), 10, 290);
        }

        loadBtn.render(renderer);
        reloadBtn.render(renderer);
        gainSlider.render(renderer);

        SDL_RenderPresent(renderer);

        // ===== FPS CAP =====
        Uint32 frameEnd = SDL_GetTicks();
        float frameTime = (frameEnd - frameStart) / 1000.0f;
        if (frameTime < targetFrameTime)
        {
            Uint32 delay = (Uint32)((targetFrameTime - frameTime) * 1000.0f);
            SDL_Delay(delay);
        }
    }

    SDL_CloseAudio();
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();

    return 0;
}