#pragma once
#include <SDL2/SDL.h>
#include <functional>

class Slider
{
public:
    SDL_Rect track;
    float value = 0.0f;   // 0.0 → 1.0
    bool dragging = false;
    bool hovered = false;

    std::function<void(float)> onChange;

    Slider(int x, int y, int w, int h, float initialValue)
    {
        track = { x, y, w, h };
        value = initialValue;
    }

    void handleEvent(const SDL_Event& e)
    {
        if (e.type == SDL_MOUSEBUTTONDOWN && e.button.button == SDL_BUTTON_LEFT)
        {
            if (isInside(e.button.x, e.button.y))
                dragging = true;
        }

        if (e.type == SDL_MOUSEBUTTONUP && e.button.button == SDL_BUTTON_LEFT)
        {
            dragging = false;
        }

        if (e.type == SDL_MOUSEMOTION && dragging)
        {
            float t = (float)(e.motion.x - track.x) / track.w;

            if (t < 0.0f) t = 0.0f;
            if (t > 1.0f) t = 1.0f;

            value = t;

            if (onChange)
                onChange(value);
        }

        if (e.type == SDL_MOUSEMOTION)
        {
            hovered = isInside(e.motion.x, e.motion.y);
        }

        if (e.type == SDL_MOUSEWHEEL)
        {
            if (hovered)
            {
                float step = 0.02f; // adjust sensitivity

                value += e.wheel.y * step;

                if (value < 0.0f) value = 0.0f;
                if (value > 1.0f) value = 1.0f;

                if (onChange)
                    onChange(value);
            }
        }

    }

    void render(SDL_Renderer* renderer)
    {
        // track
        if (hovered)
            SDL_SetRenderDrawColor(renderer, 100, 100, 100, 255);
        else
            SDL_SetRenderDrawColor(renderer, 80, 80, 80, 255);

        SDL_RenderFillRect(renderer, &track);

        // fill
        SDL_Rect fill = track;
        fill.w = (int)(value * track.w);

        if (hovered) 
            SDL_SetRenderDrawColor(renderer, 0, 200, 0, 200);
        else 
            SDL_SetRenderDrawColor(renderer, 0, 200, 0, 100);

        SDL_RenderFillRect(renderer, &fill);

        // knob
        int knobX = track.x + (int)(value * track.w);

        SDL_SetRenderDrawColor(renderer, 255, 255, 255, 255);
        SDL_Rect knob = { knobX - 4, track.y - 2, 8, track.h + 4 };
        SDL_RenderFillRect(renderer, &knob);
    }

private:
    bool isInside(int x, int y)
    {
        return 
        x >= track.x &&
        x <= track.x + track.w &&
        y >= track.y - 30 &&
        y <= track.y + track.h + 10;
    }
};