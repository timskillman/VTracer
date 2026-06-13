using System.Diagnostics;
using System.Globalization;
using Microsoft.Maui.Storage;
using VTracerMaui.Interop;
using VTracerMaui.Platforms.Windows;
using VTracerMaui.Models;

namespace VTracerMaui;

public partial class MainPage : ContentPage
{
    private readonly List<SamplePreset> _samples = [];
    private readonly SemaphoreSlim _renderGate = new(1, 1);
    private readonly string _previewDirectory = Path.Combine(FileSystem.CacheDirectory, "preview");
    private readonly string _previewHtmlPath = Path.Combine(FileSystem.CacheDirectory, "preview", "index.html");
    private readonly string _previewSvgPath = Path.Combine(FileSystem.CacheDirectory, "vtracer-preview.svg");
    private string? _currentImagePath;
    private string? _latestSvgMarkup;
    private bool _loaded;

    private NativeColorMode _colorMode = NativeColorMode.Color;
    private NativeHierarchicalMode _hierarchicalMode = NativeHierarchicalMode.Stacked;
    private NativePathMode _pathMode = NativePathMode.Spline;

    private static readonly Color BrandBlue = Color.FromArgb("#00275D");
    private static readonly Color BrandGold = Color.FromArgb("#CC972E");
    private static readonly Color LightText = Colors.White;

    public MainPage()
    {
        InitializeComponent();
        Directory.CreateDirectory(_previewDirectory);
        BuildSampleGallery();
        SampleGallery.ItemsSource = _samples;
        InitializeDefaults();
        PreviewWebView.Source = new HtmlWebViewSource { Html = BuildPlaceholderHtml() };
    }

    protected override async void OnAppearing()
    {
        base.OnAppearing();
        if (_loaded)
        {
            return;
        }

        _loaded = true;
        await Task.CompletedTask;
    }

    private void BuildSampleGallery()
    {
        _samples.Add(new SamplePreset
        {
            DisplayName = "K1 Drawing",
            RelativeAssetPath = Path.Combine("Assets", "Samples", "K1_drawing.jpg"),
            ColorMode = NativeColorMode.Binary,
            HierarchicalMode = NativeHierarchicalMode.Stacked,
            FilterSpeckle = 4,
            ColorPrecision = 6,
            GradientStep = 16,
            PathMode = NativePathMode.Spline,
            CornerThreshold = 60,
            SegmentLength = 4.0,
            SpliceThreshold = 45,
            PathPrecision = 8,
        });
        _samples.Add(new SamplePreset
        {
            DisplayName = "Cityscape",
            RelativeAssetPath = Path.Combine("Assets", "Samples", "Cityscape Sunset_DFM3-01.jpg"),
            ColorMode = NativeColorMode.Color,
            HierarchicalMode = NativeHierarchicalMode.Stacked,
            FilterSpeckle = 4,
            ColorPrecision = 8,
            GradientStep = 25,
            PathMode = NativePathMode.Spline,
            CornerThreshold = 60,
            SegmentLength = 4.0,
            SpliceThreshold = 45,
            PathPrecision = 8,
        });
        _samples.Add(new SamplePreset
        {
            DisplayName = "Gum Tree",
            RelativeAssetPath = Path.Combine("Assets", "Samples", "Gum Tree Vector.jpg"),
            ColorMode = NativeColorMode.Color,
            HierarchicalMode = NativeHierarchicalMode.Stacked,
            FilterSpeckle = 4,
            ColorPrecision = 8,
            GradientStep = 28,
            PathMode = NativePathMode.Spline,
            CornerThreshold = 60,
            SegmentLength = 4.0,
            SpliceThreshold = 45,
            PathPrecision = 8,
        });
        _samples.Add(new SamplePreset
        {
            DisplayName = "Dessert Poster",
            RelativeAssetPath = Path.Combine("Assets", "Samples", "vectorstock_31191940.png"),
            ColorMode = NativeColorMode.Color,
            HierarchicalMode = NativeHierarchicalMode.Stacked,
            FilterSpeckle = 8,
            ColorPrecision = 7,
            GradientStep = 64,
            PathMode = NativePathMode.Spline,
            CornerThreshold = 60,
            SegmentLength = 4.0,
            SpliceThreshold = 45,
            PathPrecision = 8,
        });
        _samples.Add(new SamplePreset
        {
            DisplayName = "Unsplash Dog",
            RelativeAssetPath = Path.Combine("Assets", "Samples", "angel-luciano-LATYeZyw88c-unsplash-s.jpg"),
            ColorMode = NativeColorMode.Color,
            HierarchicalMode = NativeHierarchicalMode.Stacked,
            FilterSpeckle = 10,
            ColorPrecision = 8,
            // Keep more tone separation for photo samples so the result stays closer
            // to the original VisionCortex trace and preserves fine fur/grass regions.
            GradientStep = 32,
            PathMode = NativePathMode.Spline,
            CornerThreshold = 180,
            SegmentLength = 4.0,
            SpliceThreshold = 45,
            PathPrecision = 8,
        });
        _samples.Add(new SamplePreset
        {
            DisplayName = "Tank Pixel",
            RelativeAssetPath = Path.Combine("Assets", "Samples", "tank-unit-preview.png"),
            ColorMode = NativeColorMode.Color,
            HierarchicalMode = NativeHierarchicalMode.Stacked,
            FilterSpeckle = 0,
            ColorPrecision = 8,
            GradientStep = 0,
            PathMode = NativePathMode.Pixel,
            CornerThreshold = 180,
            SegmentLength = 4.0,
            SpliceThreshold = 45,
            PathPrecision = 8,
        });

        foreach (var sample in _samples)
        {
            sample.ThumbnailPath = Path.Combine(AppContext.BaseDirectory, sample.RelativeAssetPath);
        }
    }

