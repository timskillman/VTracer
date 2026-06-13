namespace VTracerMaui;

public partial class App : Application
{
	public App()
	{
		InitializeComponent();
		UserAppTheme = AppTheme.Dark;
	}

	protected override Window CreateWindow(IActivationState? activationState)
	{
		var window = new Window(new AppShell())
		{
			Title = "Image to SVG and OBJ",
			Width = 1600,
			Height = 960,
		};

		return window;
	}
}
