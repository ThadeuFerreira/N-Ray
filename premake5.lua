workspace "NRay"
    local vulkan_shader_debug = os.getenv("NRAY_VULKAN_SHADER_DEBUG") == "1"

    configurations { "Debug", "Release", "Performance" }
    platforms { "x64" }
    -- Generate makefiles/project files under build/ so they don't clobber the
    -- hand-written wrapper Makefile at the repo root.
    location "build"

    filter "configurations:Debug"
        defines { "DEBUG" }
        symbols "On"

    if vulkan_shader_debug then
        filter "configurations:Debug"
            defines { "NRAY_VULKAN_SHADER_DEBUG" }
    end

    filter "configurations:Release"
        defines { "NDEBUG" }
        optimize "On"

    filter "configurations:Performance"
        defines { "NDEBUG", "NRAY_PERFORMANCE_BUILD" }
        optimize "Full"

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
        "PathTracingRenderer/src/**.h",
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
        "vendor/tinygltf",
        "vendor/volk",
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

    filter { "system:windows", "configurations:Performance" }
        buildoptions { "/O2", "/GL", "/fp:fast", "/arch:AVX2" }
        linkoptions { "/LTCG" }

    filter "system:linux"
        links { "raylib", "GL", "m", "pthread", "dl", "rt", "X11" }
        libdirs { "vendor/raylib/src" }
        buildoptions { "-fopenmp", "-mavx2" }
        linkoptions { "-fopenmp" }

    filter { "system:linux", "configurations:Performance" }
        buildoptions { "-O3", "-march=native", "-ffast-math", "-flto" }
        linkoptions { "-flto" }

    filter "system:macosx"
        links { "raylib", "OpenGL.framework", "Cocoa.framework", "IOKit.framework", "CoreVideo.framework", "CoreFoundation.framework" }
        buildoptions { "-Xpreprocessor -fopenmp", "-mavx2" }
        linkoptions { "-lomp" }

    filter { "system:macosx", "configurations:Performance" }
        buildoptions { "-O3", "-march=native", "-ffast-math", "-flto" }
        linkoptions { "-flto" }

project "NrayRenderDocHeadless"
    kind "ConsoleApp"
    language "C++"
    cppdialect "C++20"
    targetdir "bin/%{cfg.buildcfg}"
    objdir "obj/%{cfg.buildcfg}"

    files
    {
        "PathTracingRenderer/headless/main_headless.cpp",
        "PathTracingRenderer/headless/stb_impl.cpp",
        "PathTracingRenderer/src/vulkan_compute_preview.cpp",
        "PathTracingRenderer/src/gltf_scene.cpp",
        "PathTracingRenderer/src/tinygltf_impl.cpp",
        "PathTracingRenderer/src/renderer.cpp",
    }

    includedirs
    {
        "PathTracingRenderer/include",
        "PathTracingRenderer/external",
        "PathTracingRenderer/external/glm",
        "vendor/tinygltf",
        "vendor/volk",
        "PathTracingRenderer/src",
        -- raylib headers needed transitively (globalParams.h, screenStartup.h);
        -- vendor/raylib/src takes priority so versions match the main project.
        "vendor/raylib/src",
        "PathTracingRenderer/external/raylib/include",
    }

    defines { "_CRT_SECURE_NO_WARNINGS" }

    filter "system:linux"
        links { "m", "pthread", "dl", "rt" }
        buildoptions { "-mavx2" }

    filter { "system:linux", "configurations:Performance" }
        buildoptions { "-O3", "-march=native", "-ffast-math", "-flto" }
        linkoptions { "-flto" }

    filter "system:windows"
        buildoptions { "/arch:AVX2" }

    filter { "system:windows", "configurations:Performance" }
        buildoptions { "/O2", "/GL", "/fp:fast", "/arch:AVX2" }
        linkoptions { "/LTCG" }