    private void InitializeDefaults()
    {
        FilterSpeckleSlider.Value = 4;
        ColorPrecisionSlider.Value = 6;
        GradientStepSlider.Value = 16;
        CornerSlider.Value = 60;
        LengthSlider.Value = 4.0;
        SpliceSlider.Value = 45;
        UpdateControlState();
    }

    private async Task LoadSampleAsync(SamplePreset sample)
    {
        _currentImagePath = sample.ThumbnailPath;

        _colorMode = sample.ColorMode;
        _hierarchicalMode = sample.HierarchicalMode;
        _pathMode = sample.PathMode;

        FilterSpeckleSlider.Value = sample.FilterSpeckle;
        ColorPrecisionSlider.Value = sample.ColorPrecision;
        GradientStepSlider.Value = sample.GradientStep;
        CornerSlider.Value = sample.CornerThreshold;
        LengthSlider.Value = sample.SegmentLength;
        SpliceSlider.Value = sample.SpliceThreshold;

        UpdateControlState();
        await RefreshPreviewAsync();
    }

    private static string BuildPlaceholderHtml()
    {
        return """
            <!DOCTYPE html>
            <html>
            <head>
              <meta charset="utf-8" />
              <style>
                html, body {
                  margin: 0;
                  height: 100%;
                  background: #1D1D1D;
                  color: #CC972E;
                  font-family: 'Segoe UI', sans-serif;
                }
                body {
                  display: flex;
                  align-items: center;
                  justify-content: center;
                  padding: 28px;
                  box-sizing: border-box;
                }
                .drop {
                  width: 100%;
                  height: 100%;
                  border: 3px dashed #CC972E;
                  background: #00275D;
                  display: flex;
                  align-items: center;
                  justify-content: center;
                  text-align: center;
                  font-size: 24px;
                }
                .note {
                  color: #F2F2F2;
                  font-size: 14px;
                  display: block;
                  margin-top: 10px;
                }
              </style>
            </head>
            <body>
              <div class="drop">
                <div>
                  Select a file from the header
                  <span class="note">or choose a bundled gallery sample below.</span>
                </div>
              </div>
            </body>
            </html>
            """;
    }

