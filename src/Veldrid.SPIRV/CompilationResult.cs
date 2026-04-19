using System.Runtime.InteropServices;

namespace Veldrid.SPIRV;

[StructLayout(LayoutKind.Sequential)]
internal unsafe struct CompilationResult
{
    public Bool32 Succeeded;
    public InteropArray DataBuffers;
    public ReflectionInfo ReflectionInfo;

    public readonly uint GetLength(uint index)
    {
        ArgumentOutOfRangeException.ThrowIfGreaterThanOrEqual(index, DataBuffers.Count);
        return DataBuffers.Ref<InteropArray>(index).Count;
    }

    public readonly void* GetData(uint index)
    {
        ArgumentOutOfRangeException.ThrowIfGreaterThanOrEqual(index, DataBuffers.Count);
        return DataBuffers.Ref<InteropArray>(index).Data;
    }
}
