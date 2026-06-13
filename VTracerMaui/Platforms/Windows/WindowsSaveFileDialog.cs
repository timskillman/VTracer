using Windows.Storage.Pickers;
using WinRT.Interop;

namespace VTracerMaui.Platforms.Windows;

internal static class WindowsSaveFileDialog
{
    public static async Task<string?> PickAsync(
        string title,
        string suggestedFileName,
        string extension,
        string fileTypeDescription)
    {
        var window = Microsoft.Maui.Controls.Application.Current?.Windows.FirstOrDefault()?.Handler?.PlatformView as Microsoft.UI.Xaml.Window
            ?? throw new InvalidOperationException("The application window is not available.");

        var picker = new FileSavePicker
        {
            SuggestedStartLocation = PickerLocationId.DocumentsLibrary,
            SuggestedFileName = Path.GetFileNameWithoutExtension(suggestedFileName),
            CommitButtonText = "Export",
            SettingsIdentifier = $"VTracer{extension}Export",
        };
        picker.FileTypeChoices.Add(fileTypeDescription, [extension]);

        InitializeWithWindow.Initialize(picker, WindowNative.GetWindowHandle(window));
        var file = await picker.PickSaveFileAsync();
        return file?.Path;
    }
}