    private NativeConfig BuildConfig()
    {
        var config = NativeMethods.GetDefaultConfig();
        config.ColorMode = _colorMode;
        config.HierarchicalMode = _hierarchicalMode;
        config.FilterSpeckle = (int)Math.Round(FilterSpeckleSlider.Value);
        config.ColorPrecision = (int)Math.Round(ColorPrecisionSlider.Value);
        config.LayerDifference = (int)Math.Round(GradientStepSlider.Value);
        config.PathMode = _pathMode;
        config.CornerThreshold = (int)Math.Round(CornerSlider.Value);
        config.LengthThreshold = Math.Round(LengthSlider.Value * 2.0, MidpointRounding.AwayFromZero) / 2.0;
        config.MaxIterations = 10;
        config.SpliceThreshold = (int)Math.Round(SpliceSlider.Value);
        config.PathPrecision = 8;
        config.HasPathPrecision = 1;
        return config;
    }

    private async Task RefreshPreviewAsync()
    {
        if (string.IsNullOrWhiteSpace(_currentImagePath))
        {
            PreviewWebView.Source = new HtmlWebViewSource { Html = BuildPlaceholderHtml() };
            return;
        }

        await _renderGate.WaitAsync();
        try
        {
            TracingProgressBar.IsVisible = true;
            TracingProgressBar.Progress = 0.15;
            StatusLabel.Text = "Tracing image...";
            SetExportButtonsEnabled(false);

            var config = BuildConfig();
            var stats = await Task.Run(() => NativeMethods.TraceFile(_currentImagePath!, _previewSvgPath, config));
            TracingProgressBar.Progress = 0.85;

            _latestSvgMarkup = await File.ReadAllTextAsync(_previewSvgPath);
            await WritePreviewDocumentAsync(_currentImagePath!, _colorMode == NativeColorMode.Binary);
            PreviewWebView.Source = new UrlWebViewSource
            {
                Url = new Uri(_previewHtmlPath).AbsoluteUri,
            };

            TracingProgressBar.Progress = 1;
            SetExportButtonsEnabled(true);
            StatusLabel.Text = $"{stats.OutputPaths} paths, {stats.TotalRegions} regions, {stats.FilteredRegions} filtered";
        }
        catch (Exception ex)
        {
            _latestSvgMarkup = null;
            SetExportButtonsEnabled(false);
            StatusLabel.Text = "Tracing failed.";
            await DisplayAlertAsync("Tracing Failed", ex.Message, "OK");
        }
        finally
        {
            await Task.Delay(120);
            TracingProgressBar.IsVisible = false;
            TracingProgressBar.Progress = 0;
            _renderGate.Release();
        }
    }

    private async Task WritePreviewDocumentAsync(string imagePath, bool binaryMode)
    {
        var extension = Path.GetExtension(imagePath);
        var previewImagePath = Path.Combine(_previewDirectory, $"source{extension}");
        File.Copy(imagePath, previewImagePath, true);

        var imageUri = new Uri(previewImagePath).AbsoluteUri;
        var svgUri = new Uri(_previewSvgPath).AbsoluteUri;
        var imageDisplay = "display:none;";
        var stageBackground = binaryMode ? "#FFFFFF" : "#000000";

        var html = $$"""
            <!DOCTYPE html>
            <html>
            <head>
              <meta charset="utf-8" />
              <style>
                html, body {
                  margin: 0;
                  height: 100%;
                  background: #1D1D1D;
                  overflow: hidden;
                }
                .stage {
                  width: 100%;
                  height: 100%;
                  padding: 24px;
                  box-sizing: border-box;
                  display: flex;
                  align-items: center;
                  justify-content: center;
                }
                .frame {
                  position: relative;
                  width: 100%;
                  height: 100%;
                  background: {{stageBackground}};
                  box-shadow: inset 0 0 0 1px rgba(255,255,255,0.05);
                }
                .source {
                  position: absolute;
                  inset: 0;
                  width: 100%;
                  height: 100%;
                  object-fit: contain;
                  {{imageDisplay}}
                }
                .vector {
                  position: absolute;
                  inset: 0;
                  display: flex;
                  align-items: center;
                  justify-content: center;
                }
                .vector svg {
                  width: 100%;
                  height: 100%;
                  display: block;
                }
              </style>
            </head>
            <body>
              <div class="stage">
                <div class="frame">
                  <img class="source" src="{{imageUri}}" />
                  <div class="vector">
                    <object type="image/svg+xml" data="{{svgUri}}" style="width:100%;height:100%;"></object>
                  </div>
                </div>
              </div>
            </body>
            </html>
            """;

        await File.WriteAllTextAsync(_previewHtmlPath, html);
    }

