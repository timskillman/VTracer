# VTracerCPP

A C++20 port of VTracer for converting raster images into SVG files. The repository includes a native library, a command-line application, and a Windows .NET MAUI frontend.

![alt text](https://github.com/timskillman/VTracer/blob/master/VTracerMaui/Assets/Screenshot.jpg "Maui App")

## Requirements

- CMake 3.20 or newer
- A C++20 compiler
- Visual Studio 2022 with C++ desktop tools (Windows)
- .NET 10 SDK with the .NET MAUI workload (for the GUI)

## Build the native targets

```powershell
cmake -S . -B build-native
cmake --build build-native --config Release
```

This builds:

- `vtracer_core` - C++ tracing library
- `vtracer_native` - shared library with a C API
- `vtracer_cli` - command-line application

## Use the CLI

```powershell
.\build-native\Release\vtracer_cli.exe --input input.png --output output.svg
```

Run `vtracer_cli --help` to see all tracing options and presets.

## Run the MAUI application

```powershell
dotnet workload install maui
dotnet build .\VTracerMaui\VTracerMaui.csproj
dotnet run --project .\VTracerMaui\VTracerMaui.csproj
```

The MAUI project builds the native tracer automatically through CMake.

## Project layout

- `app/` - CLI entry point
- `include/` - public C++ and C headers
- `src/` - native implementation
- `external/` - third-party headers
- `VTracerMaui/` - .NET MAUI desktop application

