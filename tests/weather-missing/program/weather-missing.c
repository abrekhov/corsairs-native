object Render;
object Rain;
object SunGlow;
object WaterFlare;

void Main()
{
    Trace("WEATHER-MAIN-ENTER");
    SetEventHandler("CreateWithoutWeather", "CreateWithoutWeather", 0);
    PostEvent("CreateWithoutWeather", 100);
}

void CreateWithoutWeather()
{
    CreateEntity(&Rain, "rain");
    Rain.isDone = "";
    CreateEntity(&SunGlow, "SunGlow");
    CreateEntity(&WaterFlare, "waterflare");
    Trace("WEATHER-MISSING-OK");
    ExitProgram();
}
