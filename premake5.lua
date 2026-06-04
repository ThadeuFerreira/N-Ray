workspace "NRay"
    configurations { "Debug", "Release" }
    platforms { "x64" }
    -- Generate makefiles/project files under build/ so they don't clobber the
    -- hand-written wrapper Makefile at the repo root.
    location "build"

    filter "configurations:Debug"
        defines { "DEBUG" }
        symbols "On"

    filter "configurations:Release"
        defines { "NDEBUG" }
        optimize "On"

    filter "platforms:x64"
        architecture "x86_64"

project "PathTracingRenderer"
    kind "ConsoleApp"
    language "C++"
    cppdialect "C++20"
    targetdir "bin/%{cfg.buildcfg}"
    objdir "obj/%{cfg.buildcfg}"

    files
    {
        "PathTracingRenderer/src/**.cpp",
        "PathTracingRenderer/include/**.h",
        "PathTracingRenderer/include/**.cpp",
        "PathTracingRenderer/external/imgui/*.cpp",
        "PathTracingRenderer/external/imgui/*.h",
        "PathTracingRenderer/external/rlImGui/*.cpp",
        "PathTracingRenderer/external/rlImGui/*.h",
        "PathTracingRenderer/external/intellisenseFix.h"
    }

    includedirs
    {
        "PathTracingRenderer/include",
        "PathTracingRenderer/external",
        "PathTracingRenderer/external/imgui",
        "PathTracingRenderer/external/imgui/backends",
        "PathTracingRenderer/external/glm",
        "PathTracingRenderer/external/rlImGui",
        "vendor/raylib/src",
        "PathTracingRenderer/external/raylib/include"
    }

    forceincludes { "PathTracingRenderer/external/intellisenseFix.h" }

    defines { "_CRT_SECURE_NO_WARNINGS" }

    filter "system:windows"
        systemversion "latest"
        links { "raylib", "opengl32", "gdi32", "winmm", "shell32" }
        libdirs { "PathTracingRenderer/external/raylib/Release" }
        buildoptions { "/openmp", "/arch:AVX2" }

    filter "system:linux"
        links { "raylib", "GL", "m", "pthread", "dl", "rt", "X11" }
        libdirs { "vendor/raylib/src" }
        buildoptions { "-fopenmp", "-mavx2" }
        linkoptions { "-fopenmp" }

    filter "system:macosx"
        links { "raylib", "OpenGL.framework", "Cocoa.framework", "IOKit.framework", "CoreVideo.framework", "CoreFoundation.framework" }
        buildoptions { "-Xpreprocessor -fopenmp", "-mavx2" }
        linkoptions { "-lomp" }
