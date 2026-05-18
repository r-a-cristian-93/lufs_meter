#pragma once

void drawText(SDL_Renderer* renderer, TTF_Font* font, const std::string& text, int x, int y)
{
    SDL_Color color = { 255, 255, 255, 255 };

    SDL_Surface* surface = TTF_RenderText_Blended(font, text.c_str(), color);
    SDL_Texture* texture = SDL_CreateTextureFromSurface(renderer, surface);

    SDL_Rect rect = { x, y, surface->w, surface->h };

    SDL_RenderCopy(renderer, texture, NULL, &rect);

    SDL_FreeSurface(surface);
    SDL_DestroyTexture(texture);
}