    private void UpdateControlState()
    {
        FilterSpeckleValueLabel.Text = ((int)Math.Round(FilterSpeckleSlider.Value)).ToString();
        ColorPrecisionValueLabel.Text = ((int)Math.Round(ColorPrecisionSlider.Value)).ToString();
        GradientStepValueLabel.Text = ((int)Math.Round(GradientStepSlider.Value)).ToString();
        CornerValueLabel.Text = ((int)Math.Round(CornerSlider.Value)).ToString();
        LengthValueLabel.Text = (Math.Round(LengthSlider.Value * 2.0, MidpointRounding.AwayFromZero) / 2.0).ToString("0.0");
        SpliceValueLabel.Text = ((int)Math.Round(SpliceSlider.Value)).ToString();

        var showColorOptions = _colorMode == NativeColorMode.Color;
        HierarchicalRow.IsVisible = showColorOptions;
        ColorPrecisionLabel.IsVisible = showColorOptions;
        ColorPrecisionRow.IsVisible = showColorOptions;
        GradientStepLabel.IsVisible = showColorOptions;
        GradientStepRow.IsVisible = showColorOptions;

        var showSplineOptions = _pathMode == NativePathMode.Spline;
        CornerLabel.IsVisible = showSplineOptions;
        CornerRow.IsVisible = showSplineOptions;
        LengthLabel.IsVisible = showSplineOptions;
        LengthRow.IsVisible = showSplineOptions;
        SpliceLabel.IsVisible = showSplineOptions;
        SpliceRow.IsVisible = showSplineOptions;

        ApplyToggleStyle(BinaryButton, _colorMode == NativeColorMode.Binary);
        ApplyToggleStyle(ColorButton, _colorMode == NativeColorMode.Color);
        ApplyToggleStyle(CutoutButton, _hierarchicalMode == NativeHierarchicalMode.Cutout);
        ApplyToggleStyle(StackedButton, _hierarchicalMode == NativeHierarchicalMode.Stacked);
        ApplyToggleStyle(PixelButton, _pathMode == NativePathMode.Pixel);
        ApplyToggleStyle(PolygonButton, _pathMode == NativePathMode.Polygon);
        ApplyToggleStyle(SplineButton, _pathMode == NativePathMode.Spline);
    }

    private static void ApplyToggleStyle(Button button, bool selected)
    {
        button.BackgroundColor = selected ? BrandBlue : Colors.Transparent;
        button.BorderColor = selected ? BrandGold : Colors.White;
        button.TextColor = LightText;
    }

    private async void OnOpenImageClicked(object? sender, EventArgs e)
    {
        try
        {
            var file = await FilePicker.Default.PickAsync(new PickOptions
            {
                PickerTitle = "Select a raster image",
                FileTypes = FilePickerFileType.Images,
            });

            if (file is null || string.IsNullOrWhiteSpace(file.FullPath))
            {
                return;
            }

            SampleGallery.SelectedItem = null;
            _currentImagePath = file.FullPath;
            StatusLabel.Text = Path.GetFileName(file.FullPath);
            await RefreshPreviewAsync();
        }
        catch (Exception ex)
        {
            await DisplayAlertAsync("Open Image Failed", ex.Message, "OK");
        }
    }

    private void SetExportButtonsEnabled(bool enabled)
    {
        ExportSvgButton.IsEnabled = enabled;
        ExportObjButton.IsEnabled = enabled;
    }

