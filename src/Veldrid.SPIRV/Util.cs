using System.Text;

namespace Veldrid.SPIRV;

internal static class Util
{
    private static ReadOnlySpan<byte> SpirvMagic => [0x03, 0x02, 0x23, 0x07];

    internal static unsafe string? GetString(byte* data, uint length)
    {
        if (data is null) return null;

        return Encoding.UTF8.GetString(data, (int)length);
    }

    internal static bool HasSpirvHeader(byte[] bytes)
    {
        return bytes.Length > 4
            && bytes.AsSpan(0, 4).SequenceEqual(SpirvMagic);
    }
}
