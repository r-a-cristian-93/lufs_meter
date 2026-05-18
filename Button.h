#pragma once
#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>
#include <string>
#include <functional>
#include "drawText.h"

class Button
{
public:
    SDL_Rect rect;
    std::string label;
    bool hovered = false;
    bool pressed = false;
    TTF_Font* font;

    std::function<void()> onClick;

    Button(int x, int y, int w, int h, int fontSize, const std::string& text)
    {
        rect = { x, y, w, h };
        label = text;
        font = TTF_OpenFont("C:/Windows/Fonts/consola.ttf", fontSize);
    }

void handleEvent(const SDL_Event& e)
{
    if (e.type == SDL_MOUSEMOTION)
    {
        int mx = e.motion.x;
        int my = e.motion.y;

        hovered = (mx >= rect.x && mx <= rect.x + rect.w &&
                   my >= rect.y && my <= rect.y + rect.h);
    }

    if (e.type == SDL_MOUSEBUTTONDOWN)
    {
        int mx = e.button.x;
        int my = e.button.y;

        if (mx >= rect.x && mx <= rect.x + rect.w &&
            my >= rect.y && my <= rect.y + rect.h)
        {
            pressed = true;
        }
    }

    if (e.type == SDL_MOUSEBUTTONUP)
    {
        int mx = e.button.x;
        int my = e.button.y;

        bool inside =
            (mx >= rect.x && mx <= rect.x + rect.w &&
             my >= rect.y && my <= rect.y + rect.h);

        if (pressed && inside && onClick)
            onClick();

        pressed = false;
    }
}

    void render(SDL_Renderer* renderer)
    {
        // Background
        if (pressed)
            SDL_SetRenderDrawColor(renderer, 40, 40, 40, 255);
        else if (hovered)
            SDL_SetRenderDrawColor(renderer, 80, 80, 80, 255);
        else
            SDL_SetRenderDrawColor(renderer, 60, 60, 60, 255);

        SDL_RenderFillRect(renderer, &rect);

        // Border
        SDL_SetRenderDrawColor(renderer, 180, 180, 180, 255);
        SDL_RenderDrawRect(renderer, &rect);

        // Centered text
        int textX = rect.x + 10;
        int textY = rect.y + (rect.h / 2 - 6);

        drawText(renderer, font, label, textX, textY);
    }
};