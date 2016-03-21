/*

    THE GRAND LIST OF TO-DOS!

    Big stuff:
    [ ] Code cleanup! Move stuff out of main()
    [X] Implement mouse dragging when image size exceeds monitor size
        [ ] Make it so the zoom-in is focused on the cursor
    [/] Make the window appear before loading the image(s)
        [ ] Show some kind of loader/
    [ ] Implement archive support
    [ ] Implement folder support?
    [ ] Implement file navigation

    Cleanup stuff:
    [ ] Figure out why sometimes the image isn't shown on screen when it's supposed to.

*/

#include <vapoursynth/VapourSynth.h>
#include <vapoursynth/VSScript.h>
#include "platform.h"

s32
OpenFilterChain(char *chain_filename)
{
    s32 length;

    FILE *chain_file = fopen(chain_filename, "r");
    if (!chain_file)
    {
        SDL_Log("Couldn't open the filter chain file %s.\n", chain_filename);
        return 0;
    }

    // Get length
    fseek(chain_file, 0, SEEK_END);
    length = ftell(chain_file);
    fseek(chain_file, 0, SEEK_SET);

    g_filter_chain = (char *)calloc(length, sizeof(char));
    if (!g_filter_chain)
    {
        SDL_Log("Couldn't allocate memory for the filter chain's contents.\n");
        return 0;
    }

    fread(g_filter_chain, 1, length, chain_file);
    fclose(chain_file);

    return length;
}

// TODO: Do we need to lock g_vsapi?
u8 *
GenerateTextureBuffer(VSNodeRef *node, s32 *image_width, s32 *image_height)
{
    u8 *texture_buffer;

    char errMsg[1024];
    const VSFrameRef *frame;
    frame = g_vsapi->getFrame(0, node, errMsg, sizeof errMsg);

    if (!frame)
    {
        SDL_Log("Error getting frame from VapourSynth:\n%s", errMsg);
        return nullptr;
    }

    s32 width = g_vsapi->getFrameWidth(frame, 0);
    *image_width = width;
    s32 height = g_vsapi->getFrameHeight(frame, 0);
    *image_height = height;

    s32 stride = g_vsapi->getStride(frame, 0);
    const u8 *r_ptr = g_vsapi->getReadPtr(frame, 0);
    const u8 *g_ptr = g_vsapi->getReadPtr(frame, 1);
    const u8 *b_ptr = g_vsapi->getReadPtr(frame, 2);

    // NOTE: The 3 here refers to R G B
    texture_buffer = (u8 *)malloc((width * 3) * height);
    u8 *texture_write = (u8 *)texture_buffer;

    for (s32 y = 0; y < height; ++y)
    {
        for (s32 x = 0; x < width; ++x)
        {
            // TODO: This might not play nice with endianness?
            *texture_write++ = b_ptr[x];
            *texture_write++ = g_ptr[x];
            *texture_write++ = r_ptr[x];
        }

        r_ptr += stride;
        g_ptr += stride;
        b_ptr += stride;
    }

    g_vsapi->freeFrame(frame);

    return texture_buffer;
}

void
UpdateScaling(State *s)
{
    s32 scaled_width = s->image_width * s->zoom;
    s32 scaled_height = s->image_height * s->zoom;

    s->dest_rect->x = (g_display_width / 2) - (scaled_width / 2);

    if (scaled_width < s->image_width)
    {
        s->dest_rect->y = (g_display_height / 2) - (scaled_height / 2);
    }

    if (scaled_height > g_display_height)
    {
        s->dest_rect->y = 0;
    }

    s->dest_rect->w = scaled_width;
    s->dest_rect->h = scaled_height;
}

s32
HandleEvents(State *s)
{
    s32 quit = 0;

    SDL_Event ev;
    while (SDL_PollEvent(&ev))
    {
        if (ev.type == SDL_QUIT)
        {
            quit = 1;
        }
        else if (ev.type == SDL_KEYDOWN)
        {
            SDL_Keycode key = ev.key.keysym.sym;
            if (key == SDLK_ESCAPE)
            {
                quit = 1;
            }
            else if (key == SDLK_SPACE)
            {
                if ((SDL_GetModState() & KMOD_CTRL) != 0)
                {
                    s->zoom = g_display_height / (r32)s->image_height;
                }
                else if ((SDL_GetModState() & KMOD_SHIFT) != 0)
                {
                    s->zoom = g_display_width / (r32)s->image_width;
                }
                else
                {
                    s->zoom = 1.0f;
                }

                UpdateScaling(s);
            }
        }
        else if (ev.type == SDL_MOUSEWHEEL)
        {
            // Zooming! Zoom zoom!
            s->zoom += (ev.wheel.y / 10.0f);
            if (s->zoom < 0.2f)
                s->zoom = 0.2f;

            UpdateScaling(s);
        }
        else if (ev.type == SDL_MOUSEBUTTONDOWN)
        {
            if (ev.button.button == SDL_BUTTON_LEFT)
            {
                s->mouse_button0_held = 1;
            }
        }
        else if (ev.type == SDL_MOUSEBUTTONUP)
        {
            if (ev.button.button == SDL_BUTTON_LEFT)
            {
                s->mouse_button0_held = 0;
            }
        }
        else if (ev.type == SDL_MOUSEMOTION && s->mouse_button0_held)
        {
            SDL_Rect *r = s->dest_rect;
            s32 scaled_width = s->image_width * s->zoom;
            s32 scaled_height = s->image_height * s->zoom;
            
            if (scaled_height > g_display_height)
            {
                r->y += ev.motion.yrel;
                if (r->y >= 0)
                    r->y = 0;
                if ((r->y + scaled_height) <= g_display_height)
                    r->y -= ev.motion.yrel;
            }

            if (scaled_width > g_display_width)
            {
                r->x += ev.motion.xrel;
                if (r->x >= 0)
                    r->x = 0;
                if ((r->x + scaled_width) <= g_display_width)
                    r->x -= ev.motion.xrel;
            }
        }
    }

    return quit;
}

