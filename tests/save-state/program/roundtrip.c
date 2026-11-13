object Render;
object source;
object copied;
object items[2];
aref sourceRef;
aref destinationRef;
aref copiedRef;
aref arrayRef;
aref removedRef;

void Main()
{
    SetEventHandler("RunTest", "RunTest", 0);
    PostEvent("RunTest", 100);
}

void RunTest()
{
    source.branch.child.token = "copied-value";
    makearef(sourceRef, source);
    makearef(destinationRef, copied);
    CopyAttributes(destinationRef, sourceRef);
    makearef(copiedRef, copied.branch.child);
    items[1].branch.token = "array-value";
    makearef(arrayRef, items[1].branch);
    source.removed.token = "deleted-value";
    makearef(removedRef, source.removed);
    DeleteAttribute(sourceRef, "removed");
    SaveEngineState("save/attributes-e2e");
    LoadEngineState("save/attributes-e2e");
}

void OnLoad()
{
    if (copied.branch.child.token != "copied-value")
    {
        Trace("SAVE_E2E_FAIL: copied object");
        ExitProgram();
        return;
    }
    if (copiedRef.token != "copied-value")
    {
        Trace("SAVE_E2E_FAIL: copied reference");
        ExitProgram();
        return;
    }
    if (arrayRef.token != "array-value")
    {
        Trace("SAVE_E2E_FAIL: array reference");
        ExitProgram();
        return;
    }
    if (CheckAttribute(sourceRef, "removed"))
    {
        Trace("SAVE_E2E_FAIL: removed branch");
        ExitProgram();
        return;
    }
    copiedRef.token = "updated-value";
    if (copied.branch.child.token != "updated-value")
    {
        Trace("SAVE_E2E_FAIL: reference write-through");
        ExitProgram();
        return;
    }
    Trace("SAVE_E2E_PASS: copied tree, live/array references, removed branch, save/load");
    ExitProgram();
}
