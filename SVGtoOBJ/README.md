# svg2obj

A C++20 command-line converter that flattens SVG `<path>` curves, triangulates
the filled regions with the bundled `delaunator-cpp`, extrudes each path on the
X,Z plane (Y up), and writes a sealed Wavefront OBJ plus MTL file.

## Build

```powershell
cmake -S . -B build
cmake --build build --config Release
```

With Visual Studio 2026 installed in its default location, `scripts\\build-msvc.bat`
configures a Ninja release build using the Visual Studio developer environment.

## Usage

```text
svg2obj <input.svg> <output.obj> <height> [options]

Options:
  --curve-segments <n>   Samples per Bezier curve (default: 12, minimum: 1)
  --curve-tolerance <d>  Adaptive curve flatness tolerance; overrides segments
  --help                 Show help
```

The MTL is written beside the OBJ with the same base name. SVG path fills can
come from `fill`, inherited group fill, or an inline `style`. Hex, `rgb()`, and
common named colors are supported. `fill-rule` values `nonzero` and `evenodd`,
path/group transforms, all SVG path commands, concave paths, and holes are
handled. Open subpaths are implicitly closed for filling, as SVG requires.

SVG X maps to OBJ X and SVG Y maps to negative OBJ Y, correcting SVG's
downward-positive Y axis. The shape lies on the X,Y plane and extrusion runs
from OBJ Z=0 to OBJ Z=`height`, so it remains upright when viewed from positive
Z. Each visible SVG path becomes an OBJ object and material.