void
RenderFrame(State *s)
{
    // TODO: Lock g_renderer
    SDL_RenderClear(g_renderer);

    SDL_RenderCopy(g_renderer, s->texture, nullptr, s->dest_rect);

    SDL_RenderPresent(g_renderer);
}

static s32
ProcessImage(void *thread_data)
{
    ProcessImageData *d = (ProcessImageData *)thread_data;
    s32 exit_code = 0;

    s32 script_buffer_length = strlen(g_vs_boilerplate) + strlen(d->file_path) + g_chain_length + 4;
    char *script_buffer = (char *)calloc(script_buffer_length, sizeof(char));
    if (!script_buffer)
    {
        SDL_Log("Couldn't allocate enough memory for the generated filter chain.\n");
        exit_code = 1;
        goto cleanup;
    }

    s32 written = sprintf(script_buffer, g_vs_boilerplate, g_display_width, g_display_height, d->file_path);
    if (!written)
    {
        SDL_Log("Couldn't generate the filter chain header.\n");
        exit_code = 1;
        goto cleanup;
    }

    strcat(script_buffer, g_filter_chain);

    VSScript *se = nullptr;
    if (vsscript_evaluateScript(&se, script_buffer, g_filter_chain, 0))
    {
        SDL_Log("Script evaluation failed:\n%s", vsscript_getError(se));
        exit_code = 1;
        goto cleanup;
    }

    free(script_buffer);

    VSNodeRef *node = vsscript_getOutput(se, 0);
    if (!node)
    {
        SDL_Log("Failed to retrieve VapourSynth output node. Make sure that you are calling set_output().\n");
        exit_code = 1;
        goto cleanup;
    }

    const VSVideoInfo *vi = g_vsapi->getVideoInfo(node);
    if (!vi->numFrames)
    {
        SDL_Log("The VapourSynth clip is of unknown length. Please check your chain for errors.\n");
        exit_code = 1;
        goto cleanup;
    }

    s32 image_width;
    s32 image_height;
    u8 *texture_buffer = GenerateTextureBuffer(node, &image_width, &image_height);
    if (!texture_buffer)
    {
        SDL_Log("Couldn't generate a texture from the filter chain.\n");
        exit_code = 1;
        goto cleanup;
    }

    // NOTE: We assume 3 bytes per sample here.
    auto surface = SDL_CreateRGBSurfaceFrom(texture_buffer,
        image_width, image_height,
        24, image_width * 3,
        0, 0, 0, 0);
    
    SDL_Texture *texture = nullptr;
    do
    {
        auto locked = MT_CompareExchange(&g_renderer_locked, 1, 0);
        if (!locked)
        {
            texture = SDL_CreateTextureFromSurface(g_renderer, surface);
            locked = MT_Exchange(&g_renderer_locked, 0);
        }
    } while (!texture);
    
    // TODO: Lock g_images
    ProcessedImage *image = &g_images[d->index];
    image->texture = texture;
    image->width = image_width;
    image->height = image_height;
    image->processed = 1;

cleanup:
    if (node)
        g_vsapi->freeNode(node);
    if (se)
        vsscript_freeScript(se);
    if (surface)
        SDL_FreeSurface(surface);
    if (texture_buffer)
        free(texture_buffer);

    return exit_code;
}