    private async void OnExportSvgClicked(object? sender, EventArgs e)
    {
        if (string.IsNullOrWhiteSpace(_latestSvgMarkup))
        {
            return;
        }

        try
        {
            var outputPath = await WindowsSaveFileDialog.PickAsync(
                "Export SVG",
                BuildSuggestedFileName("svg"),
                ".svg",
                "SVG image");
            if (outputPath is null)
            {
                return;
            }

            await File.WriteAllTextAsync(outputPath, _latestSvgMarkup);
            StatusLabel.Text = $"Saved {Path.GetFileName(outputPath)}";
        }
        catch (Exception ex)
        {
            await DisplayAlertAsync("Export SVG Failed", ex.Message, "OK");
        }
    }

    private async void OnExportObjClicked(object? sender, EventArgs e)
    {
        if (string.IsNullOrWhiteSpace(_latestSvgMarkup))
        {
            return;
        }

        var heightText = await DisplayPromptAsync(
            "Export OBJ",
            "Extrude height (mm)",
            "EXPORT OBJ",
            "CANCEL",
            "10",
            keyboard: Keyboard.Numeric);
        if (heightText is null)
        {
            return;
        }

        if (!TryParsePositiveHeight(heightText, out var height))
        {
            await DisplayAlertAsync("Invalid Height", "Enter an extrude height greater than 0 mm.", "OK");
            return;
        }

        try
        {
            var outputPath = await WindowsSaveFileDialog.PickAsync(
                "Export OBJ",
                BuildSuggestedFileName("obj"),
                ".obj",
                "Wavefront OBJ");
            if (outputPath is null)
            {
                return;
            }

            SetExportButtonsEnabled(false);
            StatusLabel.Text = "Converting SVG to OBJ...";
            await File.WriteAllTextAsync(_previewSvgPath, _latestSvgMarkup);
            await Task.Run(() => ConvertSvgToObj(_previewSvgPath, outputPath, height));
            StatusLabel.Text = $"Saved {Path.GetFileName(outputPath)} and {Path.GetFileNameWithoutExtension(outputPath)}.mtl";
        }
        catch (Exception ex)
        {
            StatusLabel.Text = "OBJ export failed.";
            await DisplayAlertAsync("Export OBJ Failed", ex.Message, "OK");
        }
        finally
        {
            SetExportButtonsEnabled(!string.IsNullOrWhiteSpace(_latestSvgMarkup));
        }
    }

    private string BuildSuggestedFileName(string extension)
    {
        var sourceName = Path.GetFileNameWithoutExtension(_currentImagePath);
        if (string.IsNullOrWhiteSpace(sourceName))
        {
            sourceName = $"export-{DateTime.Now:yyyy-MM-dd-HHmmss}";
        }

        return $"{sourceName}.{extension}";
    }

    private static bool TryParsePositiveHeight(string text, out double height)
    {
        return (double.TryParse(text, NumberStyles.Float, CultureInfo.CurrentCulture, out height)
                || double.TryParse(text, NumberStyles.Float, CultureInfo.InvariantCulture, out height))
            && double.IsFinite(height)
            && height > 0;
    }

    private static void ConvertSvgToObj(string inputPath, string outputPath, double height)
    {
        var converterPath = Path.Combine(AppContext.BaseDirectory, "svg2obj.exe");
        if (!File.Exists(converterPath))
        {
            throw new FileNotFoundException("The SVGtoOBJ converter was not found beside the application.", converterPath);
        }

        var outputDirectory = Path.GetDirectoryName(outputPath)
            ?? throw new InvalidOperationException("The OBJ output directory is not valid.");
        var outputFileName = Path.GetFileName(outputPath);
        var mtlPath = Path.ChangeExtension(outputPath, ".mtl");
        var temporaryDirectory = Path.Combine(FileSystem.CacheDirectory, $"obj-export-{Guid.NewGuid():N}");
        var temporaryObjPath = Path.Combine(temporaryDirectory, outputFileName);
        var temporaryMtlPath = Path.ChangeExtension(temporaryObjPath, ".mtl");

        Directory.CreateDirectory(temporaryDirectory);
        try
        {
            var startInfo = new ProcessStartInfo
            {
                FileName = converterPath,
                UseShellExecute = false,
                CreateNoWindow = true,
                RedirectStandardOutput = true,
                RedirectStandardError = true,
            };
            startInfo.ArgumentList.Add(inputPath);
            startInfo.ArgumentList.Add(temporaryObjPath);
            startInfo.ArgumentList.Add(height.ToString("R", CultureInfo.InvariantCulture));
            startInfo.ArgumentList.Add("--curve-tolerance");
            startInfo.ArgumentList.Add("1");

            using var process = Process.Start(startInfo)
                ?? throw new InvalidOperationException("Could not start the SVGtoOBJ converter.");
            var standardOutput = process.StandardOutput.ReadToEnd();
            var standardError = process.StandardError.ReadToEnd();
            process.WaitForExit();

            if (process.ExitCode != 0)
            {
                var message = string.IsNullOrWhiteSpace(standardError) ? standardOutput : standardError;
                throw new InvalidOperationException(string.IsNullOrWhiteSpace(message)
                    ? $"SVGtoOBJ exited with code {process.ExitCode}."
                    : message.Trim());
            }

            if (!File.Exists(temporaryObjPath) || !File.Exists(temporaryMtlPath))
            {
                throw new InvalidOperationException("SVGtoOBJ did not produce both the OBJ and MTL files.");
            }

            Directory.CreateDirectory(outputDirectory);
            File.Move(temporaryMtlPath, mtlPath, true);
            File.Move(temporaryObjPath, outputPath, true);
        }
        finally
        {
            Directory.Delete(temporaryDirectory, true);
        }
    }

