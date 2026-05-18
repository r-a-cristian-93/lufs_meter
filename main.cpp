#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>
#include <sndfile.h>
#include <ebur128.h>
#include <vector>
#include <iostream>
#include <cmath>
#include <windows.h>
#include <deque>
#include <string>
#include "Button.h"
#include "drawText.h"

std::deque<std::string> logLines;
const int MAX_LOG_LINES = 20;
bool displayDebugWindow = false;


// =========================
// AUDIO DATA
// =========================
const char* filepath = nullptr;
std::vector<float> samples;
int channels;
int sampleRate = 0;
SF_INFO info;
float volume = 1.0;

// Playback
int playIndex = 0;
bool playing = false;

// LUFS data
std::vector<float> lufs;
std::vector<float> lufsMomentary;
std::vector<float> times;


void logMessage(const std::string& msg)
{
    logLines.push_back(msg);

    if (logLines.size() > MAX_LOG_LINES)
        logLines.pop_front();
}


// HISTOGRAM

// --- histogram setup ---
const int bins = 300;

std::vector<int> histogram(bins, 0);
int histogramMaxCount = 0;

void fillHistogram()
{
    // reset
    std::fill(histogram.begin(), histogram.end(), 0);

    histogramMaxCount = 1;

    // use SAME mapping as your LUFS graph
    float minLUFS = -60.0f;
    float maxLUFS = 0.0f;

    for (float v : lufs) {

        if (!std::isfinite(v)) continue;

        // clamp to visible range
        if (v < minLUFS) v = minLUFS;
        if (v > maxLUFS) v = maxLUFS;

        // map LUFS -> bin index
        float norm = (v - minLUFS) / (maxLUFS - minLUFS);
        int idx = (int)(norm * (bins - 1));

        if (idx >= 0 && idx < bins) {
            histogram[idx]++;
            if (histogram[idx] > histogramMaxCount)
                histogramMaxCount = histogram[idx];
        }
    }
}


// =========================
// LOAD AUDIO
// =========================
bool loadFile(const char* path)
{
    SNDFILE* file = sf_open(path, SFM_READ, &info);

    if (!file) return false;

    sampleRate = info.samplerate;

    std::vector<float> tmp(info.frames * info.channels);
    sf_readf_float(file, tmp.data(), info.frames);
    sf_close(file);

    samples.resize(info.frames);
    
    samples = tmp;  // keep interleaved multichannel data
    channels = info.channels;


    return true;
}