s32
main(s32 argc, char* argv[])
{
    if (argc == 1)
    {
        SDL_Log("usage: %s <filename>\n", GetFilenameFromPath(argv[0]));
        return 1;
    }

    if (!FileExists(argv[1]))
    {
        SDL_Log("%s doesn't exist.\n", argv[1]);
        return 1;
    }
    
    // TODO: Handle command-line arguments

    s32 exit_code = 0;

    if (SDL_Init(SDL_INIT_VIDEO))
    {
        SDL_Log("Couldn't initialize SDL: %s\n", SDL_GetError());
        return 1;
    }

    SDL_DisplayMode dm;
    if (SDL_GetDesktopDisplayMode(0, &dm))
    {
        SDL_Log("Couldn't retrieve the desktop display mode for display 0: %s\n", SDL_GetError());
        SDL_Quit();
        return 1;
    }

    g_display_width = dm.w;
    g_display_height = dm.h;

    if (!vsscript_init())
    {
        SDL_Log("Failed to initialize VapourSynth environment.\n");
        SDL_Quit();
        return 1;
    }

    g_vsapi = vsscript_getVSApi();
    if (!g_vsapi)
    {
        SDL_Log("Something weird happened with getVSApi, dunno\n");
        exit_code = 1;
        goto cleanup;
    }

    SDL_Window *window = SDL_CreateWindow("Comic Reader Prototype",
                                          0, 0, g_display_width, g_display_height,
                                          SDL_WINDOW_FULLSCREEN_DESKTOP);
    if (!window)
    {
        SDL_Log("Couldn't create SDL2 window: %s\n", SDL_GetError());
        exit_code = 1;
        goto cleanup;
    }

    g_renderer = SDL_CreateRenderer(window, 0, SDL_RENDERER_ACCELERATED);
    if (!g_renderer)
    {
        SDL_Log("Couldn't create SDL2 renderer: %s\n", SDL_GetError());
        exit_code = 1;
        goto cleanup;
    }
    
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "linear");

    SDL_RenderClear(g_renderer);
    SDL_RenderPresent(g_renderer);

    g_temp_directory = GetTempDirectory();
    if (!g_temp_directory)
    {
        // TODO: Handle not being able to get the temporary path.
        SDL_Log("Couldn't get the temporary path.\n");
        exit_code = 1;
        goto cleanup;
    }
    g_temp_directory_length = strlen(g_temp_directory);

    char *default_chain_name = "chain.vpy";

    g_chain_length = OpenFilterChain(default_chain_name);
    if (!g_chain_length)
    {
        SDL_Log("Couldn't open the filter chain file %s\n", default_chain_name);
        exit_code = 1;
        goto cleanup;
    }

    char *source_filename = argv[1];
    char *ext = GetFileExtension(source_filename);

    // TODO: Determine how many threads we can use

    if (IsArchive(ext))
    {
        // TODO: Handle archives. Spawn a bunch of threads wooooo
    }
    else
    {
        // TODO: Folder scanning logic

        g_num_images = 1;
        g_images = (ProcessedImage *)calloc(g_num_images, sizeof(ProcessedImage));
        ProcessedImage *i = &g_images[0];
        i->processed = 0;

        ProcessImageData thread_data = {};
        thread_data.index = 0;
        thread_data.file_path = source_filename;

        // TODO: Keep the thread handle around?
        SDL_CreateThread(ProcessImage, "ProcessImage", &thread_data);
    }
    
    s32 processing = 1;
    while (processing)
    {
        // TODO: In here, we'll show a fancy loading thing or something.
        // TODO: And also, like, actually do progress checking.
        auto locked = MT_CompareExchange(&g_renderer_locked, 1, 0);
        if (!locked)
        {
            SDL_RenderClear(g_renderer);
            SDL_RenderPresent(g_renderer);

            locked = MT_Exchange(&g_renderer_locked, 0);
        }

        // TODO: Lock g_images. And, you know, not do progress checking like *this*.
        //       (Actually, for the first image, this might be fine)
        if (g_images[0].processed)
        {
            processing = 0;
        }
    }

    s32 current_index = 0;
    ProcessedImage *image = &g_images[current_index];

    r32 zoom = 1.0f;

    s32 scaled_width = image->width;
    s32 scaled_height = image->height;
    {
        if (scaled_height > g_display_height)
        {
            zoom = g_display_height / (r32)image->height;
            scaled_width = image->width * zoom;
            scaled_height = g_display_height;
        }
    }
    
    auto target_x = (g_display_width / 2) - (scaled_width / 2);

    SDL_Rect display_rect = { target_x, 0, scaled_width, scaled_height };

    State state = {
        image->texture,
        &display_rect,
        image->width, image->height,
        0,
        zoom
    };

    s32 quit = 0;
    while (!quit)
    {
        quit = HandleEvents(&state);
        
        RenderFrame(&state);
    }

    // TODO: Remove temp files

cleanup:
    if (g_images)
        free(g_images);
    if (g_filter_chain)
        free(g_filter_chain);
    if (g_temp_directory)
        free(g_temp_directory);
    if (g_renderer)
        SDL_DestroyRenderer(g_renderer);
    if (window)
        SDL_DestroyWindow(window);

    vsscript_finalize();
    SDL_Quit();

    return exit_code;
}