    private async void OnArticleClicked(object? sender, EventArgs e)
    {
        await Launcher.Default.OpenAsync(new Uri("https://www.visioncortex.org/vtracer-docs"));
    }

    private async void OnGitHubClicked(object? sender, EventArgs e)
    {
        await Launcher.Default.OpenAsync(new Uri("https://github.com/visioncortex/vtracer"));
    }

    private async void OnSampleSelectionChanged(object? sender, SelectionChangedEventArgs e)
    {
        if (e.CurrentSelection.FirstOrDefault() is SamplePreset sample)
        {
            await LoadSampleAsync(sample);
        }
    }

    private async void OnBinaryClicked(object? sender, EventArgs e)
    {
        _colorMode = NativeColorMode.Binary;
        UpdateControlState();
        await RefreshPreviewAsync();
    }

    private async void OnColorClicked(object? sender, EventArgs e)
    {
        _colorMode = NativeColorMode.Color;
        UpdateControlState();
        await RefreshPreviewAsync();
    }

    private async void OnCutoutClicked(object? sender, EventArgs e)
    {
        _hierarchicalMode = NativeHierarchicalMode.Cutout;
        UpdateControlState();
        await RefreshPreviewAsync();
    }

    private async void OnStackedClicked(object? sender, EventArgs e)
    {
        _hierarchicalMode = NativeHierarchicalMode.Stacked;
        UpdateControlState();
        await RefreshPreviewAsync();
    }

    private async void OnPixelClicked(object? sender, EventArgs e)
    {
        _pathMode = NativePathMode.Pixel;
        UpdateControlState();
        await RefreshPreviewAsync();
    }

    private async void OnPolygonClicked(object? sender, EventArgs e)
    {
        _pathMode = NativePathMode.Polygon;
        UpdateControlState();
        await RefreshPreviewAsync();
    }

    private async void OnSplineClicked(object? sender, EventArgs e)
    {
        _pathMode = NativePathMode.Spline;
        UpdateControlState();
        await RefreshPreviewAsync();
    }

    private void OnFilterSpeckleChanged(object? sender, ValueChangedEventArgs e) => UpdateControlState();
    private void OnColorPrecisionChanged(object? sender, ValueChangedEventArgs e) => UpdateControlState();
    private void OnGradientStepChanged(object? sender, ValueChangedEventArgs e) => UpdateControlState();
    private void OnCornerChanged(object? sender, ValueChangedEventArgs e) => UpdateControlState();
    private void OnLengthChanged(object? sender, ValueChangedEventArgs e) => UpdateControlState();
    private void OnSpliceChanged(object? sender, ValueChangedEventArgs e) => UpdateControlState();

    private async void OnSliderDragCompleted(object? sender, EventArgs e)
    {
        UpdateControlState();
        await RefreshPreviewAsync();
    }
}
