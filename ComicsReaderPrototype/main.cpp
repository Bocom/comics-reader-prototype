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

#include <SDL.h>
#include <SDL_opengl.h>
#include <vapoursynth/VapourSynth.h>
#include <vapoursynth/VSScript.h>
#include <cstdio>

#ifdef _WIN32 || _WIN64
#include <Shlwapi.h>
#else

#endif

typedef unsigned char u8;
typedef int32_t       s32;
typedef uint32_t      u32;
typedef float_t       r32;

// Macros
#define ArrayCount(a) (sizeof((a)) / sizeof((a)[0]))

// Global variables
static char g_vs_boilerplate[] = R"H(import vapoursynth as vs
from os.path import abspath
core = vs.get_core()
core.std.LoadPlugin(abspath("vapoursynth-stbi.dll"))
target_width = %d
target_height = %d
filename = r"%s"
i = core.stb.Image(filename)
)H";

static char *g_filter_chain = nullptr;

static char *g_archive_extensions[] = {
    "zip", "7z", "rar", "cbz", "cbr"
};

#ifdef _WIN32 || _WIN64
static char g_directory_separator = '\\';
#else
static char g_directory_seperator = '/';
#endif

static char *g_temp_dir_name = "crp";
static u32 g_temp_dir_name_length = strlen(g_temp_dir_name);

static char *g_temp_directory = nullptr;
static u32 g_temp_directory_length;

static s32 g_display_width;
static s32 g_display_height;

// Remember to free the string you get from this function!
char *
GetTempDirectory()
{
    char *directory;

#ifdef _WIN32 || _WIN64
    char windows_path[MAX_PATH];
    // TODO: Will this mess up on non-English Windows installations?
    u32 path_length = GetTempPathA(MAX_PATH, windows_path);
    if (!path_length)
    {
        SDL_Log("Couldn't get temporary path: %d\n", GetLastError());
        directory = nullptr;
    }
    else
    {
        u32 full_length = path_length + g_temp_dir_name_length + 1;
        directory = (char *)calloc(full_length + 1, sizeof(char));
        if (!directory)
        {
            SDL_Log("Couldn't allocate memory for the temporary path.");
            return nullptr;
        }
        memcpy(directory, windows_path, path_length);
        strcat(directory, g_temp_dir_name);
        directory[full_length - 1] = g_directory_separator;
    }
#else
    // TODO: GetTempDirectory is not implemented for non-Windows platforms yet
    directory = nullptr;
#endif

    return directory;
}

// TODO: Better name?
s32
CopyFileToTemp(char *filename, char *temp_file_path)
{
    s32 result;

#ifdef _WIN32 || _WIN64
    result = CopyFileA(filename, temp_file_path, 0);
#else
    // TODO: Non-Windows copying
#endif

    return result;
}

s32
FileExists(char *path)
{
    s32 result;

#if _WIN32 || _WIN64
    result = PathFileExistsA(path);
#else
    // TODO: Non-Windows file checking
#endif

    return result;
}

s32
IsArchive(char *ext)
{
    for (s32 i = 0; i < ArrayCount(g_archive_extensions); ++i)
    {
        char *ext_check = g_archive_extensions[i];

        while (*ext == *ext_check)
        {
            if (!*ext)
                return 1;

            ++ext;
            ++ext_check;
        }
    }

    return 0;
}

inline char *
GetFilenameFromPath(char *path)
{
    char *filename;

    char *slash = strrchr(path, g_directory_separator);
    if (!slash || slash == path)
        filename = path;
    else
        filename = slash + 1;

    return filename;
}

inline char *
GetFileExtension(char *filename)
{
    char *ext;

    char *dot = strrchr(filename, '.');
    if (!dot || dot == filename)
        ext = "";
    else
        ext = dot + 1;

    return ext;
}

