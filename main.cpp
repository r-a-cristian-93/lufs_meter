#include <SDL2/SDL.h>
#include <sndfile.h>
#include <ebur128.h>
#include <vector>
#include <iostream>
#include <cmath>

// =========================
// AUDIO DATA
// =========================
std::vector<float> samples;
int sampleRate = 0;

// Playback
int playIndex = 0;
bool playing = false;

// LUFS data
std::vector<float> lufs;
std::vector<float> times;


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
    SF_INFO info;
    SNDFILE* file = sf_open(path, SFM_READ, &info);

    if (!file) return false;

    sampleRate = info.samplerate;

    std::vector<float> tmp(info.frames * info.channels);
    sf_readf_float(file, tmp.data(), info.frames);
    sf_close(file);

    samples.resize(info.frames);

    for (int i = 0; i < info.frames; i++) {
        float sum = 0;
        for (int c = 0; c < info.channels; c++)
            sum += tmp[i * info.channels + c];
        samples[i] = sum / info.channels;
    }

    return true;
}

// =========================
// LUFS ANALYSIS
// =========================
void computeLUFS()
{
    int window = sampleRate * 3;
    int hop = sampleRate / 2;

    for (int i = 0; i + window < samples.size(); i += hop) {

        ebur128_state* st = ebur128_init(1, sampleRate, EBUR128_MODE_S);

        ebur128_add_frames_float(st, samples.data() + i, window);

        double val;
        ebur128_loudness_shortterm(st, &val);

        lufs.push_back(val);
        times.push_back((float)i / sampleRate);

        ebur128_destroy(&st);
    }
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
            out[i] = samples[playIndex++];
        } else {
            out[i] = 0;
        }
    }
}

// =========================
// MAIN
// =========================
int main(int argc, char** argv)
{
    const char* filepath = nullptr;

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


    computeLUFS();
    fillHistogram();

    SDL_Init(SDL_INIT_AUDIO | SDL_INIT_VIDEO);

    // Audio setup
    SDL_AudioSpec spec{};
    spec.freq = sampleRate;
    spec.format = AUDIO_F32SYS;
    spec.channels = 1;
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
                int x = e.button.x;

                float t = (float)x / 1000.0f;
                playPos = t * times.back();

                playIndex = playPos * sampleRate;
            }

            if (e.type == SDL_KEYDOWN) {
                if (e.key.keysym.sym == SDLK_SPACE)
                    playing = !playing;
            }
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
        int height = 300;
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

        // HISTOGRAM
        float cutoff = 0.02f;

        for (int i = 1; i < bins-1; i++) {

            float val = (histogram[i-1] + histogram[i] + histogram[i+1]) / 3.0f;

            float norm = val / histogramMaxCount;

            if (norm < cutoff)
                continue;

            norm = (norm - cutoff) / (1.0f - cutoff);
            norm = sqrtf(norm);

            Uint8 alpha = norm * 255;

            float lufsVal = -60.0f + (float)i / (bins - 1) * 60.0f;
            int y = 500 - (lufsVal + 60) * 5;

            SDL_SetRenderDrawColor(renderer, 110, 60, 0, alpha);
            SDL_RenderDrawLine(renderer, 0, y, 1000, y);
        }



        // --- LUFS ---

        SDL_SetRenderDrawColor(renderer, 100, 100, 100, 255);
        SDL_RenderDrawLine(renderer, 0, 500, 1000, 500);

        SDL_SetRenderDrawColor(renderer, 255,80,80,255);

        // int lufsMed = 0;

        for (int i = 1; i < lufs.size(); i++) {
            // lufsMed = (lufsMed + lufs[i]) / 2;

            int x1 = times[i-1]/times.back() * 1000;
            int y1 = 500 - (lufs[i-1] + 60) * 5;

            int x2 = times[i]/times.back() * 1000;
            int y2 = 500 - (lufs[i] + 60) * 5;

            SDL_RenderDrawLine(renderer, x1, y1, x2, y2);
        }


        // std::cout << lufsMed << std::endl;
        // int lufsY = 500 - (lufsMed + 60) * 5;
        // SDL_RenderDrawLine(renderer, 0, lufsY, 1000, lufsY);

        // --- PLAYHEAD ---
        int px = playPos / times.back() * 1000;

        SDL_SetRenderDrawColor(renderer, 255,255,0,255);
        SDL_RenderDrawLine(renderer, px, 0, px, 600);

        SDL_RenderPresent(renderer);
    }

    SDL_CloseAudio();
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();

    return 0;
}