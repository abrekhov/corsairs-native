object Render;

void Main()
{
    SetEventHandler("RunTest", "RunTest", 0);
    PostEvent("RunTest", 100);
}

void RunTest()
{
    SaveEngineState("save/attributes-e2e");
}

void OnSave()
{
    Trace("SAVE_E2E_INTERRUPTED: writer entered");
    while (true)
    {
    }
}
