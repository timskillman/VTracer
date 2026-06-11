using System.Runtime.InteropServices;
using System.Text;

namespace VTracerMaui.Interop;

internal enum NativeColorMode
{
    Color = 0,
    Binary = 1,
}

internal enum NativeHierarchicalMode
{
    Stacked = 0,
    Cutout = 1,
}

internal enum NativePathMode
{
    Pixel = 0,
    Polygon = 1,
    Spline = 2,
}

[StructLayout(LayoutKind.Sequential)]
internal struct NativeConfig
{
    public NativeColorMode ColorMode;
    public NativeHierarchicalMode HierarchicalMode;
    public int FilterSpeckle;
    public int ColorPrecision;
    public int LayerDifference;
    public NativePathMode PathMode;
    public int CornerThreshold;
    public double LengthThreshold;
    public int MaxIterations;
    public int SpliceThreshold;
    public int PathPrecision;
    public int HasPathPrecision;
}

[StructLayout(LayoutKind.Sequential)]
internal struct NativeTraceStats
{
    public int TotalRegions;
    public int TracedRegions;
    public int FilteredRegions;
    public int OutputPaths;
}

internal static class NativeMethods
{
    [DllImport("vtracer_native", EntryPoint = "vtracer_fill_default_config", CallingConvention = CallingConvention.Cdecl)]
    private static extern void FillDefaultConfig(out NativeConfig config);

    [DllImport("vtracer_native", EntryPoint = "vtracer_trace_file_w", CallingConvention = CallingConvention.Cdecl, CharSet = CharSet.Unicode)]
    private static extern int TraceFileCore(
        string inputPath,
        string outputPath,
        ref NativeConfig config,
        out NativeTraceStats stats,
        StringBuilder errorBuffer,
        int errorBufferLength
    );

    public static NativeConfig GetDefaultConfig()
    {
        FillDefaultConfig(out var config);
        return config;
    }

    public static NativeTraceStats TraceFile(string inputPath, string outputPath, NativeConfig config)
    {
        var errorBuffer = new StringBuilder(2048);
        var succeeded = TraceFileCore(inputPath, outputPath, ref config, out var stats, errorBuffer, errorBuffer.Capacity);
        if (succeeded == 0)
        {
            var message = errorBuffer.ToString();
            throw new InvalidOperationException(string.IsNullOrWhiteSpace(message) ? "Native tracing failed." : message);
        }

        return stats;
    }
}
