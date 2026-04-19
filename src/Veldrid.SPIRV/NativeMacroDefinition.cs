using System.Runtime.InteropServices;
using System.Text;

namespace Veldrid.SPIRV;

[StructLayout(LayoutKind.Sequential, Pack = 1)]
internal unsafe struct NativeMacroDefinition
{
    private const int MaxBufferSize = 128;

    public uint NameLength;
    public fixed byte Name[MaxBufferSize];
    public uint ValueLength;
    public fixed byte Value[MaxBufferSize];

    public NativeMacroDefinition(MacroDefinition macroDefinition)
    {
        ArgumentNullException.ThrowIfNull(macroDefinition);

        if (string.IsNullOrEmpty(macroDefinition.Name))
        {
            throw new SpirvCompilationException("MacroDefinition Name must be non-null.");
        }
        if (macroDefinition.Name.Length > MaxBufferSize)
        {
            throw new SpirvCompilationException($"Macro names must be less than or equal to {MaxBufferSize} characters.");
        }

        fixed (char* nameU16Ptr = macroDefinition.Name)
        fixed (byte* namePtr = Name)
        {
            NameLength = (uint)Encoding.ASCII.GetBytes(nameU16Ptr, macroDefinition.Name.Length, namePtr, MaxBufferSize);
        }

        if (!string.IsNullOrEmpty(macroDefinition.Value))
        {
            if (macroDefinition.Value.Length > MaxBufferSize)
            {
                throw new SpirvCompilationException($"Macro values must be less than or equal to {MaxBufferSize} characters.");
            }

            fixed (char* valueU16 = macroDefinition.Value)
            fixed (byte* valuePtr = Value)
            {
                ValueLength = (uint)Encoding.ASCII.GetBytes(valueU16, macroDefinition.Value.Length, valuePtr, MaxBufferSize);
            }
        }
        else
        {
            ValueLength = 0;
        }
    }
}