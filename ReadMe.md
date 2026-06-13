# SVG and 3D OBJ export from image

A C++20 port of [VTracer](https://www.visioncortex.org/vtracer) for converting raster images into SVG files and 3D OBJ files. The repository includes a native library, a command-line application, and a Windows .NET 10.0 MAUI frontend.  The MAUI project is configured for Windows and Mac.

![alt text](https://github.com/timskillman/VTracer/blob/master/VTracerMaui/Assets/Screenshot.jpg "Maui App")
![alt text](https://github.com/timskillman/VTracer/blob/master/VTracerMaui/Assets/delaunator.jpg "OBJ export")

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
- 'SVGtoOBJ/' - SVG to OBJ file exporter based on MapBox's Delaunator
