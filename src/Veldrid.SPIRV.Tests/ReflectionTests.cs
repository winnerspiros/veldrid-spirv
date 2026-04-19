using Xunit;

namespace Veldrid.SPIRV.Tests;

public class ReflectionTests
{
    private static void AssertEqual(ResourceLayoutElementDescription a, ResourceLayoutElementDescription b)
    {
        Assert.Equal(a.Name, b.Name);
        Assert.Equal(a.Kind, b.Kind);
        Assert.Equal(a.Options, b.Options);
        Assert.Equal(a.Stages, b.Stages);
    }

    [Theory]
    [MemberData(nameof(ShaderSetsAndResources))]
    public void ReflectionFromSpirv_Succeeds(
        string vertex, string fragment,
        VertexElementDescription[] verts,
        ResourceLayoutDescription[] layouts)
    {
        byte[] vsBytes = TestUtil.LoadBytes(vertex);
        byte[] fsBytes = TestUtil.LoadBytes(fragment);
        VertexFragmentCompilationResult result = SpirvCompilation.CompileVertexFragment(
            vsBytes,
            fsBytes,
            CrossCompileTarget.HLSL,
            new CrossCompileOptions(false, false, true));

        VertexElementDescription[] reflectedVerts = result.Reflection!.VertexElements;
        Assert.Equal(verts.Length, reflectedVerts.Length);
        for (int i = 0; i < verts.Length; i++)
        {
            Assert.Equal(verts[i], reflectedVerts[i]);
        }

        ResourceLayoutDescription[] reflectedLayouts = result.Reflection.ResourceLayouts;
        Assert.Equal(layouts.Length, reflectedLayouts.Length);
        for (int i = 0; i < layouts.Length; i++)
        {
            ResourceLayoutDescription layout = layouts[i];
            ResourceLayoutDescription reflectedLayout = reflectedLayouts[i];
            Assert.Equal(layout.Elements.Length, reflectedLayout.Elements.Length);
            for (int j = 0; j < layout.Elements.Length; j++)
            {
                AssertEqual(layout.Elements[j], reflectedLayout.Elements[j]);
            }
        }
    }

    public static IEnumerable<object[]> ShaderSetsAndResources()
    {
        yield return
        [
            "planet.vert.spv",
            "planet.frag.spv",
            new VertexElementDescription[]
            {
                new("Position", VertexElementSemantic.TextureCoordinate, VertexElementFormat.Float3),
                new("Normal", VertexElementSemantic.TextureCoordinate, VertexElementFormat.Float3),
                new("TexCoord", VertexElementSemantic.TextureCoordinate, VertexElementFormat.Float2),
            },
            new ResourceLayoutDescription[]
            {
                new(
                    new ResourceLayoutElementDescription("vdspv_0_0", ResourceKind.UniformBuffer, ShaderStages.Vertex | ShaderStages.Fragment),
                    UnusedResource,
                    new ResourceLayoutElementDescription("vdspv_0_2", ResourceKind.UniformBuffer, ShaderStages.Fragment)),
                new(
                    new ResourceLayoutElementDescription("vdspv_1_0", ResourceKind.TextureReadOnly, ShaderStages.Fragment),
                    new ResourceLayoutElementDescription("vdspv_1_1", ResourceKind.Sampler, ShaderStages.Fragment))
            }
        ];
    }

    private static readonly ResourceLayoutElementDescription UnusedResource
        = new() { Options = (ResourceLayoutElementOptions)2 };
}