// =========================
// LUFS ANALYSIS
// =========================
void computeLUFS()
{
    int window = sampleRate * 3;
    int hop = sampleRate / 2;

    ebur128_state* st = ebur128_init(
        channels,
        sampleRate,
        EBUR128_MODE_S | EBUR128_MODE_I | EBUR128_MODE_M
    );

    int totalFrames = info.frames;
    int position = 0;

    while (position < totalFrames)
    {
        int framesToProcess = std::min(hop, totalFrames - position);

        // feed chunk
        ebur128_add_frames_float(
            st,
            samples.data() + position * channels,
            framesToProcess
        );

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

    ebur128_destroy(&st);
}


void loadNewFile()
{
    char filename[MAX_PATH] = "";

    OPENFILENAMEA ofn;
    ZeroMemory(&ofn, sizeof(ofn));

    ofn.lStructSize = sizeof(ofn);
    ofn.lpstrFilter = "Audio Files\0*.wav;*.mp3\0All Files\0*.*\0";
    ofn.lpstrFile = filename;
    ofn.nMaxFile = MAX_PATH;
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;

    if (GetOpenFileNameA(&ofn))
    {
        lufs.clear();
        times.clear();
        samples.clear();

        if (!loadFile(filename))
        {
            logMessage("Failed to load file");
            return;
        }

        computeLUFS();
        fillHistogram();

        playIndex = 0;
        logMessage(std::string("Loaded: ") + filename);
    }
}

void reloadFile()
{
    lufs.clear();
    times.clear();
    samples.clear();

    if (!loadFile(filepath))
    {
        logMessage("Failed to load file");
        return;
    }

    computeLUFS();
    fillHistogram();

    playIndex = 0;
    logMessage(std::string("Loaded: ") + filepath);
}

float dbToGain(float db)
{
    return powf(10.0f, db / 20.0f);
}

float volumeDB = -20.0f; // start at -12 dB
float gain = dbToGain(volumeDB);

void increaseVolume()
{
    volumeDB += 1.0f; // +1 dB
    if (volumeDB > 0.0f) volumeDB = 0.0f;

    gain = dbToGain(volumeDB);
}

void decreaseVolume()
{
    volumeDB -= 1.0f; // -1 dB
    if (volumeDB < -60.0f) volumeDB = -60.0f;

    gain = dbToGain(volumeDB);
}


// =========================
// AUDIO CALLBACK (SDL)
// =========================
void audioCallback(void* userdata, Uint8* stream, int len)
{
    float* out = (float*)stream;
    int count = len / sizeof(float);

    for (int i = 0; i < count; i++) {
        if (playing && playIndex < samples.size()) {
            out[i] = samples[playIndex++] * gain;
        } else {
            out[i] = 0;
        }
    }
}



float getCurrentLUFS(float playPos)
{
    if (lufs.empty()) return -60.0f;

    for (int i = 1; i < times.size(); i++)
    {
        if (times[i] >= playPos)
            return lufs[i];
    }

    return lufs.back();
}

float getCurrentLUFSMomentary(float playPos)
{
    if (lufsMomentary.empty()) return -60.0f;

    for (int i = 1; i < times.size(); i++)
    {
        if (times[i] >= playPos)
            return lufsMomentary[i];
    }

    return lufsMomentary.back();
}



float computeRMS(int frameIndex, int windowFrames)
{
    if (samples.empty()) return 0.0f;

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

    if (count == 0) return 0.0f;

    float rms = sqrtf(sum / count);

    return rms;
}

float rmsToDb(float rms)
{
    if (rms <= 0.000001f)
        return -60.0f;

    return 20.0f * log10f(rms);
}

float getCurrentRMS(float playPos)
{
    int frameIndex = (int)(playPos * sampleRate);

    int window = sampleRate * 0.05f; // 50 ms window (fast response)

    float rms = computeRMS(frameIndex, window);

    return rmsToDb(rms);
}

void renderRmsBar(SDL_Renderer* renderer, float rmsDB)
{
    if (rmsDB > 0) rmsDB = 0;
    if (rmsDB < -60) rmsDB = -60;

    int barHeight = (rmsDB + 60) * 5;

    SDL_Rect fill = {
        0,
        500,
        8,
        -barHeight
    };

    SDL_SetRenderDrawColor(renderer, 0, 200, 255, 255);
    SDL_RenderFillRect(renderer, &fill);
}


// =========================
// MAIN
// =========================
int main(int argc, char** argv)
{
    SetDllDirectory("dll");

    if (argc > 1) {
        filepath = argv[1];
    } else {
        std::cout << "Usage: app.exe <audio_file>\n";
        filepath = "audio.wav";
    }

    if (!loadFile(filepath)) {
        std::cout << "Failed to load audio: " << filepath << "\n";
        return 0;
    }

    std::cout << "Loaded: " << filepath << "\n";

    TTF_Init();
    TTF_Font* font_14 = TTF_OpenFont("C:/Windows/Fonts/consola.ttf", 14);
    TTF_Font* font_12 = TTF_OpenFont("C:/Windows/Fonts/consola.ttf", 12);
    TTF_Font* font_10 = TTF_OpenFont("C:/Windows/Fonts/consola.ttf", 10);

    Button loadBtn(900, 560, 90, 24, 14, "LOAD FILE");
    loadBtn.onClick = loadNewFile;

    Button reloadBtn(900, 530, 80, 24, 14, "RELOAD");
    reloadBtn.onClick = reloadFile;

    computeLUFS();
    fillHistogram();

    SDL_Init(SDL_INIT_AUDIO | SDL_INIT_VIDEO);

    // Audio setup
    SDL_AudioSpec spec{};
    spec.freq = sampleRate;
    spec.format = AUDIO_F32SYS;
    spec.channels = channels;
    spec.samples = 1024;
    spec.callback = audioCallback;

    SDL_OpenAudio(&spec, NULL);
    SDL_PauseAudio(0);

    // Window
    SDL_Window* window = SDL_CreateWindow(
        "LUFS Meter",
        SDL_WINDOWPOS_CENTERED,
        SDL_WINDOWPOS_CENTERED,
        1000, 600,
        SDL_WINDOW_SHOWN
    );

    SDL_Renderer* renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED);
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);

    bool running = true;
    Uint32 lastTime = SDL_GetTicks();

    float playPos = 0.0f;

    while (running) {

        SDL_Event e;
        while (SDL_PollEvent(&e)) {

            if (e.type == SDL_QUIT)
                running = false;

            if (e.type == SDL_MOUSEBUTTONDOWN) {
                if (e.button.y < 500) {
                    float t = (float)e.button.x / 1000.0f;
                    playPos = t * times.back();
                    playIndex = playPos * sampleRate * channels;
                }
            }

            if (e.type == SDL_KEYDOWN) {
                if (e.key.keysym.sym == SDLK_SPACE)
                    playing = !playing;
                if (e.key.keysym.sym == SDLK_BACKQUOTE)
                    displayDebugWindow = !displayDebugWindow;
                if (e.key.keysym.sym == SDLK_UP)
                    increaseVolume();
                if (e.key.keysym.sym == SDLK_DOWN)
                    decreaseVolume();
            }

            loadBtn.handleEvent(e);
            reloadBtn.handleEvent(e);
        }

        // ===== TIME UPDATE
        Uint32 now = SDL_GetTicks();
        float dt = (now - lastTime) / 1000.0f;
        lastTime = now;

        if (playing)
            playPos += dt;

        // ===== RENDER
        SDL_SetRenderDrawColor(renderer, 20,20,20,255);
        SDL_RenderClear(renderer);

        // --- WAVEFORM ---
        SDL_SetRenderDrawColor(renderer, 0,150,255,255);

        int N = samples.size();

        // draw 
        int width = 1000;
        int height = 200;
        int centerY = height / 2;
        int samplesPerPixel = samples.size() / width;

        SDL_SetRenderDrawColor(renderer, 0, 150, 255, 255);

        for (int x = 0; x < width; x++) {

            int start = x * samplesPerPixel;
            int end = start + samplesPerPixel;

            if (end > samples.size()) end = samples.size();

            float minVal = 1.0f;
            float maxVal = -1.0f;

            for (int i = start; i < end; i++) {
                float v = samples[i];
                if (v < minVal) minVal = v;
                if (v > maxVal) maxVal = v;
            }

            float scale = height / 2.2f;
            int y1 = centerY + (int)(minVal * scale);
            int y2 = centerY + (int)(maxVal * scale);

            SDL_RenderDrawLine(renderer, x, y1, x, y2);
        }

        // --- LUFS ---

        // LUFS GRID
        for (int l = -60; l <= 0; l += 5)
        {
            int y = 500 - (l + 60) * 5;

            // Major lines every 10 LUFS
            if (l % 10 == 0)
                SDL_SetRenderDrawColor(renderer, 140, 140, 140, 255);
            else
                SDL_SetRenderDrawColor(renderer, 80, 80, 80, 255);

            SDL_RenderDrawLine(renderer, 35, y, 1000, y);

            if (l % 10 == 0)
                drawText(renderer, font_12, std::to_string(l), 10, y - 6);
        }

        // LUFS LINE
        SDL_SetRenderDrawColor(renderer, 255,80,80,255);
        for (int i = 1; i < lufs.size(); i++) {
                // lufsMed = (lufsMed + lufs[i]) / 2;
                int val1 = lufs[i-1];
                int val2 = lufs[i];

                if (val1 > 0 ) val1 = 0;
                if (val1 < -60) val1 = -60;
                if (val2 > 0 ) val2 = 0;
                if (val2 < -60) val2 = -60;

                int x1 = times[i-1]/times.back() * 1000;
                int y1 = 500 - (val1 + 60) * 5;

                int x2 = times[i]/times.back() * 1000;
                int y2 = 500 - (val2 + 60) * 5;

                SDL_RenderDrawLine(renderer, x1, y1, x2, y2);
        }

        // INSTANT LUFS text
        float currentLufs = getCurrentLUFS(playPos);

        drawText(
            renderer,
            font_14,
            "LUFS: " + std::to_string((int)roundf(currentLufs)),
            20, 535
        );

        // RMS BAR
        float rms = getCurrentRMS(playPos);
        renderRmsBar(renderer, rms);

        drawText(
            renderer,
            font_14,
            "RMS: " + std::to_string((int)roundf(rms)),
            20, 555
        );

        drawText(
            renderer,
            font_12,
            "[RMS]",
            10, 510
        );

        if (playing) {
            logMessage("LUFS: " + std::to_string(currentLufs) + " RMS: " + std::to_string(rms));
        }

        // --- PLAYHEAD ---
        int px = playPos / times.back() * 1000;
        SDL_SetRenderDrawColor(renderer, 255,255,0,255);
        SDL_RenderDrawLine(renderer, px, 0, px, 500);

        // VOLUME
        drawText(
            renderer,
            font_14,
            "Gain: " + std::to_string(int(gain*100)) + "%",
            20, 575
        );

        // LOG
        if (displayDebugWindow)
        {
            SDL_SetRenderDrawColor(renderer, 0, 0, 0, 120);
            SDL_Rect bg = {5, 5, 400, 300};
            SDL_RenderFillRect(renderer, &bg);

            int yOffset = 10;
            for (const auto& line : logLines)
            {
                drawText(renderer, font_12, line, 10, yOffset);
                yOffset += 14;
            }
        }

        loadBtn.render(renderer);
        reloadBtn.render(renderer);

        SDL_RenderPresent(renderer);
    }

    SDL_CloseAudio();
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();

    return 0;
}