s32
OpenFilterChain(char *chain_filename)
{
    s32 length;

    FILE *chain_file = fopen(chain_filename, "r");
    if (!chain_file)
    {
        // TODO: we messed up
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

typedef struct
{
    SDL_Renderer *renderer;
    SDL_Texture *texture;
    SDL_Rect *dest_rect;
    s32 image_width;
    s32 image_height;
    s32 mouse_button0_held;
    r32 zoom;
} State;

inline void
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
    SDL_RenderClear(s->renderer);

    SDL_RenderCopy(s->renderer, s->texture, nullptr, s->dest_rect);

    SDL_RenderPresent(s->renderer);
}

s32
main(s32 argc, char* argv[])
{
    if (argc == 1)
    {
        SDL_Log("usage: %s <filename>\n", GetFilenameFromPath(argv[0]));
        return 1;
    }

    // TODO: Handle command-line arguments

    if (!FileExists(argv[1]))
    {
        SDL_Log("%s doesn't exist.\n", argv[1]);
        return 1;
    }

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

    const VSAPI *vsapi = vsscript_getVSApi();
    if (!vsapi)
    {
        SDL_Log("Something weird happened with getVSApi, dunno\n");
        vsscript_finalize();
        SDL_Quit();
        return 1;
    }

    SDL_Window *window = SDL_CreateWindow("Comic Reader Prototype",
                                          0, 0, g_display_width, g_display_height,
                                          SDL_WINDOW_FULLSCREEN_DESKTOP);
    if (!window)
    {
        SDL_Log("Couldn't create SDL2 window: %s\n", SDL_GetError());
        vsscript_finalize();
        SDL_Quit();
        return 1;
    }

    SDL_Renderer *renderer = SDL_CreateRenderer(window, 0, SDL_RENDERER_ACCELERATED);
    if (!renderer)
    {
        SDL_Log("Couldn't create SDL2 renderer: %s\n", SDL_GetError());
        SDL_DestroyWindow(window);
        vsscript_finalize();
        SDL_Quit();
        return 1;
    }
    
    SDL_RenderClear(renderer);
    SDL_RenderPresent(renderer);

    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "linear");

    g_temp_directory = GetTempDirectory();
    if (!g_temp_directory)
    {
        // TODO: Handle not being able to get the temporary path.
    }
    g_temp_directory_length = strlen(g_temp_directory);

    char *DEBUG_target_filename = "temp.file";
    char *target_file;
    char *target_file_path;
    {
        s32 copied = 0;

        char *filename = argv[1];
        //target_file = DEBUG_target_filename;
        target_file = GetFilenameFromPath(filename);
        char *ext = GetFileExtension(filename);

        u32 target_file_length = strlen(target_file);

        target_file_path = (char *)calloc(g_temp_directory_length + target_file_length + 1, sizeof(char));
        if (!target_file_path)
        {
            // TODO: Error: couldn't allocate enough memory for the script buffer.
        }
        memcpy(target_file_path, g_temp_directory, g_temp_directory_length);
        strcat(target_file_path, target_file);

        if (IsArchive(ext))
        {
            // TODO: Handle archives
        }
        else
        {
            copied = CopyFileToTemp(filename, target_file_path);
        }

        if (!copied)
        {
            SDL_Log("Couldn't copy %s to temp folder.\n", target_file);

            SDL_DestroyRenderer(renderer);
            SDL_DestroyWindow(window);
            vsscript_finalize();
            SDL_Quit();
            return 1;
        }
    }

    s32 chain_length = OpenFilterChain("chain.vpy");
    if (!chain_length)
    {
        // TODO: Something here
    }

    s32 script_buffer_length = strlen(g_vs_boilerplate) + strlen(target_file_path) + chain_length + 4;
    char *script_buffer = (char *)calloc(script_buffer_length, sizeof(char));
    if (!script_buffer)
    {
        // TODO: Error: couldn't allocate enough memory for the script buffer.
    }
    s32 written = sprintf(script_buffer, g_vs_boilerplate, g_display_width, g_display_height, target_file_path);
    if (!written)
    {
        // TODO: Something went wrong here  
    }
    strcat(script_buffer, g_filter_chain);

    VSScript *se = nullptr;
    if (vsscript_evaluateScript(&se, script_buffer, "chain.vpy", 0))
    {
        SDL_Log("Script evaluation failed:\n%s", vsscript_getError(se));
        vsscript_freeScript(se);
        SDL_DestroyRenderer(renderer);
        SDL_DestroyWindow(window);
        vsscript_finalize();
        SDL_Quit();
        return 1;
    }

    free(script_buffer);
    free(target_file_path);

    VSNodeRef *node = vsscript_getOutput(se, 0);
    if (!node)
    {
        SDL_Log("Failed to retrieve VapourSynth output node. Make sure that you are calling set_output().\n");
        vsscript_freeScript(se);
        SDL_DestroyRenderer(renderer);
        SDL_DestroyWindow(window);
        vsscript_finalize();
        SDL_Quit();
        return 1;
    }

    const VSVideoInfo *vi = vsapi->getVideoInfo(node);
    if (!vi->numFrames)
    {
        SDL_Log("The VapourSynth clip is of unknown length. Please check your chain for errors.\n");
        vsapi->freeNode(node);
        vsscript_freeScript(se);
        SDL_DestroyRenderer(renderer);
        SDL_DestroyWindow(window);
        vsscript_finalize();
        SDL_Quit();
        return 1;
    }

    s32 image_width;
    s32 image_height;
    u8 *texture_buffer;
    {
        char errMsg[1024];
        const VSFrameRef *frame;
        frame = vsapi->getFrame(0, node, errMsg, sizeof errMsg);

        if (!frame)
        {
            SDL_Log("Error getting frame from VapourSynth:\n%s", errMsg);

            vsapi->freeNode(node);
            vsscript_freeScript(se);
            SDL_DestroyRenderer(renderer);
            SDL_DestroyWindow(window);
            vsscript_finalize();
            SDL_Quit();
            return 1;
        }
        
        image_width = vsapi->getFrameWidth(frame, 0);
        image_height = vsapi->getFrameHeight(frame, 0);

        s32 stride = vsapi->getStride(frame, 0);
        const u8 *r_ptr = vsapi->getReadPtr(frame, 0);
        const u8 *g_ptr = vsapi->getReadPtr(frame, 1);
        const u8 *b_ptr = vsapi->getReadPtr(frame, 2);

        // NOTE: The 3 here refers to R G B
        texture_buffer = (u8 *)malloc((image_width * 3) * image_height);
        u8 *texture_write = (u8 *)texture_buffer;

        for (s32 y = 0; y < image_height; ++y)
        {
            for (s32 x = 0; x < image_width; ++x)
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

        vsapi->freeFrame(frame);
    }

    // NOTE: We assume 3 bytes per sample here.
    auto surface = SDL_CreateRGBSurfaceFrom(texture_buffer, 
                                            image_width, image_height, 
                                            24, image_width * 3, 
                                            0, 0, 0, 0);
    auto texture = SDL_CreateTextureFromSurface(renderer, surface);
    
    SDL_FreeSurface(surface);
    free(texture_buffer);

    r32 zoom = 1.0f;

    s32 scaled_width = image_width;
    s32 scaled_height = image_height;
    {
        if (scaled_height > g_display_height)
        {
            zoom = g_display_height / (r32)image_height;
            scaled_width = image_width * zoom;
            scaled_height = g_display_height;
        }
    }
    
    auto target_x = (g_display_width / 2) - (scaled_width / 2);

    SDL_Rect display_rect = { target_x, 0, scaled_width, scaled_height };

    State state = {
        renderer, texture,
        &display_rect,
        image_width, image_height,
        0,
        zoom
    };

    s32 quit = 0;
    while (!quit)
    {
        quit = HandleEvents(&state);
        
        RenderFrame(&state);
    }

    // TODO: Remove temp file

    vsapi->freeNode(node);
    vsscript_freeScript(se);
    if (g_filter_chain)
        free(g_filter_chain);
    if (g_temp_directory)
        free(g_temp_directory);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    vsscript_finalize();
    SDL_Quit();

    return 0;
}