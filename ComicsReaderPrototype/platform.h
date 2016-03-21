#pragma once

#include <SDL.h>
#include <SDL_opengl.h>
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

#ifdef _WIN32 || _WIN64
#define MT_CompareExchange(ptr, value, cmp) InterlockedCompareExchange((volatile long *)(ptr), (value), (cmp))
#define MT_Exchange(ptr, value) InterlockedExchange((volatile long *)(ptr), (value))
#else
// TODO: Make sure that these work as they should
#define MT_CompareExchange(ptr, value, cmp) __sync_val_compare_and_swap((ptr), (cmp), (value))
#define MT_Exchange(ptr, value) __sync_lock_test_and_set((ptr), (value))
#endif

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
static s32 g_chain_length;

static char *g_temp_directory = nullptr;
static u32 g_temp_directory_length;

static s32 g_display_width;
static s32 g_display_height;

static char *g_archive_extensions[] = {
    "zip", "7z", "rar", "cbz", "cbr"
};

#ifdef _WIN32 || _WIN64
static char g_directory_separator = '\\';
#else
static char g_directory_separator = '/';
#endif

static char *g_temp_dir_name = "crp";
static u32 g_temp_dir_name_length = strlen(g_temp_dir_name);

static SDL_Renderer *g_renderer;
static const VSAPI *g_vsapi;

// Structures
typedef struct
{
    SDL_Texture *texture;
    SDL_Rect *dest_rect;
    s32 image_width;
    s32 image_height;
    s32 mouse_button0_held;
    r32 zoom;
} State;

// Functions

// Remember to free the string you get from this function!
static char *
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
static s32
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

static s32
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

static s32
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