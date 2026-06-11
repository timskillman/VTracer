using VTracerMaui.Interop;

namespace VTracerMaui.Models;

internal sealed class SamplePreset
{
    public string DisplayName { get; init; } = string.Empty;
    public string RelativeAssetPath { get; init; } = string.Empty;
    public string ThumbnailPath { get; set; } = string.Empty;
    public NativeColorMode ColorMode { get; init; } = NativeColorMode.Color;
    public NativeHierarchicalMode HierarchicalMode { get; init; } = NativeHierarchicalMode.Stacked;
    public int FilterSpeckle { get; init; }
    public int ColorPrecision { get; init; }
    public int GradientStep { get; init; }
    public NativePathMode PathMode { get; init; } = NativePathMode.Spline;
    public int CornerThreshold { get; init; }
    public double SegmentLength { get; init; }
    public int SpliceThreshold { get; init; }
    public int PathPrecision { get; init; } = 8;
}
