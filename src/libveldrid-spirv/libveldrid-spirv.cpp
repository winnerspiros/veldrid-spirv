// veldrid-spirv.cpp : Defines the entry point for the console application.
//

#include "libveldrid-spirv.hpp"
#include "InteropStructs.hpp"
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <tuple>
#include <vector>
#include "spirv_hlsl.hpp"
#include "spirv_glsl.hpp"
#include "spirv_msl.hpp"
#include "shaderc.hpp"

using namespace spirv_cross;

namespace Veldrid
{
void ReflectVertexInfo(const Compiler& compiler, const ShaderResources& resources, ReflectionInfo& info);

struct BindingInfo
{
    uint32_t Set;
    uint32_t Binding;

    bool operator<(const BindingInfo &other) const
    {
        return std::tie(Set, Binding) < std::tie(other.Set, other.Binding);
    }
};

struct ResourceInfo
{
    std::string Name;
    ResourceKind Kind;
    uint32_t IDs[2]{}; // 0 == VS/CS, 1 == FS
};

ResourceKind ClassifyResource(const Compiler *compiler, const Resource &resource, bool image, bool storage)
{
    const SPIRType &type = compiler->get_type(resource.type_id);
    switch (type.basetype)
    {
    case SPIRType::BaseType::Struct:
        if (storage)
        {
            auto bufferBlockFlags = compiler->get_buffer_block_flags(resource.id);
            if (bufferBlockFlags.get(spv::Decoration::DecorationNonWritable))
            {
                return StorageBufferReadOnly;
            }
            else
            {
                return StorageBufferReadWrite;
            }
        }
        else
        {
            return UniformBuffer;
        }
    case SPIRType::BaseType::Image:
        return storage ? StorageImage : SampledImage;
    case SPIRType::BaseType::Sampler:
        return ResourceKind::Sampler;
    default:
        throw std::runtime_error("Unhandled SPIR-V data type.");
    }
}

void AddResources(
    spirv_cross::SmallVector<spirv_cross::Resource> &resources,
    spirv_cross::Compiler *compiler,
    std::map<BindingInfo, ResourceInfo> &allResources,
    const uint32_t idIndex,
    bool normalizeResourceNames,
    bool image = false,
    bool storage = false)
{
    for (auto &resource : resources)
    {
        ResourceKind kind = ClassifyResource(compiler, resource, image, storage);
        BindingInfo bi{
            compiler->get_decoration(resource.id, spv::Decoration::DecorationDescriptorSet),
            compiler->get_decoration(resource.id, spv::Decoration::DecorationBinding)
        };

        ResourceInfo ri{};
        if (normalizeResourceNames)
        {
            std::string name = "vdspv_" + std::to_string(bi.Set) + "_" + std::to_string(bi.Binding);
            if (kind == ResourceKind::UniformBuffer)
            {
                compiler->set_name(resource.base_type_id, name);
            }
            else
            {
                compiler->set_name(resource.id, name);
            }
            ri.Name = std::move(name);
        }
        else
        {
            ri.Name = resource.name;
        }

        ri.IDs[idIndex] = resource.id;
        ri.Kind = kind;

        auto [it, inserted] = allResources.insert({bi, ri});
        if (!inserted) // Element already exists.
        {
            if (it->second.IDs[idIndex] != 0)
            {
                throw std::runtime_error(
                    "The same binding slot (" + std::to_string(bi.Set) + ", " + std::to_string(bi.Binding) +
                    ") was used by multiple distinct resources. First resource: " +
                    it->second.Name + ". Second resource: " + ri.Name);
            }

            it->second.IDs[idIndex] = resource.id;
            if (it->second.Kind != kind)
            {
                throw std::runtime_error(
                    "The same binding slot (" + std::to_string(bi.Set) + ", " + std::to_string(bi.Binding) +
                    ") was used by multiple resources with incompatible types: \"" +
                    std::to_string(it->second.Kind) + "\" and \"" + std::to_string(kind) + "\".");
            }
        }
    }
}

uint32_t GetResourceIndex(
    CrossCompileTarget target,
    ResourceKind resourceKind,
    uint32_t &bufferIndex,
    uint32_t &textureIndex,
    uint32_t &uavIndex,
    uint32_t &samplerIndex)
{
    switch (resourceKind)
    {
    case UniformBuffer:
        return bufferIndex++;
    case StorageBufferReadWrite:
        if (target == MSL)
        {
            return bufferIndex++;
        }
        else
        {
            return uavIndex++;
        }
    case StorageImage:
        if (target == MSL)
        {
            return textureIndex++;
        }
        else
        {
            return uavIndex++;
        }
    case SampledImage:
        return textureIndex++;
    case StorageBufferReadOnly:
        if (target == MSL)
        {
            return bufferIndex++;
        }
        else
        {
            return textureIndex++;
        }
    case Sampler:
        return samplerIndex++;
    default:
        throw std::runtime_error("Invalid ResourceKind.");
    }
}

std::unique_ptr<Compiler> GetCompiler(std::vector<uint32_t> spirvBytes, const CrossCompileInfo &info)
{
    switch (info.Target)
    {
    case HLSL:
    {
        auto ret = std::make_unique<CompilerHLSL>(std::move(spirvBytes));
        CompilerHLSL::Options opts{};
        opts.shader_model = 50;
        opts.point_size_compat = true;
        ret->set_hlsl_options(opts);
        CompilerGLSL::Options commonOpts;
        commonOpts.vertex.flip_vert_y = info.InvertY;
        commonOpts.vertex.fixup_clipspace = info.FixClipSpaceZ;
        ret->set_common_options(commonOpts);
        return ret;
    }
    case GLSL:
    case ESSL:
    {
        auto ret = std::make_unique<CompilerGLSL>(std::move(spirvBytes));
        CompilerGLSL::Options opts{};
        opts.es = info.Target == ESSL;
        opts.enable_420pack_extension = false;
        if (info.ComputeShader.Count > 0)
        {
            opts.version = info.Target == GLSL ? 430 : 310;
        }
        else
        {
            opts.version = info.Target == GLSL ? 330 : 300;
        }
        opts.vertex.fixup_clipspace = info.FixClipSpaceZ;
        opts.vertex.flip_vert_y = info.InvertY;
        ret->set_common_options(opts);
        return ret;
    }
    case MSL:
    {
        auto ret = std::make_unique<CompilerMSL>(std::move(spirvBytes));
        CompilerMSL::Options opts{};
        opts.enable_decoration_binding = true;
        ret->set_msl_options(opts);
        CompilerGLSL::Options commonOpts;
        commonOpts.vertex.flip_vert_y = info.InvertY;
        commonOpts.vertex.fixup_clipspace = info.FixClipSpaceZ;
        ret->set_common_options(commonOpts);
        return ret;
    }
    default:
        throw std::runtime_error("Invalid OutputKind.");
    }
}

void SetSpecializations(spirv_cross::Compiler *compiler, const CrossCompileInfo &info)
{
    auto specConstants = compiler->get_specialization_constants();
    for (uint32_t i = 0; i < info.Specializations.Count; i++)
    {
        uint32_t constID = info.Specializations[i].ID;
        uint32_t varID = 0;

        for (const auto &constant : specConstants)
        {
            if (constant.constant_id == constID)
            {
                varID = constant.id;
                break;
            }
        }

        if (varID != 0)
        {
            auto &constVar = compiler->get_constant(varID);
            constVar.m.c[0].r[0].u64 = info.Specializations[i].Constant;
        }
    }
}

InteropArray<ResourceLayoutDescription> CreateResourceLayoutArray(
    const std::map<BindingInfo, ResourceInfo> &resources,
    bool compute)
{
    std::vector<uint32_t> setSizes(1);
    for (const auto& [binding, resource] : resources)
    {
        if (setSizes.size() <= binding.Set)
        {
            setSizes.resize(binding.Set + 1);
        }
        setSizes[binding.Set] = std::max(setSizes[binding.Set], binding.Binding + 1);
    }

    auto setCount = static_cast<uint32_t>(setSizes.size());
    InteropArray<ResourceLayoutDescription> ret(setCount);

    for (uint32_t i = 0; i < setCount; i++)
    {
        ret[i].ResourceElements.Resize(setSizes[i]);
        for (uint32_t j = 0; j < setSizes[i]; j++)
        {
            ret[i].ResourceElements[j].Name = InteropArray<char>();
            ret[i].ResourceElements[j].Kind = ResourceKind::UniformBuffer;
            ret[i].ResourceElements[j].Stages = ShaderStages::None;
            ret[i].ResourceElements[j].Options = 2; // "Unused"
        }
    }

    for (const auto& [binding, resource] : resources)
    {
        ShaderStages stages = ShaderStages::None;
        if (resource.IDs[0] != 0)
        {
            stages = stages | (compute ? ShaderStages::Compute : ShaderStages::Vertex);
        }
        if (resource.IDs[1] != 0)
        {
            stages = stages | ShaderStages::Fragment;
        }

        auto &elem = ret[binding.Set].ResourceElements[binding.Binding];
        elem.Name.CopyFrom(static_cast<uint32_t>(resource.Name.length()), resource.Name.c_str());
        elem.Kind = resource.Kind;
        elem.Stages = stages;
        elem.Options = 0;
    }

    return ret;
}

// Length of "#version NNN" strings used in version replacement below.
constexpr size_t kVersionDirectiveLen = 12;

void ReplaceGlslVersion(std::string &shaderText, std::string_view from, std::string_view to)
{
    if (auto idx = shaderText.find(from); idx != std::string::npos)
        shaderText.replace(idx, kVersionDirectiveLen, to);
}

CompilationResult *CompileVertexFragment(const CrossCompileInfo &info)
{
    std::vector<uint32_t> vsBytes(
        info.VertexShader.Data,
        info.VertexShader.Data + info.VertexShader.Count);
    auto vsCompiler = GetCompiler(std::move(vsBytes), info);

    std::vector<uint32_t> fsBytes(
        info.FragmentShader.Data,
        info.FragmentShader.Data + info.FragmentShader.Count);
    auto fsCompiler = GetCompiler(std::move(fsBytes), info);

    SetSpecializations(vsCompiler.get(), info);
    SetSpecializations(fsCompiler.get(), info);

    ShaderResources vsResources = vsCompiler->get_shader_resources();
    ShaderResources fsResources = fsCompiler->get_shader_resources();

    std::map<BindingInfo, ResourceInfo> allResources;

    AddResources(vsResources.uniform_buffers, vsCompiler.get(), allResources, 0, info.NormalizeResourceNames);
    AddResources(vsResources.storage_buffers, vsCompiler.get(), allResources, 0, info.NormalizeResourceNames, false, true);
    AddResources(vsResources.separate_images, vsCompiler.get(), allResources, 0, info.NormalizeResourceNames, true, false);
    AddResources(vsResources.storage_images, vsCompiler.get(), allResources, 0, info.NormalizeResourceNames, true, true);
    AddResources(vsResources.separate_samplers, vsCompiler.get(), allResources, 0, info.NormalizeResourceNames);

    AddResources(fsResources.uniform_buffers, fsCompiler.get(), allResources, 1, info.NormalizeResourceNames);
    AddResources(fsResources.storage_buffers, fsCompiler.get(), allResources, 1, info.NormalizeResourceNames, false, true);
    AddResources(fsResources.separate_images, fsCompiler.get(), allResources, 1, info.NormalizeResourceNames, true, false);
    AddResources(fsResources.storage_images, fsCompiler.get(), allResources, 1, info.NormalizeResourceNames, true, true);
    AddResources(fsResources.separate_samplers, fsCompiler.get(), allResources, 1, info.NormalizeResourceNames);

    if (info.Target == HLSL || info.Target == MSL)
    {
        uint32_t bufferIndex = 0;
        uint32_t textureIndex = 0;
        uint32_t uavIndex = 0;
        uint32_t samplerIndex = 0;
        for (const auto &[binding, resource] : allResources)
        {
            uint32_t index = GetResourceIndex(info.Target, resource.Kind, bufferIndex, textureIndex, uavIndex, samplerIndex);

            if (uint32_t vsID = resource.IDs[0]; vsID != 0)
            {
                vsCompiler->set_decoration(vsID, spv::Decoration::DecorationBinding, index);
            }
            if (uint32_t fsID = resource.IDs[1]; fsID != 0)
            {
                fsCompiler->set_decoration(fsID, spv::Decoration::DecorationBinding, index);
            }
        }
    }

    if (info.Target == GLSL || info.Target == ESSL)
    {
        vsCompiler->build_dummy_sampler_for_combined_images();
        vsCompiler->build_combined_image_samplers();
        for (const auto &remap : vsCompiler->get_combined_image_samplers())
        {
            vsCompiler->set_name(remap.combined_id, vsCompiler->get_name(remap.image_id));
        }

        fsCompiler->build_dummy_sampler_for_combined_images();
        fsCompiler->build_combined_image_samplers();
        for (const auto &remap : fsCompiler->get_combined_image_samplers())
        {
            fsCompiler->set_name(remap.combined_id, fsCompiler->get_name(remap.image_id));
        }

        for (const auto &output : vsResources.stage_outputs)
        {
            uint32_t location = vsCompiler->get_decoration(output.id, spv::Decoration::DecorationLocation);
            vsCompiler->set_name(output.id, "vdspv_fsin" + std::to_string(location));
        }

        for (const auto &input : fsResources.stage_inputs)
        {
            uint32_t location = fsCompiler->get_decoration(input.id, spv::Decoration::DecorationLocation);
            fsCompiler->set_name(input.id, "vdspv_fsin" + std::to_string(location));
        }
    }

    if (info.Target == ESSL)
    {
        for (const auto &uniformBuffer : vsResources.uniform_buffers)
        {
            vsCompiler->unset_decoration(uniformBuffer.id, spv::Decoration::DecorationBinding);
        }

        uint32_t bufferIndex = 0;
        uint32_t imageIndex = 0;
        for (const auto &[binding, resource] : allResources)
        {
            if (resource.Kind == StorageBufferReadOnly || resource.Kind == StorageBufferReadWrite)
            {
                uint32_t id = bufferIndex++;
                if (resource.IDs[0] != 0)
                {
                    vsCompiler->set_decoration(resource.IDs[0], spv::Decoration::DecorationBinding, id);
                }
                if (resource.IDs[1] != 0)
                {
                    fsCompiler->set_decoration(resource.IDs[1], spv::Decoration::DecorationBinding, id);
                }
            }
            else if (resource.Kind == StorageImage)
            {
                uint32_t id = imageIndex++;
                if (resource.IDs[0] != 0)
                {
                    vsCompiler->set_decoration(resource.IDs[0], spv::Decoration::DecorationBinding, id);
                }
                if (resource.IDs[1] != 0)
                {
                    fsCompiler->set_decoration(resource.IDs[1], spv::Decoration::DecorationBinding, id);
                }
            }
        }
    }

    std::string vsText = vsCompiler->compile();

    bool usesStorageResource = !vsResources.storage_buffers.empty() || !vsResources.storage_images.empty();
    if (info.Target == GLSL && usesStorageResource)
        ReplaceGlslVersion(vsText, "#version 330", "#version 430");
    else if (info.Target == ESSL && usesStorageResource)
        ReplaceGlslVersion(vsText, "#version 300", "#version 310");

    std::string fsText = fsCompiler->compile();

    usesStorageResource = !fsResources.storage_buffers.empty() || !fsResources.storage_images.empty();
    if (info.Target == GLSL && usesStorageResource)
        ReplaceGlslVersion(fsText, "#version 330", "#version 430");
    else if (info.Target == ESSL && usesStorageResource)
        ReplaceGlslVersion(fsText, "#version 300", "#version 310");

    auto *result = new CompilationResult();
    result->Succeeded = true;

    result->DataBuffers.Resize(2);
    result->DataBuffers[0].CopyFrom(static_cast<uint32_t>(vsText.length()), reinterpret_cast<const uint8_t *>(vsText.c_str()));
    result->DataBuffers[1].CopyFrom(static_cast<uint32_t>(fsText.length()), reinterpret_cast<const uint8_t *>(fsText.c_str()));

    ReflectVertexInfo(*vsCompiler, vsResources, result->Reflection);
    result->Reflection.ResourceLayouts = CreateResourceLayoutArray(allResources, false);

    return result;
}

CompilationResult *CompileCompute(const CrossCompileInfo &info)
{
    std::vector<uint32_t> csBytes(
        info.ComputeShader.Data,
        info.ComputeShader.Data + info.ComputeShader.Count);
    auto csCompiler = GetCompiler(std::move(csBytes), info);

    SetSpecializations(csCompiler.get(), info);

    ShaderResources csResources = csCompiler->get_shader_resources();

    std::map<BindingInfo, ResourceInfo> allResources;

    AddResources(csResources.uniform_buffers, csCompiler.get(), allResources, 0, info.NormalizeResourceNames);
    AddResources(csResources.storage_buffers, csCompiler.get(), allResources, 0, info.NormalizeResourceNames, false, true);
    AddResources(csResources.separate_images, csCompiler.get(), allResources, 0, info.NormalizeResourceNames, true, false);
    AddResources(csResources.storage_images, csCompiler.get(), allResources, 0, info.NormalizeResourceNames, true, true);
    AddResources(csResources.separate_samplers, csCompiler.get(), allResources, 0, info.NormalizeResourceNames);

    if (info.Target == HLSL || info.Target == MSL)
    {
        uint32_t bufferIndex = 0;
        uint32_t textureIndex = 0;
        uint32_t uavIndex = 0;
        uint32_t samplerIndex = 0;
        for (const auto &[binding, resource] : allResources)
        {
            uint32_t index = GetResourceIndex(info.Target, resource.Kind, bufferIndex, textureIndex, uavIndex, samplerIndex);

            if (uint32_t csID = resource.IDs[0]; csID != 0)
            {
                csCompiler->set_decoration(csID, spv::Decoration::DecorationBinding, index);
            }
        }
    }

    if (info.Target == GLSL || info.Target == ESSL)
    {
        csCompiler->build_dummy_sampler_for_combined_images();
        csCompiler->build_combined_image_samplers();
        for (const auto &remap : csCompiler->get_combined_image_samplers())
        {
            csCompiler->set_name(remap.combined_id, csCompiler->get_name(remap.image_id));
        }
    }

    if (info.Target == ESSL)
    {
        for (const auto &uniformBuffer : csResources.uniform_buffers)
        {
            csCompiler->unset_decoration(uniformBuffer.id, spv::Decoration::DecorationBinding);
        }

        uint32_t bufferIndex = 0;
        uint32_t imageIndex = 0;
        for (const auto &[binding, resource] : allResources)
        {
            if (resource.Kind == StorageBufferReadOnly || resource.Kind == StorageBufferReadWrite)
            {
                csCompiler->set_decoration(resource.IDs[0], spv::Decoration::DecorationBinding, bufferIndex++);
            }
            else if (resource.Kind == StorageImage)
            {
                csCompiler->set_decoration(resource.IDs[0], spv::Decoration::DecorationBinding, imageIndex++);
            }
        }
    }

    std::string csText = csCompiler->compile();

    auto *result = new CompilationResult();
    result->Succeeded = true;
    result->DataBuffers.Resize(1);
    result->DataBuffers[0].CopyFrom(static_cast<uint32_t>(csText.length()), reinterpret_cast<const uint8_t *>(csText.c_str()));

    result->Reflection.ResourceLayouts = CreateResourceLayoutArray(allResources, true);

    return result;
}

CompilationResult *Compile(const CrossCompileInfo &info)
{
    if (info.VertexShader.Count > 0 && info.FragmentShader.Count > 0)
    {
        return CompileVertexFragment(info);
    }
    else if (info.ComputeShader.Count > 0)
    {
        return CompileCompute(info);
    }

    return new CompilationResult("The given combination of shaders was not valid.");
}

std::vector<uint32_t> ReadFile(const std::string &path)
{
    std::ifstream is(path, std::ios::binary | std::ios::ate);
    auto size = static_cast<size_t>(is.tellg());
    is.seekg(0, std::ios::beg);
    std::vector<uint32_t> ret(size / 4);
    is.read(reinterpret_cast<char *>(ret.data()), size);
    return ret;
}

void WriteToFile(const std::string &path, const std::string &text)
{
    auto outFile = std::ofstream(path);
    outFile << text;
    outFile.close();
}

CompilationResult *CompileGLSLToSPIRV(
    const std::string &sourceText,
    shaderc_shader_kind kind,
    const std::string &fileName,
    const shaderc::CompileOptions &options)
{
    shaderc::Compiler compiler;
    shaderc::SpvCompilationResult result = compiler.CompileGlslToSpv(sourceText, kind, fileName.c_str(), options);

    if (result.GetCompilationStatus() != shaderc_compilation_status_success)
    {
        return new CompilationResult(result.GetErrorMessage());
    }

    uint32_t length = static_cast<uint32_t>(result.end() - result.begin()) * sizeof(uint32_t);
    auto *ret = new CompilationResult();
    ret->Succeeded = 1;
    ret->DataBuffers.Resize(1);
    ret->DataBuffers[0].CopyFrom(length, reinterpret_cast<const uint8_t *>(result.begin()));
    return ret;
}

VD_EXPORT CompilationResult *CrossCompile(CrossCompileInfo *info)
{
    try
    {
        return Compile(*info);
    }
    catch (const std::exception &e)
    {
        return new CompilationResult(e.what());
    }
}

VD_EXPORT CompilationResult *CompileGlslToSpirv(GlslCompileInfo *info)
{
    try
    {
        shaderc::CompileOptions options;

        if (info->Debug)
        {
            options.SetGenerateDebugInfo();
        }
        else
        {
            options.SetOptimizationLevel(shaderc_optimization_level_performance);
        }

        for (uint32_t i = 0; i < info->Macros.Count; i++)
        {
            const MacroDefinition &macro = info->Macros[i];
            if (macro.ValueLength == 0)
            {
                options.AddMacroDefinition(std::string(macro.Name, macro.NameLength));
            }
            else
            {
                options.AddMacroDefinition(
                    std::string(macro.Name, macro.NameLength),
                    std::string(macro.Value, macro.ValueLength));
            }
        }

        return CompileGLSLToSPIRV(
            std::string(info->SourceText.Data, info->SourceText.Count),
            info->Kind,
            std::string(info->FileName.Data, info->FileName.Count),
            options);
    }
    catch (const std::exception &e)
    {
        return new CompilationResult(e.what());
    }
}

VD_EXPORT void FreeResult(CompilationResult *result)
{
    delete result;
}

constexpr VertexElementFormat FloatFormats[] = {
    VertexElementFormat::Float1,
    VertexElementFormat::Float1,
    VertexElementFormat::Float2,
    VertexElementFormat::Float3,
    VertexElementFormat::Float4,
};

constexpr VertexElementFormat IntFormats[] = {
    VertexElementFormat::Int1,
    VertexElementFormat::Int1,
    VertexElementFormat::Int2,
    VertexElementFormat::Int3,
    VertexElementFormat::Int4,
};

constexpr VertexElementFormat UIntFormats[] = {
    VertexElementFormat::UInt1,
    VertexElementFormat::UInt1,
    VertexElementFormat::UInt2,
    VertexElementFormat::UInt3,
    VertexElementFormat::UInt4,
};

void ReflectVertexInfo(const Compiler &compiler, const ShaderResources &resources, ReflectionInfo &info)
{
    uint32_t elementCount = 0;
    for (const auto &input : resources.stage_inputs)
    {
        uint32_t location = compiler.get_decoration(input.id, spv::DecorationLocation);
        elementCount = std::max(location + 1, elementCount);
    }

    info.VertexElements = InteropArray<VertexElementDescription>(elementCount);

    for (const auto &input : resources.stage_inputs)
    {
        uint32_t location = compiler.get_decoration(input.id, spv::DecorationLocation);
        info.VertexElements[location].Semantic = VertexElementSemantic::TextureCoordinate;
        std::string name = compiler.get_name(input.id);
        if (name.empty())
        {
            name = compiler.get_fallback_name(input.id);
        }
        info.VertexElements[location].Name.CopyFrom(static_cast<uint32_t>(name.size()), name.c_str());
        const SPIRType &baseType = compiler.get_type(input.base_type_id);
        switch (baseType.basetype)
        {
        case SPIRType::Float:
            info.VertexElements[location].Format = FloatFormats[baseType.vecsize];
            break;
        case SPIRType::Int:
            info.VertexElements[location].Format = IntFormats[baseType.vecsize];
            break;
        case SPIRType::UInt:
            info.VertexElements[location].Format = UIntFormats[baseType.vecsize];
            break;
        default:
            throw std::runtime_error("Unhandled SPIR-V vertex input data type.");
        }
    }
}
} // namespace Veldrid
